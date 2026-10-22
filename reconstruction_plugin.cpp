/* Optional SDK bridge. This software contains source code provided by NVIDIA
 * Corporation. SDK headers and redistributables retain their upstream license. */
#include "reconstruction_api.h"
#include "reconstruction_camera.h"
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssg_vk.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "reconstruction_display.h"
#include <dlfcn.h>
#ifdef ENCORE_WITH_FSR
extern "C" void *encore_fsr_create(const EncoreReconstructionDevice *,const EncoreReconstructionDescription *,char *,size_t);
extern "C" int encore_fsr_dispatch(void *,uintptr_t,const EncoreReconstructionFrame *,char *,size_t);
extern "C" void encore_fsr_destroy(void *);
#endif

namespace {
std::recursive_mutex sdk_mutex;
struct NgxDevice { EncoreReconstructionDevice vk; uint32_t capabilities=0; bool initialized=false; };
std::map<uintptr_t,std::weak_ptr<NgxDevice>> devices;
void log_ngx(const char *message,NVSDK_NGX_Logging_Level,NVSDK_NGX_Feature) {
    if (std::getenv("ENCORE_RECONSTRUCTION_DEBUG"))std::fprintf(stderr,"NGX: %s\n",message);
}
std::shared_ptr<NgxDevice> device(const EncoreReconstructionDevice *vk,char *error,size_t capacity) {
    if (auto shared=devices[vk->device].lock())return shared;
    auto state=std::shared_ptr<NgxDevice>(new NgxDevice{*vk},[](NgxDevice *d){
        std::lock_guard<std::recursive_mutex> lock(sdk_mutex);
        if(d->initialized)NVSDK_NGX_VULKAN_Shutdown1((VkDevice)d->vk.device);delete d;
    });
    Dl_info info{};dladdr((void *)&log_ngx,&info);
    std::string path=info.dli_fname?info.dli_fname:".";
    path=path.substr(0,path.find_last_of('/'));
    std::wstring wide(path.begin(),path.end());const wchar_t *paths[]={wide.c_str()};
    NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
    common.LoggingInfo.LoggingCallback=log_ngx;common.LoggingInfo.DisableOtherLoggingSinks=true;
    NVSDK_NGX_Result status=NVSDK_NGX_VULKAN_Init_with_ProjectID(
        "f24ace02-d9fa-4815-8a50-57dcb9235620",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"0.1.0",L"/tmp",
        (VkInstance)vk->instance,(VkPhysicalDevice)vk->physical_device,(VkDevice)vk->device,
        (PFN_vkGetInstanceProcAddr)vk->get_instance_proc,(PFN_vkGetDeviceProcAddr)vk->get_device_proc,&common);
    if(NVSDK_NGX_FAILED(status)){std::snprintf(error,capacity,"NGX initialization failed (0x%x)",unsigned(status));return {};}
    state->initialized=true;
    NVSDK_NGX_Parameter *params=nullptr;status=NVSDK_NGX_VULKAN_GetCapabilityParameters(&params);
    if(NVSDK_NGX_SUCCEED(status) && params) {
        int sr=0,fg=0;unsigned max_count=0;
        params->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&sr);
        params->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&fg);
        params->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&max_count);
        if(std::getenv("ENCORE_RECONSTRUCTION_DEBUG")) {
            int init=0,needs=0;unsigned major=0,minor=0;
            params->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,&init);
            params->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&needs);
            params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,&major);
            params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,&minor);
            std::fprintf(stderr,"NGX SR=%d init=0x%x update=%d minimum=%u.%u FG=%d max=%u\n",sr,unsigned(init),needs,major,minor,fg,max_count);
        }
        if(sr)state->capabilities|=2u|4u;
        if(fg)state->capabilities|=16u;
        if(fg && max_count>=2)state->capabilities|=32u;
        if(fg && max_count>=3)state->capabilities|=64u;
        NVSDK_NGX_VULKAN_DestroyParameters(params);
    }
    devices[vk->device]=state;return state;
}
struct Context {
    EncoreReconstructionDevice vk{};
    std::unique_ptr<encore_reconstruction::DisplayColor> display;
    std::shared_ptr<NgxDevice> device;
    EncoreReconstructionDescription desc;
    NVSDK_NGX_Parameter *params=nullptr;
    NVSDK_NGX_Handle *feature=nullptr;
    void *fsr=nullptr;
};
NVSDK_NGX_Resource_VK resource(const EncoreReconstructionImage &i,bool writable=false) {
    VkImageSubresourceRange range{(i.format==VK_FORMAT_D32_SFLOAT)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    return NVSDK_NGX_Create_ImageView_Resource_VK((VkImageView)i.view,(VkImage)i.image,range,(VkFormat)i.format,i.width,i.height,writable);
}
void *create(const EncoreReconstructionDevice *vk,const EncoreReconstructionDescription *desc,char *error,size_t size) {
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);
    if(desc->method==ENCORE_RECONSTRUCTION_FSR || desc->method==ENCORE_RECONSTRUCTION_FSR_FG){
#ifdef ENCORE_WITH_FSR
        auto c=std::make_unique<Context>();c->vk=*vk;c->desc=*desc;c->fsr=encore_fsr_create(vk,desc,error,size);return c->fsr?c.release():nullptr;
#else
        std::snprintf(error,size,"FSR provider is not installed");return nullptr;
#endif
    }
    auto owner=device(vk,error,size);if(!owner)return nullptr;
    uint32_t mask=desc->method==5?16u:2u;
    if(!(owner->capabilities&mask)){std::snprintf(error,size,"Requested NGX feature is unavailable on this device/driver");return nullptr;}
    auto c=std::make_unique<Context>();c->vk=*vk;c->device=owner;c->desc=*desc;
    auto status=NVSDK_NGX_VULKAN_AllocateParameters(&c->params);
    if(NVSDK_NGX_FAILED(status)){std::snprintf(error,size,"Unable to allocate NGX parameters (0x%x)",unsigned(status));return nullptr;}
    return c.release();
}
int dispatch_native(void *handle,uintptr_t command,const EncoreReconstructionFrame *f,char *error,size_t size) {
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);
    auto c=(Context *)handle;auto cmd=(VkCommandBuffer)command;NVSDK_NGX_Result status;
