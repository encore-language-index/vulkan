/* Vulkan buffers, textures, transfers and resource lifetimes. */
size_t encore_vk_encoder_create(size_t device_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)calloc(1, sizeof(EncoreVkEncoder));
    if (encoder == NULL) return 0;
    encoder->owner = device;
    if (device == NULL || !device->available || device->command_pool == NULL) {
        strncpy(encoder->error, "Invalid Encore graphics device", sizeof(encoder->error) - 1); return (size_t)encoder;
    }
    VkCommandBufferAllocateInfo allocate_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, device->command_pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1
    };
    if (device->allocate_command_buffers(device->device, &allocate_info, &encoder->command_buffer) != VK_SUCCESS) {
        strncpy(encoder->error, "Unable to allocate Vulkan command buffer", sizeof(encoder->error) - 1); return (size_t)encoder;
    }
    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL
    };
    if (device->begin_command_buffer(encoder->command_buffer, &begin_info) != VK_SUCCESS) {
        strncpy(encoder->error, "Unable to begin Vulkan command buffer", sizeof(encoder->error) - 1); return (size_t)encoder;
    }
    encoder->recording = true;
    return (size_t)encoder;
}

bool encore_vk_encoder_available(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    return encoder != NULL && encoder->recording && encoder->command_buffer != NULL;
}

encore_str encore_vk_encoder_error(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    return encore_string(encoder == NULL ? "Invalid command encoder" : encoder->error);
}

bool encore_vk_encoder_bind_compute_pipeline(size_t encoder_handle, size_t pipeline_handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)encoder_handle;
    EncoreVkComputePipeline *pipeline = (EncoreVkComputePipeline *)pipeline_handle;
    if (encoder == NULL || !encoder->recording || encoder->command_buffer == NULL ||
        encoder->owner == NULL || encoder->owner->cmd_bind_pipeline == NULL ||
        pipeline == NULL || !pipeline->available || pipeline->pipeline == NULL ||
        pipeline->owner != encoder->owner) {
        if (encoder != NULL) strncpy(encoder->error, "Invalid compute pipeline bind", sizeof(encoder->error) - 1);
        return false;
    }
    encoder->owner->cmd_bind_pipeline(encoder->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    encoder->bound_compute_pipeline = pipeline;
    encoder->bound_compute_group = NULL;
    return true;
}

bool encore_vk_encoder_bind_compute_group(size_t encoder_handle, size_t pipeline_handle, size_t group_handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)encoder_handle;
    EncoreVkComputePipeline *pipeline = (EncoreVkComputePipeline *)pipeline_handle;
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)group_handle;
    if (encoder == NULL || !encoder->recording || encoder->command_buffer == NULL ||
        encoder->owner == NULL || encoder->owner->cmd_bind_descriptor_sets == NULL ||
        pipeline == NULL || !pipeline->available || pipeline->layout == NULL ||
        pipeline->owner != encoder->owner || encoder->bound_compute_pipeline != pipeline ||
        group == NULL || !group->available || group->set == NULL || group->owner != encoder->owner ||
        group->layout != pipeline->bind_group_layout) {
        if (encoder != NULL) strncpy(encoder->error, "Invalid or incompatible compute bind group", sizeof(encoder->error) - 1);
        return false;
    }
    encoder->owner->cmd_bind_descriptor_sets(encoder->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline->layout, 0, 1, &group->set, 0, NULL);
    encoder->bound_compute_group = group;
    return true;
}

bool encore_vk_encoder_dispatch(size_t handle, uint32_t x, uint32_t y, uint32_t z) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    if (encoder == NULL || !encoder->recording || encoder->command_buffer == NULL ||
        encoder->owner == NULL || encoder->owner->cmd_dispatch == NULL ||
        encoder->bound_compute_pipeline == NULL || encoder->bound_compute_group == NULL ||
        x == 0 || y == 0 || z == 0) {
        if (encoder != NULL) strncpy(encoder->error, "Compute dispatch requires a valid pipeline and compatible bind group", sizeof(encoder->error) - 1);
        return false;
    }
    encoder->owner->cmd_dispatch(encoder->command_buffer, x, y, z); return true;
}

