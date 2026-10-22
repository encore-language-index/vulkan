/* GPU-only adaptation from the linear scene ABI to display-ready SDR FG input.
 * The encode uses linear-sRGB to display-sRGB conversion. An HDR presentation
 * path needs an explicit output-color-space contract before adding HDR FG. */
#ifndef ENCORE_RECONSTRUCTION_DISPLAY_H
#define ENCORE_RECONSTRUCTION_DISPLAY_H
#include "reconstruction_display_spirv.h"
#include <vector>
namespace encore_reconstruction {
class DisplayColor {
    struct Image {VkImage image=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;};
    struct Binding {uintptr_t source,destination;uint32_t source_layout;VkDescriptorSet set;};
    VkDevice device=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkPipeline encode_pipeline=VK_NULL_HANDLE,decode_pipeline=VK_NULL_HANDLE;
    Image color,output;
    std::vector<Binding> bindings;
    uint32_t width=0,height=0;
    static bool failure(char *error,size_t size,const char *step,VkResult status){std::snprintf(error,size,"FG display conversion: %s (%d)",step,int(status));return false;}
    bool image(Image &out,char *error,size_t size){
        VkImageCreateInfo description{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        description.imageType=VK_IMAGE_TYPE_2D;description.format=VK_FORMAT_R8G8B8A8_UNORM;
        description.extent={width,height,1};description.mipLevels=description.arrayLayers=1;description.samples=VK_SAMPLE_COUNT_1_BIT;
        description.tiling=VK_IMAGE_TILING_OPTIMAL;description.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto status=vkCreateImage(device,&description,nullptr,&out.image);if(status!=VK_SUCCESS)return failure(error,size,"image",status);
        VkMemoryRequirements requirements;vkGetImageMemoryRequirements(device,out.image,&requirements);
        VkPhysicalDeviceMemoryProperties properties;vkGetPhysicalDeviceMemoryProperties(physical,&properties);
        uint32_t type=properties.memoryTypeCount;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
        if(type==properties.memoryTypeCount)return failure(error,size,"device-local memory",VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
        status=vkAllocateMemory(device,&allocation,nullptr,&out.memory);if(status!=VK_SUCCESS)return failure(error,size,"allocate",status);
        status=vkBindImageMemory(device,out.image,out.memory,0);if(status!=VK_SUCCESS)return failure(error,size,"bind",status);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=out.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=description.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        status=vkCreateImageView(device,&view,nullptr,&out.view);return status==VK_SUCCESS || failure(error,size,"view",status);
    }
    bool pipeline(const uint32_t *words,size_t bytes,VkPipeline &out,char *error,size_t size){
        VkShaderModuleCreateInfo code{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};code.codeSize=bytes;code.pCode=words;VkShaderModule shader=VK_NULL_HANDLE;
        auto status=vkCreateShaderModule(device,&code,nullptr,&shader);if(status!=VK_SUCCESS)return failure(error,size,"shader",status);
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};info.layout=layout;
        status=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&out);vkDestroyShaderModule(device,shader,nullptr);
        return status==VK_SUCCESS || failure(error,size,"pipeline",status);
    }
    void transition(VkCommandBuffer command,Image &image,VkImageLayout destination){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image.image;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=image.layout;barrier.newLayout=destination;
        barrier.srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:image.layout==VK_IMAGE_LAYOUT_GENERAL?VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask=destination==VK_IMAGE_LAYOUT_GENERAL?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);image.layout=destination;
    }
    VkDescriptorSet binding(const EncoreReconstructionImage &source,const EncoreReconstructionImage &destination,char *error,size_t size){
        for(auto &entry:bindings)if(entry.source==source.view && entry.destination==destination.view && entry.source_layout==source.layout)return entry.set;
        if(bindings.size()>=64){failure(error,size,"descriptor capacity",VK_ERROR_OUT_OF_POOL_MEMORY);return VK_NULL_HANDLE;}
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&descriptors;
        VkDescriptorSet set=VK_NULL_HANDLE;auto status=vkAllocateDescriptorSets(device,&allocation,&set);
        if(status!=VK_SUCCESS){failure(error,size,"descriptor",status);return VK_NULL_HANDLE;}
        VkDescriptorImageInfo input{sampler,(VkImageView)source.view,(VkImageLayout)source.layout},out{VK_NULL_HANDLE,(VkImageView)destination.view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,set,0,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&input,nullptr,nullptr};
        writes[1]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,set,1,0,1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&out,nullptr,nullptr};
        vkUpdateDescriptorSets(device,2,writes,0,nullptr);bindings.push_back({source.view,destination.view,source.layout,set});return set;
    }
    EncoreReconstructionImage resource(const Image &image)const{return {(uintptr_t)image.image,(uintptr_t)image.view,width,height,VK_FORMAT_R8G8B8A8_UNORM,(uint32_t)image.layout};}
    void run(VkCommandBuffer command,VkPipeline pipeline,VkDescriptorSet set){
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        vkCmdDispatch(command,(width+7)/8,(height+7)/8,1);
    }
