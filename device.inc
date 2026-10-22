/* Vulkan logical devices, shaders, bindings, samplers and pipelines. */
static size_t encore_vk_device_create_internal(size_t context_handle, size_t adapter_index,
    VkSurfaceKHR surface, void *borrowed_device, uint32_t borrowed_queue_family,
    uint32_t required_features, uint32_t optional_features) {
    EncoreVkContext *context = (EncoreVkContext *)context_handle;
    EncoreVkDevice *result = (EncoreVkDevice *)calloc(1, sizeof(EncoreVkDevice));
    if (result == NULL) return 0;
    EncoreVkAdapter *adapter = encore_adapter(context_handle, adapter_index);
    if (context == NULL || !context->available || adapter == NULL) {
        strncpy(result->error, "Invalid Vulkan adapter", sizeof(result->error) - 1); return (size_t)result;
    }
    result->context = context;
    result->physical_device = adapter->handle;
    if (adapter->api_version < encore_vk_version(1, 2, 0)) {
        strncpy(result->error, "Encore requires Vulkan 1.2 or newer", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_properties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)context->get_proc(context->instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice create_device =
        (PFN_vkCreateDevice)context->get_proc(context->instance, "vkCreateDevice");
    PFN_vkGetDeviceProcAddr get_device_proc =
        (PFN_vkGetDeviceProcAddr)context->get_proc(context->instance, "vkGetDeviceProcAddr");
    if (get_queue_properties == NULL || create_device == NULL || get_device_proc == NULL) {
        strncpy(result->error, "Required Vulkan device functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    result->get_device_proc = get_device_proc;
    PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)context->get_proc(context->instance, "vkGetPhysicalDeviceMemoryProperties");
    if (get_memory_properties == NULL) {
        strncpy(result->error, "vkGetPhysicalDeviceMemoryProperties was not found", sizeof(result->error) - 1); return (size_t)result;
    }
    get_memory_properties(adapter->handle, &result->memory_properties);
    uint32_t count = 0;
    get_queue_properties(adapter->handle, &count, NULL);
    if (count == 0) { strncpy(result->error, "Adapter has no queue families", sizeof(result->error) - 1); return (size_t)result; }
    VkQueueFamilyProperties *families = (VkQueueFamilyProperties *)calloc(count, sizeof(VkQueueFamilyProperties));
    if (families == NULL) { strncpy(result->error, "Unable to allocate queue family data", sizeof(result->error) - 1); return (size_t)result; }
    get_queue_properties(adapter->handle, &count, families);
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = NULL;
    if (surface != 0) {
        get_surface_support = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)context->get_proc(
            context->instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (get_surface_support == NULL) {
            free(families); strncpy(result->error, "Vulkan surface support query was not found", sizeof(result->error) - 1); return (size_t)result;
        }
    }
    uint32_t selected = borrowed_device == NULL ? UINT32_MAX : borrowed_queue_family;
    if (borrowed_device == NULL) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t present = 0;
            if (surface != 0 && get_surface_support(adapter->handle, i, surface, &present) != VK_SUCCESS) present = 0;
            if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && (surface == 0 || present != 0)) {
                selected = i; break;
            }
        }
    }
    if (selected == UINT32_MAX || selected >= count || families[selected].queueCount == 0 ||
        (families[selected].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t present = 0;
            if (surface != 0 && get_surface_support(adapter->handle, i, surface, &present) != VK_SUCCESS) present = 0;
            if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                (surface == 0 || present != 0)) { selected = i; break; }
        }
    }
    if (selected == UINT32_MAX) {
        free(families); strncpy(result->error, surface == 0 ? "Adapter has no graphics queue" :
            "Adapter has no graphics queue with present support", sizeof(result->error) - 1); return (size_t)result;
    }
    result->queue_family = selected;
    result->queue_flags = families[selected].queueFlags;
    result->present = surface != 0;
    free(families);
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)context->get_proc(
            context->instance, "vkEnumerateDeviceExtensionProperties");
    PFN_vkGetPhysicalDeviceFeatures2 get_features =
        (PFN_vkGetPhysicalDeviceFeatures2)context->get_proc(
            context->instance, "vkGetPhysicalDeviceFeatures2");
    if (enumerate_extensions == NULL || get_features == NULL) {
        strncpy(result->error, "Vulkan feature negotiation functions were not found",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    bool has_maintenance = surface != 0 &&
        encore_has_extension(adapter->handle, enumerate_extensions,
            "VK_EXT_swapchain_maintenance1");
    bool has_mesh = encore_has_extension(adapter->handle, enumerate_extensions,
        "VK_EXT_mesh_shader");
    bool has_deferred_host = encore_has_extension(adapter->handle, enumerate_extensions,
        "VK_KHR_deferred_host_operations");
    bool has_acceleration = encore_has_extension(adapter->handle, enumerate_extensions,
        "VK_KHR_acceleration_structure");
    bool has_ray_query = encore_has_extension(adapter->handle, enumerate_extensions,
        "VK_KHR_ray_query");
    bool has_fragment_shader_barycentric = encore_has_extension(adapter->handle,
        enumerate_extensions, "VK_KHR_fragment_shader_barycentric");
    if (surface != 0 && !has_maintenance) {
        strncpy(result->error, "Direct Vulkan requires VK_EXT_swapchain_maintenance1",
            sizeof(result->error) - 1);
        return (size_t)result;
    }

    VkPhysicalDeviceVulkan12Features supported12 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, NULL
    };
    VkPhysicalDeviceVulkan13Features supported13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, NULL
    };
    VkPhysicalDeviceMeshShaderFeaturesEXT supported_mesh = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT, NULL
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR supported_acceleration = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, NULL
    };
    VkPhysicalDeviceRayQueryFeaturesKHR supported_ray = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, NULL
    };
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR supported_barycentric = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR, NULL, 0
    };
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT supported_maintenance = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, NULL, 0
    };
    VkPhysicalDeviceFeatures2 supported = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported12, {0}
    };
    void **supported_tail = &supported12.pNext;
    if (adapter->api_version >= encore_vk_version(1, 3, 0)) {
        *supported_tail = &supported13;
        supported_tail = &supported13.pNext;
    }
    if (has_mesh) {
        *supported_tail = &supported_mesh;
        supported_tail = &supported_mesh.pNext;
    }
    if (has_deferred_host && has_acceleration && has_ray_query) {
        *supported_tail = &supported_acceleration;
        supported_tail = &supported_acceleration.pNext;
        *supported_tail = &supported_ray;
        supported_tail = &supported_ray.pNext;
    }
    if (has_fragment_shader_barycentric) {
        *supported_tail = &supported_barycentric;
        supported_tail = &supported_barycentric.pNext;
    }
    if (surface != 0) {
        *supported_tail = &supported_maintenance;
    }
    get_features(adapter->handle, &supported);
    if (surface != 0 && supported_maintenance.swapchainMaintenance1 == 0) {
        strncpy(result->error,
            "Direct Vulkan requires swapchainMaintenance1 presentation fences",
            sizeof(result->error) - 1);
        return (size_t)result;
    }

    uint32_t supported_features = 0;
    if (supported12.descriptorIndexing != 0 &&
        supported12.runtimeDescriptorArray != 0 &&
        supported12.descriptorBindingPartiallyBound != 0 &&
        supported12.descriptorBindingVariableDescriptorCount != 0 &&
        supported12.shaderSampledImageArrayNonUniformIndexing != 0 &&
        supported12.shaderStorageBufferArrayNonUniformIndexing != 0 &&
        supported12.shaderStorageImageArrayNonUniformIndexing != 0) {
        supported_features |= ENCORE_GPU_FEATURE_DESCRIPTOR_INDEXING;
    }
    if (supported12.bufferDeviceAddress != 0)
        supported_features |= ENCORE_GPU_FEATURE_BUFFER_DEVICE_ADDRESS;
    if (supported12.timelineSemaphore != 0)
        supported_features |= ENCORE_GPU_FEATURE_TIMELINE_SEMAPHORE;
    if (supported12.drawIndirectCount != 0)
        supported_features |= ENCORE_GPU_FEATURE_DRAW_INDIRECT_COUNT;
    if (supported12.shaderBufferInt64Atomics != 0 &&
        supported12.shaderSharedInt64Atomics != 0)
        supported_features |= ENCORE_GPU_FEATURE_SHADER_INT64_ATOMICS;
    if (adapter->api_version >= encore_vk_version(1, 3, 0) &&
        supported13.synchronization2 != 0)
        supported_features |= ENCORE_GPU_FEATURE_SYNCHRONIZATION_2;
    if (adapter->api_version >= encore_vk_version(1, 3, 0) &&
        supported13.dynamicRendering != 0)
        supported_features |= ENCORE_GPU_FEATURE_DYNAMIC_RENDERING;
    if (has_mesh && supported_mesh.meshShader != 0)
        supported_features |= ENCORE_GPU_FEATURE_MESH_SHADER;
    if (has_deferred_host && has_acceleration && has_ray_query &&
        supported_acceleration.accelerationStructure != 0 &&
        supported_ray.rayQuery != 0 &&
        (supported_features & ENCORE_GPU_FEATURE_BUFFER_DEVICE_ADDRESS) != 0) {
        supported_features |= ENCORE_GPU_FEATURE_RAY_QUERY;
    }
    if (has_fragment_shader_barycentric &&
        supported_barycentric.fragmentShaderBarycentric != 0)
        supported_features |= ENCORE_GPU_FEATURE_FRAGMENT_SHADER_BARYCENTRIC;
    uint32_t requested_features = required_features | optional_features;
    uint32_t missing_required = required_features & ~supported_features;
    if (missing_required != 0) {
        snprintf(result->error, sizeof(result->error),
            "Required Vulkan rendering features are unavailable (mask 0x%x)",
            missing_required);
        return (size_t)result;
    }
    uint32_t enabled_features = requested_features & supported_features;
    if ((enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) != 0)
        enabled_features |= ENCORE_GPU_FEATURE_BUFFER_DEVICE_ADDRESS;
    result->requested_features = requested_features;
    result->enabled_features = borrowed_device == NULL ? enabled_features : 0;
    result->swapchain_maintenance = surface != 0;

    VkPhysicalDeviceVulkan12Features enabled12 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, NULL
    };
    VkPhysicalDeviceVulkan13Features enabled13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, NULL
    };
    VkPhysicalDeviceMeshShaderFeaturesEXT enabled_mesh = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT, NULL
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enabled_acceleration = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, NULL
    };
    VkPhysicalDeviceRayQueryFeaturesKHR enabled_ray = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, NULL
    };
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR enabled_barycentric = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR, NULL, 0
    };
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabled_maintenance = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        NULL, surface != 0 ? 1u : 0u
    };
    if ((enabled_features & ENCORE_GPU_FEATURE_DESCRIPTOR_INDEXING) != 0) {
        enabled12.descriptorIndexing = 1;
        enabled12.runtimeDescriptorArray = 1;
        enabled12.descriptorBindingPartiallyBound = 1;
        enabled12.descriptorBindingVariableDescriptorCount = 1;
        enabled12.shaderSampledImageArrayNonUniformIndexing = 1;
        enabled12.shaderStorageBufferArrayNonUniformIndexing = 1;
        enabled12.shaderStorageImageArrayNonUniformIndexing = 1;
    }
    enabled12.bufferDeviceAddress =
        (enabled_features & ENCORE_GPU_FEATURE_BUFFER_DEVICE_ADDRESS) != 0;
    enabled12.timelineSemaphore =
        (enabled_features & ENCORE_GPU_FEATURE_TIMELINE_SEMAPHORE) != 0;
    enabled12.drawIndirectCount =
        (enabled_features & ENCORE_GPU_FEATURE_DRAW_INDIRECT_COUNT) != 0;
    enabled12.shaderBufferInt64Atomics =
        (enabled_features & ENCORE_GPU_FEATURE_SHADER_INT64_ATOMICS) != 0;
    enabled12.shaderSharedInt64Atomics =
        (enabled_features & ENCORE_GPU_FEATURE_SHADER_INT64_ATOMICS) != 0;
    enabled13.synchronization2 =
        (enabled_features & ENCORE_GPU_FEATURE_SYNCHRONIZATION_2) != 0;
    enabled13.dynamicRendering =
        (enabled_features & ENCORE_GPU_FEATURE_DYNAMIC_RENDERING) != 0;
    enabled_mesh.meshShader =
        (enabled_features & ENCORE_GPU_FEATURE_MESH_SHADER) != 0;
    enabled_mesh.taskShader = enabled_mesh.meshShader && supported_mesh.taskShader != 0;
    enabled_acceleration.accelerationStructure =
        (enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) != 0;
    enabled_ray.rayQuery =
        (enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) != 0;
    enabled_barycentric.fragmentShaderBarycentric =
        (enabled_features & ENCORE_GPU_FEATURE_FRAGMENT_SHADER_BARYCENTRIC) != 0;

    const void *device_chain = NULL;
    void **enabled_tail = (void **)&device_chain;
    if (enabled_features != 0) {
        *enabled_tail = &enabled12;
        enabled_tail = &enabled12.pNext;
        if (adapter->api_version >= encore_vk_version(1, 3, 0)) {
            *enabled_tail = &enabled13;
            enabled_tail = &enabled13.pNext;
        }
        if ((enabled_features & ENCORE_GPU_FEATURE_MESH_SHADER) != 0) {
            *enabled_tail = &enabled_mesh;
            enabled_tail = &enabled_mesh.pNext;
        }
        if ((enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) != 0) {
            *enabled_tail = &enabled_acceleration;
            enabled_tail = &enabled_acceleration.pNext;
            *enabled_tail = &enabled_ray;
            enabled_tail = &enabled_ray.pNext;
        }
        if ((enabled_features & ENCORE_GPU_FEATURE_FRAGMENT_SHADER_BARYCENTRIC) != 0) {
            *enabled_tail = &enabled_barycentric;
            enabled_tail = &enabled_barycentric.pNext;
        }
    }
    if (surface != 0) {
        *enabled_tail = &enabled_maintenance;
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, selected, 1, &priority};
    const char *device_extensions[14];
    uint32_t device_extension_count = 0;
    if (surface != 0) {
        device_extensions[device_extension_count++] = "VK_KHR_swapchain";
        device_extensions[device_extension_count++] = "VK_EXT_swapchain_maintenance1";
    }
    if ((enabled_features & ENCORE_GPU_FEATURE_MESH_SHADER) != 0)
        device_extensions[device_extension_count++] = "VK_EXT_mesh_shader";
    if ((enabled_features & ENCORE_GPU_FEATURE_RAY_QUERY) != 0) {
        device_extensions[device_extension_count++] = "VK_KHR_deferred_host_operations";
        device_extensions[device_extension_count++] = "VK_KHR_acceleration_structure";
        device_extensions[device_extension_count++] = "VK_KHR_ray_query";
    }
    if ((enabled_features & ENCORE_GPU_FEATURE_FRAGMENT_SHADER_BARYCENTRIC) != 0)
        device_extensions[device_extension_count++] = "VK_KHR_fragment_shader_barycentric";
    /* NGX's Linux Vulkan kernels require these optional device extensions.
     * Enable only advertised extensions; other vendors retain their native path. */
    const char *reconstruction_extensions[]={"VK_NVX_binary_import","VK_NVX_image_view_handle","VK_KHR_push_descriptor","VK_KHR_get_memory_requirements2","VK_KHR_dedicated_allocation"};
    for (uint32_t e=0;e<5;++e)
        if (encore_has_extension(adapter->handle,enumerate_extensions,reconstruction_extensions[e]))
            device_extensions[device_extension_count++]=reconstruction_extensions[e];
    /* Core VkPhysicalDeviceFeatures.independentBlend, Khronos field index 3. */
    uint32_t core_features[64] = {0};
    result->independent_blend = borrowed_device == NULL && supported.features[3] != 0;
    core_features[3] = result->independent_blend;
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        device_chain, 0, 1, &queue_info, 0, NULL,
        device_extension_count, device_extension_count == 0 ? NULL : device_extensions, core_features};
    VkResult status = VK_SUCCESS;
    if (borrowed_device == NULL) {
        status = create_device(adapter->handle, &device_info, NULL, &result->device);
        result->owns_device = status == VK_SUCCESS;
    } else {
        result->device = borrowed_device;
        result->owns_device = false;
    }
    if (status != VK_SUCCESS) { strncpy(result->error, "Unable to create Vulkan logical device", sizeof(result->error) - 1); return (size_t)result; }
    get_device_proc = encore_parallel_attach(result->device, selected, get_device_proc);
    result->get_device_proc = get_device_proc;
    result->destroy_device = (PFN_vkDestroyDevice)get_device_proc(result->device, "vkDestroyDevice");
    PFN_vkGetDeviceQueue get_queue = (PFN_vkGetDeviceQueue)get_device_proc(result->device, "vkGetDeviceQueue");
    if (result->destroy_device == NULL || get_queue == NULL) {
        strncpy(result->error, "Required Vulkan queue functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    get_queue(result->device, selected, 0, &result->queue);
    if (result->queue == NULL) { strncpy(result->error, "Unable to acquire Vulkan graphics queue", sizeof(result->error) - 1); return (size_t)result; }
    PFN_vkCreateCommandPool create_command_pool =
        (PFN_vkCreateCommandPool)get_device_proc(result->device, "vkCreateCommandPool");
    result->destroy_command_pool = (PFN_vkDestroyCommandPool)get_device_proc(result->device, "vkDestroyCommandPool");
    result->allocate_command_buffers = (PFN_vkAllocateCommandBuffers)get_device_proc(result->device, "vkAllocateCommandBuffers");
    result->free_command_buffers = (PFN_vkFreeCommandBuffers)get_device_proc(result->device, "vkFreeCommandBuffers");
    result->begin_command_buffer = (PFN_vkBeginCommandBuffer)get_device_proc(result->device, "vkBeginCommandBuffer");
    result->end_command_buffer = (PFN_vkEndCommandBuffer)get_device_proc(result->device, "vkEndCommandBuffer");
    result->reset_command_buffer = (PFN_vkResetCommandBuffer)get_device_proc(result->device, "vkResetCommandBuffer");
    result->queue_submit = (PFN_vkQueueSubmit)get_device_proc(result->device, "vkQueueSubmit");
    result->queue_wait_idle = (PFN_vkQueueWaitIdle)get_device_proc(result->device, "vkQueueWaitIdle");
    result->create_buffer = (PFN_vkCreateBuffer)get_device_proc(result->device, "vkCreateBuffer");
    result->destroy_buffer = (PFN_vkDestroyBuffer)get_device_proc(result->device, "vkDestroyBuffer");
    result->get_buffer_requirements = (PFN_vkGetBufferMemoryRequirements)get_device_proc(result->device, "vkGetBufferMemoryRequirements");
    result->allocate_memory = (PFN_vkAllocateMemory)get_device_proc(result->device, "vkAllocateMemory");
    result->free_memory = (PFN_vkFreeMemory)get_device_proc(result->device, "vkFreeMemory");
    result->bind_buffer_memory = (PFN_vkBindBufferMemory)get_device_proc(result->device, "vkBindBufferMemory");
    result->map_memory = (PFN_vkMapMemory)get_device_proc(result->device, "vkMapMemory");
    result->unmap_memory = (PFN_vkUnmapMemory)get_device_proc(result->device, "vkUnmapMemory");
    result->create_image = (PFN_vkCreateImage)get_device_proc(result->device, "vkCreateImage");
    result->destroy_image = (PFN_vkDestroyImage)get_device_proc(result->device, "vkDestroyImage");
    result->get_image_requirements = (PFN_vkGetImageMemoryRequirements)get_device_proc(result->device, "vkGetImageMemoryRequirements");
    result->bind_image_memory = (PFN_vkBindImageMemory)get_device_proc(result->device, "vkBindImageMemory");
    result->create_image_view = (PFN_vkCreateImageView)get_device_proc(result->device, "vkCreateImageView");
    result->destroy_image_view = (PFN_vkDestroyImageView)get_device_proc(result->device, "vkDestroyImageView");
    result->cmd_pipeline_barrier = (PFN_vkCmdPipelineBarrier)get_device_proc(result->device, "vkCmdPipelineBarrier");
    result->cmd_copy_buffer_to_image = (PFN_vkCmdCopyBufferToImage)get_device_proc(result->device, "vkCmdCopyBufferToImage");
    result->cmd_copy_image_to_buffer = (PFN_vkCmdCopyImageToBuffer)get_device_proc(result->device, "vkCmdCopyImageToBuffer");
    result->cmd_copy_buffer = (PFN_vkCmdCopyBuffer)get_device_proc(result->device, "vkCmdCopyBuffer");
    result->cmd_update_buffer = (PFN_vkCmdUpdateBuffer)get_device_proc(result->device, "vkCmdUpdateBuffer");
    result->cmd_fill_buffer = (PFN_vkCmdFillBuffer)get_device_proc(result->device, "vkCmdFillBuffer");
    result->cmd_bind_pipeline = (PFN_vkCmdBindPipeline)get_device_proc(result->device, "vkCmdBindPipeline");
    result->cmd_bind_descriptor_sets = (PFN_vkCmdBindDescriptorSets)get_device_proc(result->device, "vkCmdBindDescriptorSets");
    result->cmd_push_constants = (void (*)(void *, void *, uint32_t, uint32_t, uint32_t, const void *))get_device_proc(result->device, "vkCmdPushConstants");
    result->cmd_dispatch = (PFN_vkCmdDispatch)get_device_proc(result->device, "vkCmdDispatch");
    result->cmd_dispatch_indirect = (PFN_vkCmdDispatchIndirect)get_device_proc(result->device, "vkCmdDispatchIndirect");
    result->create_fence = (PFN_vkCreateFence)get_device_proc(result->device, "vkCreateFence");
    result->destroy_fence = (PFN_vkDestroyFence)get_device_proc(result->device, "vkDestroyFence");
    result->get_fence_status = (PFN_vkGetFenceStatus)get_device_proc(result->device, "vkGetFenceStatus");
    result->wait_for_fences = (PFN_vkWaitForFences)get_device_proc(result->device, "vkWaitForFences");
    if (create_command_pool == NULL || result->destroy_command_pool == NULL || result->allocate_command_buffers == NULL ||
        result->free_command_buffers == NULL || result->begin_command_buffer == NULL || result->end_command_buffer == NULL ||
        result->reset_command_buffer == NULL ||
        result->queue_submit == NULL || result->queue_wait_idle == NULL || result->create_buffer == NULL ||
        result->destroy_buffer == NULL || result->get_buffer_requirements == NULL || result->allocate_memory == NULL ||
        result->free_memory == NULL || result->bind_buffer_memory == NULL || result->map_memory == NULL || result->unmap_memory == NULL ||
        result->create_image == NULL || result->destroy_image == NULL || result->get_image_requirements == NULL || result->bind_image_memory == NULL ||
        result->create_image_view == NULL || result->destroy_image_view == NULL || result->cmd_pipeline_barrier == NULL ||
        result->cmd_copy_buffer_to_image == NULL || result->cmd_copy_image_to_buffer == NULL ||
        result->cmd_copy_buffer == NULL || result->cmd_fill_buffer == NULL ||
        result->cmd_bind_pipeline == NULL ||
        result->cmd_bind_descriptor_sets == NULL || result->cmd_dispatch == NULL ||
        result->create_fence == NULL || result->destroy_fence == NULL || result->get_fence_status == NULL || result->wait_for_fences == NULL) {
        strncpy(result->error, "Required Vulkan command functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, selected
    };
    if (create_command_pool(result->device, &pool_info, NULL, &result->command_pool) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan command pool", sizeof(result->error) - 1); return (size_t)result;
    }
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_device_create(size_t context_handle, size_t adapter_index) {
    return encore_vk_device_create_internal(context_handle, adapter_index, 0, NULL, 0, 0, 0);
}

size_t encore_vk_device_create_features(size_t context_handle, size_t adapter_index,
    uint32_t required_features, uint32_t optional_features) {
    return encore_vk_device_create_internal(context_handle, adapter_index, 0, NULL, 0,
        required_features, optional_features);
}

size_t encore_vk_present_device_create(size_t context_handle, size_t adapter_index, size_t window_handle) {
    EncoreVkSurface *window = (EncoreVkSurface *)window_handle;
    if (window == NULL || window->owner != (EncoreVkContext *)context_handle || window->surface == 0) {
        EncoreVkDevice *result = (EncoreVkDevice *)calloc(1, sizeof(EncoreVkDevice));
        if (result != NULL) strncpy(result->error, "Invalid Encore presentation window", sizeof(result->error) - 1);
        return (size_t)result;
    }
    return encore_vk_device_create_internal(context_handle, adapter_index, window->surface,
        NULL, 0, 0, 0);
}

size_t encore_vk_present_device_create_features(size_t context_handle,
    size_t adapter_index, size_t window_handle, uint32_t required_features,
    uint32_t optional_features) {
    EncoreVkSurface *window = (EncoreVkSurface *)window_handle;
    if (window == NULL || window->owner != (EncoreVkContext *)context_handle ||
        window->surface == 0) {
        EncoreVkDevice *result =
            (EncoreVkDevice *)calloc(1, sizeof(EncoreVkDevice));
        if (result != NULL)
            strncpy(result->error, "Invalid Encore presentation window",
                sizeof(result->error) - 1);
        return (size_t)result;
    }
    return encore_vk_device_create_internal(context_handle, adapter_index,
        window->surface, NULL, 0, required_features, optional_features);
}

size_t encore_vk_device_borrow(size_t context_handle, size_t device_handle, uint32_t queue_family) {
    return encore_vk_device_create_internal(context_handle, 0, 0,
        (void *)device_handle, queue_family, 0, 0);
}

bool encore_vk_device_destroy(size_t handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)handle;
    if (device == NULL) return false;
    encore_timing_device_destroy(device);
    if (device->device != NULL && device->command_pool != NULL && device->destroy_command_pool != NULL)
        device->destroy_command_pool(device->device, device->command_pool, NULL);
    if (device->owns_device && device->device != NULL && device->destroy_device != NULL)
        device->destroy_device(device->device, NULL);
    free(device);
    return true;
}

bool encore_vk_device_available(size_t handle) { EncoreVkDevice *d = (EncoreVkDevice *)handle; return d != NULL && d->available; }
encore_str encore_vk_device_error(size_t handle) { EncoreVkDevice *d = (EncoreVkDevice *)handle; return encore_string(d == NULL ? "Invalid Vulkan device" : d->error); }
uint32_t encore_vk_device_queue_family(size_t handle) { EncoreVkDevice *d = (EncoreVkDevice *)handle; return d == NULL ? 0 : d->queue_family; }
size_t encore_vk_device_handle(size_t handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)handle;
    return device == NULL ? 0 : (size_t)(uintptr_t)device->device;
}
bool encore_vk_device_graphics(size_t handle) { EncoreVkDevice *d = (EncoreVkDevice *)handle; return d != NULL && (d->queue_flags & VK_QUEUE_GRAPHICS_BIT) != 0; }
bool encore_vk_device_compute(size_t handle) { EncoreVkDevice *d = (EncoreVkDevice *)handle; return d != NULL && (d->queue_flags & VK_QUEUE_COMPUTE_BIT) != 0; }
uint32_t encore_vk_device_requested_features(size_t handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)handle;
    return device == NULL ? 0 : device->requested_features;
}
uint32_t encore_vk_device_enabled_features(size_t handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)handle;
    return device == NULL ? 0 : device->enabled_features;
}