bool encore_vk_encoder_storage_host_barrier(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    if (encoder == NULL || !encoder->recording) return false;
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    encoder->owner->cmd_pipeline_barrier(encoder->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_encoder_transfer_host_barrier(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    if (encoder == NULL || !encoder->recording) return false;
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    encoder->owner->cmd_pipeline_barrier(encoder->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_encoder_transfer_geometry_barrier(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    if (encoder == NULL || !encoder->recording) return false;
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
            VK_ACCESS_SHADER_READ_BIT};
    encoder->owner->cmd_pipeline_barrier(encoder->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_encoder_copy_buffer(size_t encoder_handle, size_t source_handle, size_t destination_handle,
    uint64_t source_offset, uint64_t destination_offset, uint64_t size) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)encoder_handle;
    EncoreVkBuffer *source = (EncoreVkBuffer *)source_handle;
    EncoreVkBuffer *destination = (EncoreVkBuffer *)destination_handle;
    if (encoder == NULL || !encoder->recording || source == NULL || destination == NULL ||
        !source->available || !destination->available || source->owner != encoder->owner || destination->owner != encoder->owner ||
        (source->usage & 1u) == 0 || (destination->usage & 2u) == 0 || size == 0 ||
        source_offset > source->size || size > source->size - source_offset ||
        destination_offset > destination->size || size > destination->size - destination_offset) return false;
    VkBufferCopy region = {source_offset, destination_offset, size};
    encoder->owner->cmd_copy_buffer(encoder->command_buffer, source->buffer, destination->buffer, 1, &region);
    return true;
}

size_t encore_vk_encoder_submit(size_t handle);
bool encore_vk_submission_wait(size_t handle, uint64_t timeout_ns);
bool encore_vk_submission_destroy(size_t handle);

bool encore_vk_encoder_submit_wait(size_t handle) {
    size_t submission = encore_vk_encoder_submit(handle);
    if (submission == 0) return false;
    bool complete = encore_vk_submission_wait(submission, UINT64_MAX);
    encore_vk_submission_destroy(submission);
    return complete;
}

size_t encore_vk_encoder_submit(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    EncoreVkSubmission *submission = (EncoreVkSubmission *)calloc(1, sizeof(EncoreVkSubmission));
    if (submission == NULL) return 0;
    if (encoder == NULL || !encoder->recording || encoder->submitted || encoder->owner == NULL) {
        strncpy(submission->error, "Invalid command encoder state", sizeof(submission->error) - 1); return (size_t)submission;
    }
    EncoreVkDevice *device = encoder->owner;
    submission->owner = device;
    if (device->end_command_buffer(encoder->command_buffer) != VK_SUCCESS) {
        strncpy(submission->error, "Unable to end Vulkan command buffer", sizeof(submission->error) - 1); return (size_t)submission;
    }
    encoder->recording = false;
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0};
    if (device->create_fence(device->device, &fence_info, NULL, &submission->fence) != VK_SUCCESS) {
        strncpy(submission->error, "Unable to create Vulkan submission fence", sizeof(submission->error) - 1); return (size_t)submission;
    }
    void *buffers[1] = {encoder->command_buffer};
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, buffers, 0, NULL};
    if (device->queue_submit(device->queue, 1, &submit, submission->fence) != VK_SUCCESS) {
        strncpy(submission->error, "Unable to submit Vulkan command buffer", sizeof(submission->error) - 1); return (size_t)submission;
    }
    submission->command_buffer = encoder->command_buffer;
    encoder->command_buffer = NULL;
    encoder->submitted = true;
    return (size_t)submission;
}

bool encore_vk_submission_complete(size_t handle) {
    EncoreVkSubmission *submission = (EncoreVkSubmission *)handle;
    if (submission == NULL || submission->owner == NULL || submission->fence == NULL) return false;
    if (submission->complete) return true;
    VkResult status = submission->owner->get_fence_status(submission->owner->device, submission->fence);
    if (status == VK_SUCCESS) submission->complete = true;
    return submission->complete;
}

bool encore_vk_submission_available(size_t handle) {
    EncoreVkSubmission *submission = (EncoreVkSubmission *)handle;
    return submission != NULL && submission->owner != NULL && submission->fence != NULL && submission->command_buffer != NULL;
}

bool encore_vk_submission_wait(size_t handle, uint64_t timeout_ns) {
    EncoreVkSubmission *submission = (EncoreVkSubmission *)handle;
    if (submission == NULL || submission->owner == NULL || submission->fence == NULL) return false;
    if (submission->complete) return true;
    void *fences[1] = {submission->fence};
    VkResult status = submission->owner->wait_for_fences(submission->owner->device, 1, fences, 1, timeout_ns);
    if (status == VK_SUCCESS) submission->complete = true;
    return submission->complete;
}

encore_str encore_vk_submission_error(size_t handle) {
    EncoreVkSubmission *submission = (EncoreVkSubmission *)handle;
    return encore_string(submission == NULL ? "Invalid GPU submission" : submission->error);
}

bool encore_vk_submission_destroy(size_t handle) {
    EncoreVkSubmission *submission = (EncoreVkSubmission *)handle;
    if (submission == NULL) return false;
    if (submission->owner != NULL && submission->owner->device != NULL) {
        if (submission->fence != NULL && !submission->complete) {
            void *fences[1] = {submission->fence};
            submission->owner->wait_for_fences(submission->owner->device, 1, fences, 1, UINT64_MAX);
        }
        if (submission->command_buffer != NULL)
            submission->owner->free_command_buffers(submission->owner->device, submission->owner->command_pool, 1, &submission->command_buffer);
        if (submission->fence != NULL)
            submission->owner->destroy_fence(submission->owner->device, submission->fence, NULL);
    }
    free(submission);
    return true;
}

bool encore_vk_encoder_destroy(size_t handle) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)handle;
    if (encoder == NULL) return false;
    if (encoder->owner != NULL && encoder->command_buffer != NULL && encoder->owner->device != NULL)
        encoder->owner->free_command_buffers(encoder->owner->device, encoder->owner->command_pool, 1, &encoder->command_buffer);
    free(encoder);
    return true;
}

