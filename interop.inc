/* Opaque native interop. Callers never access backend object layouts. */
size_t encore_vk_create_named_extensions(bool debug, const encore_str *names, size_t count) {
    if (count > UINT32_MAX || (count && !names)) return 0;
    char **extensions = count ? calloc(count, sizeof(char *)) : NULL;
    if (count && !extensions) return 0;
    size_t built = 0;
    for (; built < count; ++built) {
        extensions[built] = encore_c_string(names[built]);
        if (!extensions[built]) break;
    }
    size_t result = built == count ? encore_vk_create_extensions(debug,
        (const char *const *)extensions, (uint32_t)count) : 0;
    for (size_t i = 0; i < built; ++i) free(extensions[i]);
    free(extensions);
    return result;
}
size_t encore_vk_surface_borrow(size_t context, uint64_t surface, uint32_t width, uint32_t height) {
    return encore_vk_surface_attach(context, surface, width, height, NULL, NULL, NULL);
}
bool encore_vk_surface_resize(size_t handle, uint32_t width, uint32_t height) {
    EncoreVkSurface *surface = (EncoreVkSurface *)handle;
    if (!surface) return false;
    surface->width = width; surface->height = height;
    return true;
}
void *encore_vk_buffer_map(size_t handle, uint64_t bytes) {
    EncoreVkBuffer *b = (EncoreVkBuffer *)handle;
    void *mapped = NULL;
    if (!b || !b->available || !b->host_visible || !bytes || bytes > b->size) return NULL;
    if (b->owner->map_memory(b->owner->device,b->memory,0,bytes,0,&mapped)!=VK_SUCCESS) return NULL;
    return mapped;
}
void encore_vk_buffer_unmap(size_t handle) {
    EncoreVkBuffer *b = (EncoreVkBuffer *)handle;
    if (b && b->available && b->host_visible) b->owner->unmap_memory(b->owner->device,b->memory);
}
size_t encore_vk_swapchain_device(size_t handle) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)handle; return s ? (size_t)s->owner : 0;
}
bool encore_vk_swapchain_prepare(size_t handle,size_t pipeline) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)handle;
    EncoreVkPipeline *p=(EncoreVkPipeline *)pipeline;
    if (!s || !p || !s->available || !p->available || s->owner != p->owner) return false;
    return encore_vk_swapchain_prepare_pipeline(s,p);
}
const char *encore_vk_swapchain_error_cstr(size_t handle) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)handle; return s ? s->error : "Invalid swapchain";
}
const char *encore_vk_pipeline_error_cstr(size_t handle) {
    EncoreVkPipeline *p=(EncoreVkPipeline *)handle; return p ? p->error : "Invalid pipeline";
}
const char *encore_vk_frame_error_cstr(size_t handle) {
    EncoreVkFrame *f=(EncoreVkFrame *)handle; return f ? f->error : "Invalid frame";
}
size_t encore_vk_frame_swapchain(size_t handle) {
    EncoreVkFrame *f=(EncoreVkFrame *)handle; return f ? (size_t)f->owner : 0;
}
bool encore_vk_frame_in_pass(size_t handle) {
    EncoreVkFrame *f=(EncoreVkFrame *)handle; return f && f->in_render_pass;
}
bool encore_vk_swapchain_present_mode(size_t handle,uint32_t mode) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)handle;
    if (!s || !s->available || (mode != VK_PRESENT_MODE_FIFO_KHR && mode != VK_PRESENT_MODE_IMMEDIATE_KHR)) return false;
    if (s->requested_present_mode!=mode) { s->requested_present_mode=mode; s->needs_rebuild=true; }
    return true;
}
bool encore_vk_frame_scissor(size_t handle,int32_t x,int32_t y,uint32_t width,uint32_t height) {
    EncoreVkFrame *f=(EncoreVkFrame *)handle;
    if (!f || !f->recording || f->complete || !f->in_render_pass || x<0 || y<0 ||
        (uint32_t)x>f->owner->width || (uint32_t)y>f->owner->height ||
        width>f->owner->width-(uint32_t)x || height>f->owner->height-(uint32_t)y) return false;
    VkRect2D r={{x,y},{width,height}};
    f->owner->cmd_set_scissor(f->command_buffer,0,1,&r); return true;
}
size_t encore_vk_texture_borrow(size_t device,uint64_t image,uint32_t width,uint32_t height,uint32_t format,uint32_t layout) {
    EncoreVkDevice *d=(EncoreVkDevice *)device;
    if (!d || !d->available || !image || !width || !height) return 0;
    EncoreVkTexture *t=calloc(1,sizeof(*t)); if (!t) return 0;
    t->owner=d; t->image=(void *)(uintptr_t)image; t->width=width; t->height=height;
    t->mip_levels=1; t->format=format; t->layout=layout; t->aspect=format==126 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,NULL,0,t->image,1,format,{0,0,0,0},{t->aspect,0,1,0,1}};
    if (d->create_image_view(d->device,&view,NULL,&t->view)!=VK_SUCCESS) { free(t); return 0; }
    t->available=true; return (size_t)t;
}

