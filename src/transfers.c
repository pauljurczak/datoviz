#include "../include/datoviz/transfers.h"
#include "../include/datoviz/canvas.h"
#include "../include/datoviz/context.h"
#include "../include/datoviz/fifo.h"
#include "context_utils.h"
#include "transfer_utils.h"



// WARNING: these functions are convenient because they return immediately, but they are not
// optimally efficient because of the use of hard GPU synchronization primitives.



/*************************************************************************************************/
/*  Buffer transfers                                                                             */
/*************************************************************************************************/

void dvz_upload_buffer(
    DvzContext* ctx, DvzBufferRegions br, VkDeviceSize offset, VkDeviceSize size, void* data)
{
    ASSERT(ctx != NULL);
    ASSERT(br.buffer != NULL);
    ASSERT(data != NULL);
    ASSERT(size > 0);

    log_debug("upload %s to a buffer", pretty_size(size));

    // TODO: better staging buffer allocation
    DvzBufferRegions stg = dvz_ctx_buffers(ctx, DVZ_BUFFER_TYPE_STAGING, 1, size);

    // Enqueue an upload transfer task.
    _enqueue_buffer_upload(&ctx->deq, br, offset, stg, 0, size, data);
    // NOTE: we need to dequeue the copy proc manually, it is not done by the background thread
    // (the background thread only processes download/upload tasks).
    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);
}



void dvz_download_buffer(
    DvzContext* ctx, DvzBufferRegions br, VkDeviceSize offset, VkDeviceSize size, void* data)
{
    ASSERT(ctx != NULL);
    ASSERT(br.buffer != NULL);
    ASSERT(data != NULL);
    ASSERT(size > 0);

    log_debug("download %s from a buffer", pretty_size(size));

    // TODO: better staging buffer allocation
    DvzBufferRegions stg = dvz_ctx_buffers(ctx, DVZ_BUFFER_TYPE_STAGING, 1, size);

    // Enqueue an upload transfer task.
    _enqueue_buffer_download(&ctx->deq, br, offset, stg, 0, size, data);
    // NOTE: we need to dequeue the copy proc manually, it is not done by the background thread
    // (the background thread only processes download/upload tasks).
    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);

    // Wait until the download is done.
    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_EV, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_EV);
}



void dvz_copy_buffer(
    DvzContext* ctx, DvzBufferRegions src, VkDeviceSize src_offset, //
    DvzBufferRegions dst, VkDeviceSize dst_offset, VkDeviceSize size)
{
    ASSERT(ctx != NULL);
    ASSERT(src.buffer != NULL);
    ASSERT(dst.buffer != NULL);
    ASSERT(size > 0);

    // Enqueue an upload transfer task.
    _enqueue_buffer_copy(&ctx->deq, src, src_offset, dst, dst_offset, size);
    // NOTE: we need to dequeue the copy proc manually, it is not done by the background thread
    // (the background thread only processes download/upload tasks).
    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);
}



/*************************************************************************************************/
/*  Texture transfers                                                                            */
/*************************************************************************************************/

static void _full_tex_shape(DvzTexture* tex, uvec3 shape)
{
    ASSERT(tex != NULL);
    if (shape[0] == 0)
        shape[0] = tex->image->width;
    if (shape[1] == 0)
        shape[1] = tex->image->height;
    if (shape[2] == 0)
        shape[2] = tex->image->depth;
}



void dvz_upload_texture(
    DvzContext* ctx, DvzTexture* tex, uvec3 offset, uvec3 shape, VkDeviceSize size, void* data)
{
    ASSERT(ctx != NULL);
    ASSERT(tex != NULL);
    ASSERT(data != NULL);
    ASSERT(size > 0);

    _full_tex_shape(tex, shape);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    // TODO: better alloc, mark unused alloc as done
    DvzBufferRegions stg = dvz_ctx_buffers(ctx, DVZ_BUFFER_TYPE_STAGING, 1, size);
    _enqueue_texture_upload(&ctx->deq, tex, offset, shape, stg, 0, size, data);

    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);
}



void dvz_download_texture(
    DvzContext* ctx, DvzTexture* tex, uvec3 offset, uvec3 shape, VkDeviceSize size, void* data)
{
    ASSERT(ctx != NULL);
    ASSERT(tex != NULL);
    ASSERT(data != NULL);
    ASSERT(size > 0);

    _full_tex_shape(tex, shape);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    // TODO: better alloc, mark unused alloc as done
    DvzBufferRegions stg = dvz_ctx_buffers(ctx, DVZ_BUFFER_TYPE_STAGING, 1, size);
    _enqueue_texture_download(&ctx->deq, tex, offset, shape, stg, 0, size, data);

    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);

    // Wait until the download is done.
    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_EV, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_EV);
}



void dvz_copy_texture(
    DvzContext* ctx, DvzTexture* src, uvec3 src_offset, DvzTexture* dst, uvec3 dst_offset,
    uvec3 shape, VkDeviceSize size)
{
    _enqueue_texture_copy(&ctx->deq, src, src_offset, dst, dst_offset, shape);

    dvz_deq_dequeue(&ctx->deq, DVZ_TRANSFER_PROC_CPY, true);
    dvz_deq_wait(&ctx->deq, DVZ_TRANSFER_PROC_UD);
}
