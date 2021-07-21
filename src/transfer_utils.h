/*************************************************************************************************/
/* Data transfer                                                                                 */
/*************************************************************************************************/

#ifndef DVZ_TRANSFERS_UTILS_HEADER
#define DVZ_TRANSFERS_UTILS_HEADER

#include "../include/datoviz/context.h"
#include "../include/datoviz/transfers.h"
#include "../include/datoviz/vklite.h"



/*************************************************************************************************/
/*  Utils                                                                                        */
/*************************************************************************************************/

static void _texture_shape(DvzTexture* texture, uvec3 shape)
{
    ASSERT(texture != NULL);
    ASSERT(texture->image != NULL);

    if (shape[0] == 0)
        shape[0] = texture->image->width;
    if (shape[1] == 0)
        shape[1] = texture->image->height;
    if (shape[2] == 0)
        shape[2] = texture->image->depth;
}



/*************************************************************************************************/
/*  New transfers                                                                                */
/*************************************************************************************************/

// Process for the deq proc #0, which encompasses the two queues UPLOAD and DOWNLOAD.
static void* _thread_transfers(void* user_data)
{
    DvzContext* ctx = (DvzContext*)user_data;
    ASSERT(ctx != NULL);
    dvz_deq_dequeue_loop(&ctx->deq, DVZ_TRANSFER_PROC_UD);
    return NULL;
}



/*************************************************************************************************/
/*  Create tasks                                                                                 */
/*************************************************************************************************/

// Create a mappable buffer transfer task, either UPLOAD or DOWNLOAD.
static DvzDeqItem* _create_buffer_transfer(
    DvzDataTransferType type, DvzBufferRegions br, VkDeviceSize offset, VkDeviceSize size,
    void* data)
{
    // Upload/download to mappable buffer only (otherwise, will need a GPU-GPU copy task too).

    ASSERT(br.buffer != NULL);
    ASSERT(size > 0);
    ASSERT(data != NULL);

    ASSERT(type == DVZ_TRANSFER_BUFFER_UPLOAD || type == DVZ_TRANSFER_BUFFER_DOWNLOAD);

    uint32_t deq_idx =
        type == DVZ_TRANSFER_BUFFER_UPLOAD ? DVZ_TRANSFER_DEQ_UL : DVZ_TRANSFER_DEQ_DL;

    DvzTransferBuffer* tr = (DvzTransferBuffer*)calloc(1, sizeof(DvzTransferBuffer));
    tr->br = br;
    tr->offset = offset;
    tr->size = size;
    tr->data = data;

    return dvz_deq_enqueue_custom(deq_idx, (int)type, tr);
}



// Create a buffer copy task.
static DvzDeqItem* _create_buffer_copy(
    DvzBufferRegions src, VkDeviceSize src_offset, DvzBufferRegions dst, VkDeviceSize dst_offset,
    VkDeviceSize size)
{
    ASSERT(src.buffer != NULL);
    ASSERT(dst.buffer != NULL);
    ASSERT(size > 0);

    DvzTransferBufferCopy* tr = (DvzTransferBufferCopy*)calloc(1, sizeof(DvzTransferBufferCopy));
    tr->src = src;
    tr->src_offset = src_offset;
    tr->dst = dst;
    tr->dst_offset = dst_offset;
    tr->size = size;

    return dvz_deq_enqueue_custom(DVZ_TRANSFER_DEQ_COPY, (int)DVZ_TRANSFER_BUFFER_COPY, tr);
}



