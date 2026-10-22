/* FidelityFX host integration; algorithms and shaders come from AMD's SDK. */
#include "reconstruction_api.h"
#include "reconstruction_camera.h"
#include <vulkan/vulkan.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <vector>
#include <memory>
#include <cstdio>
/* The application owns presentation. The upstream Windows swapchain replacement
 * is deliberately not installed; configuring it must return an explicit error. */
extern "C" FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(const FfxFrameGenerationConfig *) {return FFX_ERROR_INVALID_ARGUMENT;}
namespace {
struct Fsr {
    FfxFsr3UpscalerContext context{};
    FfxFrameInterpolationContext interpolation{};
    FfxOpticalflowContext optical{};
    FfxInterface backend{};
    VkDeviceContext vk_device{};
    uint32_t resource_context=0;
    uint64_t frame_id=0;
    FfxResourceInternal shared[5]{};
    FfxResource resources[5]{};
    std::vector<uint8_t> scratch;
    bool created=false;
    bool backend_created=false;
    bool interpolation_created=false,optical_created=false,frame_generation=false;
    ~Fsr(){
        if(created)ffxFsr3UpscalerContextDestroy(&context);
        if(interpolation_created)ffxFrameInterpolationContextDestroy(&interpolation);
        if(optical_created)ffxOpticalflowContextDestroy(&optical);
        if(backend_created){for(auto r:shared)if(r.internalIndex)backend.fpDestroyResource(&backend,r,resource_context);backend.fpDestroyBackendContext(&backend,resource_context);}
    }
};
FfxResource image(const EncoreReconstructionImage &i,bool output=false) {
    if(!i.image)return FfxResource{};
    FfxResourceDescription d{};d.type=FFX_RESOURCE_TYPE_TEXTURE2D;
    d.format=i.format==VK_FORMAT_D32_SFLOAT?FFX_SURFACE_FORMAT_R32_FLOAT:
        i.format==VK_FORMAT_R16G16B16A16_SFLOAT?FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
        i.format==VK_FORMAT_R8G8B8A8_UNORM?FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
        i.format==VK_FORMAT_R16G16_SFLOAT?FFX_SURFACE_FORMAT_R16G16_FLOAT:
        i.format==VK_FORMAT_R32_UINT?FFX_SURFACE_FORMAT_R32_UINT:FFX_SURFACE_FORMAT_R8_UNORM;
    d.width=i.width;d.height=i.height;d.depth=1;d.mipCount=1;
    d.usage=output?FFX_RESOURCE_USAGE_UAV:i.format==VK_FORMAT_D32_SFLOAT?FFX_RESOURCE_USAGE_DEPTHTARGET:FFX_RESOURCE_USAGE_READ_ONLY;
    return ffxGetResourceVK((void *)i.image,d,L"Reconstruction input",output?FFX_RESOURCE_STATE_UNORDERED_ACCESS:FFX_RESOURCE_STATE_COMPUTE_READ);
}
}
extern "C" void *encore_fsr_create(const EncoreReconstructionDevice *vk,const EncoreReconstructionDescription *desc,char *error,size_t size) {
    auto c=std::make_unique<Fsr>();
    c->frame_generation=desc->method==ENCORE_RECONSTRUCTION_FSR_FG;
    uint32_t context_count=c->frame_generation?3:2;
    c->scratch.resize(ffxGetScratchMemorySizeVK((VkPhysicalDevice)vk->physical_device,context_count));
    c->vk_device=VkDeviceContext{(VkDevice)vk->device,(VkPhysicalDevice)vk->physical_device,(PFN_vkGetDeviceProcAddr)vk->get_device_proc};
    FfxFsr3UpscalerContextDescription d{};
    d.maxRenderSize={desc->input_width,desc->input_height};d.maxUpscaleSize={desc->output_width,desc->output_height};
    d.flags=FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE|FFX_FSR3UPSCALER_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION|FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
    auto status=ffxGetInterfaceVK(&c->backend,reinterpret_cast<FfxDevice>(&c->vk_device),c->scratch.data(),c->scratch.size(),context_count);
    FfxEffectBindlessConfig bindless{};
    if(status==FFX_OK)status=c->backend.fpCreateBackendContext(&c->backend,FFX_EFFECT_FSR3UPSCALER,&bindless,&c->resource_context);
    if(status==FFX_OK)c->backend_created=true;
    if(c->frame_generation) {
        FfxFrameInterpolationContextDescription fi{};fi.backendInterface=c->backend;
        fi.flags=FFX_FRAMEINTERPOLATION_ENABLE_JITTER_MOTION_VECTORS;
        fi.maxRenderSize={desc->input_width,desc->input_height};fi.displaySize={desc->output_width,desc->output_height};
        fi.backBufferFormat=fi.previousInterpolationSourceFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        if(status==FFX_OK)status=ffxFrameInterpolationContextCreate(&c->interpolation,&fi);
        if(status==FFX_OK)c->interpolation_created=true;
        FfxOpticalflowContextDescription of{};of.backendInterface=c->backend;of.resolution=fi.displaySize;
        if(status==FFX_OK)status=ffxOpticalflowContextCreate(&c->optical,&of);
        if(status==FFX_OK)c->optical_created=true;
        FfxFrameInterpolationSharedResourceDescriptions fi_resources{};FfxOpticalflowSharedResourceDescriptions of_resources{};
        if(status==FFX_OK)status=ffxFrameInterpolationGetSharedResourceDescriptions(&c->interpolation,&fi_resources);
        if(status==FFX_OK)status=ffxOpticalflowGetSharedResourceDescriptions(&c->optical,&of_resources);
        FfxCreateResourceDescription descriptions[]={fi_resources.dilatedDepth,fi_resources.dilatedMotionVectors,fi_resources.reconstructedPrevNearestDepth,of_resources.opticalFlowVector,of_resources.opticalFlowSCD};
        for(unsigned i=0;i<5 && status==FFX_OK;i++) {
            status=c->backend.fpCreateResource(&c->backend,&descriptions[i],c->resource_context,&c->shared[i]);
            if(status==FFX_OK)c->resources[i]=c->backend.fpGetResource(&c->backend,c->shared[i]);
        }
        if(status!=FFX_OK){std::snprintf(error,size,"FSR FG creation failed (%d)",int(status));return nullptr;}
        return c.release();
    }
    d.backendInterface=c->backend;
    if(status==FFX_OK)status=ffxFsr3UpscalerContextCreate(&c->context,&d);
    if(status!=FFX_OK){std::snprintf(error,size,"FSR 3.1 creation failed (%d)",int(status));return nullptr;}
    c->created=true;
    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    status=ffxFsr3UpscalerGetSharedResourceDescriptions(&c->context,&shared);
    FfxCreateResourceDescription descriptions[]={shared.dilatedDepth,shared.dilatedMotionVectors,shared.reconstructedPrevNearestDepth};
    for(unsigned i=0;i<3 && status==FFX_OK;i++) {
        status=c->backend.fpCreateResource(&c->backend,&descriptions[i],c->resource_context,&c->shared[i]);
        if(status==FFX_OK)c->resources[i]=c->backend.fpGetResource(&c->backend,c->shared[i]);
    }
    if(status!=FFX_OK){std::snprintf(error,size,"FSR shared resource allocation failed (%d)",int(status));return nullptr;}
    return c.release();
}
extern "C" int encore_fsr_dispatch(void *handle,uintptr_t command,const EncoreReconstructionFrame *frame,char *error,size_t size) {
    auto c=(Fsr *)handle;FfxFsr3UpscalerDispatchDescription d{};
    if(c->frame_generation) {
        if(frame->multiplier!=2 || frame->generated_index!=1){std::snprintf(error,size,"FSR FG supports one intermediate frame (2x)");return 0;}
        FfxFrameInterpolationPrepareDescription prepare{};prepare.commandList=ffxGetCommandListVK((VkCommandBuffer)command);
        prepare.renderSize={frame->depth.width,frame->depth.height};prepare.jitterOffset={frame->jitter_x,frame->jitter_y};
        prepare.motionVectorScale={-float(frame->motion.width),-float(frame->motion.height)};
        prepare.frameTimeDelta=frame->delta_ms;prepare.cameraNear=frame->near_plane;prepare.cameraFar=frame->far_plane;
        prepare.cameraFovAngleVertical=frame->vertical_fov;prepare.viewSpaceToMetersFactor=1;
        prepare.depth=image(frame->depth);prepare.motionVectors=image(frame->motion);prepare.frameID=c->frame_id;
        prepare.dilatedDepth=c->resources[0];prepare.dilatedMotionVectors=c->resources[1];prepare.reconstructedPrevDepth=c->resources[2];
        encore_reconstruction::Camera camera;
        if(!encore_reconstruction::camera(*frame,camera)){std::snprintf(error,size,"Invalid FSR FG camera transforms");return 0;}
        for(unsigned i=0;i<3;++i){prepare.cameraPosition[i]=camera.inverse_view[12+i];prepare.cameraRight[i]=camera.inverse_view[i];prepare.cameraUp[i]=camera.inverse_view[4+i];prepare.cameraForward[i]=-camera.inverse_view[8+i];}
        auto status=ffxFrameInterpolationPrepare(&c->interpolation,&prepare);
        FfxOpticalflowDispatchDescription optical{};optical.commandList=prepare.commandList;optical.color=image(frame->color);
        optical.opticalFlowVector=c->resources[3];optical.opticalFlowSCD=c->resources[4];optical.reset=frame->reset;
        optical.backbufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;optical.minMaxLuminance={0,1};
        if(status==FFX_OK)status=ffxOpticalflowContextDispatch(&c->optical,&optical);
        FfxFrameInterpolationDispatchDescription fi{};fi.commandList=prepare.commandList;
        fi.displaySize={frame->color.width,frame->color.height};fi.renderSize=prepare.renderSize;
        fi.currentBackBuffer=optical.color;fi.currentBackBuffer_HUDLess=optical.color;fi.output=image(frame->output,true);
        fi.interpolationRect={0,0,int32_t(frame->color.width),int32_t(frame->color.height)};
        fi.opticalFlowVector=c->resources[3];fi.opticalFlowSceneChangeDetection=c->resources[4];
        fi.opticalFlowBufferSize={c->resources[3].description.width,c->resources[3].description.height};
        fi.opticalFlowScale={1.f/frame->color.width,1.f/frame->color.height};fi.opticalFlowBlockSize=8;
        fi.cameraNear=frame->near_plane;fi.cameraFar=frame->far_plane;fi.cameraFovAngleVertical=frame->vertical_fov;
        fi.viewSpaceToMetersFactor=1;fi.frameTimeDelta=frame->delta_ms;fi.reset=frame->reset;
        fi.backBufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;fi.minMaxLuminance[0]=0;fi.minMaxLuminance[1]=1;
        fi.frameID=c->frame_id;fi.dilatedDepth=c->resources[0];fi.dilatedMotionVectors=c->resources[1];fi.reconstructedPrevDepth=c->resources[2];
        if(status==FFX_OK)status=ffxFrameInterpolationDispatch(&c->interpolation,&fi);
        if(status!=FFX_OK){std::snprintf(error,size,"FSR FG dispatch failed (%d)",int(status));return 0;}
        // Keep exact, monotonic IDs through camera cuts and beyond f32's 24-bit
        // integer precision. One successful dispatch is one real frame (AMD 2x).
        ++c->frame_id;
        return 1;
    }
    d.commandList=ffxGetCommandListVK((VkCommandBuffer)command);
    d.color=image(frame->color);d.depth=image(frame->depth);d.motionVectors=image(frame->motion);d.output=image(frame->output,true);
    d.reactive=image(frame->reactive);d.transparencyAndComposition=image(frame->composition);
    d.dilatedDepth=c->resources[0];d.dilatedMotionVectors=c->resources[1];d.reconstructedPrevNearestDepth=c->resources[2];
    d.jitterOffset={frame->jitter_x,frame->jitter_y};d.motionVectorScale={-float(frame->motion.width),-float(frame->motion.height)};
    d.renderSize={frame->color.width,frame->color.height};d.upscaleSize={frame->output.width,frame->output.height};
    d.enableSharpening=frame->sharpness>0;d.sharpness=frame->sharpness;d.frameTimeDelta=frame->delta_ms;
    d.preExposure=frame->exposure>0?frame->exposure:1;d.reset=frame->reset;
    d.cameraNear=frame->near_plane;d.cameraFar=frame->far_plane;d.cameraFovAngleVertical=frame->vertical_fov;d.viewSpaceToMetersFactor=1;
    auto status=ffxFsr3UpscalerContextDispatch(&c->context,&d);
    if(status!=FFX_OK){std::snprintf(error,size,"FSR 3.1 dispatch failed (%d)",int(status));return 0;}
    return 1;
}
extern "C" void encore_fsr_destroy(void *handle){delete (Fsr *)handle;}