size_t encore_vk_shader_create(size_t device_handle,
    const uint32_t *words, size_t word_count, uint32_t stage) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkShader *shader = (EncoreVkShader *)calloc(1, sizeof(EncoreVkShader));
    if (shader == NULL) return 0;
    shader->owner = device;
    shader->stage = stage;
    if (device == NULL || !device->available || words == NULL || word_count == 0 ||
        (stage != VK_SHADER_STAGE_VERTEX_BIT && stage != VK_SHADER_STAGE_FRAGMENT_BIT &&
            stage != VK_SHADER_STAGE_COMPUTE_BIT && stage != VK_SHADER_STAGE_TASK_BIT_EXT &&
            stage != VK_SHADER_STAGE_MESH_BIT_EXT) ||
        ((stage == VK_SHADER_STAGE_TASK_BIT_EXT || stage == VK_SHADER_STAGE_MESH_BIT_EXT) &&
            (device->enabled_features & ENCORE_GPU_FEATURE_MESH_SHADER) == 0)) {
        strncpy(shader->error, "Invalid SPIR-V shader input", sizeof(shader->error) - 1); return (size_t)shader;
    }
    PFN_vkCreateShaderModule create_shader_module =
        (PFN_vkCreateShaderModule)device->get_device_proc(device->device, "vkCreateShaderModule");
    shader->destroy_shader_module =
        (PFN_vkDestroyShaderModule)device->get_device_proc(device->device, "vkDestroyShaderModule");
    if (create_shader_module == NULL || shader->destroy_shader_module == NULL) {
        strncpy(shader->error, "Vulkan shader module functions were not found", sizeof(shader->error) - 1); return (size_t)shader;
    }
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
        word_count * sizeof(uint32_t), words};
    VkResult status = create_shader_module(device->device, &info, NULL, &shader->module);
    if (status != VK_SUCCESS) {
        snprintf(shader->error, sizeof(shader->error), "Unable to create Vulkan shader module (result %d)", status);
        return (size_t)shader;
    }
    shader->available = true;
    return (size_t)shader;
}