static uint32_t encore_memory_type(EncoreVkDevice *device, uint32_t allowed, uint32_t required) {
    for (uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) != 0 &&
            (device->memory_properties.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    return UINT32_MAX;
}

size_t encore_vk_buffer_create(size_t device_handle, uint64_t size, uint32_t usage, bool host_visible) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)calloc(1, sizeof(EncoreVkBuffer));
    if (buffer == NULL) return 0;
    buffer->owner = device;
    buffer->size = size;
    buffer->usage = usage;
    buffer->host_visible = host_visible;
    if (device == NULL || !device->available || size == 0 || usage == 0) {
        strncpy(buffer->error, "Invalid GPU buffer description", sizeof(buffer->error) - 1); return (size_t)buffer;
    }
    VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage, 0, 0, NULL};
    if (device->create_buffer(device->device, &create_info, NULL, &buffer->buffer) != VK_SUCCESS) {
        strncpy(buffer->error, "Unable to create Vulkan buffer", sizeof(buffer->error) - 1); return (size_t)buffer;
    }
    VkMemoryRequirements requirements;
    device->get_buffer_requirements(device->device, buffer->buffer, &requirements);
    uint32_t required = host_visible
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t fallback = encore_memory_type(device, requirements.memoryTypeBits,
        host_visible ? required | 8u /* HOST_CACHED */ : required);
    if(fallback==UINT32_MAX)fallback=encore_memory_type(device,requirements.memoryTypeBits,required);
    if(fallback==UINT32_MAX && !host_visible)fallback=encore_memory_type(device,requirements.memoryTypeBits,0);
    /* Vertex/instance/uniform and shader storage benefit from mapped VRAM.
     * Indirect streams also carry CPU feedback: keep their coherent, cached
     * host placement. Transfer-only uploads/readbacks stay host-local too. */
    uint32_t gpu_use=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    uint32_t preferred=fallback;
    if(host_visible && (usage & gpu_use) && !(usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)) {
        uint32_t local=encore_memory_type(device,requirements.memoryTypeBits,required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(local!=UINT32_MAX)preferred=local;
    }
    if(preferred==UINT32_MAX) {
        strncpy(buffer->error,"No compatible Vulkan memory type",sizeof(buffer->error)-1);return (size_t)buffer;
    }
    VkMemoryAllocateFlagsInfo address_flags={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,NULL,VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,0};
    for(unsigned attempt=0;attempt<2;++attempt) {
        uint32_t type=attempt?fallback:preferred;
        if(type==UINT32_MAX || (attempt && type==preferred))break;
        VkMemoryAllocateInfo allocate_info={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)?&address_flags:NULL,requirements.size,type};
        buffer->memory=NULL;buffer->mapped=NULL;
        if(device->allocate_memory(device->device,&allocate_info,NULL,&buffer->memory)!=VK_SUCCESS)continue;
        /* Map before binding, so an exhausted BAR mapping can fall back to
         * coherent host memory without illegally rebinding a live VkBuffer. */
        if(host_visible && (device->map_memory(device->device,buffer->memory,0,size,0,&buffer->mapped)!=VK_SUCCESS || !buffer->mapped)) {
            device->free_memory(device->device,buffer->memory,NULL);buffer->memory=NULL;continue;
        }
        if(device->bind_buffer_memory(device->device,buffer->buffer,buffer->memory,0)!=VK_SUCCESS) {
            if(buffer->mapped)device->unmap_memory(device->device,buffer->memory);
            device->free_memory(device->device,buffer->memory,NULL);buffer->mapped=NULL;buffer->memory=NULL;continue;
        }
        buffer->memory_type=type;
        buffer->available=true;return (size_t)buffer;
    }
    strncpy(buffer->error,"Unable to allocate, map or bind Vulkan buffer memory",sizeof(buffer->error)-1);
    return (size_t)buffer;
}