#ifdef ENCORE_WITH_FSR
    if(c->fsr)return encore_fsr_dispatch(c->fsr,command,f,error,size);
#endif
    const bool fg=c->desc.method==5;
    if(!c->feature) {
        if(fg) {
            NVSDK_NGX_DLSSG_Create_Params p{};p.Width=c->desc.output_width;p.Height=c->desc.output_height;p.NativeBackbufferFormat=f->color.format;
            p.RenderWidth=c->desc.input_width;p.RenderHeight=c->desc.input_height;
            status=NGX_VK_CREATE_DLSSG(cmd,1,1,&c->feature,c->params,&p);
        } else {
            NVSDK_NGX_DLSS_Create_Params p{};
            p.Feature.InWidth=c->desc.input_width;p.Feature.InHeight=c->desc.input_height;
            p.Feature.InTargetWidth=c->desc.output_width;p.Feature.InTargetHeight=c->desc.output_height;
            p.Feature.InPerfQualityValue=c->desc.method==3?NVSDK_NGX_PerfQuality_Value_DLAA:
                c->desc.quality==2?NVSDK_NGX_PerfQuality_Value_Balanced:c->desc.quality==3?NVSDK_NGX_PerfQuality_Value_MaxPerf:
                c->desc.quality==4?NVSDK_NGX_PerfQuality_Value_UltraPerformance:NVSDK_NGX_PerfQuality_Value_MaxQuality;
            p.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_MVJittered|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            status=NGX_VULKAN_CREATE_DLSS_EXT1((VkDevice)c->device->vk.device,cmd,1,1,&c->feature,c->params,&p);
        }
        if(NVSDK_NGX_FAILED(status)){std::snprintf(error,size,"NGX create method %u failed (0x%x)",c->desc.method,unsigned(status));return 0;}
    }
    auto color=resource(f->color),depth=resource(f->depth),motion=resource(f->motion),output=resource(f->output,true),reactive=resource(f->reactive);
    if(fg) {
        uint32_t required=f->multiplier==4?64u:f->multiplier==3?32u:16u;
        if(!(c->device->capabilities&required) || f->multiplier<2 || f->generated_index<1 || f->generated_index>=f->multiplier) {
            std::snprintf(error,size,"Unsupported FG multiplier or frame index");return 0;
        }
        NVSDK_NGX_VK_DLSSG_Eval_Params p{};p.pBackbuffer=&color;p.pDepth=&depth;p.pMVecs=&motion;p.pHudless=&color;p.pOutputInterpFrame=&output;
        NVSDK_NGX_DLSSG_Opt_Eval_Params o{};
        o.multiFrameCount=f->multiplier-1;o.multiFrameIndex=f->generated_index;
        encore_reconstruction::Camera camera;
        if(!encore_reconstruction::camera(*f,camera)){std::snprintf(error,size,"Invalid FG camera transforms");return 0;}
        std::memcpy(o.cameraViewToClip,f->projection,64);std::memcpy(o.clipToCameraView,camera.inverse_projection,64);
        for(unsigned i=0;i<4;i++)o.clipToLensClip[i][i]=1;
        std::memcpy(o.clipToPrevClip,camera.clip_to_previous,64);std::memcpy(o.prevClipToClip,camera.previous_to_clip,64);
        o.jitterOffset[0]=2.f*f->jitter_x/f->motion.width;o.jitterOffset[1]=2.f*f->jitter_y/f->motion.height;
        o.mvecScale[0]=-float(f->motion.width);o.mvecScale[1]=-float(f->motion.height);
        for(unsigned i=0;i<3;++i){o.cameraPos[i]=camera.inverse_view[12+i];o.cameraRight[i]=camera.inverse_view[i];o.cameraUp[i]=camera.inverse_view[4+i];o.cameraFwd[i]=-camera.inverse_view[8+i];}
        o.cameraNear=f->near_plane;o.cameraFar=f->far_plane;o.cameraFOV=f->vertical_fov;
        o.cameraAspectRatio=float(f->color.width)/f->color.height;o.cameraMotionIncluded=true;o.reset=f->reset;
        o.mvecsSubrectSize={f->motion.width,f->motion.height};o.depthSubrectSize={f->depth.width,f->depth.height};
        status=NGX_VK_EVALUATE_DLSSG(cmd,c->feature,c->params,&p,&o);
    } else {
        NVSDK_NGX_VK_DLSS_Eval_Params p{};p.Feature.pInColor=&color;p.Feature.pInOutput=&output;
        p.pInDepth=&depth;p.pInMotionVectors=&motion;p.pInBiasCurrentColorMask=f->reactive.image?&reactive:nullptr;p.InJitterOffsetX=f->jitter_x;p.InJitterOffsetY=f->jitter_y;
        p.InRenderSubrectDimensions={f->color.width,f->color.height};p.InReset=f->reset;
        p.InMVScaleX=-float(f->motion.width);p.InMVScaleY=-float(f->motion.height);
        p.InPreExposure=f->exposure>0?f->exposure:1;p.InExposureScale=1;p.InFrameTimeDeltaInMsec=f->delta_ms;
        status=NGX_VULKAN_EVALUATE_DLSS_EXT(cmd,c->feature,c->params,&p);
    }
    if(NVSDK_NGX_FAILED(status)){std::snprintf(error,size,"NGX evaluate method %u failed (0x%x)",c->desc.method,unsigned(status));return 0;}
    return 1;
}
int dispatch(void *handle,uintptr_t command,const EncoreReconstructionFrame *f,char *error,size_t size) {
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);auto c=(Context *)handle;
    if(c->desc.method>=4) {
        if(f->color.format==VK_FORMAT_R16G16B16A16_SFLOAT && f->output.format==VK_FORMAT_R16G16B16A16_SFLOAT) {
            if(!c->display){
                auto display=std::make_unique<encore_reconstruction::DisplayColor>();
                if(!display->initialize(c->vk,c->desc.output_width,c->desc.output_height,error,size))return 0;
                c->display=std::move(display);
            }
            EncoreReconstructionFrame display{};auto cmd=(VkCommandBuffer)command;
            if(!c->display->prepare(cmd,*f,display,error,size))return 0;
            if(!dispatch_native(handle,command,&display,error,size))return 0;
            return c->display->finish(cmd,*f,error,size);
        }
        if(f->color.format!=VK_FORMAT_R8G8B8A8_UNORM || f->output.format!=VK_FORMAT_R8G8B8A8_UNORM){std::snprintf(error,size,"FG requires matching linear RGBA16F or display-ready RGBA8 buffers");return 0;}
    }
    return dispatch_native(handle,command,f,error,size);
}
void destroy(void *handle) {
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);std::unique_ptr<Context> c((Context *)handle);
#ifdef ENCORE_WITH_FSR
    if(c->fsr)encore_fsr_destroy(c->fsr);