bool encore_vk_shader_available(size_t handle) { EncoreVkShader *s = (EncoreVkShader *)handle; return s != NULL && s->available; }
encore_str encore_vk_shader_error(size_t handle) { EncoreVkShader *s = (EncoreVkShader *)handle; return encore_string(s == NULL ? "Invalid shader module" : s->error); }
uint32_t encore_vk_shader_stage(size_t handle) { EncoreVkShader *s = (EncoreVkShader *)handle; return s == NULL ? 0 : s->stage; }
bool encore_vk_shader_destroy(size_t handle) {
    EncoreVkShader *shader = (EncoreVkShader *)handle;
    if (shader == NULL) return false;
    if (shader->module != NULL && shader->owner != NULL && shader->owner->device != NULL && shader->destroy_shader_module != NULL)
        shader->destroy_shader_module(shader->owner->device, shader->module, NULL);
    free(shader);
    return true;
}

size_t encore_vk_bind_group_layout_create_entries(size_t device_handle,
    const uint32_t *bindings, size_t binding_count,
    const uint32_t *types, size_t type_count,
    const uint32_t *stages, size_t stage_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *result = (EncoreVkBindGroupLayout *)calloc(1, sizeof(EncoreVkBindGroupLayout));
    if (result == NULL) return 0;
    result->owner = device;
    if (device == NULL || !device->available || binding_count == 0 || binding_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count != type_count || binding_count != stage_count || !bindings || !types || !stages) {
        strncpy(result->error, "Invalid bind group layout", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateDescriptorSetLayout create_layout =
        (PFN_vkCreateDescriptorSetLayout)device->get_device_proc(device->device, "vkCreateDescriptorSetLayout");
    result->destroy_layout =
        (PFN_vkDestroyDescriptorSetLayout)device->get_device_proc(device->device, "vkDestroyDescriptorSetLayout");
    if (create_layout == NULL || result->destroy_layout == NULL) {
        strncpy(result->error, "Vulkan descriptor layout functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorSetLayoutBinding layout_bindings[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < binding_count; ++i) {
        if ((types[i] != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && types[i] != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                types[i] != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
                types[i] != VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) ||
            (stages[i] & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT |
                VK_SHADER_STAGE_MESH_BIT_EXT)) == 0) {
            strncpy(result->error, "Unsupported bind group layout entry", sizeof(result->error) - 1); return (size_t)result;
        }
        for (size_t previous = 0; previous < i; ++previous) if (bindings[previous] == bindings[i]) {
            strncpy(result->error, "Duplicate bind group binding", sizeof(result->error) - 1); return (size_t)result;
        }
        result->bindings[i] = bindings[i]; result->types[i] = types[i]; result->stages[i] = stages[i];
        layout_bindings[i] = (VkDescriptorSetLayoutBinding){bindings[i], types[i], 1, stages[i], NULL};
    }
    result->binding_count = (uint32_t)binding_count;
    VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0,
        (uint32_t)binding_count, layout_bindings};
    if (create_layout(device->device, &info, NULL, &result->layout) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan bind group layout", sizeof(result->error) - 1); return (size_t)result;
    }
    result->available = true; return (size_t)result;
}

size_t encore_vk_bind_group_layout_create(size_t device_handle, uint32_t binding, uint32_t stage) {
    uint32_t type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    return encore_vk_bind_group_layout_create_entries(
        device_handle, &binding, 1, &type, 1, &stage, 1);
}

bool encore_vk_bind_group_layout_available(size_t handle) { EncoreVkBindGroupLayout *l = (EncoreVkBindGroupLayout *)handle; return l != NULL && l->available; }
encore_str encore_vk_bind_group_layout_error(size_t handle) { EncoreVkBindGroupLayout *l = (EncoreVkBindGroupLayout *)handle; return encore_string(l == NULL ? "Invalid bind group layout" : l->error); }
bool encore_vk_bind_group_layout_destroy(size_t handle) {
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)handle;
    if (layout == NULL) return false;
    if (layout->layout != NULL && layout->owner != NULL && layout->owner->device != NULL && layout->destroy_layout != NULL)
        layout->destroy_layout(layout->owner->device, layout->layout, NULL);
    free(layout); return true;
}