bool encore_vk_buffer_available(size_t handle) { EncoreVkBuffer *b = (EncoreVkBuffer *)handle; return b != NULL && b->available; }
encore_str encore_vk_buffer_error(size_t handle) { EncoreVkBuffer *b = (EncoreVkBuffer *)handle; return encore_string(b == NULL ? "Invalid GPU buffer" : b->error); }
uint64_t encore_vk_buffer_size(size_t handle) { EncoreVkBuffer *b = (EncoreVkBuffer *)handle; return b == NULL ? 0 : b->size; }
bool encore_vk_buffer_host_visible(size_t handle) { EncoreVkBuffer *b = (EncoreVkBuffer *)handle; return b != NULL && b->host_visible; }

bool encore_vk_buffer_write_u32(size_t handle, uint64_t offset, uint32_t value) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (buffer == NULL || !buffer->available || !buffer->host_visible || offset > buffer->size || buffer->size - offset < 4) return false;
    if (buffer->mapped == NULL) return false;
    memcpy((uint8_t *)buffer->mapped + offset, &value, sizeof(value));
    return true;
}

bool encore_vk_buffer_write_f32(size_t handle, uint64_t offset, float value) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (buffer == NULL || !buffer->available || !buffer->host_visible || offset > buffer->size || buffer->size - offset < 4) return false;
    if (buffer->mapped == NULL) return false;
    memcpy((uint8_t *)buffer->mapped + offset, &value, sizeof(value));
    return true;
}

bool encore_vk_buffer_write_u32s(size_t handle, uint64_t offset,
    const uint32_t *values, size_t count) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    uint64_t bytes = (uint64_t)count * sizeof(uint32_t);
    if (buffer == NULL || !buffer->available || !buffer->host_visible ||
        buffer->mapped == NULL || values == NULL || offset > buffer->size ||
        bytes > buffer->size - offset) return false;
    memcpy((uint8_t *)buffer->mapped + offset, values, (size_t)bytes);
    return true;
}

bool encore_vk_buffer_write_f32s(size_t handle, uint64_t offset,
    const float *values, size_t count) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    uint64_t bytes = (uint64_t)count * sizeof(float);
    if (buffer == NULL || !buffer->available || !buffer->host_visible ||
        buffer->mapped == NULL || values == NULL || offset > buffer->size ||
        bytes > buffer->size - offset) return false;
    memcpy((uint8_t *)buffer->mapped + offset, values, (size_t)bytes);
    return true;
}

uint32_t encore_vk_buffer_read_u32(size_t handle, uint64_t offset) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (buffer == NULL || !buffer->available || !buffer->host_visible || offset > buffer->size || sizeof(uint32_t) > buffer->size - offset) return 0;
    if (buffer->mapped == NULL) return 0;
    uint32_t value = 0;
    memcpy(&value, (uint8_t *)buffer->mapped + offset, sizeof(value));
    return value;
}

float encore_vk_buffer_read_f32(size_t handle, uint64_t offset) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (buffer == NULL || !buffer->available || !buffer->host_visible ||
        offset > buffer->size || sizeof(float) > buffer->size - offset ||
        buffer->mapped == NULL) return 0.0f;
    float value = 0.0f;
    memcpy(&value, (uint8_t *)buffer->mapped + offset, sizeof(value));
    return value;
}

bool encore_vk_buffer_read_f32s(size_t handle, uint64_t offset,
    float *values, size_t count) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (!buffer || !buffer->available || !buffer->host_visible || !buffer->mapped ||
        count > SIZE_MAX / sizeof(float) || offset > buffer->size ||
        (uint64_t)count * sizeof(float) > buffer->size - offset ||
        (count && !values)) return false;
    if (count) memcpy(values, (uint8_t *)buffer->mapped + offset, count * sizeof(float));
    return true;
}

bool encore_vk_buffer_destroy(size_t handle) {
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)handle;
    if (buffer == NULL) return false;
    if (buffer->owner != NULL && buffer->owner->device != NULL) {
        if (buffer->mapped != NULL && buffer->memory != NULL)
            buffer->owner->unmap_memory(buffer->owner->device, buffer->memory);
        if (buffer->buffer != NULL) buffer->owner->destroy_buffer(buffer->owner->device, buffer->buffer, NULL);
        if (buffer->memory != NULL) buffer->owner->free_memory(buffer->owner->device, buffer->memory, NULL);
    }
    free(buffer);
    return true;
}

static uint64_t encore_vk_buffer_device_address(
    EncoreVkDevice *device, EncoreVkBuffer *buffer) {
    if (device == NULL || buffer == NULL || !buffer->available ||
        buffer->owner != device ||
        (buffer->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0)
        return 0;
    PFN_vkGetBufferDeviceAddress get_address =
        (PFN_vkGetBufferDeviceAddress)device->get_device_proc(
            device->device, "vkGetBufferDeviceAddress");
    if (get_address == NULL)
        get_address = (PFN_vkGetBufferDeviceAddress)device->get_device_proc(
            device->device, "vkGetBufferDeviceAddressKHR");
    if (get_address == NULL) return 0;
    VkBufferDeviceAddressInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, buffer->buffer};
    return get_address(device->device, &info);
}