#endif
    if(c->feature)NVSDK_NGX_VULKAN_ReleaseFeature(c->feature);
    if(c->params)NVSDK_NGX_VULKAN_DestroyParameters(c->params);
}
uint32_t capabilities(const EncoreReconstructionDevice *vk,char *error,size_t size) {
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);auto owner=device(vk,error,size);
    uint32_t result=owner?owner->capabilities:0;
#ifdef ENCORE_WITH_FSR
    result|=1|8;
#endif
    return result;
}
int resolution(const EncoreReconstructionDevice *vk,uint32_t method,uint32_t ow,uint32_t oh,uint32_t quality,uint32_t *iw,uint32_t *ih) {
    if(!ow || !oh || quality>4)return 0;
    if(method==3 || quality==0){*iw=ow;*ih=oh;return 1;}
    if(method==1){float ratio=quality==2?1.7f:quality==3?2.f:quality==4?3.f:1.5f;*iw=uint32_t(ow/ratio);*ih=uint32_t(oh/ratio);return *iw && *ih;}
    std::lock_guard<std::recursive_mutex> lock(sdk_mutex);char error[256];auto state=device(vk,error,sizeof(error));if(!state)return 0;
    NVSDK_NGX_Parameter *p=nullptr;auto status=NVSDK_NGX_VULKAN_GetCapabilityParameters(&p);if(NVSDK_NGX_FAILED(status))return 0;
    uint32_t maxw=0,maxh=0,minw=0,minh=0;float sharp=0;
    auto q=quality==2?NVSDK_NGX_PerfQuality_Value_Balanced:quality==3?NVSDK_NGX_PerfQuality_Value_MaxPerf:quality==4?NVSDK_NGX_PerfQuality_Value_UltraPerformance:NVSDK_NGX_PerfQuality_Value_MaxQuality;
    status=NGX_DLSS_GET_OPTIMAL_SETTINGS(p,ow,oh,q,iw,ih,&maxw,&maxh,&minw,&minh,&sharp);
    NVSDK_NGX_VULKAN_DestroyParameters(p);return NVSDK_NGX_SUCCEED(status) && *iw && *ih;
}
const EncoreReconstructionApi api{ENCORE_RECONSTRUCTION_ABI,create,dispatch,destroy,capabilities,resolution};
}
extern "C" __attribute__((visibility("default"))) const EncoreReconstructionApi *encore_reconstruction_get_api(uint32_t version) {
    return version==ENCORE_RECONSTRUCTION_ABI?&api:nullptr;
}
