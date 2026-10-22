/* SDKs are optional plugins. Legacy rendering never requires a vendor runtime. */
#include "reconstruction_api.h"
#include <math.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif
typedef struct {
    EncoreVkDevice *device;
    const EncoreReconstructionApi *api;
    void *context;
    atomic_bool faulted;
    char error[512];
} EncoreVkReconstruction;
static const EncoreReconstructionApi *encore_reconstruction_api;
static char encore_reconstruction_load_error[512];
static const EncoreReconstructionApi *encore_reconstruction_load(void) {
    if (encore_reconstruction_api) return encore_reconstruction_api;
#if !defined(_WIN32)
    const char *path=getenv("ENCORE_RECONSTRUCTION_LIBRARY");
    char adjacent[4096];
    if (!path || !*path) {
        ssize_t n=readlink("/proc/self/exe",adjacent,sizeof(adjacent)-64);
        if (n<=0) { snprintf(encore_reconstruction_load_error,sizeof(encore_reconstruction_load_error),"Reconstruction runtime is not installed");return NULL; }
        adjacent[n]=0;char *end=strrchr(adjacent,'/');
        if (!end)return NULL;
        strcpy(end+1,"libencore_reconstruction.so");path=adjacent;
    }
    void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if (!library) {snprintf(encore_reconstruction_load_error,sizeof(encore_reconstruction_load_error),"Reconstruction runtime unavailable: %.400s",dlerror());return NULL;}
    EncoreReconstructionEntry entry=(EncoreReconstructionEntry)dlsym(library,"encore_reconstruction_get_api");
    const EncoreReconstructionApi *api=entry?entry(ENCORE_RECONSTRUCTION_ABI):NULL;
    if (!api || api->abi!=ENCORE_RECONSTRUCTION_ABI || !api->create || !api->dispatch || !api->destroy || !api->capabilities) {
        dlclose(library);snprintf(encore_reconstruction_load_error,sizeof(encore_reconstruction_load_error),"Incompatible reconstruction runtime");return NULL;
    }
    /* Process lifetime: contexts and queued command packets retain this code. */
    encore_reconstruction_api=api;return api;
#else
    snprintf(encore_reconstruction_load_error,sizeof(encore_reconstruction_load_error),"Reconstruction runtime is unavailable on this build");return NULL;
#endif
}
static EncoreReconstructionDevice encore_reconstruction_device(EncoreVkDevice *d) {
    /* SDK command emission uses real Vulkan entrypoints on its recording worker. */
    EncoreParallelDevice *p=encore_parallel_device(d->device);
    return (EncoreReconstructionDevice){(uintptr_t)d->context->instance,(uintptr_t)d->physical_device,
        (uintptr_t)d->device,(uintptr_t)d->context->get_proc,
        (uintptr_t)(p?p->proc:d->get_device_proc),d->queue_family};
}
uint32_t encore_vk_reconstruction_capabilities(size_t device) {
    EncoreVkDevice *d=(EncoreVkDevice *)device;
    if (!d || !d->available)return 0;
    const EncoreReconstructionApi *api=encore_reconstruction_load();
    if (!api)return 0;
    EncoreReconstructionDevice descriptor=encore_reconstruction_device(d);
    return api->capabilities(&descriptor,encore_reconstruction_load_error,sizeof(encore_reconstruction_load_error));
}
uint64_t encore_vk_reconstruction_resolution(size_t device,uint32_t method,uint32_t width,uint32_t height,uint32_t quality) {
    EncoreVkDevice *d=(EncoreVkDevice *)device;const EncoreReconstructionApi *api=encore_reconstruction_load();
    if(!d || !d->available || !api || !api->resolution)return 0;
    EncoreReconstructionDevice descriptor=encore_reconstruction_device(d);uint32_t w=0,h=0;
    return api->resolution(&descriptor,method,width,height,quality,&w,&h)?((uint64_t)w<<32)|h:0;
}
size_t encore_vk_reconstruction_create(size_t device,uint32_t method,uint32_t iw,uint32_t ih,uint32_t ow,uint32_t oh,uint32_t quality) {
    EncoreVkDevice *d=(EncoreVkDevice *)device;
    if (!d || !d->available || !iw || !ih || !ow || !oh || method<1 || method>5 || quality>4 || (method==3 && (iw!=ow || ih!=oh)))return 0;
    EncoreVkReconstruction *r=calloc(1,sizeof(*r));if (!r)return 0;
    atomic_init(&r->faulted,false);r->device=d;r->api=encore_reconstruction_load();
    if (!r->api) {snprintf(r->error,sizeof(r->error),"%s",encore_reconstruction_load_error);return (size_t)r;}
    EncoreReconstructionDevice descriptor=encore_reconstruction_device(d);
    EncoreReconstructionDescription desc={method,iw,ih,ow,oh,quality};
    r->context=r->api->create(&descriptor,&desc,r->error,sizeof(r->error));
    return (size_t)r;
}
bool encore_vk_reconstruction_available(size_t handle) {EncoreVkReconstruction *r=(EncoreVkReconstruction *)handle;return r && r->context && !atomic_load_explicit(&r->faulted,memory_order_acquire);}
encore_str encore_vk_reconstruction_error(size_t handle) {
    EncoreVkReconstruction *r=(EncoreVkReconstruction *)handle;
    /* The recording worker publishes its error before releasing faulted. */
    if(r && r->context && !atomic_load_explicit(&r->faulted,memory_order_acquire))return encore_string("");
    return encore_string(r?r->error:encore_reconstruction_load_error);
}
static EncoreReconstructionImage encore_reconstruction_attachment(EncoreVkSwapchain *s,uint32_t attachment) {
    uint32_t i=s->last_image_index;
    void *image=attachment==0?s->images[i]:attachment==1?(s->auxiliary_images?s->auxiliary_images[i]:NULL):(s->sampled_depth?s->depth_images[i]:NULL);
    void *view=attachment==0?s->views[i]:attachment==1?(s->auxiliary_views?s->auxiliary_views[i]:NULL):(s->sampled_depth?s->depth_views[i]:NULL);
    return (EncoreReconstructionImage){(uintptr_t)image,(uintptr_t)view,s->width,s->height,attachment==0?s->format:attachment==1?s->auxiliary_format:126,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
typedef struct {EncoreVkReconstruction *context;uintptr_t command;EncoreReconstructionFrame frame;EncoreParallelCommand *owner;} EncoreReconstructionPacket;
static void encore_reconstruction_replay(unsigned char *bytes) {
    EncoreReconstructionPacket *p=(EncoreReconstructionPacket *)bytes;
    bool success=!atomic_load_explicit(&p->context->faulted,memory_order_acquire) &&
        p->context->api->dispatch(p->context->context,p->command,&p->frame,p->context->error,sizeof(p->context->error));
    if(!success) {
        /* A failed optional provider must not poison the whole Vulkan device.
         * Initialize the output before consumers, then let policy select its
         * fallback on the next logical frame. Never publish undefined VRAM. */
        EncoreReconstructionDevice d=encore_reconstruction_device(p->context->device);
        PFN_vkGetDeviceProcAddr proc=(PFN_vkGetDeviceProcAddr)d.get_device_proc;
        PFN_vkCmdPipelineBarrier barrier=(PFN_vkCmdPipelineBarrier)proc((void *)d.device,"vkCmdPipelineBarrier");
        PFN_vkCmdClearColorImage clear=(PFN_vkCmdClearColorImage)proc((void *)d.device,"vkCmdClearColorImage");
        VkImageMemoryBarrier visibility={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,(void *)p->frame.output.image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        barrier((void *)p->command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&visibility);
        VkClearColorValue color={{0.028f,0.034f,0.043f,1.f}};VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        clear((void *)p->command,(void *)p->frame.output.image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
        atomic_store_explicit(&p->context->faulted,true,memory_order_release);
    }
}
bool encore_vk_reconstruction_record_images(size_t handle,size_t frame,size_t source,size_t output,size_t color_override,size_t reactive,size_t composition,const float *values,size_t count) {
    EncoreVkReconstruction *r=(EncoreVkReconstruction *)handle;EncoreVkFrame *f=(EncoreVkFrame *)frame;
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)source;EncoreVkTexture *t=(EncoreVkTexture *)output;
    if (!encore_vk_reconstruction_available(handle) || !f || !f->recording || f->in_render_pass || !s || !t || !t->available ||
        r->device!=s->owner || r->device!=t->owner || r->device!=f->owner->owner || !s->sampled_depth || !s->auxiliary_images || count<16 || !values)return false;
    for(size_t i=0;i<count;++i)if(!isfinite(values[i]))return false;
    if(values[4]<=0 || values[5]<=0 || values[6]<=values[5] || values[7]<=0 || values[7]>=3.141593f ||
       values[11]<0 || values[11]>=4294967296.0f || values[12]<0 || values[12]>4 || values[13]<0 || values[13]>3)return false;
    EncoreReconstructionPacket packet={.context=r,.command=(uintptr_t)f->command_buffer};
    packet.frame.color=encore_reconstruction_attachment(s,0);packet.frame.motion=encore_reconstruction_attachment(s,1);packet.frame.depth=encore_reconstruction_attachment(s,2);
    size_t textures[]={color_override,reactive,composition};
    EncoreReconstructionImage *images[]={&packet.frame.color,&packet.frame.reactive,&packet.frame.composition};
    for(unsigned i=0;i<3;++i)if(textures[i]) {
        EncoreVkTexture *image=(EncoreVkTexture *)textures[i];
        if(!image->available || image->owner!=r->device || image->image==t->image)return false;
        *images[i]=(EncoreReconstructionImage){(uintptr_t)image->image,(uintptr_t)image->view,image->width,image->height,image->format,image->layout};
    }
    packet.frame.output=(EncoreReconstructionImage){(uintptr_t)t->image,(uintptr_t)t->view,t->width,t->height,t->format,VK_IMAGE_LAYOUT_GENERAL};
    packet.frame.jitter_x=values[0];packet.frame.jitter_y=values[1];packet.frame.previous_jitter_x=values[2];packet.frame.previous_jitter_y=values[3];
    packet.frame.delta_ms=values[4];packet.frame.near_plane=values[5];packet.frame.far_plane=values[6];packet.frame.vertical_fov=values[7];
    packet.frame.sharpness=values[8];packet.frame.exposure=values[9];packet.frame.reset=values[10]!=0;
    packet.frame.frame_index=(uint32_t)values[11];packet.frame.multiplier=(uint32_t)values[12];packet.frame.generated_index=(uint32_t)values[13];
    if (count>=48) {memcpy(packet.frame.clip_to_previous,values+16,64);memcpy(packet.frame.previous_to_clip,values+32,64);}
    for(unsigned j=0;j<4;++j){packet.frame.view_projection[j*5]=1;packet.frame.previous_view_projection[j*5]=1;packet.frame.projection[j*5]=1;packet.frame.view[j*5]=1;}
    if(count>=112){memcpy(packet.frame.view_projection,values+48,64);memcpy(packet.frame.previous_view_projection,values+64,64);memcpy(packet.frame.projection,values+80,64);memcpy(packet.frame.view,values+96,64);}
    VkImageMemoryBarrier before={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT,
        t->layout,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,t->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    r->device->cmd_pipeline_barrier(f->command_buffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&before);
    EncoreParallelCommand *c=encore_parallel_command(f->command_buffer);
    /* Stateful SDK host calls must follow temporal submission order, too.
     * The common recording lane is FIFO; unrelated passes retain all workers.
     * A command is not enqueued until present/submit, so reassignment here is
     * safe (begin/reset already joined its previous task). */
    if(c)c->worker=&c->owner->workers[0];
    if (c) {packet.owner=c;EncoreReconstructionPacket *p=encore_parallel_append(c,sizeof(packet),encore_reconstruction_replay);if (!p)return false;*p=packet;}
    else encore_reconstruction_replay((unsigned char *)&packet);
    VkImageMemoryBarrier after=before;after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    after.oldLayout=VK_IMAGE_LAYOUT_GENERAL;after.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    after.srcAccessMask|=VK_ACCESS_TRANSFER_WRITE_BIT;
    r->device->cmd_pipeline_barrier(f->command_buffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&after);
    t->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;return true;
}
bool encore_vk_reconstruction_record(size_t handle,size_t frame,size_t source,size_t output,const float *values,size_t count) {
    return encore_vk_reconstruction_record_images(handle,frame,source,output,0,0,0,values,count);
}
bool encore_vk_reconstruction_destroy(size_t handle) {
    EncoreVkReconstruction *r=(EncoreVkReconstruction *)handle;if (!r)return false;
    EncoreParallelDevice *p=encore_parallel_device(r->device->device);if(p)encore_parallel_drain(p);
    /* Caller must retire GPU use before destroying the view/context. */
    if (r->context)r->api->destroy(r->context);free(r);return true;
}

size_t encore_vk_texture_image(size_t texture) {EncoreVkTexture *t=(EncoreVkTexture *)texture;return t && t->available?(size_t)t->image:0;}

void encore_vk_presentation_yield(void) {struct timespec delay={0,500000};thrd_sleep(&delay,NULL);}