static EncoreVkAccelerationStructure *encore_vk_build_acceleration_structure(
    EncoreVkDevice *device, uint32_t type,
    VkAccelerationStructureGeometryKHR *geometry, uint32_t primitive_count) {
    EncoreVkAccelerationStructure *result =
        (EncoreVkAccelerationStructure *)calloc(
            1, sizeof(EncoreVkAccelerationStructure));
    if (result == NULL) return NULL;
    result->owner = device;
    result->type = type;
    if (device == NULL || !device->available || primitive_count == 0 ||
        (device->enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) == 0) {
        strncpy(result->error, "Ray-query acceleration structures are unavailable",
            sizeof(result->error) - 1);
        return result;
    }
    PFN_vkCreateAccelerationStructureKHR create_as =
        (PFN_vkCreateAccelerationStructureKHR)device->get_device_proc(
            device->device, "vkCreateAccelerationStructureKHR");
    result->destroy_acceleration_structure =
        (PFN_vkDestroyAccelerationStructureKHR)device->get_device_proc(
            device->device, "vkDestroyAccelerationStructureKHR");
    PFN_vkGetAccelerationStructureBuildSizesKHR get_sizes =
        (PFN_vkGetAccelerationStructureBuildSizesKHR)device->get_device_proc(
            device->device, "vkGetAccelerationStructureBuildSizesKHR");
    PFN_vkCmdBuildAccelerationStructuresKHR cmd_build =
        (PFN_vkCmdBuildAccelerationStructuresKHR)device->get_device_proc(
            device->device, "vkCmdBuildAccelerationStructuresKHR");
    PFN_vkGetAccelerationStructureDeviceAddressKHR get_as_address =
        (PFN_vkGetAccelerationStructureDeviceAddressKHR)device->get_device_proc(
            device->device, "vkGetAccelerationStructureDeviceAddressKHR");
    if (create_as == NULL || result->destroy_acceleration_structure == NULL ||
        get_sizes == NULL || cmd_build == NULL || get_as_address == NULL) {
        strncpy(result->error, "Vulkan acceleration-structure functions were not found",
            sizeof(result->error) - 1);
        return result;
    }
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        NULL, type, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        NULL, NULL, 1, geometry, NULL, {.deviceAddress = 0}
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        NULL, 0, 0, 0
    };
    get_sizes(device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);
    if (sizes.accelerationStructureSize == 0 || sizes.buildScratchSize == 0) {
        strncpy(result->error, "Vulkan returned empty acceleration-structure sizes",
            sizeof(result->error) - 1);
        return result;
    }
    result->backing = (EncoreVkBuffer *)encore_vk_buffer_create(
        (size_t)device, sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false);
    if (result->backing == NULL || !result->backing->available) {
        strncpy(result->error, "Unable to allocate acceleration-structure storage",
            sizeof(result->error) - 1);
        return result;
    }
    VkAccelerationStructureCreateInfoKHR create = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        NULL, 0, result->backing->buffer, 0,
        sizes.accelerationStructureSize, type, 0
    };
    if (create_as(device->device, &create, NULL,
            &result->acceleration_structure) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan acceleration structure",
            sizeof(result->error) - 1);
        return result;
    }
    EncoreVkBuffer *scratch = (EncoreVkBuffer *)encore_vk_buffer_create(
        (size_t)device, sizes.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false);
    if (scratch == NULL || !scratch->available) {
        if (scratch != NULL) encore_vk_buffer_destroy((size_t)scratch);
        strncpy(result->error, "Unable to allocate acceleration-structure scratch memory",
            sizeof(result->error) - 1);
        return result;
    }
    build.dstAccelerationStructure = result->acceleration_structure;
    build.scratchData.deviceAddress =
        encore_vk_buffer_device_address(device, scratch);
    EncoreVkEncoder *encoder =
        (EncoreVkEncoder *)encore_vk_encoder_create((size_t)device);
    VkAccelerationStructureBuildRangeInfoKHR range = {
        primitive_count, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *range_pointer = &range;
    bool built = encoder != NULL && encoder->recording &&
        build.scratchData.deviceAddress != 0;
    if (built) {
        cmd_build(encoder->command_buffer, 1, &build, &range_pointer);
        built = encore_vk_encoder_submit_wait((size_t)encoder);
    }
    if (encoder != NULL) encore_vk_encoder_destroy((size_t)encoder);
    encore_vk_buffer_destroy((size_t)scratch);
    if (!built) {
        strncpy(result->error, "Acceleration-structure GPU build failed",
            sizeof(result->error) - 1);
        return result;
    }
    VkAccelerationStructureDeviceAddressInfoKHR address_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        NULL, result->acceleration_structure
    };
    result->device_address =
        get_as_address(device->device, &address_info);
    result->available = result->device_address != 0;
    if (!result->available)
        strncpy(result->error, "Acceleration structure has no device address",
            sizeof(result->error) - 1);
    return result;
}

