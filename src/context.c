#include "../include/datoviz/context.h"
#include "../include/datoviz/atlas.h"
#include "context_utils.h"
#include "transfer_utils.h"
#include "vklite_utils.h"
#include <stdlib.h>



/*************************************************************************************************/
/*  Constants                                                                                    */
/*************************************************************************************************/

#define TRANSFERABLE (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)

#define DVZ_BUFFER_TYPE_STAGING_SIZE  (4 * 1024 * 1024)
#define DVZ_BUFFER_TYPE_VERTEX_SIZE   (4 * 1024 * 1024)
#define DVZ_BUFFER_TYPE_INDEX_SIZE    (4 * 1024 * 1024)
#define DVZ_BUFFER_TYPE_STORAGE_SIZE  (1 * 1024 * 1024)
#define DVZ_BUFFER_TYPE_UNIFORM_SIZE  (1 * 1024 * 1024)
#define DVZ_BUFFER_TYPE_MAPPABLE_SIZE DVZ_BUFFER_TYPE_UNIFORM_SIZE



/*************************************************************************************************/
/*  Context                                                                                      */
/*************************************************************************************************/

static void _default_queues(DvzGpu* gpu, bool has_present_queue)
{
    dvz_gpu_queue(gpu, DVZ_DEFAULT_QUEUE_TRANSFER, DVZ_QUEUE_TRANSFER);
    dvz_gpu_queue(gpu, DVZ_DEFAULT_QUEUE_COMPUTE, DVZ_QUEUE_COMPUTE);
    dvz_gpu_queue(gpu, DVZ_DEFAULT_QUEUE_RENDER, DVZ_QUEUE_RENDER);
    if (has_present_queue)
        dvz_gpu_queue(gpu, DVZ_DEFAULT_QUEUE_PRESENT, DVZ_QUEUE_PRESENT);
}



static DvzBuffer* _make_new_buffer(DvzContext* ctx)
{
    ASSERT(ctx != NULL);
    DvzBuffer* buffer = (DvzBuffer*)dvz_container_alloc(&ctx->buffers);
    *buffer = dvz_buffer(ctx->gpu);
    ASSERT(buffer != NULL);

    // All buffers may be accessed from these queues.
    dvz_buffer_queue_access(buffer, DVZ_DEFAULT_QUEUE_TRANSFER);
    dvz_buffer_queue_access(buffer, DVZ_DEFAULT_QUEUE_COMPUTE);
    dvz_buffer_queue_access(buffer, DVZ_DEFAULT_QUEUE_RENDER);

    return buffer;
}

static void _make_staging_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_STAGING);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(buffer, TRANSFERABLE);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_CPU_ONLY);
    // dvz_buffer_memory(
    //     buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));
}

static void _make_vertex_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_VERTEX);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(
        buffer,
        TRANSFERABLE | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    // dvz_buffer_memory(buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_GPU_ONLY);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));
}

static void _make_index_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_INDEX);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(buffer, TRANSFERABLE | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    // dvz_buffer_memory(buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_GPU_ONLY);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));
}

static void _make_storage_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_STORAGE);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(buffer, TRANSFERABLE | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    // dvz_buffer_memory(buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_GPU_ONLY);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));
}

static void _make_uniform_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_UNIFORM);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(buffer, TRANSFERABLE | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    // dvz_buffer_memory(buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_GPU_ONLY);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));
}