/* Explicit diagnostic readback, never used by the presentation hot path.
 * A NULL destination queries the required size without submitting GPU work. */
static size_t encore_vk_readback_image(EncoreVkDevice *d,void *image,uint32_t width,uint32_t height,uint32_t format,uint32_t layout,uint32_t aspect,uintptr_t destination,size_t capacity) {
    if (format!=37 && format!=43 && format!=44 && format!=50 && format!=97 && format!=126) return 0;
    size_t bytes=(size_t)width*height*(format==97?8:4);
    if (!destination) return bytes;
    if (capacity<bytes || layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return 0;
    size_t buffer_handle=encore_vk_buffer_create((size_t)d,bytes,2,true);
    EncoreVkBuffer *buffer=(EncoreVkBuffer *)buffer_handle;
    size_t encoder_handle=encore_vk_encoder_create((size_t)d);
    EncoreVkEncoder *encoder=(EncoreVkEncoder *)encoder_handle;
    if (!buffer || !buffer->available || !encoder || !encoder->recording) {
        encore_vk_encoder_destroy(encoder_handle);encore_vk_buffer_destroy(buffer_handle);return 0;
    }
    VkImageMemoryBarrier transfer={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,image,{aspect,0,1,0,1}};
    d->cmd_pipeline_barrier(encoder->command_buffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&transfer);
    VkBufferImageCopy copy={0,0,0,{aspect,0,0,1},{0,0,0},{width,height,1}};
    d->cmd_copy_image_to_buffer(encoder->command_buffer,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer->buffer,1,&copy);
    VkImageMemoryBarrier restore=transfer;
    restore.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;restore.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    restore.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;restore.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    d->cmd_pipeline_barrier(encoder->command_buffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&restore);
    encore_vk_encoder_transfer_host_barrier(encoder_handle);
    size_t submission=encore_vk_encoder_submit(encoder_handle);
    bool complete=encore_vk_submission_wait(submission,UINT64_MAX);
    if (complete && buffer->mapped) memcpy((void *)destination,buffer->mapped,bytes); else complete=false;
    encore_vk_submission_destroy(submission);encore_vk_encoder_destroy(encoder_handle);encore_vk_buffer_destroy(buffer_handle);
    return complete?bytes:0;
}

size_t encore_vk_target_readback(size_t target,uint32_t attachment,uintptr_t destination,size_t capacity) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)target;
    if (!s || !s->available || !s->shared || s->frame_active || attachment>2 ||
        (attachment==1 && !s->auxiliary_images) || (attachment==2 && !s->sampled_depth))return 0;
    return encore_vk_readback_image(s->owner,(void *)encore_vk_target_attachment(target,attachment),s->width,s->height,
        attachment==1?s->auxiliary_format:attachment==2?126:s->format,s->external_layout,
        attachment==2?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,destination,capacity);
}
/* Diagnostic only. Caller must have submitted the last writer. */
size_t encore_vk_texture_readback(size_t texture,uintptr_t destination,size_t capacity) {
    EncoreVkTexture *t=(EncoreVkTexture *)texture;
    if(!t || !t->available || t->mip_levels!=1)return 0;
    return encore_vk_readback_image(t->owner,t->image,t->width,t->height,t->format,t->layout,t->aspect,destination,capacity);
}