size_t encore_vk_blas_create_triangles(size_t device_handle,
    size_t vertex_handle, uint64_t vertex_stride, uint32_t vertex_count,
    size_t index_handle, uint32_t index_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBuffer *vertices = (EncoreVkBuffer *)vertex_handle;
    EncoreVkBuffer *indices = (EncoreVkBuffer *)index_handle;
    uint64_t vertex_address =
        encore_vk_buffer_device_address(device, vertices);
    uint64_t index_address =
        encore_vk_buffer_device_address(device, indices);
    if (device == NULL || vertices == NULL || indices == NULL ||
        vertex_address == 0 || index_address == 0 || vertex_stride < 12 ||
        vertex_count < 3 || index_count < 3 || index_count % 3 != 0)
        return (size_t)encore_vk_build_acceleration_structure(
            device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            NULL, 0);
    VkAccelerationStructureGeometryTrianglesDataKHR triangles = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        NULL, VK_FORMAT_R32G32B32_SFLOAT,
        {.deviceAddress = vertex_address}, vertex_stride,
        vertex_count - 1, VK_INDEX_TYPE_UINT32,
        {.deviceAddress = index_address}, {.deviceAddress = 0}
    };
    VkAccelerationStructureGeometryKHR geometry;
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    return (size_t)encore_vk_build_acceleration_structure(
        device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        &geometry, index_count / 3);
}

size_t encore_vk_tlas_create_instances(size_t device_handle,
    const size_t *blas_handles, size_t blas_count,
    const float *transforms, size_t transform_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    if (device == NULL || blas_handles == NULL || transforms == NULL ||
        blas_count == 0 || transform_count != blas_count * 16)
        return (size_t)encore_vk_build_acceleration_structure(
            device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, NULL, 0);
    EncoreVkBuffer *instances =
        (EncoreVkBuffer *)encore_vk_buffer_create(
            device_handle,
            blas_count * sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true);
    if (instances == NULL || !instances->available || instances->mapped == NULL) {
        if (instances != NULL) encore_vk_buffer_destroy((size_t)instances);
        return (size_t)encore_vk_build_acceleration_structure(
            device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, NULL, 0);
    }
    VkAccelerationStructureInstanceKHR *destination =
        (VkAccelerationStructureInstanceKHR *)instances->mapped;
    for (size_t i = 0; i < blas_count; ++i) {
        EncoreVkAccelerationStructure *blas =
            (EncoreVkAccelerationStructure *)blas_handles[i];
        if (blas == NULL || !blas->available ||
            blas->owner != device ||
            blas->type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
            encore_vk_buffer_destroy((size_t)instances);
            return (size_t)encore_vk_build_acceleration_structure(
                device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, NULL, 0);
        }
        const float *matrix = transforms + i * 16;
        destination[i] = (VkAccelerationStructureInstanceKHR){
            .matrix = {
                {matrix[0], matrix[4], matrix[8], matrix[12]},
                {matrix[1], matrix[5], matrix[9], matrix[13]},
                {matrix[2], matrix[6], matrix[10], matrix[14]}
            },
            .custom_index_mask = ((uint32_t)i & 0x00ffffffu) | 0xff000000u,
            .sbt_offset_flags = 0,
            .accelerationStructureReference = blas->device_address
        };
    }
    uint64_t instance_address =
        encore_vk_buffer_device_address(device, instances);
    VkAccelerationStructureGeometryInstancesDataKHR instance_data = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        NULL, 0, {.deviceAddress = instance_address}
    };
    VkAccelerationStructureGeometryKHR geometry;
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instance_data;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    EncoreVkAccelerationStructure *result =
        encore_vk_build_acceleration_structure(
            device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            &geometry, (uint32_t)blas_count);
    encore_vk_buffer_destroy((size_t)instances);
    return (size_t)result;
}

/* Build storage is retained until the actual producer retires, including
 * rejected/unsubmitted frames. Opaque AS handles do not borrow frame wrappers. */