static void _make_mappable_buffer(DvzBuffer* buffer, VkDeviceSize size)
{
    ASSERT(buffer != NULL);
    dvz_buffer_type(buffer, DVZ_BUFFER_TYPE_MAPPABLE);
    dvz_buffer_size(buffer, size);
    dvz_buffer_usage(buffer, TRANSFERABLE | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    // dvz_buffer_memory(
    //     buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    dvz_buffer_vma_usage(buffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
    dvz_buffer_create(buffer);
    ASSERT(dvz_obj_is_created(&buffer->obj));

    // Permanently map the buffer.
    buffer->mmap = dvz_buffer_map(buffer, 0, VK_WHOLE_SIZE);
}



static VkDeviceSize _find_alignment(DvzContext* ctx, DvzBufferType type)
{
    ASSERT(ctx != NULL);
    ASSERT(type < DVZ_BUFFER_TYPE_COUNT);

    VkDeviceSize alignment = 0;
    bool needs_align = type == DVZ_BUFFER_TYPE_UNIFORM || type == DVZ_BUFFER_TYPE_MAPPABLE;
    if (needs_align)
        alignment = ctx->gpu->device_properties.limits.minUniformBufferOffsetAlignment;
    return alignment;
}



static DvzBuffer* _get_shared_buffer(DvzContext* ctx, DvzBufferType type)
{
    ASSERT(ctx != NULL);
    DvzBuffer* buffer = (DvzBuffer*)dvz_container_get(&ctx->buffers, (uint32_t)type);
    ASSERT(buffer->type == type);
    return buffer;
}

static DvzBuffer* _get_standalone_buffer(DvzContext* ctx, DvzBufferType type, VkDeviceSize size)
{
    ASSERT(ctx != NULL);
    DvzBuffer* buffer = _make_new_buffer(ctx);

    switch (type)
    {
    case DVZ_BUFFER_TYPE_STAGING:
        _make_staging_buffer(buffer, size);
        break;

    case DVZ_BUFFER_TYPE_VERTEX:
        _make_vertex_buffer(buffer, size);
        break;

    case DVZ_BUFFER_TYPE_INDEX:
        _make_index_buffer(buffer, size);
        break;

    case DVZ_BUFFER_TYPE_STORAGE:
        _make_storage_buffer(buffer, size);
        break;

    case DVZ_BUFFER_TYPE_UNIFORM:
        _make_uniform_buffer(buffer, size);
        break;

    case DVZ_BUFFER_TYPE_MAPPABLE:
        _make_mappable_buffer(buffer, size);
        break;

    default:
        log_error("unknown buffer type %d", type);
        break;
    }

    return buffer;
}


static DvzAlloc* _get_alloc(DvzContext* ctx, DvzBufferType type)
{
    ASSERT(ctx != NULL);
    ASSERT((uint32_t)type < DVZ_BUFFER_TYPE_COUNT);
    return &ctx->allocators[(uint32_t)type];
}

static DvzAlloc* _make_allocator(DvzContext* ctx, DvzBufferType type, VkDeviceSize size)
{
    ASSERT(ctx != NULL);
    ASSERT((uint32_t)type < DVZ_BUFFER_TYPE_COUNT);

    DvzAlloc* alloc = _get_alloc(ctx, type);
    VkDeviceSize alignment = _find_alignment(ctx, type);
    *alloc = dvz_alloc(size, alignment);
    return alloc;
}



static VkDeviceSize _allocate_dat(DvzContext* ctx, DvzBufferType type, VkDeviceSize req_size)
{
    ASSERT(ctx != NULL);
    ASSERT(type < DVZ_BUFFER_TYPE_COUNT);
    ASSERT(req_size > 0);

    VkDeviceSize resized = 0; // will be non-zero if the buffer must be resized
    DvzAlloc* alloc = _get_alloc(ctx, type);
    // Make the allocation.
    VkDeviceSize offset = dvz_alloc_new(alloc, req_size, &resized);

    // Need to resize the underlying DvzBuffer.
    if (resized)
    {
        DvzBuffer* buffer = _get_shared_buffer(ctx, type);
        log_info("reallocating buffer %d to %s", type, pretty_size(resized));
        dvz_buffer_resize(buffer, resized);
    }

    return offset;
}



static void _default_buffers(DvzContext* context)
{
    ASSERT(context != NULL);
    ASSERT(context->gpu != NULL);

    // Create a predetermined set of buffers.
    for (uint32_t i = 0; i < DVZ_BUFFER_TYPE_COUNT; i++)
    {
        // IMPORTANT: buffer #i should have buffer type i
        _make_new_buffer(context);
    }

    DvzBuffer* buffer = NULL;

    // Staging buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_STAGING);
    _make_staging_buffer(buffer, DVZ_BUFFER_TYPE_STAGING_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_STAGING, DVZ_BUFFER_TYPE_STAGING_SIZE);
    // Permanently map the buffer.
    // buffer->mmap = dvz_buffer_map(buffer, 0, VK_WHOLE_SIZE);

    // Vertex buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_VERTEX);
    _make_vertex_buffer(buffer, DVZ_BUFFER_TYPE_VERTEX_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_VERTEX, DVZ_BUFFER_TYPE_VERTEX_SIZE);

    // Index buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_INDEX);
    _make_index_buffer(buffer, DVZ_BUFFER_TYPE_INDEX_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_INDEX, DVZ_BUFFER_TYPE_INDEX_SIZE);

    // Storage buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_STORAGE);
    _make_storage_buffer(buffer, DVZ_BUFFER_TYPE_STORAGE_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_STORAGE, DVZ_BUFFER_TYPE_STORAGE_SIZE);

    // Uniform buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_UNIFORM);
    _make_uniform_buffer(buffer, DVZ_BUFFER_TYPE_UNIFORM_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_UNIFORM, DVZ_BUFFER_TYPE_UNIFORM_SIZE);

    // Mappable uniform buffer
    buffer = dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_MAPPABLE);
    _make_mappable_buffer(buffer, DVZ_BUFFER_TYPE_MAPPABLE_SIZE);
    _make_allocator(context, DVZ_BUFFER_TYPE_MAPPABLE, DVZ_BUFFER_TYPE_MAPPABLE_SIZE);
}



static void _default_resources(DvzContext* context)
{
    ASSERT(context != NULL);

    // Create the default buffers.
    _default_buffers(context);

    // TODO
    // // Create the font atlas and assign it to the context.
    // context->font_atlas = dvz_font_atlas(context);

    // // Color texture.
    // context->color_texture.arr = _load_colormaps();
    // context->color_texture.texture =
    //     dvz_ctx_texture(context, 2, (uvec3){256, 256, 1}, VK_FORMAT_R8G8B8A8_UNORM);
    // dvz_texture_address_mode(
    //     context->color_texture.texture, DVZ_TEXTURE_AXIS_U,
    //     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // dvz_texture_address_mode(
    //     context->color_texture.texture, DVZ_TEXTURE_AXIS_V,
    //     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // dvz_context_colormap(context);

    // // Default 1D texture, for transfer functions.
    // context->transfer_texture = _default_transfer_texture(context);
}