public:
    bool initialize(const EncoreReconstructionDevice &vk,uint32_t w,uint32_t h,char *error,size_t size){
        device=(VkDevice)vk.device;physical=(VkPhysicalDevice)vk.physical_device;width=w;height=h;
        if(!image(color,error,size) || !image(output,error,size))return false;
        VkDescriptorSetLayoutBinding entries[]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor_info.bindingCount=2;descriptor_info.pBindings=entries;
        auto status=vkCreateDescriptorSetLayout(device,&descriptor_info,nullptr,&descriptors);if(status!=VK_SUCCESS)return failure(error,size,"layout",status);
        VkPipelineLayoutCreateInfo pipeline_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipeline_info.setLayoutCount=1;pipeline_info.pSetLayouts=&descriptors;
        status=vkCreatePipelineLayout(device,&pipeline_info,nullptr,&layout);if(status!=VK_SUCCESS)return failure(error,size,"pipeline layout",status);
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,64},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,64}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool_info.maxSets=64;pool_info.poolSizeCount=2;pool_info.pPoolSizes=sizes;
        status=vkCreateDescriptorPool(device,&pool_info,nullptr,&pool);if(status!=VK_SUCCESS)return failure(error,size,"pool",status);
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler_info.magFilter=sampler_info.minFilter=VK_FILTER_NEAREST;sampler_info.addressModeU=sampler_info.addressModeV=sampler_info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        status=vkCreateSampler(device,&sampler_info,nullptr,&sampler);if(status!=VK_SUCCESS)return failure(error,size,"sampler",status);
        return pipeline(reconstruction_display_encode,sizeof(reconstruction_display_encode),encode_pipeline,error,size) && pipeline(reconstruction_display_decode,sizeof(reconstruction_display_decode),decode_pipeline,error,size);
    }
    bool prepare(VkCommandBuffer command,const EncoreReconstructionFrame &frame,EncoreReconstructionFrame &display,char *error,size_t size){
        if(frame.color.width!=width || frame.color.height!=height || frame.output.width!=width || frame.output.height!=height)return failure(error,size,"extent mismatch",VK_ERROR_INITIALIZATION_FAILED);
        if(frame.generated_index==1){
            transition(command,color,VK_IMAGE_LAYOUT_GENERAL);
            auto set=binding(frame.color,resource(color),error,size);if(!set)return false;
            run(command,encode_pipeline,set);transition(command,color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else if(color.layout==VK_IMAGE_LAYOUT_UNDEFINED)return failure(error,size,"missing first intermediate",VK_ERROR_INITIALIZATION_FAILED);
        transition(command,output,VK_IMAGE_LAYOUT_GENERAL);
        display=frame;display.color=resource(color);display.output=resource(output);return true;
    }
    bool finish(VkCommandBuffer command,const EncoreReconstructionFrame &frame,char *error,size_t size){
        transition(command,output,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto set=binding(resource(output),frame.output,error,size);if(!set)return false;run(command,decode_pipeline,set);return true;
    }
    ~DisplayColor(){
        if(!device)return;
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);if(encode_pipeline)vkDestroyPipeline(device,encode_pipeline,nullptr);if(decode_pipeline)vkDestroyPipeline(device,decode_pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(descriptors)vkDestroyDescriptorSetLayout(device,descriptors,nullptr);if(sampler)vkDestroySampler(device,sampler,nullptr);
        for(auto image:{color,output}){if(image.view)vkDestroyImageView(device,image.view,nullptr);if(image.image)vkDestroyImage(device,image.image,nullptr);if(image.memory)vkFreeMemory(device,image.memory,nullptr);}
    }
};
}
#endif