uint64_t encore_vk_acceleration_structure_memory_bytes(size_t handle) {
    EncoreVkAccelerationStructure *s=(EncoreVkAccelerationStructure *)handle;
    if(!s)return 0;
    return (s->backing?s->backing->size:0)+(s->build_scratch?s->build_scratch->size:0)+(s->build_inputs?s->build_inputs->size:0);
}
bool encore_vk_acceleration_structure_release_build_resources(size_t handle) {
    EncoreVkAccelerationStructure *s=(EncoreVkAccelerationStructure *)handle;if(!s)return false;
    if(s->build_completion) {
        uint32_t status=encore_vk_completion_status(s->build_completion);
        if(status!=2 && status!=3)return false;
        if(status==3)s->available=false;
        encore_vk_completion_destroy(s->build_completion);s->build_completion=0;
    }
    if(s->build_scratch)encore_vk_buffer_destroy((size_t)s->build_scratch);
    if(s->build_inputs)encore_vk_buffer_destroy((size_t)s->build_inputs);
    s->build_scratch=NULL;s->build_inputs=NULL;return true;
}
bool encore_vk_acceleration_structure_available(size_t handle) {
    EncoreVkAccelerationStructure *structure =
        (EncoreVkAccelerationStructure *)handle;
    return structure != NULL && structure->available;
}

encore_str encore_vk_acceleration_structure_error(size_t handle) {
    EncoreVkAccelerationStructure *structure =
        (EncoreVkAccelerationStructure *)handle;
    return encore_string(structure == NULL
        ? "Invalid acceleration structure" : structure->error);
}

bool encore_vk_acceleration_structure_destroy(size_t handle) {
    EncoreVkAccelerationStructure *structure =
        (EncoreVkAccelerationStructure *)handle;
    if (structure == NULL) return false;
    if(!encore_vk_acceleration_structure_release_build_resources(handle))return false;
    if (structure->acceleration_structure != NULL &&
        structure->owner != NULL && structure->owner->device != NULL &&
        structure->destroy_acceleration_structure != NULL)
        structure->destroy_acceleration_structure(
            structure->owner->device,
            structure->acceleration_structure, NULL);
    if (structure->backing != NULL)
        encore_vk_buffer_destroy((size_t)structure->backing);
    free(structure);
    return true;
}

size_t encore_vk_texture_create(size_t device_handle, uint32_t width, uint32_t height,
    uint32_t mip_levels, uint32_t format, uint32_t usage) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkTexture *texture = (EncoreVkTexture *)calloc(1, sizeof(EncoreVkTexture));
    if (texture == NULL) return 0;
    texture->owner = device;
    texture->width = width;
    texture->height = height;
    texture->mip_levels = mip_levels;
    texture->format = format;
    texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    texture->aspect = format == 126 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    texture->owns_image = true;
    if (device == NULL || !device->available || width == 0 || height == 0 || mip_levels == 0 || format == 0 || usage == 0) {
        strncpy(texture->error, "Invalid GPU texture description", sizeof(texture->error) - 1); return (size_t)texture;
    }
    VkImageCreateInfo create_info = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, 1, format, {width, height, 1},
        mip_levels, 1, 1, 0, usage, 0, 0, NULL, 0
    };
    if (device->create_image(device->device, &create_info, NULL, &texture->image) != VK_SUCCESS) {
        strncpy(texture->error, "Unable to create Vulkan image", sizeof(texture->error) - 1); return (size_t)texture;
    }
    VkMemoryRequirements requirements;
    device->get_image_requirements(device->device, texture->image, &requirements);
    uint32_t memory_type = encore_memory_type(device, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = encore_memory_type(device, requirements.memoryTypeBits, 0);
    if (memory_type == UINT32_MAX) {
        strncpy(texture->error, "No compatible Vulkan image memory type", sizeof(texture->error) - 1); return (size_t)texture;
    }
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, requirements.size, memory_type};
    if (device->allocate_memory(device->device, &allocate_info, NULL, &texture->memory) != VK_SUCCESS) {
        strncpy(texture->error, "Unable to allocate Vulkan image memory", sizeof(texture->error) - 1); return (size_t)texture;
    }
    if (device->bind_image_memory(device->device, texture->image, texture->memory, 0) != VK_SUCCESS) {
        strncpy(texture->error, "Unable to bind Vulkan image memory", sizeof(texture->error) - 1); return (size_t)texture;
    }
    VkImageViewCreateInfo view_info = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, texture->image, 1, format,
        {0, 0, 0, 0}, {texture->aspect, 0, mip_levels, 0, 1}
    };
    if (device->create_image_view(device->device, &view_info, NULL, &texture->view) != VK_SUCCESS) {
        strncpy(texture->error, "Unable to create Vulkan image view", sizeof(texture->error) - 1); return (size_t)texture;
    }
    texture->available = true;
    return (size_t)texture;
}