static void _create_resources(DvzContext* context)
{
    ASSERT(context != NULL);

    context->buffers =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzBuffer), DVZ_OBJECT_TYPE_BUFFER);
    context->dats =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzDat), DVZ_OBJECT_TYPE_BUFFER);
    context->images =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzImages), DVZ_OBJECT_TYPE_IMAGES);
    context->samplers =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzSampler), DVZ_OBJECT_TYPE_SAMPLER);
    context->textures =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzTexture), DVZ_OBJECT_TYPE_TEXTURE);
    context->computes =
        dvz_container(DVZ_CONTAINER_DEFAULT_COUNT, sizeof(DvzCompute), DVZ_OBJECT_TYPE_COMPUTE);
}



static void _destroy_resources(DvzContext* context)
{
    ASSERT(context != NULL);

    log_trace("context destroy buffers");
    CONTAINER_DESTROY_ITEMS(DvzBuffer, context->buffers, dvz_buffer_destroy)

    log_trace("context destroy dats");
    CONTAINER_DESTROY_ITEMS(DvzDat, context->dats, dvz_dat_destroy)

    log_trace("context destroy sets of images");
    CONTAINER_DESTROY_ITEMS(DvzImages, context->images, dvz_images_destroy)

    log_trace("context destroy samplers");
    CONTAINER_DESTROY_ITEMS(DvzSampler, context->samplers, dvz_sampler_destroy)

    log_trace("context destroy textures");
    CONTAINER_DESTROY_ITEMS(DvzTexture, context->textures, dvz_texture_destroy)

    log_trace("context destroy computes");
    CONTAINER_DESTROY_ITEMS(DvzCompute, context->computes, dvz_compute_destroy)
}



static void _gpu_default_features(DvzGpu* gpu)
{
    ASSERT(gpu != NULL);
    dvz_gpu_request_features(gpu, (VkPhysicalDeviceFeatures){.independentBlend = true});
}



void dvz_gpu_default(DvzGpu* gpu, DvzWindow* window)
{
    ASSERT(gpu != NULL);

    // Specify the default queues.
    _default_queues(gpu, window != NULL);

    // Default features
    _gpu_default_features(gpu);

    // Create the GPU after the default queues have been set.
    if (!dvz_obj_is_created(&gpu->obj))
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (window != NULL)
            surface = window->surface;
        dvz_gpu_create(gpu, surface);
    }
}



/*************************************************************************************************/
/*  Context                                                                                      */
/*************************************************************************************************/

static void _create_transfers(DvzContext* context)
{
    ASSERT(context != NULL);
    context->deq = dvz_deq(4);

    // Three producer/consumer pairs (deq processes).
    dvz_deq_proc(
        &context->deq, DVZ_TRANSFER_PROC_UD, //
        2, (uint32_t[]){DVZ_TRANSFER_DEQ_UL, DVZ_TRANSFER_DEQ_DL});
    dvz_deq_proc(
        &context->deq, DVZ_TRANSFER_PROC_CPY, //
        1, (uint32_t[]){DVZ_TRANSFER_DEQ_COPY});
    dvz_deq_proc(
        &context->deq, DVZ_TRANSFER_PROC_EV, //
        1, (uint32_t[]){DVZ_TRANSFER_DEQ_EV});

    // Transfer deq callbacks.
    // Uploads.
    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_UL, //
        DVZ_TRANSFER_BUFFER_UPLOAD,         //
        _process_buffer_upload, context);

    // Downloads.
    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_DL, //
        DVZ_TRANSFER_BUFFER_DOWNLOAD,       //
        _process_buffer_download, context);

    // Copies.
    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_COPY, //
        DVZ_TRANSFER_BUFFER_COPY,             //
        _process_buffer_copy, context);

    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_COPY, //
        DVZ_TRANSFER_TEXTURE_COPY,            //
        _process_texture_copy, context);

    // Buffer/texture copies.
    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_COPY, //
        DVZ_TRANSFER_TEXTURE_BUFFER,          //
        _process_texture_buffer, context);

    dvz_deq_callback(
        &context->deq, DVZ_TRANSFER_DEQ_COPY, //
        DVZ_TRANSFER_BUFFER_TEXTURE,          //
        _process_buffer_texture, context);

    // Transfer thread.
    context->thread = dvz_thread(_thread_transfers, context);
}



static void _destroy_transfers(DvzContext* context)
{
    ASSERT(context != NULL);

    // Enqueue a STOP task to stop the UL and DL threads.
    dvz_deq_enqueue(&context->deq, DVZ_TRANSFER_DEQ_UL, 0, NULL);
    dvz_deq_enqueue(&context->deq, DVZ_TRANSFER_DEQ_DL, 0, NULL);

    // Join the UL and DL threads.
    dvz_thread_join(&context->thread);

    dvz_deq_destroy(&context->deq);
}



DvzContext* dvz_context(DvzGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(dvz_obj_is_created(&gpu->obj));
    log_trace("creating context");

    DvzContext* context = calloc(1, sizeof(DvzContext));
    ASSERT(context != NULL);
    context->gpu = gpu;

    // Allocate memory for buffers, textures, and computes.
    _create_resources(context);

    // Transfer dequeues.
    _create_transfers(context);

    // HACK: the vklite module makes the assumption that the queue #0 supports transfers.
    // Here, in the context, we make the same assumption. The first queue is reserved to transfers.
    ASSERT(DVZ_DEFAULT_QUEUE_TRANSFER == 0);

    // Create the context.
    gpu->context = context;
    dvz_obj_created(&context->obj);

    // Create the default resources.
    _default_resources(context);

    return context;
}