size_t encore_vk_bind_group_create(size_t device_handle, size_t layout_handle, size_t buffer_handle,
    uint64_t offset, uint64_t range) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device; result->layout = layout;
    if (device == NULL || !device->available || layout == NULL || !layout->available || layout->owner != device ||
        buffer == NULL || !buffer->available || buffer->owner != device || range == 0 || offset > buffer->size || range > buffer->size - offset) {
        strncpy(result->error, "Invalid uniform bind group resources", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateDescriptorPool create_pool = (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool = (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets = (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets = (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL || allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error, "Vulkan descriptor set functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &pool_size};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan descriptor pool", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate Vulkan descriptor set", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorBufferInfo buffer_info = {buffer->buffer, offset, range};
    if (layout->binding_count != 1 || layout->types[0] != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        strncpy(result->error, "Uniform bind group requires one uniform layout entry", sizeof(result->error) - 1); return (size_t)result;
    }
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, layout->bindings[0],
        0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &buffer_info, NULL};
    update_sets(device->device, 1, &write, 0, NULL);
    result->available = true; return (size_t)result;
}

size_t encore_vk_storage_bind_group_create(size_t device_handle, size_t layout_handle, size_t buffer_handle,
    uint64_t offset, uint64_t range) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device; result->layout = layout;
    if (device == NULL || !device->available || layout == NULL || !layout->available || layout->owner != device ||
        layout->binding_count != 1 || layout->types[0] != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
        buffer == NULL || !buffer->available || buffer->owner != device || range == 0 || offset > buffer->size || range > buffer->size - offset) {
        strncpy(result->error, "Invalid storage bind group resources", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateDescriptorPool create_pool = (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool = (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets = (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets = (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL || allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error, "Vulkan storage descriptor functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &pool_size};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS) return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS) return (size_t)result;
    VkDescriptorBufferInfo buffer_info = {buffer->buffer, offset, range};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, layout->bindings[0],
        0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &buffer_info, NULL};
    update_sets(device->device, 1, &write, 0, NULL);
    result->available = true; return (size_t)result;
}

size_t encore_vk_storage_bind_group_create_many(size_t device_handle, size_t layout_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device; result->layout = layout;
    if (device == NULL || !device->available || layout == NULL || !layout->available ||
        layout->owner != device || binding_count == 0 || binding_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count != buffer_count || binding_count != offset_count ||
        binding_count != range_count || layout->binding_count != binding_count ||
        bindings == NULL || buffer_handles == NULL || offsets == NULL || ranges == NULL) {
        strncpy(result->error, "Invalid multi-storage bind group resources", sizeof(result->error) - 1);
        return (size_t)result;
    }
    EncoreVkBuffer *buffers[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < binding_count; ++i) {
        buffers[i] = (EncoreVkBuffer *)buffer_handles[i];
        bool layout_binding = false;
        for (uint32_t j = 0; j < layout->binding_count; ++j)
            if (layout->bindings[j] == bindings[i] &&
                layout->types[j] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) layout_binding = true;
        if (!layout_binding || buffers[i] == NULL || !buffers[i]->available ||
            buffers[i]->owner != device || ranges[i] == 0 ||
            offsets[i] > buffers[i]->size ||
            ranges[i] > buffers[i]->size - offsets[i]) {
            strncpy(result->error, "Invalid multi-storage buffer range", sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    PFN_vkCreateDescriptorPool create_pool =
        (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool =
        (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets =
        (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error, "Vulkan multi-storage descriptor functions were not found",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)binding_count};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL, 0, 1, 1, &pool_size};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorBufferInfo infos[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    VkWriteDescriptorSet writes[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < binding_count; ++i) {
        infos[i] = (VkDescriptorBufferInfo){buffers[i]->buffer, offsets[i], ranges[i]};
        writes[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            NULL, result->set, bindings[i], 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &infos[i], NULL};
    }
    update_sets(device->device, (uint32_t)binding_count, writes, 0, NULL);
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_buffer_bind_group_create_many(size_t device_handle,
    size_t layout_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout =
        (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBindGroup *result =
        (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device;
    result->layout = layout;
    if (device == NULL || !device->available || layout == NULL ||
        !layout->available || layout->owner != device ||
        binding_count == 0 || binding_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count != buffer_count || binding_count != offset_count ||
        binding_count != range_count ||
        layout->binding_count != binding_count || bindings == NULL ||
        buffer_handles == NULL || offsets == NULL || ranges == NULL) {
        strncpy(result->error, "Invalid multi-buffer bind group resources",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    EncoreVkBuffer *buffers[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    uint32_t descriptor_types[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    uint32_t uniform_count = 0, storage_count = 0;
    for (size_t i = 0; i < binding_count; ++i) {
        buffers[i] = (EncoreVkBuffer *)buffer_handles[i];
        bool layout_binding = false;
        for (uint32_t j = 0; j < layout->binding_count; ++j) {
            if (layout->bindings[j] == bindings[i] &&
                (layout->types[j] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 layout->types[j] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) {
                descriptor_types[i] = layout->types[j];
                layout_binding = true;
                if (layout->types[j] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    ++uniform_count;
                else
                    ++storage_count;
                break;
            }
        }
        if (!layout_binding || buffers[i] == NULL || !buffers[i]->available ||
            buffers[i]->owner != device || ranges[i] == 0 ||
            offsets[i] > buffers[i]->size ||
            ranges[i] > buffers[i]->size - offsets[i]) {
            strncpy(result->error, "Invalid multi-buffer descriptor range",
                sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    PFN_vkCreateDescriptorPool create_pool =
        (PFN_vkCreateDescriptorPool)device->get_device_proc(
            device->device, "vkCreateDescriptorPool");
    result->destroy_pool =
        (PFN_vkDestroyDescriptorPool)device->get_device_proc(
            device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets =
        (PFN_vkAllocateDescriptorSets)device->get_device_proc(
            device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)device->get_device_proc(
            device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error,
            "Vulkan multi-buffer descriptor functions were not found",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkDescriptorPoolSize pool_sizes[2];
    uint32_t pool_size_count = 0;
    if (uniform_count > 0)
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_count};
    if (storage_count > 0)
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_count};
    VkDescriptorPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL, 0, 1, pool_size_count, pool_sizes};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorBufferInfo infos[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    VkWriteDescriptorSet writes[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < binding_count; ++i) {
        infos[i] = (VkDescriptorBufferInfo){
            buffers[i]->buffer, offsets[i], ranges[i]};
        writes[i] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            NULL, result->set, bindings[i], 0, 1,
            descriptor_types[i], NULL, &infos[i], NULL};
    }
    update_sets(device->device, (uint32_t)binding_count, writes, 0, NULL);
    result->available = true;
    return (size_t)result;
}

/* Validate the complete layout before allocating a pool. Descriptor sets are
 * immutable after publication; callers retire replaced sets behind frame fences. */
size_t encore_vk_buffer_textures_bind_group_create_many(size_t device_handle,
    size_t layout_handle, const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count,
    const uint32_t *texture_bindings, size_t texture_binding_count,
    const size_t *texture_handles, size_t texture_count,
    const size_t *sampler_handles, size_t sampler_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBindGroup *result = calloc(1, sizeof(*result));
    if (result == NULL) return 0;
    result->owner = device;
    result->layout = layout;
    if (device == NULL || !device->available || layout == NULL ||
        !layout->available || layout->owner != device ||
        binding_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES || texture_count == 0 || texture_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count + texture_count > ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count != buffer_count || binding_count != offset_count ||
        binding_count != range_count || texture_binding_count != texture_count ||
        sampler_count != texture_count ||
        layout->binding_count != binding_count + texture_count ||
        (binding_count != 0 && (bindings == NULL || buffer_handles == NULL ||
            offsets == NULL || ranges == NULL)) ||
        texture_bindings == NULL || texture_handles == NULL || sampler_handles == NULL) {
        strncpy(result->error, "Invalid multi-texture bind group resources",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    bool used[ENCORE_VK_MAX_BIND_GROUP_ENTRIES] = {false};
    EncoreVkBuffer *buffers[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    uint32_t descriptor_types[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    uint32_t uniform_count = 0, storage_count = 0;
    for (size_t i = 0; i < binding_count; ++i) {
        buffers[i] = (EncoreVkBuffer *)buffer_handles[i];
        bool found = false;
        for (uint32_t j = 0; j < layout->binding_count; ++j) {
            if (layout->bindings[j] == bindings[i] && !used[j] &&
                (layout->types[j] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 layout->types[j] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) {
                descriptor_types[i] = layout->types[j];
                used[j] = found = true;
                if (layout->types[j] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ++uniform_count;
                else ++storage_count;
                break;
            }
        }
        if (!found || buffers[i] == NULL || !buffers[i]->available ||
            buffers[i]->owner != device || ranges[i] == 0 ||
            offsets[i] > buffers[i]->size ||
            ranges[i] > buffers[i]->size - offsets[i]) {
            strncpy(result->error, "Invalid or duplicate buffer descriptor",
                sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    VkDescriptorImageInfo images[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < texture_count; ++i) {
        EncoreVkTexture *texture = (EncoreVkTexture *)texture_handles[i];
        EncoreVkSampler *sampler = (EncoreVkSampler *)sampler_handles[i];
        bool found = false;
        for (uint32_t j = 0; j < layout->binding_count; ++j) {
            if (layout->bindings[j] == texture_bindings[i] && !used[j] &&
                layout->types[j] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                used[j] = found = true;
                break;
            }
        }
        if (!found || texture == NULL || !texture->available ||
            texture->owner != device ||
            texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            sampler == NULL || !sampler->available || sampler->owner != device) {
            strncpy(result->error, "Invalid or duplicate texture descriptor",
                sizeof(result->error) - 1);
            return (size_t)result;
        }
        images[i] = (VkDescriptorImageInfo){
            sampler->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    PFN_vkCreateDescriptorPool create_pool = (PFN_vkCreateDescriptorPool)
        device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool = (PFN_vkDestroyDescriptorPool)
        device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets = (PFN_vkAllocateDescriptorSets)
        device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets = (PFN_vkUpdateDescriptorSets)
        device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error, "Vulkan descriptor functions unavailable", sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkDescriptorPoolSize pool_sizes[3];
    uint32_t pool_count = 0;
    if (uniform_count) pool_sizes[pool_count++] = (VkDescriptorPoolSize){
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_count};
    if (storage_count) pool_sizes[pool_count++] = (VkDescriptorPoolSize){
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_count};
    pool_sizes[pool_count++] = (VkDescriptorPoolSize){
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)texture_count};
    VkDescriptorPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, pool_count, pool_sizes};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate descriptor pool", sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkDescriptorSetAllocateInfo allocate = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate descriptor set", sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkDescriptorBufferInfo infos[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    VkWriteDescriptorSet writes[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    for (size_t i = 0; i < binding_count; ++i) {
        infos[i] = (VkDescriptorBufferInfo){buffers[i]->buffer, offsets[i], ranges[i]};
        writes[i] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set,
            bindings[i], 0, 1, descriptor_types[i], NULL, &infos[i], NULL};
    }
    for (size_t i = 0; i < texture_count; ++i) {
        writes[binding_count + i] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set,
            texture_bindings[i], 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &images[i], NULL, NULL};
    }
    update_sets(device->device, (uint32_t)(binding_count + texture_count), writes, 0, NULL);
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_buffer_texture_bind_group_create_many(size_t device_handle,
    size_t layout_handle, const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count, uint32_t texture_binding,
    size_t texture_handle, size_t sampler_handle) {
    return encore_vk_buffer_textures_bind_group_create_many(device_handle,
        layout_handle, bindings, binding_count, buffer_handles, buffer_count,
        offsets, offset_count, ranges, range_count,
        &texture_binding, 1, &texture_handle, 1, &sampler_handle, 1);
}

size_t encore_vk_ray_query_bind_group_create(size_t device_handle,
    size_t layout_handle, uint32_t acceleration_binding,
    size_t acceleration_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout =
        (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkAccelerationStructure *acceleration =
        (EncoreVkAccelerationStructure *)acceleration_handle;
    EncoreVkBindGroup *result =
        (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device;
    result->layout = layout;
    bool acceleration_found = false;
    if (layout != NULL) for (uint32_t i = 0; i < layout->binding_count; ++i)
        if (layout->bindings[i] == acceleration_binding &&
            layout->types[i] == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
            acceleration_found = true;
    if (device == NULL || !device->available || layout == NULL ||
        !layout->available || layout->owner != device ||
        acceleration == NULL || !acceleration->available ||
        acceleration->owner != device ||
        acceleration->type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ||
        !acceleration_found || binding_count == 0 || binding_count >= ENCORE_VK_MAX_BIND_GROUP_ENTRIES ||
        binding_count != buffer_count || binding_count != offset_count ||
        binding_count != range_count ||
        layout->binding_count != binding_count + 1 ||
        bindings == NULL || buffer_handles == NULL ||
        offsets == NULL || ranges == NULL) {
        strncpy(result->error, "Invalid ray-query bind group resources",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    EncoreVkBuffer *buffers[ENCORE_VK_MAX_BIND_GROUP_ENTRIES - 1u];
    for (size_t i = 0; i < binding_count; ++i) {
        buffers[i] = (EncoreVkBuffer *)buffer_handles[i];
        bool storage_found = false;
        for (uint32_t j = 0; j < layout->binding_count; ++j)
            if (layout->bindings[j] == bindings[i] &&
                layout->types[j] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                storage_found = true;
        if (!storage_found || buffers[i] == NULL || !buffers[i]->available ||
            buffers[i]->owner != device || ranges[i] == 0 ||
            offsets[i] > buffers[i]->size ||
            ranges[i] > buffers[i]->size - offsets[i]) {
            strncpy(result->error, "Invalid ray-query storage buffer range",
                sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    PFN_vkCreateDescriptorPool create_pool =
        (PFN_vkCreateDescriptorPool)device->get_device_proc(
            device->device, "vkCreateDescriptorPool");
    result->destroy_pool =
        (PFN_vkDestroyDescriptorPool)device->get_device_proc(
            device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets =
        (PFN_vkAllocateDescriptorSets)device->get_device_proc(
            device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)device->get_device_proc(
            device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL)
        return (size_t)result;
    VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)binding_count}
    };
    VkDescriptorPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL, 0, 1, 2, pool_sizes};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS)
        return (size_t)result;
    void *acceleration_object = acceleration->acceleration_structure;
    VkWriteDescriptorSetAccelerationStructureKHR acceleration_info = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        NULL, 1, &acceleration_object
    };
    VkWriteDescriptorSet writes[ENCORE_VK_MAX_BIND_GROUP_ENTRIES];
    memset(writes, 0, sizeof(writes));
    writes[0] = (VkWriteDescriptorSet){
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &acceleration_info,
        result->set, acceleration_binding, 0, 1,
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        NULL, NULL, NULL};
    VkDescriptorBufferInfo infos[ENCORE_VK_MAX_BIND_GROUP_ENTRIES - 1u];
    for (size_t i = 0; i < binding_count; ++i) {
        infos[i] = (VkDescriptorBufferInfo){
            buffers[i]->buffer, offsets[i], ranges[i]};
        writes[i + 1] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
            result->set, bindings[i], 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            NULL, &infos[i], NULL};
    }
    update_sets(device->device, (uint32_t)binding_count + 1,
        writes, 0, NULL);
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_compute_pipeline_create(size_t device_handle, size_t shader_handle, size_t layout_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkShader *shader = (EncoreVkShader *)shader_handle;
    EncoreVkBindGroupLayout *bind_layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkComputePipeline *result = (EncoreVkComputePipeline *)calloc(1, sizeof(EncoreVkComputePipeline));
    if (result == NULL) return 0;
    result->owner = device;
    result->bind_group_layout = bind_layout;
    if (device == NULL || !device->available || shader == NULL || !shader->available || shader->owner != device ||
        shader->stage != VK_SHADER_STAGE_COMPUTE_BIT || bind_layout == NULL || !bind_layout->available || bind_layout->owner != device) {
        strncpy(result->error, "Invalid compute pipeline resources", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreatePipelineLayout create_layout = (PFN_vkCreatePipelineLayout)device->get_device_proc(device->device, "vkCreatePipelineLayout");
    PFN_vkCreateComputePipelines create_pipeline = (PFN_vkCreateComputePipelines)device->get_device_proc(device->device, "vkCreateComputePipelines");
    result->destroy_pipeline_layout = (PFN_vkDestroyPipelineLayout)device->get_device_proc(device->device, "vkDestroyPipelineLayout");
    result->destroy_pipeline = (PFN_vkDestroyPipeline)device->get_device_proc(device->device, "vkDestroyPipeline");
    if (create_layout == NULL || create_pipeline == NULL || result->destroy_pipeline_layout == NULL || result->destroy_pipeline == NULL) {
        strncpy(result->error, "Vulkan compute pipeline functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &bind_layout->layout, 0, NULL};
    if (create_layout(device->device, &layout_info, NULL, &result->layout) != VK_SUCCESS) return (size_t)result;
    VkComputePipelineCreateInfo info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader->module, "main", NULL},
        result->layout, NULL, -1};
    if (create_pipeline(device->device, NULL, 1, &info, NULL, &result->pipeline) != VK_SUCCESS) return (size_t)result;
    result->available = true; return (size_t)result;
}

bool encore_vk_compute_pipeline_available(size_t handle) { EncoreVkComputePipeline *p = (EncoreVkComputePipeline *)handle; return p != NULL && p->available; }
encore_str encore_vk_compute_pipeline_error(size_t handle) { EncoreVkComputePipeline *p = (EncoreVkComputePipeline *)handle; return encore_string(p == NULL ? "Invalid compute pipeline" : p->error); }
bool encore_vk_compute_pipeline_destroy(size_t handle) {
    EncoreVkComputePipeline *pipeline = (EncoreVkComputePipeline *)handle;
    if (pipeline == NULL) return false;
    if (pipeline->owner != NULL && pipeline->owner->device != NULL) {
        if (pipeline->pipeline != NULL && pipeline->destroy_pipeline != NULL) pipeline->destroy_pipeline(pipeline->owner->device, pipeline->pipeline, NULL);
        if (pipeline->layout != NULL && pipeline->destroy_pipeline_layout != NULL) pipeline->destroy_pipeline_layout(pipeline->owner->device, pipeline->layout, NULL);
    }
    free(pipeline); return true;
}

bool encore_vk_bind_group_available(size_t handle) { EncoreVkBindGroup *g = (EncoreVkBindGroup *)handle; return g != NULL && g->available; }
encore_str encore_vk_bind_group_error(size_t handle) { EncoreVkBindGroup *g = (EncoreVkBindGroup *)handle; return encore_string(g == NULL ? "Invalid bind group" : g->error); }
bool encore_vk_bind_group_destroy(size_t handle) {
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)handle;
    if (group == NULL) return false;
    if (group->pool != NULL && group->owner != NULL && group->owner->device != NULL && group->destroy_pool != NULL)
        group->destroy_pool(group->owner->device, group->pool, NULL);
    free(group); return true;
}

size_t encore_vk_texture_bind_group_create(size_t device_handle, size_t layout_handle,
    size_t texture_handle, size_t sampler_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkTexture *texture = (EncoreVkTexture *)texture_handle;
    EncoreVkSampler *sampler = (EncoreVkSampler *)sampler_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device;
    result->layout = layout;
    if (device == NULL || !device->available || layout == NULL || !layout->available ||
        layout->owner != device || layout->binding_count != 1 ||
        layout->types[0] != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
        texture == NULL || !texture->available || texture->owner != device ||
        texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        sampler == NULL || !sampler->available || sampler->owner != device) {
        strncpy(result->error, "Invalid texture bind group resources",
            sizeof(result->error) - 1);
        return (size_t)result;
    }
    PFN_vkCreateDescriptorPool create_pool =
        (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool =
        (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets =
        (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL) return (size_t)result;
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL, 0, 1, 1, &pool_size};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorImageInfo image = {sampler->sampler, texture->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
        result->set, layout->bindings[0], 0, 1,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image, NULL, NULL};
    update_sets(device->device, 1, &write, 0, NULL);
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_sampler_create(size_t device_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkSampler *result = (EncoreVkSampler *)calloc(1, sizeof(EncoreVkSampler));
    if (result == NULL) return 0;
    result->owner = device;
    if (device == NULL || !device->available) {
        strncpy(result->error, "Invalid sampler device", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateSampler create_sampler = (PFN_vkCreateSampler)device->get_device_proc(device->device, "vkCreateSampler");
    result->destroy_sampler = (PFN_vkDestroySampler)device->get_device_proc(device->device, "vkDestroySampler");
    if (create_sampler == NULL || result->destroy_sampler == NULL) {
        strncpy(result->error, "Vulkan sampler functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, 0, 1.0f, 0, 0, 0.0f, 16.0f, 0, 0};
    if (create_sampler(device->device, &info, NULL, &result->sampler) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan sampler", sizeof(result->error) - 1); return (size_t)result;
    }
    result->available = true; return (size_t)result;
}

size_t encore_vk_sampler_create_repeat(size_t device_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkSampler *result = (EncoreVkSampler *)calloc(1, sizeof(EncoreVkSampler));
    if (result == NULL) return 0;
    result->owner = device;
    if (device == NULL || !device->available) {
        strncpy(result->error, "Invalid sampler device", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateSampler create_sampler = (PFN_vkCreateSampler)device->get_device_proc(device->device, "vkCreateSampler");
    result->destroy_sampler = (PFN_vkDestroySampler)device->get_device_proc(device->device, "vkDestroySampler");
    if (create_sampler == NULL || result->destroy_sampler == NULL) return (size_t)result;
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0,
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.0f, 0, 1.0f, 0, 0, 0.0f, 16.0f, 0, 0};
    if (create_sampler(device->device, &info, NULL, &result->sampler) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create repeat sampler", sizeof(result->error) - 1); return (size_t)result;
    }
    result->available = true; return (size_t)result;
}

bool encore_vk_sampler_available(size_t handle) { EncoreVkSampler *s = (EncoreVkSampler *)handle; return s != NULL && s->available; }
encore_str encore_vk_sampler_error(size_t handle) { EncoreVkSampler *s = (EncoreVkSampler *)handle; return encore_string(s == NULL ? "Invalid sampler" : s->error); }
bool encore_vk_sampler_destroy(size_t handle) {
    EncoreVkSampler *sampler = (EncoreVkSampler *)handle;
    if (sampler == NULL) return false;
    if (sampler->sampler != NULL && sampler->owner != NULL && sampler->owner->device != NULL && sampler->destroy_sampler != NULL)
        sampler->destroy_sampler(sampler->owner->device, sampler->sampler, NULL);
    free(sampler); return true;
}

/*
 * Descriptor sets are allocated per frame slot by callers. Updating a buffer
 * binding is safe after that slot's fence has completed and avoids rebuilding
 * every pipeline-owned bind group when a resize-dependent storage buffer
 * changes.
 */
bool encore_vk_bind_group_update_buffer(size_t bind_group_handle,
    uint32_t binding, size_t buffer_handle, uint64_t offset, uint64_t range) {
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)bind_group_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (group == NULL || !group->available || group->owner == NULL ||
        group->layout == NULL || !group->layout->available ||
        buffer == NULL || !buffer->available || buffer->owner != group->owner ||
        range == 0 || offset > buffer->size || range > buffer->size - offset) {
        return false;
    }
    uint32_t descriptor_type = 0;
    bool found = false;
    for (uint32_t i = 0; i < group->layout->binding_count; ++i) {
        if (group->layout->bindings[i] == binding &&
            (group->layout->types[i] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
             group->layout->types[i] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) {
            descriptor_type = group->layout->types[i];
            found = true;
            break;
        }
    }
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)group->owner->get_device_proc(
            group->owner->device, "vkUpdateDescriptorSets");
    if (!found || update_sets == NULL) return false;
    VkDescriptorBufferInfo buffer_info = {buffer->buffer, offset, range};
    VkWriteDescriptorSet write = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, group->set,
        binding, 0, 1, descriptor_type, NULL, &buffer_info, NULL
    };
    update_sets(group->owner->device, 1, &write, 0, NULL);
    return true;
}

size_t encore_vk_material_bind_group_create(size_t device_handle, size_t layout_handle,
    uint32_t uniform_binding, size_t buffer_handle, uint64_t offset, uint64_t range,
    uint32_t texture_binding, size_t texture_handle, size_t sampler_handle) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    EncoreVkTexture *texture = (EncoreVkTexture *)texture_handle;
    EncoreVkSampler *sampler = (EncoreVkSampler *)sampler_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device; result->layout = layout;
    bool uniform_found = false, texture_found = false;
    if (layout != NULL) for (uint32_t i = 0; i < layout->binding_count; ++i) {
        if (layout->bindings[i] == uniform_binding && layout->types[i] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) uniform_found = true;
        if (layout->bindings[i] == texture_binding && layout->types[i] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) texture_found = true;
    }
    if (device == NULL || !device->available || layout == NULL || !layout->available || layout->owner != device ||
        layout->binding_count != 2 || !uniform_found || !texture_found || buffer == NULL || !buffer->available || buffer->owner != device ||
        texture == NULL || !texture->available || texture->owner != device || texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        sampler == NULL || !sampler->available || sampler->owner != device || range == 0 || offset > buffer->size || range > buffer->size - offset) {
        strncpy(result->error, "Invalid material bind group resources", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateDescriptorPool create_pool = (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool = (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets = (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets = (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL || allocate_sets == NULL || update_sets == NULL) {
        strncpy(result->error, "Vulkan material descriptor functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 2, pool_sizes};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create material descriptor pool", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate material descriptor set", sizeof(result->error) - 1); return (size_t)result;
    }
    VkDescriptorBufferInfo buffer_info = {buffer->buffer, offset, range};
    VkDescriptorImageInfo image_info = {sampler->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, uniform_binding, 0, 1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &buffer_info, NULL},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, texture_binding, 0, 1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image_info, NULL, NULL}
    };
    update_sets(device->device, 2, writes, 0, NULL);
    result->available = true; return (size_t)result;
}

size_t encore_vk_scene_bind_group_create(size_t device_handle, size_t layout_handle,
    uint32_t uniform_binding, size_t uniform_handle, uint64_t uniform_range,
    uint32_t storage_binding, size_t storage_handle, uint64_t storage_range) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkBindGroupLayout *layout = (EncoreVkBindGroupLayout *)layout_handle;
    EncoreVkBuffer *uniform = (EncoreVkBuffer *)uniform_handle;
    EncoreVkBuffer *storage = (EncoreVkBuffer *)storage_handle;
    EncoreVkBindGroup *result = (EncoreVkBindGroup *)calloc(1, sizeof(EncoreVkBindGroup));
    if (result == NULL) return 0;
    result->owner = device; result->layout = layout;
    bool uniform_found = false, storage_found = false;
    if (layout != NULL) for (uint32_t i = 0; i < layout->binding_count; ++i) {
        if (layout->bindings[i] == uniform_binding &&
            layout->types[i] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) uniform_found = true;
        if (layout->bindings[i] == storage_binding &&
            layout->types[i] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) storage_found = true;
    }
    if (device == NULL || !device->available || layout == NULL || !layout->available ||
        layout->owner != device || layout->binding_count != 2 || !uniform_found || !storage_found ||
        uniform == NULL || !uniform->available || uniform->owner != device ||
        storage == NULL || !storage->available || storage->owner != device ||
        uniform_range == 0 || uniform_range > uniform->size ||
        storage_range == 0 || storage_range > storage->size) {
        strncpy(result->error, "Invalid scene bind group resources", sizeof(result->error) - 1);
        return (size_t)result;
    }
    PFN_vkCreateDescriptorPool create_pool =
        (PFN_vkCreateDescriptorPool)device->get_device_proc(device->device, "vkCreateDescriptorPool");
    result->destroy_pool =
        (PFN_vkDestroyDescriptorPool)device->get_device_proc(device->device, "vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets allocate_sets =
        (PFN_vkAllocateDescriptorSets)device->get_device_proc(device->device, "vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets update_sets =
        (PFN_vkUpdateDescriptorSets)device->get_device_proc(device->device, "vkUpdateDescriptorSets");
    if (create_pool == NULL || result->destroy_pool == NULL ||
        allocate_sets == NULL || update_sets == NULL) return (size_t)result;
    VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL, 0, 1, 2, pool_sizes};
    if (create_pool(device->device, &pool_info, NULL, &result->pool) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL, result->pool, 1, &layout->layout};
    if (allocate_sets(device->device, &allocate, &result->set) != VK_SUCCESS)
        return (size_t)result;
    VkDescriptorBufferInfo uniform_info = {uniform->buffer, 0, uniform_range};
    VkDescriptorBufferInfo storage_info = {storage->buffer, 0, storage_range};
    VkWriteDescriptorSet writes[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, uniform_binding, 0, 1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &uniform_info, NULL},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, result->set, storage_binding, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &storage_info, NULL}
    };
    update_sets(device->device, 2, writes, 0, NULL);
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_pipeline_create(size_t device_handle, size_t swapchain_handle, size_t vertex_handle, size_t fragment_handle,
    size_t bind_group_layout_handle, bool requested_depth_attachment,
    bool depth_test, bool depth_write, uint32_t cull_mode, bool alpha_blend,
    bool additive_blend, bool load_color,
    uint32_t vertex_stride,
    const uint32_t *locations, size_t location_count,
    const uint32_t *formats, size_t format_count,
    const uint32_t *offsets, size_t offset_count) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)swapchain_handle;
    EncoreVkShader *vertex = (EncoreVkShader *)vertex_handle;
    EncoreVkShader *fragment = (EncoreVkShader *)fragment_handle;
    EncoreVkBindGroupLayout *bind_group_layout = (EncoreVkBindGroupLayout *)bind_group_layout_handle;
    EncoreVkPipeline *result = (EncoreVkPipeline *)calloc(1, sizeof(EncoreVkPipeline));
    if (result == NULL) return 0;
    result->owner = device;
    result->depth_test = depth_test;
    if (device == NULL || !device->available || swapchain == NULL || !swapchain->available ||
        swapchain->owner != device || vertex == NULL || fragment == NULL || !vertex->available || !fragment->available ||
        vertex->owner != device || fragment->owner != device ||
        (vertex->stage != VK_SHADER_STAGE_VERTEX_BIT && vertex->stage != VK_SHADER_STAGE_MESH_BIT_EXT) ||
        (vertex->stage == VK_SHADER_STAGE_MESH_BIT_EXT &&
            (device->enabled_features & ENCORE_GPU_FEATURE_MESH_SHADER) == 0) ||
        fragment->stage != VK_SHADER_STAGE_FRAGMENT_BIT ||
        (bind_group_layout != NULL && (!bind_group_layout->available || bind_group_layout->owner != device)) ||
        location_count != format_count ||
        location_count != offset_count || location_count > 16 || (location_count > 0 && vertex_stride == 0) ||
        (cull_mode != VK_CULL_MODE_NONE && cull_mode != VK_CULL_MODE_FRONT_BIT && cull_mode != VK_CULL_MODE_BACK_BIT)) {
        strncpy(result->error, "Invalid render pipeline resources", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkCreateRenderPass create_render_pass = (PFN_vkCreateRenderPass)device->get_device_proc(device->device, "vkCreateRenderPass");
    PFN_vkCreatePipelineLayout create_layout = (PFN_vkCreatePipelineLayout)device->get_device_proc(device->device, "vkCreatePipelineLayout");
    PFN_vkCreateGraphicsPipelines create_pipeline = (PFN_vkCreateGraphicsPipelines)device->get_device_proc(device->device, "vkCreateGraphicsPipelines");
    result->destroy_render_pass = (PFN_vkDestroyRenderPass)device->get_device_proc(device->device, "vkDestroyRenderPass");
    result->destroy_pipeline_layout = (PFN_vkDestroyPipelineLayout)device->get_device_proc(device->device, "vkDestroyPipelineLayout");
    result->destroy_pipeline = (PFN_vkDestroyPipeline)device->get_device_proc(device->device, "vkDestroyPipeline");
    if (create_render_pass == NULL || create_layout == NULL || create_pipeline == NULL ||
        result->destroy_render_pass == NULL || result->destroy_pipeline_layout == NULL || result->destroy_pipeline == NULL) {
        strncpy(result->error, "Required Vulkan pipeline functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    uint32_t samples = swapchain->samples ? swapchain->samples : 1;
    bool depth_attachment = requested_depth_attachment || depth_test || depth_write || !swapchain->offscreen || samples > 1 || swapchain->auxiliary_format;
    VkAttachmentDescription attachments[3] = {
        {0, swapchain->format, samples, load_color ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            samples > 1 ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            load_color ? (swapchain->offscreen ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) : VK_IMAGE_LAYOUT_UNDEFINED,
            swapchain->offscreen ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {0,126,samples,VK_ATTACHMENT_LOAD_OP_CLEAR, swapchain->sampled_depth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        {0,swapchain->auxiliary_format ? swapchain->auxiliary_format : swapchain->format,1,
            VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference colors[2] = {{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depth_reference = {1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolve = {2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    uint32_t color_count = swapchain->auxiliary_format ? 2u : 1u;
    VkSubpassDescription subpass = {0,VK_PIPELINE_BIND_POINT_GRAPHICS,0,NULL,color_count,colors,
        samples > 1 ? &resolve : NULL,depth_attachment ? &depth_reference : NULL,0,NULL};
    VkSubpassDependency dependency = {VK_SUBPASS_EXTERNAL,0,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_SHADER_READ_BIT | (load_color ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0),
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,0};
    VkRenderPassCreateInfo render_pass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,NULL,0,
        (samples > 1 || swapchain->auxiliary_format) ? 3u : (depth_attachment ? 2u : 1u),attachments,1,&subpass,1,&dependency};
    if (create_render_pass(device->device, &render_pass_info, NULL, &result->render_pass) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create render pipeline pass", sizeof(result->error) - 1); return (size_t)result;
    }
    void *descriptor_layout = bind_group_layout == NULL ? NULL : bind_group_layout->layout;
    /* Vulkan guarantees at least 128 bytes. A common graphics range keeps
       pipeline switches compatible without coupling this API to a UI. */
    VkPushConstantRange constants = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0,
        bind_group_layout == NULL ? 0 : 1, bind_group_layout == NULL ? NULL : &descriptor_layout, 1, &constants};
    if (create_layout(device->device, &layout_info, NULL, &result->layout) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create render pipeline layout", sizeof(result->error) - 1); return (size_t)result;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, vertex->stage, vertex->module, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment->module, "main", NULL}
    };
    VkVertexInputBindingDescription binding = {0, vertex_stride, 0};
    VkVertexInputAttributeDescription attributes[16];
    for (size_t i = 0; i < location_count; ++i) {
        attributes[i].location = locations[i]; attributes[i].binding = 0;
        attributes[i].format = formats[i]; attributes[i].offset = offsets[i];
    }
    VkPipelineVertexInputStateCreateInfo vertex_input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0,
        location_count == 0 ? 0 : 1, location_count == 0 ? NULL : &binding,
        (uint32_t)location_count, location_count == 0 ? NULL : attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0};
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0,
        0, 0, VK_POLYGON_MODE_FILL, cull_mode, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0.0f, 0.0f, 0.0f, 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0,
        samples, 0, 0.0f, NULL, 0, 0};
    VkPipelineDepthStencilStateCreateInfo depth = {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0,
        depth_test ? 1u : 0u, depth_write ? 1u : 0u, VK_COMPARE_OP_LESS,
        0, 0, {0}, {0}, 0.0f, 1.0f
    };
    bool blend_enabled = alpha_blend || additive_blend;
    VkPipelineColorBlendAttachmentState blend_attachment = {blend_enabled ? 1u : 0u,
        alpha_blend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE,
        additive_blend ? VK_BLEND_FACTOR_ONE :
            (alpha_blend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO),
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, additive_blend ? VK_BLEND_FACTOR_ONE :
            (alpha_blend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO),
        VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_RGBA_BITS};
    VkPipelineColorBlendAttachmentState blend_attachments[2] = {blend_attachment, blend_attachment};
    /* Auxiliary attachments store data, such as motion/depth. Preserve their values
       instead of blending by the validity/depth component in alpha. */
    if (color_count > 1 && device->independent_blend)
        blend_attachments[1].blendEnable = 0;
    /* Unknown borrowed devices retain identical portable attachment states. */
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0,
        0, 0, color_count, blend_attachments, {0.0f, 0.0f, 0.0f, 0.0f}};
    uint32_t dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dynamic_states};
    bool mesh_pipeline = vertex->stage == VK_SHADER_STAGE_MESH_BIT_EXT;
    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0,
        2, stages, mesh_pipeline ? NULL : &vertex_input, mesh_pipeline ? NULL : &assembly, NULL, &viewport, &raster, &multisample,
        depth_attachment ? &depth : NULL, &blend, &dynamic,
        result->layout, result->render_pass, 0, NULL, -1};
    VkResult status = create_pipeline(device->device, NULL, 1, &pipeline_info, NULL, &result->pipeline);
    if (status != VK_SUCCESS) {
        snprintf(result->error, sizeof(result->error), "Unable to create Vulkan render pipeline (result %d)", status);
        return (size_t)result;
    }
    result->format = swapchain->format;
    result->samples = samples;
    result->auxiliary_format = swapchain->auxiliary_format;
    result->depth_attachment = depth_attachment;
    result->available = true;
    return (size_t)result;
}

bool encore_vk_pipeline_available(size_t handle) { EncoreVkPipeline *p = (EncoreVkPipeline *)handle; return p != NULL && p->available; }
encore_str encore_vk_pipeline_error(size_t handle) { EncoreVkPipeline *p = (EncoreVkPipeline *)handle; return encore_string(p == NULL ? "Invalid render pipeline" : p->error); }
bool encore_vk_pipeline_destroy(size_t handle) {
    EncoreVkPipeline *pipeline = (EncoreVkPipeline *)handle;
    if (pipeline == NULL) return false;
    if (pipeline->owner != NULL && pipeline->owner->device != NULL) {
        if (pipeline->pipeline != NULL && pipeline->destroy_pipeline != NULL) pipeline->destroy_pipeline(pipeline->owner->device, pipeline->pipeline, NULL);
        if (pipeline->layout != NULL && pipeline->destroy_pipeline_layout != NULL) pipeline->destroy_pipeline_layout(pipeline->owner->device, pipeline->layout, NULL);
        if (pipeline->render_pass != NULL && pipeline->destroy_render_pass != NULL) pipeline->destroy_render_pass(pipeline->owner->device, pipeline->render_pass, NULL);
    }
    free(pipeline); return true;
}

bool encore_vk_device_independent_blend(size_t handle) {EncoreVkDevice *d=(EncoreVkDevice*)handle;return d&&d->available&&d->independent_blend;}