// Create a buffer <-> texture copy task.
static DvzDeqItem* _create_buffer_texture_copy(
    DvzDataTransferType type,                                        //
    DvzBufferRegions br, VkDeviceSize buf_offset, VkDeviceSize size, //
    DvzTexture* tex, uvec3 tex_offset, uvec3 shape                   //
)
{
    ASSERT(type == DVZ_TRANSFER_TEXTURE_BUFFER || type == DVZ_TRANSFER_BUFFER_TEXTURE);
    ASSERT(br.buffer != NULL);
    ASSERT(size > 0);

    ASSERT(tex != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    DvzTransferBufferTexture* tr = (DvzTransferBufferTexture*)calloc(
        1, sizeof(DvzTransferBufferTexture)); // will be free-ed by the callbacks

    tr->br = br;
    tr->buf_offset = buf_offset;
    tr->size = size;

    tr->tex = tex;
    memcpy(tr->tex_offset, tex_offset, sizeof(uvec3));
    memcpy(tr->shape, shape, sizeof(uvec3));

    return dvz_deq_enqueue_custom(DVZ_TRANSFER_DEQ_COPY, (int)type, tr);
}



// Create a texture to texture copy task.
static DvzDeqItem* _create_texture_copy(
    DvzTexture* src, uvec3 src_offset,             //
    DvzTexture* dst, uvec3 dst_offset, uvec3 shape //
)
{
    ASSERT(src != NULL);
    ASSERT(dst != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    DvzTransferTextureCopy* tr = (DvzTransferTextureCopy*)calloc(
        1, sizeof(DvzTransferTextureCopy)); // will be free-ed by the callbacks
    tr->src = src;
    tr->dst = dst;
    memcpy(tr->src_offset, src_offset, sizeof(uvec3));
    memcpy(tr->dst_offset, dst_offset, sizeof(uvec3));
    memcpy(tr->shape, shape, sizeof(uvec3));

    return dvz_deq_enqueue_custom(DVZ_TRANSFER_DEQ_COPY, (int)DVZ_TRANSFER_TEXTURE_COPY, tr);
}



// Create a download done task.
static DvzDeqItem* _create_download_done(VkDeviceSize size, void* data)
{
    ASSERT(data != NULL);

    DvzTransferDownload* tr = (DvzTransferDownload*)calloc(
        1, sizeof(DvzTransferDownload)); // will be free-ed by the callbacks
    tr->size = size;
    tr->data = data;
    return dvz_deq_enqueue_custom(DVZ_TRANSFER_DEQ_EV, (int)DVZ_TRANSFER_DOWNLOAD_DONE, tr);
}



/*************************************************************************************************/
/*  Buffer transfer task enqueuing                                                               */
/*************************************************************************************************/

static void _enqueue_buffer_upload(
    DvzDeq* deq,                                   //
    DvzBufferRegions br, VkDeviceSize buf_offset,  // destination buffer
    DvzBufferRegions stg, VkDeviceSize stg_offset, // optional staging buffer
    VkDeviceSize size, void* data)
{
    ASSERT(deq != NULL);
    ASSERT(size > 0);
    ASSERT(data != NULL);
    log_trace("enqueue buffer upload");

    DvzDeqItem* deq_item = NULL;
    DvzDeqItem* next_item = NULL;

    if (stg.buffer == NULL)
    {
        // Upload in one step, directly to the destination buffer that is assumed to be mappable.
        deq_item = _create_buffer_transfer(DVZ_TRANSFER_BUFFER_UPLOAD, br, buf_offset, size, data);
    }
    else
    {
        // First, upload to the staging buffer.
        deq_item =
            _create_buffer_transfer(DVZ_TRANSFER_BUFFER_UPLOAD, stg, stg_offset, size, data);

        // Then, need to do a copy to the destination buffer.
        next_item = _create_buffer_copy(stg, stg_offset, br, buf_offset, size);

        // Dependency.
        dvz_deq_enqueue_next(deq_item, next_item, false);
    }

    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_buffer_download(
    DvzDeq* deq,                                   //
    DvzBufferRegions br, VkDeviceSize buf_offset,  //
    DvzBufferRegions stg, VkDeviceSize stg_offset, //
    VkDeviceSize size, void* data)
{
    ASSERT(deq != NULL);
    ASSERT(size > 0);
    ASSERT(data != NULL);
    log_trace("enqueue buffer download");

    DvzDeqItem* deq_item = NULL;
    DvzDeqItem* next_item = NULL;
    DvzDeqItem* done_item = NULL;

    if (stg.buffer == NULL)
    {
        // Upload in one step, directly to the destination buffer that is assumed to be mappable.
        deq_item =
            _create_buffer_transfer(DVZ_TRANSFER_BUFFER_DOWNLOAD, br, buf_offset, size, data);

        // Enqueue the DOWNLOAD_DONE item after the download has finished.
        done_item = _create_download_done(size, data);
        dvz_deq_enqueue_next(deq_item, done_item, false);
    }
    else
    {
        // First, need to do a copy to the staging buffer.
        deq_item = _create_buffer_copy(br, buf_offset, stg, stg_offset, size);

        // Then, download from the staging buffer.
        next_item =
            _create_buffer_transfer(DVZ_TRANSFER_BUFFER_DOWNLOAD, stg, stg_offset, size, data);

        // Dependency.
        dvz_deq_enqueue_next(deq_item, next_item, false);

        // Enqueue the DOWNLOAD_DONE item after the download has finished.
        done_item = _create_download_done(size, data);
        dvz_deq_enqueue_next(next_item, done_item, false);
    }

    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_buffer_copy(
    DvzDeq* deq,                                   //
    DvzBufferRegions src, VkDeviceSize src_offset, //
    DvzBufferRegions dst, VkDeviceSize dst_offset, //
    VkDeviceSize size)
{
    ASSERT(deq != NULL);
    ASSERT(src.buffer != NULL);
    ASSERT(dst.buffer != NULL);
    ASSERT(size > 0);
    log_trace("enqueue buffer copy");

    DvzDeqItem* deq_item = _create_buffer_copy(src, src_offset, dst, dst_offset, size);
    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_buffer_download_done(DvzDeq* deq, VkDeviceSize size, void* data)
{
    ASSERT(deq != NULL);
    ASSERT(size > 0);
    log_trace("enqueue buffer download done");
    dvz_deq_enqueue_submit(deq, _create_download_done(size, data), false);
}



/*************************************************************************************************/
/*  Texture transfer task enqueuing                                                              */
/*************************************************************************************************/

static void _enqueue_texture_upload(
    DvzDeq* deq, DvzTexture* tex, uvec3 offset, uvec3 shape, //
    DvzBufferRegions stg, VkDeviceSize stg_offset, VkDeviceSize size, void* data)
{
    ASSERT(deq != NULL);

    ASSERT(tex != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(stg.buffer != NULL);

    ASSERT(size > 0);
    ASSERT(data != NULL);

    log_trace("enqueue texture upload");

    DvzDeqItem* deq_item = NULL;
    DvzDeqItem* next_item = NULL;

    // First, upload to the staging buffer.
    deq_item = _create_buffer_transfer(DVZ_TRANSFER_BUFFER_UPLOAD, stg, stg_offset, size, data);

    // Then, need to do a copy to the texture.
    next_item = _create_buffer_texture_copy(
        DVZ_TRANSFER_BUFFER_TEXTURE, stg, stg_offset, size, tex, offset, shape);

    // Dependency.
    dvz_deq_enqueue_next(deq_item, next_item, false);

    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_texture_download(
    DvzDeq* deq, DvzTexture* tex, uvec3 offset, uvec3 shape, //
    DvzBufferRegions stg, VkDeviceSize stg_offset, VkDeviceSize size, void* data)
{
    ASSERT(deq != NULL);

    ASSERT(tex != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(stg.buffer != NULL);

    ASSERT(size > 0);
    ASSERT(data != NULL);

    log_trace("enqueue texture download");

    DvzDeqItem* deq_item = NULL;
    DvzDeqItem* next_item = NULL;

    // First, need to do a copy to the texture.
    deq_item = _create_buffer_texture_copy(
        DVZ_TRANSFER_TEXTURE_BUFFER, stg, stg_offset, size, tex, offset, shape);

    // Then, download from the staging buffer.
    next_item = _create_buffer_transfer(DVZ_TRANSFER_BUFFER_DOWNLOAD, stg, stg_offset, size, data);

    // Dependency.
    dvz_deq_enqueue_next(deq_item, next_item, false);

    // Enqueue the DOWNLOAD_DONE item after the download has finished.
    DvzDeqItem* done_item = _create_download_done(size, data);
    dvz_deq_enqueue_next(next_item, done_item, false);

    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_texture_copy(
    DvzDeq* deq, DvzTexture* src, uvec3 src_offset, DvzTexture* dst, uvec3 dst_offset, uvec3 shape)
{
    ASSERT(deq != NULL);
    ASSERT(src != NULL);
    ASSERT(dst != NULL);

    log_trace("enqueue texture copy");

    DvzDeqItem* deq_item = _create_texture_copy(src, src_offset, dst, dst_offset, shape);
    dvz_deq_enqueue_submit(deq, deq_item, false);
}



/*************************************************************************************************/
/*  Buffer/Texture copy transfer task enqueuing                                                  */
/*************************************************************************************************/

static void _enqueue_texture_buffer(
    DvzDeq* deq,                                    //
    DvzTexture* tex, uvec3 tex_offset, uvec3 shape, //
    DvzBufferRegions br, VkDeviceSize buf_offset, VkDeviceSize size)
{
    ASSERT(deq != NULL);

    ASSERT(tex != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(br.buffer != NULL);
    ASSERT(size > 0);

    log_trace("enqueue texture buffer copy");

    // First, need to do a copy to the texture.
    DvzDeqItem* deq_item = _create_buffer_texture_copy(
        DVZ_TRANSFER_TEXTURE_BUFFER, br, buf_offset, size, tex, tex_offset, shape);
    dvz_deq_enqueue_submit(deq, deq_item, false);
}



static void _enqueue_buffer_texture(
    DvzDeq* deq,                                                     //
    DvzBufferRegions br, VkDeviceSize buf_offset, VkDeviceSize size, //
    DvzTexture* tex, uvec3 tex_offset, uvec3 shape                   //
)
{
    ASSERT(deq != NULL);

    ASSERT(tex != NULL);
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    ASSERT(br.buffer != NULL);
    ASSERT(size > 0);

    log_trace("enqueue texture buffer copy");

    // First, need to do a copy to the texture.
    DvzDeqItem* deq_item = _create_buffer_texture_copy(
        DVZ_TRANSFER_TEXTURE_BUFFER, br, buf_offset, size, tex, tex_offset, shape);
    dvz_deq_enqueue_submit(deq, deq_item, false);
}



/*************************************************************************************************/
/*  Buffer transfer task processing                                                              */
/*************************************************************************************************/

// NOTE: only upload to mappable buffer
static void _process_buffer_upload(DvzDeq* deq, void* item, void* user_data)
{
    DvzTransferBuffer* tr = (DvzTransferBuffer*)item;
    ASSERT(tr != NULL);
    log_trace("process mappable buffer upload");

    // Copy the data to the staging buffer.
    ASSERT(tr->br.buffer != NULL);
    ASSERT(tr->br.size > 0);
    ASSERT(tr->size > 0);
    ASSERT(tr->offset + tr->size <= tr->br.size);

    // Take offset and size into account in the staging buffer.
    // NOTE: this call blocks while the data is being copied from CPU to GPU (mapped memcpy).
    dvz_buffer_regions_upload(&tr->br, 0, tr->offset, tr->size, tr->data);
}



static void _process_buffer_download(DvzDeq* deq, void* item, void* user_data)
{
    DvzTransferBuffer* tr = (DvzTransferBuffer*)item;
    ASSERT(tr != NULL);
    log_trace("process mappable buffer download");

    // Copy the data to the staging buffer.
    ASSERT(tr->br.buffer != NULL);
    ASSERT(tr->br.size > 0);
    ASSERT(tr->size > 0);
    ASSERT(tr->offset + tr->size <= tr->br.size);

    // Take offset and size into account in the staging buffer.
    // NOTE: this call blocks while the data is being copied from GPU to CPU (mapped
    // memcpy).
    dvz_buffer_regions_download(&tr->br, 0, tr->offset, tr->size, tr->data);
}



static void _process_buffer_copy(DvzDeq* deq, void* item, void* user_data)
{
    ASSERT(user_data != NULL);
    DvzContext* ctx = (DvzContext*)user_data;
    log_trace("process buffer copy (sync)");

    DvzTransferBufferCopy* tr = (DvzTransferBufferCopy*)item;
    ASSERT(tr != NULL);

    // Make the GPU-GPU buffer copy (block the GPU and wait for the copy to finish).
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_RENDER);
    dvz_buffer_regions_copy(&tr->src, tr->src_offset, &tr->dst, tr->dst_offset, tr->size);
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



/*************************************************************************************************/
/*  Buffer/Texture copy transfer task processing                                                 */
/*************************************************************************************************/

static void _process_texture_buffer(DvzDeq* deq, void* item, void* user_data)
{
    DvzTransferBufferTexture* tr = (DvzTransferBufferTexture*)item;
    ASSERT(tr != NULL);
    log_trace("process copy texture to buffer (sync)");

    // Copy the data to the staging buffer.
    ASSERT(tr->tex != NULL);
    ASSERT(tr->br.buffer != NULL);

    DvzContext* ctx = tr->tex->context;
    ASSERT(ctx != NULL);

    ASSERT(tr->shape[0] > 0);
    ASSERT(tr->shape[1] > 0);
    ASSERT(tr->shape[2] > 0);

    dvz_texture_copy_to_buffer(
        tr->tex, tr->tex_offset, tr->shape, tr->br, tr->buf_offset, tr->size);
    // Wait for the copy to be finished.
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



static void _process_buffer_texture(DvzDeq* deq, void* item, void* user_data)
{
    DvzTransferBufferTexture* tr = (DvzTransferBufferTexture*)item;
    ASSERT(tr != NULL);
    log_trace("process copy buffer to texture (sync)");

    // Copy the data to the staging buffer.
    ASSERT(tr->tex != NULL);
    ASSERT(tr->br.buffer != NULL);

    DvzContext* ctx = tr->tex->context;
    ASSERT(ctx != NULL);

    ASSERT(tr->shape[0] > 0);
    ASSERT(tr->shape[1] > 0);
    ASSERT(tr->shape[2] > 0);

    dvz_texture_copy_from_buffer(
        tr->tex, tr->tex_offset, tr->shape, tr->br, tr->buf_offset, tr->size);
    // Wait for the copy to be finished.
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



/*************************************************************************************************/
/*  Texture transfer task processing                                                             */
/*************************************************************************************************/

static void _process_texture_copy(DvzDeq* deq, void* item, void* user_data)
{
    ASSERT(user_data != NULL);
    DvzContext* ctx = (DvzContext*)user_data;
    log_trace("process texture copy");

    DvzTransferTextureCopy* tr = (DvzTransferTextureCopy*)item;
    ASSERT(tr != NULL);

    // Make the GPU-GPU buffer copy (block the GPU and wait for the copy to finish).
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_RENDER);
    dvz_texture_copy(tr->src, tr->src_offset, tr->dst, tr->dst_offset, tr->shape);
    dvz_queue_wait(ctx->gpu, DVZ_DEFAULT_QUEUE_TRANSFER);
}



#endif
