#include "../include/datoviz/canvas.h"
#include "../include/datoviz/app.h"
#include "../include/datoviz/context.h"
#include "../include/datoviz/input.h"
#include "../include/datoviz/vklite.h"
#include "canvas_utils.h"
#include "events_utils.h"
#include "vklite_utils.h"

#include <stdlib.h>



// NOTE: all functions here are to be used from the main thread. Other user-exposed canvas
// functions will be defined in run.c as they will require task enqueue in the main canvas queue.



/*************************************************************************************************/
/*  Constants                                                                                    */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Utils                                                                                    */
/*************************************************************************************************/

static bool _show_fps(DvzCanvas* canvas)
{
    ASSERT(canvas != NULL);
    return ((canvas->flags >> 1) & 1) != 0;
}

static bool _support_pick(DvzCanvas* canvas)
{
    ASSERT(canvas != NULL);
    return ((canvas->flags >> 2) & 1) != 0;
}



static void _copy_image_to_staging(
    DvzCanvas* canvas, DvzImages* images, DvzImages* staging, ivec3 offset, uvec3 shape)
{
    ASSERT(canvas != NULL);
    ASSERT(images != NULL);
    ASSERT(staging != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    DvzBarrier barrier = dvz_barrier(canvas->gpu);
    dvz_barrier_stages(&barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    dvz_barrier_images(&barrier, images);

    DvzCommands* cmds = &canvas->cmds_transfer;
    dvz_cmd_reset(cmds, 0);
    dvz_cmd_begin(cmds, 0);
    dvz_barrier_images_layout(
        &barrier, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    dvz_barrier_images_access(&barrier, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);
    dvz_cmd_copy_image_region(cmds, 0, images, offset, staging, (ivec3){0, 0, 0}, shape);
    dvz_barrier_images_layout(
        &barrier, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    dvz_barrier_images_access(&barrier, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);
    dvz_cmd_end(cmds, 0);
    dvz_cmd_submit_sync(cmds, 0);
}



static DvzImages
_staging_image(DvzCanvas* canvas, VkFormat format, uint32_t width, uint32_t height)
{
    ASSERT(canvas != NULL);
    ASSERT(width > 0);
    ASSERT(height > 0);

    DvzImages staging = dvz_images(canvas->gpu, VK_IMAGE_TYPE_2D, 1);
    dvz_images_format(&staging, format);
    dvz_images_size(&staging, width, height, 1);
    dvz_images_tiling(&staging, VK_IMAGE_TILING_LINEAR);
    dvz_images_usage(&staging, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    dvz_images_layout(&staging, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    dvz_images_memory(
        &staging, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    dvz_images_create(&staging);
    dvz_images_transition(&staging);
    return staging;
}



DvzViewport dvz_viewport_default(uint32_t width, uint32_t height)
{
    DvzViewport viewport = {0};

    viewport.viewport.x = 0;
    viewport.viewport.y = 0;
    viewport.viewport.minDepth = +0;
    viewport.viewport.maxDepth = +1;

    viewport.size_framebuffer[0] = viewport.viewport.width = (float)width;
    viewport.size_framebuffer[1] = viewport.viewport.height = (float)height;
    viewport.size_screen[0] = viewport.size_framebuffer[0];
    viewport.size_screen[1] = viewport.size_framebuffer[1];

    return viewport;
}



/*************************************************************************************************/
/*  Canvas creation                                                                              */
/*************************************************************************************************/

static DvzCanvas*
_canvas(DvzGpu* gpu, uint32_t width, uint32_t height, bool offscreen, bool overlay, int flags)
{
    ASSERT(gpu != NULL);
    DvzApp* app = gpu->app;

    ASSERT(app != NULL);
    // HACK: create the canvas container here because vklite.c does not know the size of DvzCanvas.
    if (app->canvases.capacity == 0)
    {
        log_trace("create canvases container");
        app->canvases =
            dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzCanvas), DVZ_OBJECT_TYPE_CANVAS);
    }

    DvzCanvas* canvas = dvz_container_alloc(&app->canvases);
    canvas->app = app;
    canvas->gpu = gpu;
    canvas->offscreen = offscreen;

    canvas->dpi_scaling = DVZ_DEFAULT_DPI_SCALING;
    int flag_dpi = flags >> 12;
    if (flag_dpi > 0)
        canvas->dpi_scaling *= (.5 * flag_dpi);

    canvas->overlay = overlay;
    canvas->flags = flags;

    bool show_fps = _show_fps(canvas);
    bool support_pick = _support_pick(canvas);
    log_trace("creating canvas with show_fps=%d, support_pick=%d", show_fps, support_pick);

    // Allocate memory for canvas objects.
    canvas->commands =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzCommands), DVZ_OBJECT_TYPE_COMMANDS);
    canvas->graphics =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzGraphics), DVZ_OBJECT_TYPE_GRAPHICS);

    dvz_obj_init(&canvas->obj);

    return canvas;
}



DvzCanvas* dvz_canvas(DvzGpu* gpu, uint32_t width, uint32_t height, int flags)
{
    ASSERT(gpu != NULL);
    bool offscreen = ((flags & DVZ_CANVAS_FLAGS_OFFSCREEN) != 0) ||
                     (gpu->app->backend == DVZ_BACKEND_GLFW ? false : true);
    bool overlay = (flags & DVZ_CANVAS_FLAGS_IMGUI) != 0;
    if (offscreen && overlay)
    {
        log_warn("overlay is not supported in offscreen mode, disabling it");
        overlay = false;
    }

#if SWIFTSHADER
    log_warn("swiftshader mode is active, forcing offscreen rendering");
    offscreen = true;
    overlay = false;
#endif

    return _canvas(gpu, width, height, offscreen, overlay, flags);
}



void dvz_canvas_reset(DvzCanvas* canvas)
{
    ASSERT(canvas != NULL);
    ASSERT(canvas->app != NULL);
    dvz_app_wait(canvas->app);

    ASSERT(canvas->gpu != NULL);
    ASSERT(canvas->gpu->context != NULL);
    dvz_context_reset(canvas->gpu->context);

    _clock_init(&canvas->clock);
    canvas->cur_frame = 0;
    canvas->frame_idx = 0;
    canvas->fps.last_frame_idx = 0;
}



/*************************************************************************************************/
/*  Canvas misc                                                                                  */
/*************************************************************************************************/

void dvz_canvas_size(DvzCanvas* canvas, DvzCanvasSizeType type, uvec2 size)
{
    ASSERT(canvas != NULL);

    if (canvas->window == NULL && type == DVZ_CANVAS_SIZE_SCREEN)
    {
        ASSERT(canvas->offscreen);
        log_trace("cannot determine window size in screen coordinates with offscreen canvas");
        type = DVZ_CANVAS_SIZE_FRAMEBUFFER;
    }

    switch (type)
    {
    case DVZ_CANVAS_SIZE_SCREEN:
        ASSERT(canvas->window != NULL);
        size[0] = canvas->window->width;
        size[1] = canvas->window->height;
        break;
    case DVZ_CANVAS_SIZE_FRAMEBUFFER:
        size[0] = canvas->render.framebuffers.attachments[0]->width;
        size[1] = canvas->render.framebuffers.attachments[0]->height;
        break;
    default:
        log_warn("unknown size type %d", type);
        break;
    }
}



double dvz_canvas_aspect(DvzCanvas* canvas)
{
    ASSERT(canvas != NULL);
    ASSERT(canvas->render.swapchain.images->width > 0);
    ASSERT(canvas->render.swapchain.images->height > 0);
    return canvas->render.swapchain.images->width /
           (double)canvas->render.swapchain.images->height;
}



DvzCommands* dvz_canvas_commands(DvzCanvas* canvas, uint32_t queue_idx, uint32_t count)
{
    ASSERT(canvas != NULL);
    DvzCommands* commands = dvz_container_alloc(&canvas->commands);
    *commands = dvz_commands(canvas->gpu, queue_idx, count);
    return commands;
}



void dvz_canvas_buffers(
    DvzCanvas* canvas, DvzBufferRegions br, //
    VkDeviceSize offset, VkDeviceSize size, const void* data)
{
    ASSERT(canvas != NULL);
    ASSERT(size > 0);
    ASSERT(data != NULL);
    ASSERT(br.buffer != NULL);
    ASSERT(br.count == canvas->render.swapchain.img_count);
    if (br.buffer->type != DVZ_BUFFER_TYPE_UNIFORM_MAPPABLE)
    {
        log_error("dvz_canvas_buffers() can only be used on mappable buffers.");
        return;
    }
    ASSERT(br.buffer->mmap != NULL);
    uint32_t idx = canvas->render.swapchain.img_idx;
    ASSERT(idx < br.count);
    dvz_buffer_upload(br.buffer, br.offsets[idx] + offset, size, data);
}


/*************************************************************************************************/
/*  Screenshot                                                                                   */
/*************************************************************************************************/

uint8_t* dvz_screenshot(DvzCanvas* canvas, bool has_alpha)
{
    // WARNING: this function is SLOW because it recreates a staging buffer at every call.
    // Also because it forces a hard synchronization on the whole GPU.
    // TODO: more efficient screenshot saving with screencast

    ASSERT(canvas != NULL);

    DvzGpu* gpu = canvas->gpu;
    ASSERT(gpu != NULL);

    // Hard GPU synchronization.
    dvz_gpu_wait(gpu);

    DvzImages* images = canvas->render.swapchain.images;
    if (images == NULL)
    {
        log_error("empty swapchain images, aborting screenshot creation");
        return NULL;
    }

    // Staging images.
    DvzImages staging = _staging_image(canvas, images->format, images->width, images->height);

    // Copy from the swapchain image to the staging image.
    uvec3 shape = {images->width, images->height, images->depth};
    _copy_image_to_staging(canvas, images, &staging, (ivec3){0, 0, 0}, shape);

    // Make the screenshot.
    VkDeviceSize size = staging.width * staging.height * (has_alpha ? 4 : 3) * sizeof(uint8_t);
    uint8_t* rgba = calloc(size, 1);
    dvz_images_download(&staging, 0, sizeof(uint8_t), true, has_alpha, rgba);
    dvz_gpu_wait(gpu);
    dvz_images_destroy(&staging);

    {
        uint8_t* null = calloc(size, 1);
        if (memcmp(rgba, null, size) == 0)
            log_warn("screenshot was blank");
        FREE(null);
    }

    // NOTE: the caller MUST free the returned pointer.
    return rgba;
}



void dvz_screenshot_file(DvzCanvas* canvas, const char* png_path)
{
    ASSERT(canvas != NULL);
    ASSERT(png_path != NULL);
    ASSERT(strlen(png_path) > 0);

    log_info("saving screenshot of canvas to %s with full synchronization (slow)", png_path);
    uint8_t* rgb = dvz_screenshot(canvas, false);
    if (rgb == NULL)
    {
        log_error("screenshot failed");
        return;
    }
    DvzImages* images = canvas->render.swapchain.images;
    dvz_write_png(png_path, images->width, images->height, rgb);
    FREE(rgb);
}



/*************************************************************************************************/
/*  Canvas destruction                                                                           */
/*************************************************************************************************/

void dvz_canvas_destroy(DvzCanvas* canvas)
{
    if (canvas == NULL || canvas->obj.status == DVZ_OBJECT_STATUS_DESTROYED)
    {
        log_trace("skip destruction of already-destroyed canvas");
        return;
    }
    log_debug("destroying canvas");

    ASSERT(canvas != NULL);
    ASSERT(canvas->app != NULL);
    ASSERT(canvas->gpu != NULL);

    // Stop the event thread.
    dvz_gpu_wait(canvas->gpu);

    // Destroy the canvas deq.
    {
        dvz_input_destroy(&canvas->input);
        dvz_deq_destroy(&canvas->deq);
    }

    // Destroy the graphics.
    log_trace("canvas destroy graphics pipelines");
    CONTAINER_DESTROY_ITEMS(DvzGraphics, canvas->graphics, dvz_graphics_destroy)
    dvz_container_destroy(&canvas->graphics);

    // Destroy the depth and pick images.
    dvz_images_destroy(&canvas->render.depth_image);
    dvz_images_destroy(&canvas->render.pick_image);
    dvz_images_destroy(&canvas->render.pick_staging);

    // Destroy the renderpasses.
    log_trace("canvas destroy renderpass");
    dvz_renderpass_destroy(&canvas->render.renderpass);
    if (canvas->overlay)
        dvz_renderpass_destroy(&canvas->render.renderpass_overlay);

    // Destroy the swapchain.
    log_trace("canvas destroy swapchain");
    dvz_swapchain_destroy(&canvas->render.swapchain);

    // Destroy the framebuffers.
    log_trace("canvas destroy framebuffers");
    dvz_framebuffers_destroy(&canvas->render.framebuffers);
    if (canvas->overlay)
        dvz_framebuffers_destroy(&canvas->render.framebuffers_overlay);

    // Destroy the Dear ImGui context if it was initialized.

    // HACK: we should NOT destroy imgui when using multiple DvzApp, since Dear ImGui uses
    // global context shared by all DvzApps. In practice, this is for now equivalent to using the
    // offscreen backend (which does not support Dear ImGui at the moment anyway).
    // if (canvas->app->backend != DVZ_BACKEND_OFFSCREEN)
    //     dvz_imgui_destroy();

    // Destroy the window.
    log_trace("canvas destroy window");
    if (canvas->window != NULL)
    {
        ASSERT(canvas->window->app != NULL);
        dvz_window_destroy(canvas->window);
    }

    log_trace("canvas destroy commands");
    CONTAINER_DESTROY_ITEMS(DvzCommands, canvas->commands, dvz_commands_destroy)
    dvz_container_destroy(&canvas->commands);

    // Destroy the semaphores.
    log_trace("canvas destroy semaphores");
    dvz_semaphores_destroy(&canvas->sync.sem_img_available);
    dvz_semaphores_destroy(&canvas->sync.sem_render_finished);

    // Destroy the fences.
    log_trace("canvas destroy fences");
    dvz_fences_destroy(&canvas->sync.fences_render_finished);

    // Free the GUI context if it has been set.
    // FREE(canvas->gui_context);

    // CONTAINER_DESTROY_ITEMS(DvzGui, canvas->guis, dvz_gui_destroy)
    // dvz_container_destroy(&canvas->guis);


    dvz_obj_destroyed(&canvas->obj);
}



void dvz_canvases_destroy(DvzContainer* canvases)
{
    if (canvases == NULL || canvases->capacity == 0)
        return;
    log_trace("destroy all canvases");
    CONTAINER_DESTROY_ITEMS(DvzCanvas, (*canvases), dvz_canvas_destroy)
    dvz_container_destroy(canvases);
}