bool encore_vk_texture_available(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t != NULL && t->available; }
encore_str encore_vk_texture_error(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return encore_string(t == NULL ? "Invalid GPU texture" : t->error); }
uint32_t encore_vk_texture_width(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t == NULL ? 0 : t->width; }
uint32_t encore_vk_texture_height(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t == NULL ? 0 : t->height; }
uint32_t encore_vk_texture_mip_levels(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t == NULL ? 0 : t->mip_levels; }
uint32_t encore_vk_texture_format(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t == NULL ? 0 : t->format; }
uint32_t encore_vk_texture_layout(size_t handle) { EncoreVkTexture *t = (EncoreVkTexture *)handle; return t == NULL ? 0 : t->layout; }

bool encore_vk_texture_destroy(size_t handle) {
    EncoreVkTexture *texture = (EncoreVkTexture *)handle;
    if (texture == NULL) return false;
    if (texture->owner != NULL && texture->owner->device != NULL) {
        if (texture->view != NULL) texture->owner->destroy_image_view(texture->owner->device, texture->view, NULL);
        if (texture->owns_image && texture->image != NULL)
            texture->owner->destroy_image(texture->owner->device, texture->image, NULL);
        if (texture->owns_image && texture->memory != NULL)
            texture->owner->free_memory(texture->owner->device, texture->memory, NULL);
    }
    free(texture);
    return true;
}

static void encore_layout_state(uint32_t layout, uint32_t *stage, uint32_t *access) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: *stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; *access = 0; break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: *stage = VK_PIPELINE_STAGE_TRANSFER_BIT; *access = VK_ACCESS_TRANSFER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: *stage = VK_PIPELINE_STAGE_TRANSFER_BIT; *access = VK_ACCESS_TRANSFER_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: *stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; *access = VK_ACCESS_SHADER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: *stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; *access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
        default: *stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; *access = 0; break;
    }
}

bool encore_vk_encoder_transition_texture(size_t encoder_handle, size_t texture_handle, uint32_t new_layout) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)encoder_handle;
    EncoreVkTexture *texture = (EncoreVkTexture *)texture_handle;
    if (encoder == NULL || !encoder->recording || texture == NULL || !texture->available || encoder->owner != texture->owner) return false;
    if (texture->layout == new_layout) return true;
    uint32_t source_stage, source_access, destination_stage, destination_access;
    encore_layout_state(texture->layout, &source_stage, &source_access);
    encore_layout_state(new_layout, &destination_stage, &destination_access);
    VkImageMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, source_access, destination_access,
        texture->layout, new_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        texture->image, {texture->aspect, 0, texture->mip_levels, 0, 1}
    };
    encoder->owner->cmd_pipeline_barrier(encoder->command_buffer, source_stage, destination_stage, 0,
        0, NULL, 0, NULL, 1, &barrier);
    texture->layout = new_layout;
    return true;
}

bool encore_vk_encoder_copy_buffer_to_texture_region(size_t encoder_handle,
    size_t buffer_handle, size_t texture_handle, uint32_t width,
    uint32_t height, uint32_t mip_level, uint32_t x, uint32_t y);

bool encore_vk_encoder_copy_buffer_to_texture(size_t encoder_handle, size_t buffer_handle,
    size_t texture_handle, uint32_t width, uint32_t height, uint32_t mip_level) {
    return encore_vk_encoder_copy_buffer_to_texture_region(encoder_handle,
        buffer_handle, texture_handle, width, height, mip_level, 0, 0);
}

bool encore_vk_encoder_copy_buffer_to_texture_region(size_t encoder_handle,
    size_t buffer_handle, size_t texture_handle, uint32_t width,
    uint32_t height, uint32_t mip_level, uint32_t x, uint32_t y) {
    EncoreVkEncoder *encoder = (EncoreVkEncoder *)encoder_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    EncoreVkTexture *texture = (EncoreVkTexture *)texture_handle;
    if (encoder == NULL || !encoder->recording || buffer == NULL || !buffer->available || texture == NULL ||
        !texture->available || encoder->owner != buffer->owner || encoder->owner != texture->owner ||
        texture->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || width == 0 || height == 0 ||
        x > texture->width || width > texture->width - x ||
        y > texture->height || height > texture->height - y ||
        mip_level >= texture->mip_levels ||
        buffer->size < (uint64_t)width * (uint64_t)height *
            (texture->format == 109u ? 16u : texture->format == 97u ? 8u :
             texture->format == 70u ? 2u : texture->format == 9u ? 1u : 4u)) return false;
    VkBufferImageCopy region = {
        0, 0, 0, {texture->aspect, mip_level, 0, 1},
        {(int32_t)x, (int32_t)y, 0}, {width, height, 1}
    };
    encoder->owner->cmd_copy_buffer_to_image(encoder->command_buffer, buffer->buffer, texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    return true;
}
