/* Frame-recorded ray geometry. There is no GPU submission/wait in these APIs.
 * Callers retain inputs and AS objects through their producer/consumer tokens.
 * Synchronization: https://docs.vulkan.org/spec/latest/chapters/accelstructures.html
 * Property ABI subset: Khronos Vulkan-Headers, Apache-2.0 OR MIT. */
typedef struct {
    uint32_t sType;void *pNext;
    uint64_t maxGeometryCount,maxInstanceCount,maxPrimitiveCount;
    uint32_t maxPerStageDescriptorAccelerationStructures;
    uint32_t maxPerStageDescriptorUpdateAfterBindAccelerationStructures;
    uint32_t maxDescriptorSetAccelerationStructures;
    uint32_t maxDescriptorSetUpdateAfterBindAccelerationStructures;
    uint32_t minAccelerationStructureScratchOffsetAlignment;
} EncoreAsProperties;
static uint64_t encore_as_scratch_alignment(EncoreVkDevice *d,uint32_t type,uint32_t primitives) {
    if(!d || !d->context)return 0;
    typedef void (*GetProperties2)(VkPhysicalDevice,void *);
    GetProperties2 get=(GetProperties2)d->context->get_proc(d->context->instance,"vkGetPhysicalDeviceProperties2");
    if(!get)get=(GetProperties2)d->context->get_proc(d->context->instance,"vkGetPhysicalDeviceProperties2KHR");
    if(!get)return 0;
    EncoreAsProperties as={.sType=1000150014};
    struct {uint32_t sType;void *pNext;EncoreVkTimingProperties properties;} properties={.sType=1000059001,.pNext=&as};
    get(d->physical_device,&properties);
    if(!as.maxGeometryCount || (type==VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR && primitives>as.maxPrimitiveCount) ||
        (type==VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR && primitives>as.maxInstanceCount))return 0;
    uint64_t alignment=as.minAccelerationStructureScratchOffsetAlignment;
    return alignment && !(alignment&(alignment-1))?alignment:0;
}
static EncoreVkAccelerationStructure *encore_frame_as_error(EncoreVkFrame *frame,uint32_t type,const char *message) {
    EncoreVkAccelerationStructure *result=calloc(1,sizeof *result);
    if(result){result->owner=frame&&frame->owner?frame->owner->owner:NULL;result->type=type;strncpy(result->error,message,sizeof(result->error)-1);}
    return result;
}
static EncoreVkAccelerationStructure *encore_frame_as_build(EncoreVkFrame *frame,uint32_t type,
    VkAccelerationStructureGeometryKHR *geometry,uint32_t primitives,EncoreVkBuffer *inputs,uint64_t byte_limit) {
    EncoreVkAccelerationStructure *result=encore_frame_as_error(frame,type,"Invalid frame acceleration build");
    if(!result){if(inputs)encore_vk_buffer_destroy((size_t)inputs);return NULL;}
    result->build_inputs=inputs;
    EncoreVkDevice *d=result->owner;
    if(!frame || !frame->recording || frame->submitted || frame->in_render_pass || !d || !d->available ||
        !(d->enabled_features&ENCORE_GPU_FEATURE_RAY_QUERY) || !geometry || !primitives)return result;
    PFN_vkGetAccelerationStructureBuildSizesKHR get_sizes=(PFN_vkGetAccelerationStructureBuildSizesKHR)d->get_device_proc(d->device,"vkGetAccelerationStructureBuildSizesKHR");
    PFN_vkCreateAccelerationStructureKHR create=(PFN_vkCreateAccelerationStructureKHR)d->get_device_proc(d->device,"vkCreateAccelerationStructureKHR");
    PFN_vkCmdBuildAccelerationStructuresKHR record=(PFN_vkCmdBuildAccelerationStructuresKHR)d->get_device_proc(d->device,"vkCmdBuildAccelerationStructuresKHR");
    PFN_vkGetAccelerationStructureDeviceAddressKHR address=(PFN_vkGetAccelerationStructureDeviceAddressKHR)d->get_device_proc(d->device,"vkGetAccelerationStructureDeviceAddressKHR");
    result->destroy_acceleration_structure=(PFN_vkDestroyAccelerationStructureKHR)d->get_device_proc(d->device,"vkDestroyAccelerationStructureKHR");
    uint64_t alignment=encore_as_scratch_alignment(d,type,primitives);
    if(!get_sizes || !create || !record || !address || !result->destroy_acceleration_structure || !alignment)return result;
    VkAccelerationStructureBuildGeometryInfoKHR build={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,NULL,type,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,NULL,NULL,1,geometry,NULL,{.deviceAddress=0}};
    VkAccelerationStructureBuildSizesInfoKHR sizes={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,NULL,0,0,0};
    get_sizes(d->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&primitives,&sizes);
    uint64_t input_bytes=inputs?inputs->size:0;
    if(!sizes.accelerationStructureSize || !sizes.buildScratchSize || input_bytes>byte_limit ||
        sizes.accelerationStructureSize>byte_limit-input_bytes ||
        sizes.buildScratchSize>byte_limit-input_bytes-sizes.accelerationStructureSize ||
        alignment-1>byte_limit-input_bytes-sizes.accelerationStructureSize-sizes.buildScratchSize) {
        strncpy(result->error,"Acceleration build exceeds its memory budget",sizeof(result->error)-1);return result;
    }
    result->backing=(EncoreVkBuffer *)encore_vk_buffer_create((size_t)d,sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,false);
    result->build_scratch=(EncoreVkBuffer *)encore_vk_buffer_create((size_t)d,sizes.buildScratchSize+alignment-1,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,false);
    if(!result->backing || !result->backing->available || !result->build_scratch || !result->build_scratch->available)return result;
    uint64_t scratch=encore_vk_buffer_device_address(d,result->build_scratch);
    if(!scratch || scratch>UINT64_MAX-(alignment-1))return result;
    build.scratchData.deviceAddress=(scratch+alignment-1)&~(alignment-1);
    VkAccelerationStructureCreateInfoKHR info={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,NULL,0,
        result->backing->buffer,0,sizes.accelerationStructureSize,type,0};
    if(create(d->device,&info,NULL,&result->acceleration_structure)!=VK_SUCCESS)return result;
    VkAccelerationStructureDeviceAddressInfoKHR query={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,NULL,result->acceleration_structure};
    result->device_address=address(d->device,&query);
    if(!result->device_address)return result;
    result->build_completion=encore_vk_frame_completion((size_t)frame);
    if(!result->build_completion)return result;
    VkMemoryBarrier input={VK_STRUCTURE_TYPE_MEMORY_BARRIER,NULL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
    d->cmd_pipeline_barrier(frame->command_buffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0x02000000u,
        0,1,&input,0,NULL,0,NULL);
    build.dstAccelerationStructure=result->acceleration_structure;
    VkAccelerationStructureBuildRangeInfoKHR range={primitives,0,0,0};const VkAccelerationStructureBuildRangeInfoKHR *ranges=&range;
    record(frame->command_buffer,1,&build,&ranges);
    VkMemoryBarrier output={VK_STRUCTURE_TYPE_MEMORY_BARRIER,NULL,0x00400000u,0x00200000u};
    d->cmd_pipeline_barrier(frame->command_buffer,0x02000000u,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&output,0,NULL,0,NULL);
    result->available=true;result->error[0]=0;return result;
}
size_t encore_vk_frame_build_triangle_blas(size_t frame_handle,size_t vertex_handle,uint64_t stride,uint32_t vertex_count,
    size_t index_handle,uint32_t index_count,uint64_t byte_limit) {
    EncoreVkFrame *frame=(EncoreVkFrame *)frame_handle;EncoreVkDevice *d=frame&&frame->owner?frame->owner->owner:NULL;
    EncoreVkBuffer *vertices=(EncoreVkBuffer *)vertex_handle,*indices=(EncoreVkBuffer *)index_handle;
    if(!d || !vertices || !indices || vertices->owner!=d || indices->owner!=d || !vertices->available || !indices->available ||
        stride<12 || stride>UINT32_MAX || stride%4 || vertex_count<3 || index_count<3 || index_count%3 || (uint64_t)(vertex_count-1)>(UINT64_MAX-12)/stride ||
        (uint64_t)(vertex_count-1)*stride+12>vertices->size || (uint64_t)index_count*4>indices->size ||
        !(vertices->usage&VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) ||
        !(indices->usage&VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR))
        return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,"Invalid acceleration build input buffers");
    uint64_t vertex_address=encore_vk_buffer_device_address(d,vertices),index_address=encore_vk_buffer_device_address(d,indices);
    if(!vertex_address || !index_address)return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,"Acceleration inputs need device addresses");
    VkAccelerationStructureGeometryKHR geometry={0};geometry.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles=(VkAccelerationStructureGeometryTrianglesDataKHR){VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        NULL,VK_FORMAT_R32G32B32_SFLOAT,{.deviceAddress=vertex_address},stride,vertex_count-1,VK_INDEX_TYPE_UINT32,{.deviceAddress=index_address},{.deviceAddress=0}};
    return (size_t)encore_frame_as_build(frame,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,&geometry,index_count/3,NULL,byte_limit);
}
size_t encore_vk_frame_build_tlas(size_t frame_handle,const size_t *handles,size_t count,const float *transforms,size_t floats,uint64_t byte_limit) {
    EncoreVkFrame *frame=(EncoreVkFrame *)frame_handle;EncoreVkDevice *d=frame&&frame->owner?frame->owner->owner:NULL;
    if(!frame || !frame->recording || !d || !handles || !transforms || !count || count>16777216u || floats!=count*16 ||
        count>byte_limit/sizeof(VkAccelerationStructureInstanceKHR))
        return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Invalid acceleration instances or memory budget");
    for(size_t i=0;i<count;i++) {
        EncoreVkAccelerationStructure *b=(EncoreVkAccelerationStructure *)handles[i];
        if(!b || !b->available || b->owner!=d || b->type!=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR || !b->device_address)
            return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Invalid instance acceleration structure");
        if(b->build_completion) {
            uint32_t status=encore_vk_completion_status(b->build_completion);
            if(status==3 || status==4 || (status==1 && !encore_vk_completion_submitted(b->build_completion) &&
                b->build_completion!=(size_t)frame->owner->completions[frame->frame_index]))
                return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Unordered acceleration producer");
        }
    }
    for(size_t i=0;i<count;i++) {
        const float *m=transforms+i*16;
        for(unsigned k=0;k<16;k++){uint32_t word;memcpy(&word,m+k,sizeof word);if((word&0x7f800000u)==0x7f800000u)return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Non-finite acceleration transform");}
        double determinant=(double)m[0]*((double)m[5]*m[10]-(double)m[9]*m[6])-(double)m[4]*((double)m[1]*m[10]-(double)m[9]*m[2])+(double)m[8]*((double)m[1]*m[6]-(double)m[5]*m[2]);
        if(!determinant || m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Acceleration transform must be invertible and affine");
    }
    EncoreVkBuffer *inputs=(EncoreVkBuffer *)encore_vk_buffer_create((size_t)d,count*sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,true);
    if(!inputs || !inputs->available || !inputs->mapped){if(inputs)encore_vk_buffer_destroy((size_t)inputs);return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Unable to allocate acceleration instances");}
    VkAccelerationStructureInstanceKHR *out=inputs->mapped;
    for(size_t i=0;i<count;i++) {
        EncoreVkAccelerationStructure *b=(EncoreVkAccelerationStructure *)handles[i];const float *m=transforms+i*16;
        out[i]=(VkAccelerationStructureInstanceKHR){.matrix={{m[0],m[4],m[8],m[12]},{m[1],m[5],m[9],m[13]},{m[2],m[6],m[10],m[14]}},
            .custom_index_mask=((uint32_t)i&0x00ffffffu)|0xff000000u,.sbt_offset_flags=0,.accelerationStructureReference=b->device_address};
    }
    uint64_t address=encore_vk_buffer_device_address(d,inputs);
    if(!address){encore_vk_buffer_destroy((size_t)inputs);return (size_t)encore_frame_as_error(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,"Acceleration instances need device addresses");}
    VkAccelerationStructureGeometryKHR geometry={0};geometry.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances=(VkAccelerationStructureGeometryInstancesDataKHR){VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,NULL,0,{.deviceAddress=address}};
    return (size_t)encore_frame_as_build(frame,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,&geometry,(uint32_t)count,inputs,byte_limit);
}
