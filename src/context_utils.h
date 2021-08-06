#ifndef DVZ_CONTEXT_UTILS_HEADER
#define DVZ_CONTEXT_UTILS_HEADER

#include "../include/datoviz/context.h"

#ifdef __cplusplus
extern "C" {
#endif



/*************************************************************************************************/
/*  Staging buffer                                                                               */
/*************************************************************************************************/

// Get the staging buffer, and make sure it can contain `size` bytes.
static DvzBuffers* staging_buffer(DvzContext* context, VkDeviceSize size)
{
    log_trace("requesting staging buffer of size %s", pretty_size(size));
    DvzBuffers* staging =
        (DvzBuffers*)dvz_container_get(&context->buffers, DVZ_BUFFER_TYPE_STAGING);
    ASSERT(staging != NULL);
    ASSERT(staging->buffer != VK_NULL_HANDLE);

    // Resize the staging buffer is needed.
    // TODO: keep staging buffer fixed and copy parts of the data to staging buffer in several
    // steps?
    if (staging->size < size)
    {
        VkDeviceSize new_size = dvz_next_pow2(size);
        log_debug("reallocating staging buffer to %s", pretty_size(new_size));
        dvz_buffer_resize(staging, new_size);
    }
    ASSERT(staging->size >= size);
    return staging;
}



/*************************************************************************************************/
/*  Default resources                                                                            */
/*************************************************************************************************/

static DvzTexture* _default_transfer_texture(DvzContext* context)
{
    ASSERT(context != NULL);
    DvzGpu* gpu = context->gpu;
    ASSERT(gpu != NULL);

    uvec3 shape = {256, 1, 1};
    DvzTexture* texture = dvz_ctx_texture(context, 1, shape, VK_FORMAT_R32_SFLOAT);
    float* tex_data = (float*)calloc(256, sizeof(float));
    for (uint32_t i = 0; i < 256; i++)
        tex_data[i] = i / 255.0;
    dvz_texture_address_mode(texture, DVZ_TEXTURE_AXIS_U, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    uvec3 offset = {0, 0, 0};

    dvz_upload_texture(context, texture, offset, shape, 256 * sizeof(float), tex_data);
    // dvz_queue_wait(gpu, DVZ_DEFAULT_QUEUE_TRANSFER);

    FREE(tex_data);
    return texture;
}



#ifdef __cplusplus
}
#endif

#endif