void dvz_context_colormap(DvzContext* context)
{
    ASSERT(context != NULL);
    ASSERT(context->color_texture.texture != NULL);
    ASSERT(context->color_texture.arr != NULL);

    // TODO
    // dvz_upload_texture(
    //     context, context->color_texture.texture, DVZ_ZERO_OFFSET, DVZ_ZERO_OFFSET, 256 * 256 *
    //     4, context->color_texture.arr);
    // dvz_queue_wait(context->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



void dvz_context_reset(DvzContext* context)
{
    ASSERT(context != NULL);
    log_trace("reset the context");
    _destroy_resources(context);
    _default_resources(context);
}



void dvz_context_destroy(DvzContext* context)
{
    if (context == NULL)
    {
        log_error("skip destruction of null context");
        return;
    }
    log_trace("destroying context");
    ASSERT(context != NULL);
    ASSERT(context->gpu != NULL);

    // Destroy the font atlas.
    // TODO
    // dvz_font_atlas_destroy(&context->font_atlas);

    // Destroy the buffers, images, samplers, textures, computes.
    _destroy_resources(context);

    // Destroy the transfers queue.
    _destroy_transfers(context);

    // Destroy the DvzDat allocators.
    for (uint32_t i = 0; i < DVZ_BUFFER_TYPE_COUNT; i++)
        dvz_alloc_destroy(&context->allocators[i]);

    // Free the allocated memory.
    dvz_container_destroy(&context->buffers);
    dvz_container_destroy(&context->images);
    dvz_container_destroy(&context->samplers);
    dvz_container_destroy(&context->textures);
    dvz_container_destroy(&context->computes);
}



void dvz_app_reset(DvzApp* app)
{
    ASSERT(app != NULL);
    dvz_app_wait(app);
    DvzContainerIterator iter = dvz_container_iterator(&app->gpus);
    DvzGpu* gpu = NULL;
    while (iter.item != NULL)
    {
        gpu = iter.item;
        ASSERT(gpu != NULL);
        if (dvz_obj_is_created(&gpu->obj) && gpu->context != NULL)
            dvz_context_reset(gpu->context);
        dvz_container_iter(&iter);
    }
    dvz_app_wait(app);
}



/*************************************************************************************************/
/*  Dats and texs                                                                                */
/*************************************************************************************************/

DvzDat* dvz_dat(DvzContext* ctx, DvzBufferType type, VkDeviceSize size, uint32_t count, int flags)
{
    ASSERT(ctx != NULL);
    ASSERT(size > 0);
    ASSERT(count > 0);
    ASSERT(count <= 10); // consistency check

    DvzDat* dat = (DvzDat*)dvz_container_alloc(&ctx->dats);
    dat->context = ctx;
    dat->flags = flags;
    bool shared = (flags & DVZ_DAT_FLAGS_SHARED) > 0;
    VkDeviceSize alignment = _find_alignment(ctx, type);

    DvzBuffer* buffer = NULL;
    VkDeviceSize offset = 0; // to determine with allocator

    // Make sure the requested size is aligned.
    size = _align(size, alignment);

    // Shared buffer.
    if (shared)
    {
        // Get the  unique shared buffer of the requested type.
        buffer = _get_shared_buffer(ctx, type);

        // Allocate a DvzDat from it.
        // NOTE: this call may resize the underlying DvzBuffer, which is slow (hard GPU sync).
        offset = _allocate_dat(ctx, type, count * size);
    }

    // Standalone buffer.
    else
    {
        // Create a brand new buffer just for this DvzDat.
        buffer = _get_standalone_buffer(ctx, type, count * size);
        // Allocate the entire buffer, so offset is 0, and the size is the requested (aligned if
        // necessary) size.
        offset = 0;
    }

    // Check alignment.
    if (alignment > 0)
    {
        ASSERT(offset % alignment == 0);
        ASSERT(offset % size == 0);
    }

    // Set the buffer region.
    dat->br = dvz_buffer_regions(buffer, count, offset, size, alignment);

    dvz_obj_created(&dat->obj);
    return dat;
}



void dvz_dat_upload(DvzDat* dat, VkDeviceSize offset, VkDeviceSize size, void* data)
{
    ASSERT(dat != NULL);
    // asynchronous function
    // if staging
    //     allocate staging buffer if there isn't already one
    // enqueue a buffer upload transfer
    // the copy to staging will be done in a background thread automatically
    // need the caller to call dvz_ctx_frame()
    //     dequeue all pending copies, with hard gpu sync
}



void dvz_dat_download(DvzDat* dat, VkDeviceSize size, void* data)
{
    ASSERT(dat != NULL);
    // asynchronous function
}



void dvz_dat_destroy(DvzDat* dat)
{
    ASSERT(dat != NULL);
    // free the region in the buffer
}



DvzTex* dvz_tex(DvzContext* ctx, DvzTexDims dims, uvec3 shape, int flags)
{
    ASSERT(ctx != NULL);
    // create a new image

    return NULL;
}



void dvz_tex_upload(DvzTex* tex, uvec3 offset, uvec3 shape, VkDeviceSize size, void* data)
{
    ASSERT(tex != NULL);
}



void dvz_tex_download(DvzTex* tex, uvec3 offset, uvec3 shape, VkDeviceSize size, void* data)
{
    ASSERT(tex != NULL);
}



void dvz_tex_destroy(DvzTex* tex) { ASSERT(tex != NULL); }



void dvz_ctx_clear(DvzContext* ctx)
{
    ASSERT(ctx != NULL);
    // free all buffers, delete all images
}



/*************************************************************************************************/
/*  Buffer allocation                                                                            */
/*************************************************************************************************/

DvzBufferRegions dvz_ctx_buffers(
    DvzContext* context, DvzBufferType buffer_type, uint32_t buffer_count, VkDeviceSize size)
{
    ASSERT(context != NULL);
    ASSERT(context->gpu != NULL);
    ASSERT(buffer_count > 0);
    ASSERT(size > 0);
    ASSERT(buffer_type < DVZ_BUFFER_TYPE_COUNT);

    // Choose the first buffer with the requested type.
    DvzContainerIterator iter = dvz_container_iterator(&context->buffers);
    DvzBuffer* buffer = NULL;
    while (iter.item != NULL)
    {
        buffer = iter.item;
        if (dvz_obj_is_created(&buffer->obj) && buffer->type == buffer_type)
            break;
        dvz_container_iter(&iter);
    }
    if (buffer == NULL)
    {
        log_error("could not find buffer with requested type %d", buffer_type);
        return (DvzBufferRegions){0};
    }
    ASSERT(buffer != NULL);
    ASSERT(buffer->type == buffer_type);
    ASSERT(dvz_obj_is_created(&buffer->obj));

    VkDeviceSize alignment = 0;
    VkDeviceSize offset = buffer->allocated_size;
    bool needs_align =
        buffer_type == DVZ_BUFFER_TYPE_UNIFORM || buffer_type == DVZ_BUFFER_TYPE_MAPPABLE;
    if (needs_align)
    {
        alignment = context->gpu->device_properties.limits.minUniformBufferOffsetAlignment;
        ASSERT(offset % alignment == 0); // offset should be already aligned
    }

    DvzBufferRegions regions = dvz_buffer_regions(buffer, buffer_count, offset, size, alignment);
    VkDeviceSize alsize = regions.aligned_size;
    if (alsize == 0)
        alsize = size;
    ASSERT(alsize > 0);

    if (!dvz_obj_is_created(&buffer->obj))
    {
        log_error("invalid buffer %d", buffer_type);
        return regions;
    }

    // Check alignment for uniform buffers.
    if (needs_align)
    {
        ASSERT(alignment > 0);
        ASSERT(alsize % alignment == 0);
        for (uint32_t i = 0; i < buffer_count; i++)
            ASSERT(regions.offsets[i] % alignment == 0);
    }

    // Need to reallocate?
    if (offset + alsize * buffer_count > regions.buffer->size)
    {
        VkDeviceSize new_size = dvz_next_pow2(offset + alsize * buffer_count);
        log_info("reallocating buffer %d to %s", buffer_type, pretty_size(new_size));
        dvz_buffer_resize(regions.buffer, new_size);
    }

    log_debug(
        "allocating %d buffers (type %d) with size %s (aligned size %s)", //
        buffer_count, buffer_type, pretty_size(size), pretty_size(alsize));
    ASSERT(offset + alsize * buffer_count <= regions.buffer->size);
    buffer->allocated_size += alsize * buffer_count;

    ASSERT(regions.offsets[buffer_count - 1] + alsize == buffer->allocated_size);
    return regions;
}



void dvz_ctx_buffers_resize(DvzContext* context, DvzBufferRegions* br, VkDeviceSize new_size)
{
    // NOTE: this function tries to resize a buffer region in-place, which only works if
    // it is the last allocated region in the buffer. Otherwise a brand new region is allocated,
    // which wastes space. TODO: smarter memory management, defragmentation etc.
    ASSERT(br->buffer != NULL);
    ASSERT(br->count > 0);
    if (br->count > 1)
    {
        log_error("dvz_buffer_regions_resize() currently only supports regions with buf count=1");
        return;
    }
    ASSERT(br->count == 1);

    // The region is the last allocated in the buffer, we can safely resize it.
    VkDeviceSize old_size = br->aligned_size > 0 ? br->aligned_size : br->size;
    ASSERT(old_size > 0);
    if (br->offsets[0] + old_size == br->buffer->allocated_size)
    {
        log_debug("resize the buffer region in-place");
        br->size = new_size;
        if (br->alignment > 0)
            br->aligned_size = aligned_size(new_size, br->alignment);
        br->buffer->allocated_size = br->offsets[0] + new_size;

        // Need to reallocate a new underlying buffer.
        if (br->offsets[0] + old_size > br->buffer->size)
        {
            VkDeviceSize bs = dvz_next_pow2(br->offsets[0] + old_size);
            log_info("reallocating buffer #%d to %s", br->buffer->type, pretty_size(bs));
            dvz_buffer_resize(br->buffer, bs);
        }
    }

    // The region cannot be resized directly, need to make a new region allocation.
    else
    {
        log_debug("failed to resize the buffer region in-place, allocating a new region");
        *br = dvz_ctx_buffers(context, br->buffer->type, 1, new_size);
    }
}



/*************************************************************************************************/
/*  Compute                                                                                      */
/*************************************************************************************************/

DvzCompute* dvz_ctx_compute(DvzContext* context, const char* shader_path)
{
    ASSERT(context != NULL);
    ASSERT(shader_path != NULL);

    DvzCompute* compute = dvz_container_alloc(&context->computes);
    *compute = dvz_compute(context->gpu, shader_path);
    return compute;
}



/*************************************************************************************************/
/*  Texture                                                                                      */
/*************************************************************************************************/

static VkImageType image_type_from_dims(uint32_t dims)
{
    switch (dims)
    {
    case 1:
        return VK_IMAGE_TYPE_1D;
        break;
    case 2:
        return VK_IMAGE_TYPE_2D;
        break;
    case 3:
        return VK_IMAGE_TYPE_3D;
        break;

    default:
        break;
    }
    log_error("invalid image dimensions %d", dims);
    return VK_IMAGE_TYPE_2D;
}



DvzTexture* dvz_ctx_texture(DvzContext* context, uint32_t dims, uvec3 size, VkFormat format)
{
    ASSERT(context != NULL);
    ASSERT(1 <= dims && dims <= 3);
    log_debug(
        "creating %dD texture with shape %dx%dx%d and format %d", //
        dims, size[0], size[1], size[2], format);

    DvzTexture* texture = dvz_container_alloc(&context->textures);
    DvzImages* image = dvz_container_alloc(&context->images);
    // DvzSampler* sampler = dvz_container_alloc(&context->samplers);

    texture->context = context;
    *image = dvz_images(context->gpu, image_type_from_dims(dims), 1);
    // *sampler = dvz_sampler(context->gpu);

    texture->dims = dims;
    texture->image = image;
    // texture->sampler = sampler;

    // Create the image.
    dvz_images_format(image, format);
    dvz_images_size(image, size[0], size[1], size[2]);
    dvz_images_tiling(image, VK_IMAGE_TILING_OPTIMAL);
    dvz_images_layout(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dvz_images_usage(
        image,                                                    //
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | //
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    dvz_images_memory(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    dvz_images_queue_access(image, DVZ_DEFAULT_QUEUE_TRANSFER);
    dvz_images_queue_access(image, DVZ_DEFAULT_QUEUE_COMPUTE);
    dvz_images_queue_access(image, DVZ_DEFAULT_QUEUE_RENDER);
    dvz_images_create(image);

    // // Create the sampler.
    // dvz_sampler_min_filter(sampler, VK_FILTER_NEAREST);
    // dvz_sampler_mag_filter(sampler, VK_FILTER_NEAREST);
    // dvz_sampler_address_mode(sampler, DVZ_TEXTURE_AXIS_U,
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE); dvz_sampler_address_mode(sampler,
    // DVZ_TEXTURE_AXIS_V, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // dvz_sampler_address_mode(sampler, DVZ_TEXTURE_AXIS_V,
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE); dvz_sampler_create(sampler);

    dvz_obj_created(&texture->obj);

    // Immediately transition the image to its layout.
    dvz_texture_transition(texture);

    return texture;
}



void dvz_texture_resize(DvzTexture* texture, uvec3 size)
{
    ASSERT(texture != NULL);
    ASSERT(texture->image != NULL);

    dvz_images_resize(texture->image, size[0], size[1], size[2]);
}



void dvz_texture_filter(DvzTexture* texture, DvzFilterType type, VkFilter filter)
{
    ASSERT(texture != NULL);
    // ASSERT(texture->sampler != NULL);

    // switch (type)
    // {
    // case DVZ_FILTER_MIN:
    //     dvz_sampler_min_filter(texture->sampler, filter);
    //     break;
    // case DVZ_FILTER_MAG:
    //     dvz_sampler_mag_filter(texture->sampler, filter);
    //     break;
    // default:
    //     log_error("invalid filter type %d", type);
    //     break;
    // }
    // dvz_sampler_destroy(texture->sampler);
    // dvz_sampler_create(texture->sampler);
}



void dvz_texture_address_mode(
    DvzTexture* texture, DvzTextureAxis axis, VkSamplerAddressMode address_mode)
{
    ASSERT(texture != NULL);
    // ASSERT(texture->sampler != NULL);

    // dvz_sampler_address_mode(texture->sampler, axis, address_mode);

    // dvz_sampler_destroy(texture->sampler);
    // dvz_sampler_create(texture->sampler);
}



// // TODO: this function should be discarded and integrated into upload_texture() in multiple
// steps void dvz_texture_upload(
//     DvzTexture* texture, uvec3 offset, uvec3 shape, VkDeviceSize size, const void* data)
// {
//     ASSERT(texture != NULL);
//     DvzContext* context = texture->context;
//     ASSERT(context != NULL);
//     ASSERT(size > 0);
//     ASSERT(data != NULL);

//     // Take the staging buffer.
//     DvzBuffer* staging = staging_buffer(context, size);

//     // Memcpy into the staging buffer.
//     dvz_buffer_upload(staging, 0, size, data);

//     // Copy from the staging buffer to the texture.
//     _copy_texture_from_staging(context, texture, offset, shape, size);

//     // IMPORTANT: need to wait for the texture to be copied to the staging buffer, *before*
//     // downloading the data from the staging buffer.
//     dvz_queue_wait(context->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
// }



// // TODO: this function should be discarded and integrated into upload_texture() in multiple
// steps void dvz_texture_download(
//     DvzTexture* texture, uvec3 offset, uvec3 shape, VkDeviceSize size, void* data)
// {
//     ASSERT(texture != NULL);
//     DvzContext* context = texture->context;
//     ASSERT(context != NULL);
//     ASSERT(size > 0);
//     ASSERT(data != NULL);

//     // Take the staging buffer.
//     DvzBuffer* staging = staging_buffer(context, size);

//     // Copy from the staging buffer to the texture.
//     _copy_texture_to_staging(context, texture, offset, shape, size);

//     // IMPORTANT: need to wait for the texture to be copied to the staging buffer, *before*
//     // downloading the data from the staging buffer.
//     dvz_queue_wait(context->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);

//     // Memcpy into the staging buffer.
//     dvz_buffer_download(staging, 0, size, data);
// }



void dvz_texture_copy(
    DvzTexture* src, uvec3 src_offset, DvzTexture* dst, uvec3 dst_offset, uvec3 shape)
{
    ASSERT(src != NULL);
    ASSERT(dst != NULL);
    DvzContext* context = src->context;
    DvzGpu* gpu = context->gpu;
    ASSERT(context != NULL);

    // Take transfer cmd buf.
    DvzCommands cmds_ = dvz_commands(gpu, 0, 1);
    DvzCommands* cmds = &cmds_;
    dvz_cmd_reset(cmds, 0);
    dvz_cmd_begin(cmds, 0);

    DvzBarrier src_barrier = dvz_barrier(gpu);
    dvz_barrier_stages(
        &src_barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    dvz_barrier_images(&src_barrier, src->image);

    DvzBarrier dst_barrier = dvz_barrier(gpu);
    dvz_barrier_stages(
        &dst_barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    dvz_barrier_images(&dst_barrier, dst->image);

    // Source image transition.
    if (src->image->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        log_trace("source image %d transition", src->image->images[0]);
        dvz_barrier_images_layout(
            &src_barrier, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dvz_barrier_images_access(&src_barrier, 0, VK_ACCESS_TRANSFER_READ_BIT);
        dvz_cmd_barrier(cmds, 0, &src_barrier);
    }

    // Destination image transition.
    {
        log_trace("destination image %d transition", dst->image->images[0]);
        dvz_barrier_images_layout(
            &dst_barrier, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        dvz_barrier_images_access(&dst_barrier, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        dvz_cmd_barrier(cmds, 0, &dst_barrier);
    }

    // Copy texture command.
    VkImageCopy copy = {0};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent.width = shape[0];
    copy.extent.height = shape[1];
    copy.extent.depth = shape[2];
    copy.srcOffset.x = (int32_t)src_offset[0];
    copy.srcOffset.y = (int32_t)src_offset[1];
    copy.srcOffset.z = (int32_t)src_offset[2];
    copy.dstOffset.x = (int32_t)dst_offset[0];
    copy.dstOffset.y = (int32_t)dst_offset[1];
    copy.dstOffset.z = (int32_t)dst_offset[2];

    vkCmdCopyImage(
        cmds->cmds[0],                                               //
        src->image->images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, //
        dst->image->images[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, //
        1, &copy);

    // Source image transition.
    if (src->image->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
        src->image->layout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        log_trace("source image transition back");
        dvz_barrier_images_layout(
            &src_barrier, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src->image->layout);
        dvz_barrier_images_access(
            &src_barrier, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        dvz_cmd_barrier(cmds, 0, &src_barrier);
    }

    // Destination image transition.
    if (dst->image->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        dst->image->layout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        log_trace("destination image transition back");
        dvz_barrier_images_layout(
            &dst_barrier, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst->image->layout);
        dvz_barrier_images_access(&dst_barrier, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        dvz_cmd_barrier(cmds, 0, &dst_barrier);
    }

    dvz_cmd_end(cmds, 0);

    // Wait for the render queue to be idle.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_RENDER);

    // Submit the commands to the transfer queue.
    DvzSubmit submit = dvz_submit(gpu);
    dvz_submit_commands(&submit, cmds);
    log_debug("copy %dx%dx%d between 2 textures", shape[0], shape[1], shape[2]);
    dvz_submit_send(&submit, 0, NULL, 0);

    // Wait for the transfer queue to be idle.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



void dvz_texture_copy_from_buffer(
    DvzTexture* tex, uvec3 tex_offset, uvec3 shape, //
    DvzBufferRegions br, VkDeviceSize buf_offset, VkDeviceSize size)
{
    ASSERT(tex != NULL);
    DvzContext* context = tex->context;

    ASSERT(context != NULL);

    DvzGpu* gpu = context->gpu;
    ASSERT(gpu != NULL);

    DvzBuffer* buffer = br.buffer;
    ASSERT(buffer != NULL);
    buf_offset = br.offsets[0] + buf_offset;

    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(tex_offset[0] + shape[0] <= tex->image->width);
    ASSERT(tex_offset[1] + shape[1] <= tex->image->height);
    ASSERT(tex_offset[2] + shape[2] <= tex->image->depth);

    // Take transfer cmd buf.
    DvzCommands cmds_ = dvz_commands(gpu, 0, 1);
    DvzCommands* cmds = &cmds_;
    dvz_cmd_reset(cmds, 0);
    dvz_cmd_begin(cmds, 0);

    // Image transition.
    DvzBarrier barrier = dvz_barrier(gpu);
    dvz_barrier_stages(&barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    ASSERT(tex != NULL);
    ASSERT(tex->image != NULL);
    dvz_barrier_images(&barrier, tex->image);
    dvz_barrier_images_layout(
        &barrier, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    dvz_barrier_images_access(&barrier, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);

    // Copy to staging buffer
    dvz_cmd_copy_buffer_to_image(cmds, 0, buffer, buf_offset, tex->image, tex_offset, shape);

    // Image transition.
    dvz_barrier_images_layout(&barrier, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex->image->layout);
    dvz_barrier_images_access(&barrier, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);

    dvz_cmd_end(cmds, 0);

    // Wait for the render queue to be idle.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_RENDER);

    // Submit the commands to the transfer queue.
    DvzSubmit submit = dvz_submit(gpu);
    dvz_submit_commands(&submit, cmds);
    dvz_submit_send(&submit, 0, NULL, 0);

    // Wait for the transfer queue to be idle.
    // TODO: less brutal synchronization with semaphores. Here we wait for the
    // transfer to be complete before we send new rendering commands.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



void dvz_texture_copy_to_buffer(
    DvzTexture* tex, uvec3 tex_offset, uvec3 shape, //
    DvzBufferRegions br, VkDeviceSize buf_offset, VkDeviceSize size)
{
    ASSERT(tex != NULL);
    DvzContext* context = tex->context;

    ASSERT(context != NULL);

    DvzGpu* gpu = context->gpu;
    ASSERT(gpu != NULL);

    DvzBuffer* buffer = br.buffer;
    ASSERT(buffer != NULL);
    buf_offset = br.offsets[0] + buf_offset;

    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(tex_offset[0] + shape[0] <= tex->image->width);
    ASSERT(tex_offset[1] + shape[1] <= tex->image->height);
    ASSERT(tex_offset[2] + shape[2] <= tex->image->depth);

    // Take transfer cmd buf.
    DvzCommands cmds_ = dvz_commands(gpu, 0, 1);
    DvzCommands* cmds = &cmds_;
    dvz_cmd_reset(cmds, 0);
    dvz_cmd_begin(cmds, 0);

    // Image transition.
    DvzBarrier barrier = dvz_barrier(gpu);
    dvz_barrier_stages(&barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    ASSERT(tex != NULL);
    ASSERT(tex->image != NULL);
    dvz_barrier_images(&barrier, tex->image);
    dvz_barrier_images_layout(
        &barrier, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    dvz_barrier_images_access(&barrier, 0, VK_ACCESS_TRANSFER_READ_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);

    // Copy to staging buffer
    dvz_cmd_copy_image_to_buffer(cmds, 0, tex->image, tex_offset, shape, buffer, buf_offset);

    // Image transition.
    dvz_barrier_images_layout(&barrier, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex->image->layout);
    dvz_barrier_images_access(&barrier, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);

    dvz_cmd_end(cmds, 0);

    // Wait for the render queue to be idle.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_RENDER);

    // Submit the commands to the transfer queue.
    DvzSubmit submit = dvz_submit(gpu);
    dvz_submit_commands(&submit, cmds);
    dvz_submit_send(&submit, 0, NULL, 0);

    // Wait for the transfer queue to be idle.
    // TODO: less brutal synchronization with semaphores. Here we wait for the
    // transfer to be complete before we send new rendering commands.
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



void dvz_texture_transition(DvzTexture* tex)
{
    ASSERT(tex != NULL);
    ASSERT(tex->context != NULL);
    DvzGpu* gpu = tex->context->gpu;
    ASSERT(gpu != NULL);
    DvzCommands cmds_ = dvz_commands(gpu, 0, 1);
    DvzCommands* cmds = &cmds_;

    dvz_cmd_reset(cmds, 0);
    dvz_cmd_begin(cmds, 0);

    DvzBarrier barrier = dvz_barrier(gpu);
    dvz_barrier_stages(&barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    dvz_barrier_images(&barrier, tex->image);
    dvz_barrier_images_layout(&barrier, VK_IMAGE_LAYOUT_UNDEFINED, tex->image->layout);
    dvz_barrier_images_access(&barrier, 0, VK_ACCESS_TRANSFER_READ_BIT);
    dvz_cmd_barrier(cmds, 0, &barrier);

    dvz_cmd_end(cmds, 0);
    dvz_cmd_submit_sync(cmds, 0);
}



void dvz_texture_destroy(DvzTexture* texture)
{
    ASSERT(texture != NULL);
    dvz_images_destroy(texture->image);
    // dvz_sampler_destroy(texture->sampler);

    texture->image = NULL;
    // texture->sampler = NULL;
    dvz_obj_destroyed(&texture->obj);
}
