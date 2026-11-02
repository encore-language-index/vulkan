/* Formats and multisampling are backend capabilities, not engine policy. */
uint32_t encore_vk_target_sample_counts(size_t handle, uint32_t format) {
    EncoreVkDevice *d = (EncoreVkDevice *)handle;
    if (!d || !d->available) return 0;
    EncoreGetImageFormatProperties query = (EncoreGetImageFormatProperties)d->context->get_proc(d->context->instance, "vkGetPhysicalDeviceImageFormatProperties");
    EncoreVkImageFormatProperties color = {0}, depth = {0};
    if (!query || query(d->physical_device, format, 1, 0, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &color) != VK_SUCCESS ||
        query(d->physical_device, 126, 1, 0, 32, 0, &depth) != VK_SUCCESS) return 0;
    return color.sampleCounts & depth.sampleCounts;
}
static bool encore_vk_target_image(EncoreVkSwapchain *s, uint32_t format, uint32_t samples, uint32_t usage,
    void **image, void **memory, void **view) {
    EncoreVkDevice *d = s->owner;
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, 1, format,
        {s->width, s->height, 1}, 1, 1, samples, 0, usage, 0, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (d->create_image(d->device, &info, NULL, image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    d->get_image_requirements(d->device, *image, &req);
    uint32_t type = encore_memory_type(d, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return false;
    s->attachment_bytes += req.size;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    if (d->allocate_memory(d->device, &alloc, NULL, memory) != VK_SUCCESS || d->bind_image_memory(d->device, *image, *memory, 0) != VK_SUCCESS) return false;
    PFN_vkCreateImageView create = (PFN_vkCreateImageView)d->get_device_proc(d->device, "vkCreateImageView");
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, *image, 1, format,
        {0,0,0,0}, {format == 126 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1}};
    return create && create(d->device, &vi, NULL, view) == VK_SUCCESS;
}
/* Swapchain lifecycle, explicit frame recording and presentation. */
static bool encore_vk_swapchain_create_render_support(EncoreVkSwapchain *swapchain) {
    EncoreVkDevice *device = swapchain->owner;
    swapchain->cmd_clear_color_image = (PFN_vkCmdClearColorImage)device->get_device_proc(device->device, "vkCmdClearColorImage");
    if (!swapchain->cmd_clear_color_image) {
        strncpy(swapchain->error, "vkCmdClearColorImage was not found", sizeof(swapchain->error)-1);
        return false;
    }
    PFN_vkCreateFramebuffer create_framebuffer = (PFN_vkCreateFramebuffer)device->get_device_proc(device->device, "vkCreateFramebuffer");
    PFN_vkCreateImageView create_image_view = (PFN_vkCreateImageView)device->get_device_proc(device->device, "vkCreateImageView");
    swapchain->destroy_image_view = (PFN_vkDestroyImageView)device->get_device_proc(device->device, "vkDestroyImageView");
    swapchain->destroy_framebuffer = (PFN_vkDestroyFramebuffer)device->get_device_proc(device->device, "vkDestroyFramebuffer");
    swapchain->cmd_begin_render_pass = (PFN_vkCmdBeginRenderPass)device->get_device_proc(device->device, "vkCmdBeginRenderPass");
    swapchain->cmd_end_render_pass = (PFN_vkCmdEndRenderPass)device->get_device_proc(device->device, "vkCmdEndRenderPass");
    swapchain->cmd_bind_pipeline = (PFN_vkCmdBindPipeline)device->get_device_proc(device->device, "vkCmdBindPipeline");
    swapchain->cmd_draw = (PFN_vkCmdDraw)device->get_device_proc(device->device, "vkCmdDraw");
    swapchain->cmd_set_viewport = (PFN_vkCmdSetViewport)device->get_device_proc(device->device, "vkCmdSetViewport");
    swapchain->cmd_set_scissor = (PFN_vkCmdSetScissor)device->get_device_proc(device->device, "vkCmdSetScissor");
    swapchain->cmd_bind_vertex_buffers = (PFN_vkCmdBindVertexBuffers)device->get_device_proc(device->device, "vkCmdBindVertexBuffers");
    swapchain->cmd_bind_index_buffer = (PFN_vkCmdBindIndexBuffer)device->get_device_proc(device->device, "vkCmdBindIndexBuffer");
    swapchain->cmd_draw_indexed = (PFN_vkCmdDrawIndexed)device->get_device_proc(device->device, "vkCmdDrawIndexed");
    swapchain->cmd_draw_indexed_indirect_count = (PFN_vkCmdDrawIndexedIndirectCount)
        device->get_device_proc(device->device, "vkCmdDrawIndexedIndirectCount");
    if (swapchain->cmd_draw_indexed_indirect_count == NULL)
        swapchain->cmd_draw_indexed_indirect_count = (PFN_vkCmdDrawIndexedIndirectCount)
            device->get_device_proc(device->device, "vkCmdDrawIndexedIndirectCountKHR");
    swapchain->cmd_draw_mesh_tasks = (PFN_vkCmdDrawMeshTasksEXT)
        device->get_device_proc(device->device, "vkCmdDrawMeshTasksEXT");
    swapchain->cmd_draw_mesh_tasks_indirect =
        (PFN_vkCmdDrawMeshTasksIndirectEXT)
        device->get_device_proc(device->device,
            "vkCmdDrawMeshTasksIndirectEXT");
    swapchain->cmd_bind_descriptor_sets = (PFN_vkCmdBindDescriptorSets)device->get_device_proc(device->device, "vkCmdBindDescriptorSets");
    swapchain->create_framebuffer = create_framebuffer;
    if (create_framebuffer == NULL || create_image_view == NULL || swapchain->destroy_image_view == NULL ||
        swapchain->destroy_framebuffer == NULL || swapchain->cmd_begin_render_pass == NULL ||
        swapchain->cmd_end_render_pass == NULL || swapchain->cmd_bind_pipeline == NULL || swapchain->cmd_draw == NULL ||
        swapchain->cmd_set_viewport == NULL || swapchain->cmd_set_scissor == NULL ||
        swapchain->cmd_bind_vertex_buffers == NULL || swapchain->cmd_bind_index_buffer == NULL ||
        swapchain->cmd_draw_indexed == NULL || swapchain->cmd_bind_descriptor_sets == NULL) {
        strncpy(swapchain->error, "Required Vulkan graphics pipeline functions were not found", sizeof(swapchain->error) - 1); return false;
    }
    swapchain->views = (void **)calloc(swapchain->image_count, sizeof(void *));
    swapchain->depth_images = (void **)calloc(swapchain->image_count, sizeof(void *));
    swapchain->depth_views = (void **)calloc(swapchain->image_count, sizeof(void *));
    swapchain->depth_memories = (void **)calloc(swapchain->image_count, sizeof(void *));
    swapchain->framebuffers = (void **)calloc(swapchain->image_count, sizeof(void *));
    swapchain->framebuffer_sets[0] = swapchain->framebuffers;
    if (swapchain->views == NULL || swapchain->depth_images == NULL || swapchain->depth_views == NULL ||
        swapchain->depth_memories == NULL || swapchain->framebuffers == NULL) {
        strncpy(swapchain->error, "Unable to allocate swapchain render resources", sizeof(swapchain->error) - 1); return false;
    }
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, swapchain->images[i], 1,
            swapchain->format, {0, 0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        if (create_image_view(device->device, &view, NULL, &swapchain->views[i]) != VK_SUCCESS) {
            strncpy(swapchain->error, "Unable to create a swapchain image view", sizeof(swapchain->error) - 1); return false;
        }
        VkImageCreateInfo depth_image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, 1, 126,
            {swapchain->width, swapchain->height, 1}, 1, 1, swapchain->samples ? swapchain->samples : 1, 0,
            32 | (swapchain->sampled_depth ? (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) : 0), 0, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
        if (device->create_image(device->device, &depth_image, NULL, &swapchain->depth_images[i]) != VK_SUCCESS) {
            strncpy(swapchain->error, "Unable to create swapchain depth image", sizeof(swapchain->error) - 1); return false;
        }
        VkMemoryRequirements depth_requirements;
        device->get_image_requirements(device->device, swapchain->depth_images[i], &depth_requirements);
        uint32_t depth_memory_type = encore_memory_type(device, depth_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (depth_memory_type == UINT32_MAX) depth_memory_type = encore_memory_type(device, depth_requirements.memoryTypeBits, 0);
        if (depth_memory_type == UINT32_MAX) {
            strncpy(swapchain->error, "No compatible depth image memory", sizeof(swapchain->error) - 1); return false;
        }
        swapchain->attachment_bytes += depth_requirements.size;
        VkMemoryAllocateInfo depth_allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL,
            depth_requirements.size, depth_memory_type};
        if (device->allocate_memory(device->device, &depth_allocation, NULL, &swapchain->depth_memories[i]) != VK_SUCCESS ||
            device->bind_image_memory(device->device, swapchain->depth_images[i], swapchain->depth_memories[i], 0) != VK_SUCCESS) {
            strncpy(swapchain->error, "Unable to allocate swapchain depth image", sizeof(swapchain->error) - 1); return false;
        }
        VkImageViewCreateInfo depth_view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, swapchain->depth_images[i], 1,
            126, {0, 0, 0, 0}, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
        if (create_image_view(device->device, &depth_view, NULL, &swapchain->depth_views[i]) != VK_SUCCESS) {
            strncpy(swapchain->error, "Unable to create swapchain depth view", sizeof(swapchain->error) - 1); return false;
        }
    }
    if (swapchain->samples > 1 || swapchain->auxiliary_format) {
        void ***images = swapchain->samples > 1 ? &swapchain->multisample_images : &swapchain->auxiliary_images;
        void ***views = swapchain->samples > 1 ? &swapchain->multisample_views : &swapchain->auxiliary_views;
        void ***memories = swapchain->samples > 1 ? &swapchain->multisample_memories : &swapchain->auxiliary_memories;
        *images = calloc(swapchain->image_count, sizeof(void *));
        *views = calloc(swapchain->image_count, sizeof(void *));
        *memories = calloc(swapchain->image_count, sizeof(void *));
        if (!*images || !*views || !*memories) return false;
        for (uint32_t i = 0; i < swapchain->image_count; ++i) {
            if (!encore_vk_target_image(swapchain, swapchain->samples > 1 ? swapchain->format : swapchain->auxiliary_format,
                    swapchain->samples > 1 ? swapchain->samples : 1,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (swapchain->samples > 1 ? 0 : (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)),
                    &(*images)[i], &(*memories)[i], &(*views)[i])) {
                strncpy(swapchain->error, "Unable to allocate additional target attachments", sizeof(swapchain->error)-1);
                return false;
            }
        }
    }
    return true;
}

static size_t encore_vk_swapchain_create_internal(size_t device_handle, size_t window_handle,
    uint64_t old_swapchain, uint32_t requested_present_mode) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkSurface *window = (EncoreVkSurface *)window_handle;
    EncoreVkSwapchain *result = (EncoreVkSwapchain *)calloc(1, sizeof(EncoreVkSwapchain));
    if (result == NULL) return 0;
    result->owner = device;
    result->window = window;
    result->requested_present_mode = requested_present_mode;
    if (device == NULL || !device->available || !device->present || window == NULL ||
        window->surface == 0 || window->owner != device->context) {
        strncpy(result->error, "Invalid presentation device or window", sizeof(result->error) - 1); return (size_t)result;
    }
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_capabilities =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)device->context->get_proc(device->context->instance,
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_formats =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)device->context->get_proc(device->context->instance,
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_present_modes =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)device->context->get_proc(
            device->context->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    PFN_vkCreateSwapchainKHR create_swapchain = (PFN_vkCreateSwapchainKHR)
        device->get_device_proc(device->device, "vkCreateSwapchainKHR");
    PFN_vkGetSwapchainImagesKHR get_images = (PFN_vkGetSwapchainImagesKHR)
        device->get_device_proc(device->device, "vkGetSwapchainImagesKHR");
    PFN_vkCreateSemaphore create_semaphore = (PFN_vkCreateSemaphore)
        device->get_device_proc(device->device, "vkCreateSemaphore");
    result->destroy_swapchain = (PFN_vkDestroySwapchainKHR)device->get_device_proc(device->device, "vkDestroySwapchainKHR");
    result->acquire_next_image = (PFN_vkAcquireNextImageKHR)device->get_device_proc(device->device, "vkAcquireNextImageKHR");
    result->queue_present = (PFN_vkQueuePresentKHR)device->get_device_proc(device->device, "vkQueuePresentKHR");
    result->destroy_semaphore = (PFN_vkDestroySemaphore)device->get_device_proc(device->device, "vkDestroySemaphore");
    result->reset_fences = (PFN_vkResetFences)device->get_device_proc(device->device, "vkResetFences");
    result->cmd_clear_color_image = (PFN_vkCmdClearColorImage)device->get_device_proc(device->device, "vkCmdClearColorImage");
    if (get_capabilities == NULL || get_formats == NULL || get_present_modes == NULL ||
        create_swapchain == NULL || get_images == NULL ||
        create_semaphore == NULL || result->destroy_swapchain == NULL || result->acquire_next_image == NULL ||
        result->queue_present == NULL || result->destroy_semaphore == NULL || result->reset_fences == NULL ||
        result->cmd_clear_color_image == NULL) {
        strncpy(result->error, "Required Vulkan swapchain functions were not found", sizeof(result->error) - 1); return (size_t)result;
    }
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t format_count = 0;
    if (get_capabilities(device->physical_device, window->surface, &capabilities) != VK_SUCCESS ||
        get_formats(device->physical_device, window->surface, &format_count, NULL) != VK_SUCCESS || format_count == 0) {
        strncpy(result->error, "Unable to query Vulkan surface", sizeof(result->error) - 1); return (size_t)result;
    }
    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)calloc(format_count, sizeof(VkSurfaceFormatKHR));
    if (formats == NULL || get_formats(device->physical_device, window->surface, &format_count, formats) != VK_SUCCESS) {
        free(formats); strncpy(result->error, "Unable to query Vulkan surface formats", sizeof(result->error) - 1); return (size_t)result;
    }
    VkSurfaceFormatKHR chosen = formats[0];
    bool found_unorm = false;
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = formats[i];
            found_unorm = true;
            break;
        }
    }
    if (!found_unorm) {
        for (uint32_t i = 0; i < format_count; ++i) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
                chosen = formats[i];
                break;
            }
        }
    }
    free(formats);
    uint32_t present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (requested_present_mode != VK_PRESENT_MODE_FIFO_KHR) {
        uint32_t present_mode_count = 0;
        if (get_present_modes(device->physical_device, window->surface,
                &present_mode_count, NULL) == VK_SUCCESS && present_mode_count > 0) {
            uint32_t *present_modes = (uint32_t *)calloc(present_mode_count, sizeof(uint32_t));
            if (present_modes != NULL &&
                get_present_modes(device->physical_device, window->surface,
                    &present_mode_count, present_modes) == VK_SUCCESS) {
                bool mailbox_available = false;
                for (uint32_t i = 0; i < present_mode_count; ++i) {
                    if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                        present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                        break;
                    }
                    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                        mailbox_available = true;
                }
                if (present_mode != VK_PRESENT_MODE_IMMEDIATE_KHR && mailbox_available)
                    present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
            free(present_modes);
        }
    }
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = window->width < capabilities.minImageExtent.width ? capabilities.minImageExtent.width : window->width;
        if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
        extent.height = window->height < capabilities.minImageExtent.height ? capabilities.minImageExtent.height : window->height;
        if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
    }
    if (encore_vk_trace_enabled()) {
        fprintf(stderr, "encore-vk: window=%ux%u current=%ux%u min=%ux%u max=%ux%u selected=%ux%u images=%u..%u\n",
            window->width, window->height, capabilities.currentExtent.width, capabilities.currentExtent.height,
            capabilities.minImageExtent.width, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.width, capabilities.maxImageExtent.height,
            extent.width, extent.height, capabilities.minImageCount, capabilities.maxImageCount);
    }
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount) image_count = capabilities.maxImageCount;
    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, window->surface,
        image_count, chosen.format, chosen.colorSpace, extent, 1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        0, 0, NULL,
        capabilities.currentTransform, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, present_mode, 1, old_swapchain};
    if (create_swapchain(device->device, &info, NULL, &result->swapchain) != VK_SUCCESS) {
        strncpy(result->error, "Unable to create Vulkan swapchain", sizeof(result->error) - 1); return (size_t)result;
    }
    if (get_images(device->device, result->swapchain, &result->image_count, NULL) != VK_SUCCESS || result->image_count == 0) {
        strncpy(result->error, "Unable to enumerate Vulkan swapchain images", sizeof(result->error) - 1); return (size_t)result;
    }
    result->images = (void **)calloc(result->image_count, sizeof(void *));
    if (result->images == NULL || get_images(device->device, result->swapchain, &result->image_count, result->images) != VK_SUCCESS) {
        strncpy(result->error, "Unable to store Vulkan swapchain images", sizeof(result->error) - 1); return (size_t)result;
    }
    VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
    VkCommandBufferAllocateInfo allocation = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, device->command_pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
    if (device->allocate_command_buffers(device->device, &allocation, result->command_buffers) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate Vulkan frame command buffers", sizeof(result->error) - 1); return (size_t)result;
    }
    for (uint32_t frame = 0; frame < 2; ++frame) {
        if (create_semaphore(device->device, &semaphore_info, NULL, &result->image_available[frame]) != VK_SUCCESS ||
            create_semaphore(device->device, &semaphore_info, NULL, &result->render_finished[frame]) != VK_SUCCESS ||
            device->create_fence(device->device, &fence_info, NULL,
                &result->fences[frame]) != VK_SUCCESS ||
            device->create_fence(device->device, &fence_info, NULL,
                &result->present_fences[frame]) != VK_SUCCESS) {
            strncpy(result->error, "Unable to create Vulkan frame synchronization", sizeof(result->error) - 1); return (size_t)result;
        }
    }
    result->format = chosen.format; result->width = extent.width; result->height = extent.height;
    if (!encore_vk_swapchain_create_render_support(result)) return (size_t)result;
    result->available = true;
    return (size_t)result;
}

size_t encore_vk_swapchain_create(size_t device_handle, size_t window_handle) {
    return encore_vk_swapchain_create_internal(
        device_handle, window_handle, 0, VK_PRESENT_MODE_FIFO_KHR);
}

static size_t encore_vk_offscreen_create_internal(size_t device_handle, uint32_t width,
    uint32_t height, bool shared, uint32_t format, uint32_t samples, uint32_t auxiliary, bool sampled_depth) {
    EncoreVkDevice *device = (EncoreVkDevice *)device_handle;
    EncoreVkSwapchain *result = (EncoreVkSwapchain *)calloc(1, sizeof(EncoreVkSwapchain));
    if (result == NULL) return 0;
    result->owner = device;
    result->offscreen = true;
    result->shared = shared;
    result->image_count = 2;
    result->format = format;
    result->samples = samples;
    result->auxiliary_format = auxiliary;
    result->sampled_depth = sampled_depth;
    result->width = width;
    result->height = height;
    if (device == NULL || !device->available || (device->queue_flags & VK_QUEUE_GRAPHICS_BIT) == 0 ||
        width == 0 || height == 0 || (samples != 1 && samples != 2 && samples != 4) ||
        (samples > 1 && (auxiliary || sampled_depth)) ||
        !(encore_vk_target_sample_counts(device_handle, format) & samples)) {
        strncpy(result->error, "Invalid offscreen target device or dimensions", sizeof(result->error) - 1);
        return (size_t)result;
    }
    result->reset_fences = (PFN_vkResetFences)device->get_device_proc(device->device, "vkResetFences");
    if (result->reset_fences == NULL) {
        strncpy(result->error, "Required offscreen synchronization function was not found", sizeof(result->error) - 1);
        return (size_t)result;
    }
    result->images = (void **)calloc(result->image_count, sizeof(void *));
    result->color_memories = (void **)calloc(result->image_count, sizeof(void *));
    if (result->images == NULL || result->color_memories == NULL) {
        strncpy(result->error, "Unable to allocate offscreen color resources", sizeof(result->error) - 1);
        return (size_t)result;
    }
    for (uint32_t i = 0; i < result->image_count; ++i) {
        VkImageCreateInfo color_image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, 1, result->format,
            {width, height, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, 0,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                (shared ? VK_IMAGE_USAGE_SAMPLED_BIT : VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
            0, 0, NULL,
            VK_IMAGE_LAYOUT_UNDEFINED};
        if (device->create_image(device->device, &color_image, NULL, &result->images[i]) != VK_SUCCESS) {
            strncpy(result->error, "Unable to create offscreen color image", sizeof(result->error) - 1);
            return (size_t)result;
        }
        VkMemoryRequirements requirements;
        device->get_image_requirements(device->device, result->images[i], &requirements);
        uint32_t memory_type = encore_memory_type(device, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == UINT32_MAX) memory_type = encore_memory_type(device, requirements.memoryTypeBits, 0);
        if (memory_type == UINT32_MAX) {
            strncpy(result->error, "No compatible offscreen color memory", sizeof(result->error) - 1);
            return (size_t)result;
        }
        result->attachment_bytes += requirements.size;
        VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL,
            requirements.size, memory_type};
        if (device->allocate_memory(device->device, &allocation, NULL, &result->color_memories[i]) != VK_SUCCESS ||
            device->bind_image_memory(device->device, result->images[i], result->color_memories[i], 0) != VK_SUCCESS) {
            strncpy(result->error, "Unable to allocate offscreen color image", sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    VkCommandBufferAllocateInfo command_allocation = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
        device->command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
    if (device->allocate_command_buffers(device->device, &command_allocation, result->command_buffers) != VK_SUCCESS) {
        strncpy(result->error, "Unable to allocate offscreen command buffers", sizeof(result->error) - 1);
        return (size_t)result;
    }
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
    for (uint32_t frame = 0; frame < 2; ++frame) {
        if (device->create_fence(device->device, &fence_info, NULL, &result->fences[frame]) != VK_SUCCESS) {
            strncpy(result->error, "Unable to create offscreen frame fence", sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    if (!shared) {
        result->readback_pitch = width * 4;
        VkBufferCreateInfo readback_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
            (uint64_t)result->readback_pitch * height, 2, 0, 0, NULL};
        if (device->create_buffer(device->device, &readback_info, NULL, &result->readback_buffer) != VK_SUCCESS) {
            strncpy(result->error, "Unable to create offscreen readback buffer", sizeof(result->error) - 1);
            return (size_t)result;
        }
        VkMemoryRequirements readback_requirements;
        device->get_buffer_requirements(device->device, result->readback_buffer, &readback_requirements);
        uint32_t readback_type = encore_memory_type(device, readback_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (readback_type == UINT32_MAX) {
            strncpy(result->error, "No host-visible offscreen readback memory", sizeof(result->error) - 1);
            return (size_t)result;
        }
        VkMemoryAllocateInfo readback_allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL,
            readback_requirements.size, readback_type};
        if (device->allocate_memory(device->device, &readback_allocation, NULL, &result->readback_memory) != VK_SUCCESS ||
            device->bind_buffer_memory(device->device, result->readback_buffer, result->readback_memory, 0) != VK_SUCCESS ||
            device->map_memory(device->device, result->readback_memory, 0, readback_requirements.size, 0,
                &result->readback_pixels) != VK_SUCCESS) {
            strncpy(result->error, "Unable to map offscreen readback buffer", sizeof(result->error) - 1);
            return (size_t)result;
        }
    }
    if (!encore_vk_swapchain_create_render_support(result)) return (size_t)result;
    result->available = true;
    if (encore_vk_trace_enabled()) fprintf(stderr, "encore-vk: offscreen=%ux%u images=%u\n",
        width, height, result->image_count);
    return (size_t)result;
}

size_t encore_vk_offscreen_create(size_t device_handle, uint32_t width, uint32_t height) {
    return encore_vk_offscreen_create_internal(device_handle, width, height, false, VK_FORMAT_B8G8R8A8_UNORM, 1, 0, false);
}

size_t encore_vk_shared_offscreen_create(size_t device_handle, uint32_t width, uint32_t height) {
    return encore_vk_offscreen_create_internal(device_handle, width, height, true, VK_FORMAT_B8G8R8A8_UNORM, 1, 0, false);
}

size_t encore_vk_render_target_create(size_t device, uint32_t width, uint32_t height,
    uint32_t format, uint32_t samples, uint32_t auxiliary_format, bool sampled_depth) {
    return encore_vk_offscreen_create_internal(device, width, height, true, format, samples, auxiliary_format, sampled_depth);
}
size_t encore_vk_target_attachment(size_t handle, uint32_t attachment) {
    EncoreVkSwapchain *s = (EncoreVkSwapchain *)handle;
    if (!s || !s->available) return 0;
    uint32_t i = s->last_image_index;
    if (attachment == 0) return (size_t)s->images[i];
    if (attachment == 1 && s->auxiliary_images) return (size_t)s->auxiliary_images[i];
    if (attachment == 2 && s->sampled_depth) return (size_t)s->depth_images[i];
    return 0;
}
uint64_t encore_vk_target_render_signature(size_t handle) {
    const EncoreVkSwapchain *s = (const EncoreVkSwapchain *)handle;
    if (!s || !s->available) return 0;
    return (uint64_t)s->format | ((uint64_t)s->samples << 16) | ((uint64_t)s->auxiliary_format << 32);
}
static void encore_vk_swapchain_release_render_resources(EncoreVkSwapchain *swapchain) {
    EncoreVkDevice *device = swapchain->owner;
    if (device == NULL || device->device == NULL) return;
    for (unsigned kind=0;kind<3;++kind) {
        void **set=swapchain->framebuffer_sets[kind];
        if(set && swapchain->destroy_framebuffer)for(uint32_t i=0;i<swapchain->image_count;++i)
            if(set[i])swapchain->destroy_framebuffer(device->device,set[i],NULL);
        free(set);swapchain->framebuffer_sets[kind]=NULL;
    }
    swapchain->framebuffers=NULL;
    if (swapchain->views != NULL && swapchain->destroy_image_view != NULL) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i)
            if (swapchain->views[i] != NULL) swapchain->destroy_image_view(device->device, swapchain->views[i], NULL);
    }
    if (swapchain->depth_views != NULL && swapchain->destroy_image_view != NULL) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i)
            if (swapchain->depth_views[i] != NULL) swapchain->destroy_image_view(device->device, swapchain->depth_views[i], NULL);
    }
    if (swapchain->depth_images != NULL) for (uint32_t i = 0; i < swapchain->image_count; ++i)
        if (swapchain->depth_images[i] != NULL) device->destroy_image(device->device, swapchain->depth_images[i], NULL);
    if (swapchain->depth_memories != NULL) for (uint32_t i = 0; i < swapchain->image_count; ++i)
        if (swapchain->depth_memories[i] != NULL) device->free_memory(device->device, swapchain->depth_memories[i], NULL);
    if (swapchain->offscreen && swapchain->images != NULL) for (uint32_t i = 0; i < swapchain->image_count; ++i)
        if (swapchain->images[i] != NULL) device->destroy_image(device->device, swapchain->images[i], NULL);
    if (swapchain->color_memories != NULL) for (uint32_t i = 0; i < swapchain->image_count; ++i)
        if (swapchain->color_memories[i] != NULL) device->free_memory(device->device, swapchain->color_memories[i], NULL);
    void **extra_images[2] = {swapchain->multisample_images, swapchain->auxiliary_images};
    void **extra_views[2] = {swapchain->multisample_views, swapchain->auxiliary_views};
    void **extra_memories[2] = {swapchain->multisample_memories, swapchain->auxiliary_memories};
    for (int a=0; a<2; ++a) {
        for (uint32_t i=0; i<swapchain->image_count; ++i) {
            if (extra_views[a] && extra_views[a][i]) swapchain->destroy_image_view(device->device, extra_views[a][i], NULL);
            if (extra_images[a] && extra_images[a][i]) device->destroy_image(device->device, extra_images[a][i], NULL);
            if (extra_memories[a] && extra_memories[a][i]) device->free_memory(device->device, extra_memories[a][i], NULL);
        }
        free(extra_views[a]); free(extra_images[a]); free(extra_memories[a]);
    }

    free(swapchain->views);
    free(swapchain->depth_views);
    free(swapchain->depth_images);
    free(swapchain->depth_memories);
    free(swapchain->color_memories);
}

static void encore_vk_swapchain_release_readback(EncoreVkSwapchain *swapchain) {
    EncoreVkDevice *device = swapchain == NULL ? NULL : swapchain->owner;
    if (device == NULL || device->device == NULL) return;
    if (swapchain->readback_pixels != NULL && swapchain->readback_memory != NULL)
        device->unmap_memory(device->device, swapchain->readback_memory);
    if (swapchain->upload_staging_pixels != NULL && swapchain->upload_staging_memory != NULL)
        device->unmap_memory(device->device, swapchain->upload_staging_memory);
    if (swapchain->readback_buffer != NULL) device->destroy_buffer(device->device, swapchain->readback_buffer, NULL);
    if (swapchain->upload_staging_buffer != NULL) device->destroy_buffer(device->device, swapchain->upload_staging_buffer, NULL);
    if (swapchain->readback_memory != NULL) device->free_memory(device->device, swapchain->readback_memory, NULL);
    if (swapchain->upload_staging_memory != NULL) device->free_memory(device->device, swapchain->upload_staging_memory, NULL);
    swapchain->readback_pixels = NULL; swapchain->readback_buffer = NULL; swapchain->readback_memory = NULL;
    swapchain->upload_staging_pixels = NULL; swapchain->upload_staging_buffer = NULL; swapchain->upload_staging_memory = NULL;
}

bool encore_vk_swapchain_destroy_retired(size_t handle);

static bool encore_vk_swapchain_wait_frames(
    EncoreVkSwapchain *swapchain) {
    if (swapchain == NULL || swapchain->owner == NULL) return false;
    EncoreVkDevice *device = swapchain->owner;
    if (device->wait_for_fences(device->device, 2, swapchain->fences, 1,
            UINT64_MAX) != VK_SUCCESS) return false;
    if (!swapchain->offscreen &&
        device->wait_for_fences(device->device, 2,
            swapchain->present_fences, 1, UINT64_MAX) != VK_SUCCESS)
        return false;
    return true;
}

static VkResult encore_vk_swapchain_retired_status(
    const EncoreVkSwapchain *swapchain) {
    if (swapchain == NULL || swapchain->owner == NULL) return VK_SUCCESS;
    for (uint32_t frame = 0; frame < 2; ++frame) {
        if (swapchain->fences[frame] != NULL) {
            VkResult status = swapchain->owner->get_fence_status(
                swapchain->owner->device, swapchain->fences[frame]);
            if (status != VK_SUCCESS) return status;
        }
        if (swapchain->present_fences[frame] != NULL) {
            VkResult status = swapchain->owner->get_fence_status(
                swapchain->owner->device, swapchain->present_fences[frame]);
            if (status != VK_SUCCESS) return status;
        }
    }
    return VK_SUCCESS;
}

static bool encore_vk_swapchain_collect_retired(
    EncoreVkSwapchain *swapchain) {
    if (swapchain == NULL) return false;
    EncoreVkSwapchain **link =
        (EncoreVkSwapchain **)&swapchain->retired_next;
    while (*link != NULL) {
        EncoreVkSwapchain *retired = *link;
        VkResult status = encore_vk_swapchain_retired_status(retired);
        if (status == VK_NOT_READY) {
            link = (EncoreVkSwapchain **)&retired->retired_next;
            continue;
        }
        if (status != VK_SUCCESS) {
            snprintf(swapchain->error, sizeof(swapchain->error), "Retired frame fence failed (%d)", status);
            return false;
        }
        *link = (EncoreVkSwapchain *)retired->retired_next;
        retired->retired_next = NULL;
        encore_vk_swapchain_destroy_retired((size_t)retired);
    }
    return true;
}

static bool encore_vk_swapchain_rebuild(EncoreVkSwapchain *swapchain) {
    EncoreVkDevice *device = swapchain->owner;
    encore_vk_surface_refresh(swapchain->window);
    EncoreVkSwapchain *fresh = (EncoreVkSwapchain *)encore_vk_swapchain_create_internal(
        (size_t)device, (size_t)swapchain->window, swapchain->swapchain,
        swapchain->requested_present_mode);
    if (fresh == NULL || !fresh->available) {
        if (fresh != NULL) {
            if (encore_vk_trace_enabled()) fprintf(stderr, "encore-vk: rebuild failed: %s\n", fresh->error);
            strncpy(swapchain->error, fresh->error, sizeof(swapchain->error) - 1);
            encore_vk_swapchain_destroy_retired((size_t)fresh);
        }
        return false;
    }
    EncoreVkSwapchain *retired =
        (EncoreVkSwapchain *)malloc(sizeof(EncoreVkSwapchain));
    if (retired == NULL) {
        strncpy(swapchain->error, "Unable to retire the previous Vulkan swapchain",
            sizeof(swapchain->error) - 1);
        encore_vk_swapchain_destroy_retired((size_t)fresh);
        return false;
    }
    *retired = *swapchain;
    retired->frame_active = false;
    *swapchain = *fresh;
    swapchain->retired_next = retired;
    free(fresh);
    return encore_vk_swapchain_collect_retired(swapchain);
}

static bool encore_vk_swapchain_prepare_pipeline(EncoreVkSwapchain *swapchain, EncoreVkPipeline *pipeline) {
    if (pipeline == NULL || !pipeline->available || pipeline->owner != swapchain->owner || pipeline->format != swapchain->format || pipeline->samples != (swapchain->samples ? swapchain->samples : 1) || pipeline->auxiliary_format != swapchain->auxiliary_format) {
        strncpy(swapchain->error, "Render pipeline is incompatible with this swapchain", sizeof(swapchain->error) - 1); return false;
    }
    if (swapchain->active_render_pass == pipeline->render_pass) return true;
    EncoreVkDevice *device = swapchain->owner;
    /* All generated passes for a given target and attachment count are
     * compatible: load/store operations and layouts do not affect Vulkan
     * render-pass compatibility. Retain each attachment topology until target
     * retirement rather than waiting and replacing framebuffers every pass. */
    uint32_t count=(swapchain->samples>1 || swapchain->auxiliary_format)?3u:(pipeline->depth_attachment?2u:1u);
    uint32_t kind=count-1u;
    if(!swapchain->framebuffer_sets[kind])swapchain->framebuffer_sets[kind]=calloc(swapchain->image_count,sizeof(void *));
    if(!swapchain->framebuffer_sets[kind])return false;
    swapchain->framebuffers=swapchain->framebuffer_sets[kind];
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        if(swapchain->framebuffers[i])continue;
        void *attachments[3] = {swapchain->samples > 1 ? swapchain->multisample_views[i] : swapchain->views[i], swapchain->depth_views[i],
            swapchain->samples > 1 ? swapchain->views[i] : (swapchain->auxiliary_views ? swapchain->auxiliary_views[i] : NULL)};
        VkFramebufferCreateInfo framebuffer = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0,
            pipeline->render_pass, count,
            attachments, swapchain->width, swapchain->height, 1};
        if (swapchain->create_framebuffer(device->device, &framebuffer, NULL, &swapchain->framebuffers[i]) != VK_SUCCESS) {
            strncpy(swapchain->error, "Unable to create pipeline framebuffer", sizeof(swapchain->error) - 1); return false;
        }
    }
    swapchain->active_render_pass = pipeline->render_pass;
    return true;
}

/* Avoid entering a blocking driver wait when this slot has already completed.
 * A pending fence still protects every command buffer and caller upload slot. */
static VkResult encore_vk_frame_wait_fence(EncoreVkDevice *device, void *fence,
    bool nonblocking) {
    VkResult status = device->get_fence_status(device->device, fence);
    if (status != VK_NOT_READY) return status;
    if (nonblocking) return VK_TIMEOUT;
    return device->wait_for_fences(device->device, 1, &fence, 1, UINT64_MAX);
}

static size_t encore_vk_frame_acquire_internal(size_t swapchain_handle, bool nonblocking) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)swapchain_handle;
    EncoreVkFrame *frame = (EncoreVkFrame *)calloc(1, sizeof(EncoreVkFrame));
    if (frame == NULL) return 0;
    frame->owner = swapchain;
    if (swapchain == NULL || !swapchain->available || swapchain->frame_active) {
        strncpy(frame->error, "Swapchain cannot acquire another frame", sizeof(frame->error) - 1); return (size_t)frame;
    }
    EncoreVkDevice *device = swapchain->owner;
    if (!encore_vk_swapchain_collect_retired(swapchain)) {
        strncpy(frame->error, swapchain->error, sizeof(frame->error) - 1);
        return (size_t)frame;
    }
    if (!swapchain->offscreen && swapchain->window != NULL) {
        encore_vk_surface_refresh(swapchain->window);
        if (swapchain->window->width == 0 || swapchain->window->height == 0) {
            frame->acquire_status = 3;
            return (size_t)frame;
        }
    }
    if (nonblocking) {
        uint32_t retired_count = 0;
        for (EncoreVkSwapchain *old = swapchain->retired_next; old != NULL;
             old = old->retired_next) ++retired_count;
        if (retired_count >= 2) {
            frame->acquire_status = 2;
            return (size_t)frame;
        }
    }
    if (swapchain->needs_rebuild && !encore_vk_swapchain_rebuild(swapchain)) {
        strncpy(frame->error, swapchain->error, sizeof(frame->error) - 1);
        return (size_t)frame;
    }
    if (!swapchain->offscreen && swapchain->window != NULL) {
        if ((swapchain->window->width != swapchain->width || swapchain->window->height != swapchain->height) &&
            !encore_vk_swapchain_rebuild(swapchain)) {
            strncpy(frame->error, swapchain->error, sizeof(frame->error) - 1);
            return (size_t)frame;
        }
    }
    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        frame->frame_index = swapchain->frame_index;
        /* Slots name caller-owned upload buffers too. After a rebuild the
           old swapchain may still use the same slot: do not recycle it early. */
        {
            for (EncoreVkSwapchain *old = swapchain->retired_next; old != NULL;
                 old = old->retired_next) {
                VkResult status = encore_vk_frame_wait_fence(device,
                    old->fences[frame->frame_index], nonblocking);
                if (status == VK_TIMEOUT) { frame->acquire_status = 2; return (size_t)frame; }
                if (status != VK_SUCCESS) {
                    snprintf(frame->error, sizeof(frame->error), "Retired frame fence failed (%d)", status);
                    return (size_t)frame;
                }
            }
        }
        VkResult ready = encore_vk_frame_wait_fence(device,
            swapchain->fences[frame->frame_index], nonblocking);
        if (nonblocking && ready == VK_TIMEOUT) { frame->acquire_status = 2; return (size_t)frame; }
        if (ready != VK_SUCCESS) {
            strncpy(frame->error, "Unable to wait for a frame fence", sizeof(frame->error) - 1); return (size_t)frame;
        }
        ready = swapchain->offscreen ? VK_SUCCESS :
            encore_vk_frame_wait_fence(device,
                swapchain->present_fences[frame->frame_index], nonblocking);
        if (nonblocking && ready == VK_TIMEOUT) { frame->acquire_status = 2; return (size_t)frame; }
        if (ready != VK_SUCCESS) {
            strncpy(frame->error, "Unable to wait for a presentation fence",
                sizeof(frame->error) - 1);
            return (size_t)frame;
        }
        VkResult acquired = VK_SUCCESS;
        if (swapchain->offscreen) {
            frame->image_index = frame->frame_index;
        } else {
            acquired = swapchain->acquire_next_image(device->device, swapchain->swapchain, nonblocking ? 0 : UINT64_MAX,
                swapchain->image_available[frame->frame_index], NULL, &frame->image_index);
        }
        if (encore_vk_trace_enabled()) fprintf(stderr, "encore-vk: frame acquire=%d image=%u\n", acquired, frame->image_index);
        if (!swapchain->offscreen && acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            if (nonblocking) {
                swapchain->needs_rebuild = true;
                frame->acquire_status = 2;
                return (size_t)frame;
            }
            if (!encore_vk_swapchain_rebuild(swapchain)) {
                strncpy(frame->error, swapchain->error, sizeof(frame->error) - 1); return (size_t)frame;
            }
            continue;
        }
        if (nonblocking && (acquired == VK_NOT_READY || acquired == VK_TIMEOUT)) {
            frame->acquire_status = 2;
            return (size_t)frame;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            snprintf(frame->error, sizeof(frame->error), "Unable to acquire frame image (result %d)", acquired);
            return (size_t)frame;
        }
        encore_completion_slot_retired(swapchain, frame->frame_index);
        frame->command_buffer = swapchain->command_buffers[frame->frame_index];
        if (device->reset_command_buffer(frame->command_buffer, 0) != VK_SUCCESS) {
            strncpy(frame->error, "Unable to reset frame command buffer", sizeof(frame->error) - 1); return (size_t)frame;
        }
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
        if (device->begin_command_buffer(frame->command_buffer, &begin) != VK_SUCCESS) {
            strncpy(frame->error, "Unable to begin frame command buffer", sizeof(frame->error) - 1); return (size_t)frame;
        }
        frame->recording = true;
        frame->acquire_status = 1;
        swapchain->frame_active = true;
        return (size_t)frame;
    }
    strncpy(frame->error, "Swapchain remained out of date after rebuild", sizeof(frame->error) - 1);
    return (size_t)frame;
}

size_t encore_vk_frame_acquire(size_t swapchain_handle) {
    return encore_vk_frame_acquire_internal(swapchain_handle, false);
}

size_t encore_vk_frame_try_acquire(size_t swapchain_handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)swapchain_handle;
    bool timing = swapchain && encore_timing_find(swapchain->owner) && encore_timing_find(swapchain->owner)->active >= 0;
    uint64_t start = timing ? encore_timing_clock() : 0;
    size_t frame = encore_vk_frame_acquire_internal(swapchain_handle, true);
    if (timing) encore_timing_add_wait(swapchain->owner, encore_timing_clock() - start);
    return frame;
}

uint32_t encore_vk_frame_acquire_status(size_t handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    return frame == NULL ? 0 : frame->acquire_status;
}

bool encore_vk_frame_constants(size_t handle, const float *values, size_t count) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    if (frame == NULL || !frame->in_render_pass || frame->pipeline == NULL ||
        values == NULL || count == 0 || count > 32 ||
        frame->owner->owner->cmd_push_constants == NULL) return false;
    frame->owner->owner->cmd_push_constants(frame->command_buffer,
        frame->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, (uint32_t)(count * sizeof(float)), values);
    return true;
}

bool encore_vk_frame_available(size_t handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    return frame != NULL && frame->recording && !frame->complete;
}

uint32_t encore_vk_frame_slot(size_t handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    return frame == NULL ? 0 : frame->frame_index;
}

encore_str encore_vk_frame_error(size_t handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    return encore_string(frame == NULL ? "Invalid render frame" : frame->error);
}

/* Protect queued shader/vertex/transfer consumers before modifying shared
 * GPU storage. The CPU never overwrites a previous frame's mapped inputs. */
static void encore_vk_frame_buffer_to_transfer(EncoreVkFrame *frame,EncoreVkBuffer *buffer,uint64_t offset,uint64_t size) {
    VkBufferMemoryBarrier barrier={44,NULL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,buffer->buffer,offset,size};
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
}
bool encore_vk_frame_update_f32s(size_t frame_handle,size_t buffer_handle,uint64_t offset,const float *values,size_t count) {
    EncoreVkFrame *frame=(EncoreVkFrame *)frame_handle;EncoreVkBuffer *buffer=(EncoreVkBuffer *)buffer_handle;
    if(!frame || !frame->recording || frame->complete || frame->in_render_pass || !buffer || !buffer->available ||
        buffer->owner!=frame->owner->owner || !(buffer->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        (offset & 3u) || count>SIZE_MAX/4 || offset>buffer->size || count*4>buffer->size-offset ||
        (count && !values) || !buffer->owner->cmd_update_buffer)return false;
    if(!count)return true;
    uint64_t bytes=(uint64_t)count*4;
    encore_vk_frame_buffer_to_transfer(frame,buffer,offset,bytes);
    for(uint64_t at=0;at<bytes;) {
        uint64_t chunk=bytes-at>65536?65536:bytes-at;
        buffer->owner->cmd_update_buffer(frame->command_buffer,buffer->buffer,offset+at,chunk,(const char *)values+at);at+=chunk;
    }
    VkMemoryBarrier ready={VK_STRUCTURE_TYPE_MEMORY_BARRIER,NULL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT};
    buffer->owner->cmd_pipeline_barrier(frame->command_buffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,0,1,&ready,0,NULL,0,NULL);
    return true;
}

/* Same owned byte packet as f32 updates, for counters/indices and typed ABIs. */
bool encore_vk_frame_update_u32s(size_t frame,size_t buffer,uint64_t offset,const uint32_t *values,size_t count) {
    return encore_vk_frame_update_f32s(frame,buffer,offset,(const float *)values,count);
}

bool encore_vk_frame_dispatch_compute(size_t frame_handle,
    size_t pipeline_handle, size_t bind_group_handle,
    uint32_t x, uint32_t y, uint32_t z) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkComputePipeline *pipeline =
        (EncoreVkComputePipeline *)pipeline_handle;
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)bind_group_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass || pipeline == NULL || !pipeline->available ||
        group == NULL || !group->available || x == 0 || y == 0 || z == 0 ||
        pipeline->owner != frame->owner->owner ||
        group->owner != frame->owner->owner) {
        if (frame != NULL)
            strncpy(frame->error, "Unable to record frame compute dispatch",
                sizeof(frame->error) - 1);
        return false;
    }
    EncoreVkDevice *device = frame->owner->owner;
    device->cmd_bind_pipeline(frame->command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    device->cmd_bind_descriptor_sets(frame->command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout,
        0, 1, &group->set, 0, NULL);
    device->cmd_dispatch(frame->command_buffer, x, y, z);
    return true;
}

/* Argument buffer is GPU-owned until its frame slot completes. The caller
 * records a compute->indirect barrier after generating the dispatch counts. */
bool encore_vk_frame_dispatch_compute_indirect(size_t frame_handle,
    size_t pipeline_handle, size_t group_handle, size_t buffer_handle,
    uint64_t offset) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkComputePipeline *pipeline = (EncoreVkComputePipeline *)pipeline_handle;
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)group_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (!frame || !frame->owner || !frame->recording || frame->complete ||
        frame->in_render_pass || !pipeline || !pipeline->available ||
        !group || !group->available || !buffer || !buffer->available ||
        pipeline->owner != frame->owner->owner || group->owner != pipeline->owner ||
        buffer->owner != pipeline->owner || !(buffer->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ||
        (offset & 3u) || offset > buffer->size || buffer->size - offset < 12 ||
        !pipeline->owner->cmd_dispatch_indirect) return false;
    EncoreVkDevice *device = pipeline->owner;
    device->cmd_bind_pipeline(frame->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    device->cmd_bind_descriptor_sets(frame->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline->layout, 0, 1, &group->set, 0, NULL);
    device->cmd_dispatch_indirect(frame->command_buffer, buffer->buffer, offset);
    return true;
}

bool encore_vk_frame_compute_barrier(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass) return false;
    VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
            VK_ACCESS_HOST_READ_BIT
    };
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_frame_fill_buffer(size_t frame_handle, size_t buffer_handle,
    uint64_t offset, uint64_t size, uint32_t value) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass || buffer == NULL || !buffer->available ||
        buffer->owner != frame->owner->owner ||
        (buffer->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0 ||
        size == 0 || (offset & 3u) != 0 || (size & 3u) != 0 ||
        offset > buffer->size || size > buffer->size - offset ||
        buffer->owner->cmd_fill_buffer == NULL) return false;
    buffer->owner->cmd_fill_buffer(frame->command_buffer, buffer->buffer,
        offset, size, value);
    return true;
}

bool encore_vk_frame_transfer_storage_barrier(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass) return false;
    VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
    };
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_frame_sampled_color_barrier(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass) return false;
    VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | 0x200u,
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_frame_fragment_storage_barrier(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass) return false;
    VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT
    };
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_frame_copy_buffer(size_t frame_handle,
    size_t source_handle, size_t destination_handle,
    uint64_t source_offset, uint64_t destination_offset, uint64_t size) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *source = (EncoreVkBuffer *)source_handle;
    EncoreVkBuffer *destination = (EncoreVkBuffer *)destination_handle;
    if (frame == NULL || !frame->recording || frame->complete ||
        frame->in_render_pass || source == NULL || destination == NULL ||
        !source->available || !destination->available ||
        source->owner != frame->owner->owner ||
        destination->owner != frame->owner->owner ||
        (source->usage & 1u) == 0 || (destination->usage & 2u) == 0 ||
        size == 0 || source_offset > source->size ||
        size > source->size - source_offset ||
        destination_offset > destination->size ||
        size > destination->size - destination_offset) {
        if (frame != NULL)
            strncpy(frame->error, "Unable to record frame buffer copy",
                sizeof(frame->error) - 1);
        return false;
    }
    /* Copies also consume preceding compute/transfer writes to the source. */
    VkBufferMemoryBarrier source_ready = {44 /* VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER */, NULL,
        65536u /* VK_ACCESS_MEMORY_WRITE_BIT */, VK_ACCESS_TRANSFER_READ_BIT,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        source->buffer, source_offset, size};
    frame->owner->owner->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 1, &source_ready, 0, NULL);
    encore_vk_frame_buffer_to_transfer(frame,destination,destination_offset,size);
    VkBufferCopy region = {source_offset, destination_offset, size};
    EncoreVkDevice *device = frame->owner->owner;
    device->cmd_copy_buffer(frame->command_buffer,
        source->buffer, destination->buffer, 1, &region);
    VkMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
            VK_ACCESS_INDEX_READ_BIT | (destination->host_visible ? VK_ACCESS_HOST_READ_BIT : 0)
    };
    device->cmd_pipeline_barrier(frame->command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | (destination->host_visible ? VK_PIPELINE_STAGE_HOST_BIT : 0),
        0, 1, &barrier, 0, NULL, 0, NULL);
    return true;
}

bool encore_vk_frame_begin_render_pass(size_t frame_handle, size_t pipeline_handle,
    float red, float green, float blue, float alpha) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkPipeline *pipeline = (EncoreVkPipeline *)pipeline_handle;
    if (frame == NULL || !frame->recording || frame->in_render_pass || pipeline == NULL ||
        !encore_vk_swapchain_prepare_pipeline(frame->owner, pipeline)) {
        if (frame != NULL) strncpy(frame->error, "Unable to begin render pass", sizeof(frame->error) - 1);
        return false;
    }
    EncoreVkSwapchain *swapchain = frame->owner;
    frame->pipeline = pipeline;
    VkClearValue clear_values[3] = {{{{red, green, blue, alpha}}}, {.depthStencil = {1.0f, 0}}, {{{0,0,0,-1}}}};
    VkRenderPassBeginInfo render = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, pipeline->render_pass,
        swapchain->framebuffers[frame->image_index], {{0, 0}, {swapchain->width, swapchain->height}},
        (swapchain->samples > 1 || swapchain->auxiliary_format) ? 3u : (pipeline->depth_attachment ? 2u : 1u), clear_values};
    swapchain->cmd_begin_render_pass(frame->command_buffer, &render, VK_SUBPASS_CONTENTS_INLINE);
    swapchain->cmd_bind_pipeline(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    VkViewport viewport = {0.0f, 0.0f, (float)swapchain->width, (float)swapchain->height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {swapchain->width, swapchain->height}};
    swapchain->cmd_set_viewport(frame->command_buffer, 0, 1, &viewport);
    swapchain->cmd_set_scissor(frame->command_buffer, 0, 1, &scissor);
    frame->in_render_pass = true;
    return true;
}

bool encore_vk_frame_bind_group(size_t frame_handle, size_t bind_group_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)bind_group_handle;
    if (frame == NULL || !frame->in_render_pass || frame->pipeline == NULL || group == NULL ||
        !group->available || group->owner != frame->owner->owner) return false;
    frame->owner->cmd_bind_descriptor_sets(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        frame->pipeline->layout, 0, 1, &group->set, 0, NULL);
    return true;
}

/* The caller owns descriptor/resource lifetime until the frame completion. */
bool encore_vk_frame_bind_group_at(size_t frame_handle, uint32_t index, size_t bind_group_handle) {
    if (index == 0) return encore_vk_frame_bind_group(frame_handle, bind_group_handle);
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBindGroup *group = (EncoreVkBindGroup *)bind_group_handle;
    if (index != 1 || !frame || !frame->recording || !frame->in_render_pass || !frame->pipeline ||
        !group || !group->available || group->owner != frame->owner->owner || !group->layout ||
        !frame->pipeline->secondary_descriptor_layout ||
        group->layout->layout != frame->pipeline->secondary_descriptor_layout) return false;
    frame->owner->cmd_bind_descriptor_sets(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        frame->pipeline->layout, index, 1, &group->set, 0, NULL);
    return true;
}

bool encore_vk_frame_bind_pipeline(size_t frame_handle, size_t pipeline_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkPipeline *pipeline = (EncoreVkPipeline *)pipeline_handle;
    if (frame == NULL || !frame->recording || !frame->in_render_pass ||
        pipeline == NULL || !pipeline->available ||
        pipeline->owner != frame->owner->owner ||
        pipeline->format != frame->owner->format ||
        pipeline->samples != frame->pipeline->samples || pipeline->auxiliary_format != frame->pipeline->auxiliary_format ||
        !pipeline->depth_attachment) return false;
    frame->owner->cmd_bind_pipeline(frame->command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    frame->pipeline = pipeline;
    /* Pipeline changes must preserve the caller's viewport and scissor.
       External passes set a sub-rect before binding their material pipeline;
       resetting here expands the Encore scene over the editor's panels. */
    return true;
}

bool encore_vk_frame_bind_vertex_buffer(size_t frame_handle, size_t buffer_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (frame == NULL || !frame->in_render_pass || buffer == NULL || !buffer->available ||
        buffer->owner != frame->owner->owner) return false;
    uint64_t offset = 0;
    frame->owner->cmd_bind_vertex_buffers(frame->command_buffer, 0, 1, &buffer->buffer, &offset);
    return true;
}

bool encore_vk_frame_bind_index_buffer(size_t frame_handle, size_t buffer_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (frame == NULL || !frame->in_render_pass || buffer == NULL || !buffer->available ||
        buffer->owner != frame->owner->owner) return false;
    frame->owner->cmd_bind_index_buffer(frame->command_buffer, buffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    return true;
}

bool encore_vk_frame_draw_indexed(size_t frame_handle, uint32_t index_count,
    uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->in_render_pass || index_count == 0 || instance_count == 0) return false;
    frame->owner->cmd_draw_indexed(frame->command_buffer, index_count, instance_count,
        first_index, vertex_offset, first_instance);
    return true;
}

bool encore_vk_frame_draw_indexed_indirect_count(size_t frame_handle,
    size_t commands_handle, uint64_t commands_offset, size_t count_handle,
    uint64_t count_offset, uint32_t maximum_draw_count, uint32_t stride) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *commands = (EncoreVkBuffer *)commands_handle;
    EncoreVkBuffer *count = (EncoreVkBuffer *)count_handle;
    if (frame == NULL || !frame->in_render_pass ||
        commands == NULL || count == NULL ||
        !commands->available || !count->available ||
        commands->owner != frame->owner->owner ||
        count->owner != frame->owner->owner ||
        frame->owner->cmd_draw_indexed_indirect_count == NULL ||
        maximum_draw_count == 0 || stride < 20 ||
        (commands->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) == 0 ||
        (count->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) == 0 ||
        commands_offset > commands->size ||
        count_offset > count->size ||
        commands->size - commands_offset < 20 ||
        count->size - count_offset < sizeof(uint32_t)) return false;
    frame->owner->cmd_draw_indexed_indirect_count(frame->command_buffer,
        commands->buffer, commands_offset, count->buffer, count_offset,
        maximum_draw_count, stride);
    return true;
}

bool encore_vk_frame_draw_mesh_tasks(size_t frame_handle,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->in_render_pass ||
        frame->owner->cmd_draw_mesh_tasks == NULL ||
        group_count_x == 0 || group_count_y == 0 || group_count_z == 0) return false;
    frame->owner->cmd_draw_mesh_tasks(frame->command_buffer,
        group_count_x, group_count_y, group_count_z);
    return true;
}

bool encore_vk_frame_draw_mesh_tasks_indirect(size_t frame_handle,
    size_t commands_handle, uint64_t offset) {
    if (frame_handle < 4096u || commands_handle < 4096u) return false;
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *commands = (EncoreVkBuffer *)commands_handle;
    if (frame == NULL || !frame->in_render_pass ||
        frame->owner->cmd_draw_mesh_tasks_indirect == NULL ||
        commands == NULL || !commands->available ||
        commands->owner != frame->owner->owner ||
        (commands->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) == 0 ||
        offset > commands->size || commands->size - offset < 12u)
        return false;
    frame->owner->cmd_draw_mesh_tasks_indirect(
        frame->command_buffer, commands->buffer, offset, 1u, 12u);
    return true;
}

bool encore_vk_frame_end_render_pass(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || !frame->in_render_pass) return false;
    frame->owner->cmd_end_render_pass(frame->command_buffer);
    frame->in_render_pass = false;
    return true;
}

bool encore_vk_frame_set_viewport(size_t frame_handle, uint32_t x, uint32_t y,
    uint32_t width, uint32_t height) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->in_render_pass || width == 0 || height == 0) return false;
    EncoreVkSwapchain *swapchain = frame->owner;
    if (x >= swapchain->width || y >= swapchain->height) return false;
    if (width > swapchain->width - x) width = swapchain->width - x;
    if (height > swapchain->height - y) height = swapchain->height - y;
    VkViewport viewport = {(float)x, (float)y, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor = {{(int32_t)x, (int32_t)y}, {width, height}};
    swapchain->cmd_set_viewport(frame->command_buffer, 0, 1, &viewport);
    swapchain->cmd_set_scissor(frame->command_buffer, 0, 1, &scissor);
    return true;
}

/* On-demand color readback belongs to a recorded frame. Never read a WSI
 * image after presentation, when ownership may have passed to the compositor. */
uint32_t encore_vk_frame_copy_color(size_t frame_handle, size_t buffer_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkBuffer *buffer = (EncoreVkBuffer *)buffer_handle;
    if (!frame || !buffer || !frame->recording || frame->in_render_pass || frame->submitted || frame->complete) return 0;
    EncoreVkSwapchain *s = frame->owner;
    EncoreVkDevice *d = s->owner;
    uint64_t bytes = (uint64_t)s->width * s->height * 4;
    if (!buffer->available || buffer->owner != d || !(buffer->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) || buffer->size < bytes ||
        (s->format != VK_FORMAT_B8G8R8A8_UNORM && s->format != VK_FORMAT_B8G8R8A8_SRGB && s->format != 37u && s->format != 43u)) return 0;
    uint32_t layout = s->offscreen ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkImageMemoryBarrier to_copy = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,s->images[frame->image_index],{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    d->cmd_pipeline_barrier(frame->command_buffer,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&to_copy);
    VkBufferImageCopy region = {0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{s->width,s->height,1}};
    d->cmd_copy_image_to_buffer(frame->command_buffer,s->images[frame->image_index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer->buffer,1,&region);
    VkImageMemoryBarrier restore = to_copy;
    restore.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;restore.dstAccessMask=0;
    restore.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;restore.newLayout=layout;
    VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER,NULL,VK_ACCESS_TRANSFER_WRITE_BIT,0x2000u};
    d->cmd_pipeline_barrier(frame->command_buffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | 0x4000u,0,1,&host,0,NULL,1,&restore);
    return s->format;
}

static bool encore_vk_frame_present_internal(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    if (frame == NULL || !frame->recording || frame->in_render_pass || frame->complete || frame->submitted) return false;
    EncoreVkSwapchain *swapchain = frame->owner;
    EncoreVkDevice *device = swapchain->owner;
    uint32_t source_access = frame->transfer_clear ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    uint32_t source_stage = frame->transfer_clear ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (swapchain->offscreen && swapchain->shared) {
        VkImageMemoryBarrier to_sample = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL,
            source_access, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapchain->images[frame->image_index],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        device->cmd_pipeline_barrier(frame->command_buffer, source_stage,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_sample);
        if (swapchain->auxiliary_images || swapchain->sampled_depth) {
            VkImageMemoryBarrier barriers[2]; uint32_t count = 0;
            if (swapchain->auxiliary_images) {
                barriers[count] = to_sample; barriers[count++].image = swapchain->auxiliary_images[frame->image_index];
            }
            if (swapchain->sampled_depth) {
                barriers[count] = to_sample;
                barriers[count].image = swapchain->depth_images[frame->image_index];
                barriers[count].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                barriers[count].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barriers[count++].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            device->cmd_pipeline_barrier(frame->command_buffer, source_stage | 0x200u | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,NULL,0,NULL,count,barriers);
        }
        swapchain->external_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if (swapchain->offscreen) {
        VkImageMemoryBarrier to_transfer = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL,
            source_access, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapchain->images[frame->image_index],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        device->cmd_pipeline_barrier(frame->command_buffer, source_stage,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_transfer);
        VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0}, {swapchain->width, swapchain->height, 1}};
        device->cmd_copy_image_to_buffer(frame->command_buffer, swapchain->images[frame->image_index],
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain->readback_buffer, 1, &copy);
        VkImageMemoryBarrier restore = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL,
            VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            swapchain->images[frame->image_index], {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        device->cmd_pipeline_barrier(frame->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &restore);
    }
    if (encore_vk_trace_enabled()) { fprintf(stderr, "encore-vk: frame finalize\n"); fflush(stderr); }
    if (device->end_command_buffer(frame->command_buffer) != VK_SUCCESS ||
        swapchain->reset_fences(device->device, 1, &swapchain->fences[frame->frame_index]) != VK_SUCCESS) {
        strncpy(frame->error, "Unable to finalize frame commands", sizeof(frame->error) - 1); return false;
    }
    uint32_t wait_stage = source_stage;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL,
        swapchain->offscreen ? 0 : 1,
        swapchain->offscreen ? NULL : &swapchain->image_available[frame->frame_index],
        swapchain->offscreen ? NULL : &wait_stage, 1, &frame->command_buffer,
        swapchain->offscreen ? 0 : 1,
        swapchain->offscreen ? NULL : &swapchain->render_finished[frame->frame_index]};
    if (encore_vk_trace_enabled()) { fprintf(stderr, "encore-vk: frame submit\n"); fflush(stderr); }
    if (device->queue_submit(device->queue, 1, &submit, swapchain->fences[frame->frame_index]) != VK_SUCCESS) {
        strncpy(frame->error, "Unable to submit render frame", sizeof(frame->error) - 1); return false;
    }
    frame->submitted = true;
    encore_completion_submitted(frame);
    /*
     * Shared offscreen rendering and its consumer use the same VkQueue.
     * Queue submission order is the synchronization contract: the consumer's sampling
     * submission follows this write without stalling the CPU.  The fence is
     * still waited before this frame slot is reused, which protects command
     * buffers and the double-buffered image lifetime.
     */
    VkResult status = VK_SUCCESS;
    if (!swapchain->offscreen) {
        if (swapchain->reset_fences(device->device, 1,
                &swapchain->present_fences[frame->frame_index]) != VK_SUCCESS) {
            strncpy(frame->error, "Unable to reset the presentation fence",
                sizeof(frame->error) - 1);
            return false;
        }
        void *present_fence = swapchain->present_fences[frame->frame_index];
        VkSwapchainPresentFenceInfoEXT fence_info = {
            VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
            NULL, 1, &present_fence
        };
        VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            &fence_info, 1,
            &swapchain->render_finished[frame->frame_index], 1, &swapchain->swapchain, &frame->image_index, NULL};
        if (encore_vk_trace_enabled()) { fprintf(stderr, "encore-vk: frame queue present\n"); fflush(stderr); }
        status = swapchain->queue_present(device->queue, &present);
    }
    if (encore_vk_trace_enabled()) fprintf(stderr, "encore-vk: frame present=%d\n", status);
    swapchain->frame_index = (frame->frame_index + 1) % 2;
    swapchain->last_image_index = frame->image_index;
    swapchain->frame_active = false;
    frame->recording = false;
    frame->complete = true;
    if (!swapchain->offscreen && status == VK_SUBOPTIMAL_KHR) {
        swapchain->needs_rebuild = true;
        return true;
    }
    if (!swapchain->offscreen && status == VK_ERROR_OUT_OF_DATE_KHR) {
        /* Rendering was submitted successfully. Recreate on the next acquire,
         * after the caller releases this frame, instead of terminating a game
         * for a normal window-system resize. */
        swapchain->needs_rebuild = true;
        return true;
    }
    if (status != VK_SUCCESS) {
        snprintf(frame->error, sizeof(frame->error), "Unable to present render frame (result %d)", status); return false;
    }
    return true;
}

size_t encore_vk_swapchain_pixels(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL || !swapchain->available || swapchain->readback_pixels == NULL) return 0;
    return (size_t)swapchain->readback_pixels;
}

uint32_t encore_vk_swapchain_pitch(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    return swapchain == NULL ? 0 : swapchain->readback_pitch;
}

size_t encore_vk_swapchain_external_image(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL || !swapchain->available || !swapchain->shared ||
        swapchain->images == NULL || swapchain->external_layout == 0) return 0;
    return (size_t)swapchain->images[swapchain->last_image_index];
}

uint32_t encore_vk_swapchain_external_layout(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    return swapchain == NULL || !swapchain->shared ? 0 : swapchain->external_layout;
}

bool encore_vk_frame_present(size_t frame_handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)frame_handle;
    EncoreVkDevice *device = frame && frame->owner ? frame->owner->owner : NULL;
    EncoreTimingStream *timing = encore_timing_find(device);
    uint64_t start = timing && timing->active >= 0 ? encore_timing_clock() : 0;
    bool result = encore_vk_frame_present_internal(frame_handle);
    if (start) encore_timing_add_wait(device, encore_timing_clock() - start);
    return result;
}

bool encore_vk_frame_destroy(size_t handle) {
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    if (frame == NULL) return false;
    if (frame->recording && !frame->submitted && frame->owner != NULL) {
        if (frame->in_render_pass) encore_vk_frame_end_render_pass(handle);
        encore_vk_frame_present(handle);
    }
    if(frame->recording && frame->owner) {
        EncoreVkCompletion *c=frame->owner->completions[frame->frame_index];
        if(c && !frame->submitted)encore_completion_finish(c,false);
    }
    free(frame); return true;
}

static bool encore_vk_swapchain_draw_internal(size_t handle, EncoreVkPipeline *render_pipeline,
    EncoreVkBuffer *vertex_buffer, EncoreVkBuffer *index_buffer, EncoreVkBindGroup *bind_group,
    uint32_t index_count,
    float red, float green, float blue, float alpha, bool triangle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL || !swapchain->available) return false;
    EncoreVkDevice *device = swapchain->owner;
    if (render_pipeline != NULL && !encore_vk_swapchain_prepare_pipeline(swapchain, render_pipeline)) return false;
    if ((vertex_buffer != NULL || index_buffer != NULL) &&
        (vertex_buffer == NULL || index_buffer == NULL || !vertex_buffer->available || !index_buffer->available ||
        vertex_buffer->owner != device || index_buffer->owner != device || index_count == 0)) {
        strncpy(swapchain->error, "Invalid indexed draw buffers", sizeof(swapchain->error) - 1); return false;
    }
    if (bind_group != NULL && (!bind_group->available || bind_group->owner != device)) {
        strncpy(swapchain->error, "Invalid draw bind group", sizeof(swapchain->error) - 1); return false;
    }
    EncoreVkFrame *frame = (EncoreVkFrame *)encore_vk_frame_acquire(handle);
    if (!encore_vk_frame_available((size_t)frame)) {
        if (frame) {
            snprintf(swapchain->error,sizeof(swapchain->error),"%s",frame->error);
            encore_vk_frame_destroy((size_t)frame);
        }
        return false;
    }
    uint32_t image_index = frame->image_index;
    void *command = frame->command_buffer;
    if (triangle) {
        VkClearValue clear_value = {{{red, green, blue, alpha}}};
        VkRenderPassBeginInfo render = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, render_pipeline->render_pass,
            swapchain->framebuffers[image_index], {{0, 0}, {swapchain->width, swapchain->height}}, 1, &clear_value};
        swapchain->cmd_begin_render_pass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
        swapchain->cmd_bind_pipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline->pipeline);
        VkViewport viewport = {0.0f, 0.0f, (float)swapchain->width, (float)swapchain->height, 0.0f, 1.0f};
        VkRect2D scissor = {{0, 0}, {swapchain->width, swapchain->height}};
        swapchain->cmd_set_viewport(command, 0, 1, &viewport);
        swapchain->cmd_set_scissor(command, 0, 1, &scissor);
        if (bind_group != NULL)
            swapchain->cmd_bind_descriptor_sets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline->layout,
                0, 1, &bind_group->set, 0, NULL);
        if (vertex_buffer != NULL) {
            uint64_t vertex_offset = 0;
            swapchain->cmd_bind_vertex_buffers(command, 0, 1, &vertex_buffer->buffer, &vertex_offset);
            swapchain->cmd_bind_index_buffer(command, index_buffer->buffer, 0, VK_INDEX_TYPE_UINT32);
            swapchain->cmd_draw_indexed(command, index_count, 1, 0, 0, 0);
        } else {
            swapchain->cmd_draw(command, 3, 1, 0, 0);
        }
        swapchain->cmd_end_render_pass(command);
    } else {
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier to_transfer = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            swapchain->images[image_index], range};
        device->cmd_pipeline_barrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &to_transfer);
        VkClearColorValue color = {{red, green, blue, alpha}};
        swapchain->cmd_clear_color_image(command, swapchain->images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        VkImageMemoryBarrier to_present = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            swapchain->offscreen ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            swapchain->images[image_index], range};
        device->cmd_pipeline_barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, NULL, 0, NULL, 1, &to_present);
    }
    frame->transfer_clear = !triangle;
    bool result = encore_vk_frame_present((size_t)frame);
    if (!result) snprintf(swapchain->error,sizeof(swapchain->error),"%s",frame->error);
    encore_vk_frame_destroy((size_t)frame);
    return result;
}

bool encore_vk_swapchain_clear(size_t handle, float red, float green, float blue, float alpha) {
    return encore_vk_swapchain_draw_internal(handle, NULL, NULL, NULL, NULL, 0, red, green, blue, alpha, false);
}

bool encore_vk_swapchain_draw_pipeline(size_t handle, size_t pipeline_handle,
    float red, float green, float blue, float alpha) {
    return encore_vk_swapchain_draw_internal(handle, (EncoreVkPipeline *)pipeline_handle,
        NULL, NULL, NULL, 0, red, green, blue, alpha, true);
}

bool encore_vk_swapchain_draw_indexed(size_t handle, size_t pipeline_handle, size_t vertex_handle,
    size_t index_handle, uint32_t index_count, float red, float green, float blue, float alpha) {
    return encore_vk_swapchain_draw_internal(handle, (EncoreVkPipeline *)pipeline_handle,
        (EncoreVkBuffer *)vertex_handle, (EncoreVkBuffer *)index_handle, NULL, index_count,
        red, green, blue, alpha, true);
}

bool encore_vk_swapchain_draw_indexed_bound(size_t handle, size_t pipeline_handle, size_t vertex_handle,
    size_t index_handle, size_t bind_group_handle, uint32_t index_count,
    float red, float green, float blue, float alpha) {
    return encore_vk_swapchain_draw_internal(handle, (EncoreVkPipeline *)pipeline_handle,
        (EncoreVkBuffer *)vertex_handle, (EncoreVkBuffer *)index_handle,
        (EncoreVkBindGroup *)bind_group_handle, index_count, red, green, blue, alpha, true);
}

bool encore_vk_swapchain_destroy(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL) return false;
    EncoreVkDevice *device = swapchain->owner;
    if (device != NULL && device->device != NULL) {
        if (!encore_vk_swapchain_wait_frames(swapchain)) return false;
        EncoreVkSwapchain *retired =
            (EncoreVkSwapchain *)swapchain->retired_next;
        swapchain->retired_next = NULL;
        while (retired != NULL) {
            EncoreVkSwapchain *next =
                (EncoreVkSwapchain *)retired->retired_next;
            retired->retired_next = NULL;
            if (!encore_vk_swapchain_wait_frames(retired)) return false;
            encore_vk_swapchain_destroy_retired((size_t)retired);
            retired = next;
        }
        encore_timing_target_retired(swapchain);
        encore_completion_target_retired(swapchain);
        if (swapchain->command_buffers[0] != NULL)
            device->free_command_buffers(device->device, device->command_pool, 2, swapchain->command_buffers);
        for (uint32_t frame = 0; frame < 2; ++frame) {
            if (swapchain->image_available[frame] != NULL) swapchain->destroy_semaphore(device->device, swapchain->image_available[frame], NULL);
            if (swapchain->render_finished[frame] != NULL) swapchain->destroy_semaphore(device->device, swapchain->render_finished[frame], NULL);
            if (swapchain->fences[frame] != NULL) device->destroy_fence(device->device, swapchain->fences[frame], NULL);
        }
        for (uint32_t frame = 0; frame < 2; ++frame)
            if (swapchain->present_fences[frame] != NULL)
                device->destroy_fence(device->device,
                    swapchain->present_fences[frame], NULL);
        encore_vk_swapchain_release_render_resources(swapchain);
        encore_vk_swapchain_release_readback(swapchain);
        if (swapchain->swapchain != 0) swapchain->destroy_swapchain(device->device, swapchain->swapchain, NULL);
    }
    free(swapchain->images); free(swapchain); return true;
}
bool encore_vk_swapchain_wait(size_t handle) {
    return encore_vk_swapchain_wait_frames(
        (EncoreVkSwapchain *)handle);
}
bool encore_vk_swapchain_last_frame_complete(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL || !swapchain->available || swapchain->owner == NULL)
        return false;
    uint32_t last = (swapchain->frame_index + 1u) % 2u;
    return swapchain->owner->get_fence_status(swapchain->owner->device,
        swapchain->fences[last]) == VK_SUCCESS;
}
/* Used only after a later submission on the same queue has completed. This
   avoids a queue-wide idle wait for every live-resize event while preserving
   the normal conservative destroy API for arbitrary callers. */
bool encore_vk_swapchain_destroy_retired(size_t handle) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL) return false;
    EncoreVkDevice *device = swapchain->owner;
    if (device != NULL && device->device != NULL) {
        encore_timing_target_retired(swapchain);
        encore_completion_target_retired(swapchain);
        if (swapchain->command_buffers[0] != NULL)
            device->free_command_buffers(device->device, device->command_pool,
                2, swapchain->command_buffers);
        for (uint32_t frame = 0; frame < 2; ++frame) {
            if (swapchain->image_available[frame] != NULL)
                swapchain->destroy_semaphore(device->device,
                    swapchain->image_available[frame], NULL);
            if (swapchain->render_finished[frame] != NULL)
                swapchain->destroy_semaphore(device->device,
                    swapchain->render_finished[frame], NULL);
            if (swapchain->fences[frame] != NULL)
                device->destroy_fence(device->device,
                    swapchain->fences[frame], NULL);
        }
        for (uint32_t frame = 0; frame < 2; ++frame)
            if (swapchain->present_fences[frame] != NULL)
                device->destroy_fence(device->device,
                    swapchain->present_fences[frame], NULL);
        encore_vk_swapchain_release_render_resources(swapchain);
        encore_vk_swapchain_release_readback(swapchain);
        if (swapchain->swapchain != 0)
            swapchain->destroy_swapchain(device->device,
                swapchain->swapchain, NULL);
    }
    free(swapchain->images);
    free(swapchain);
    return true;
}
bool encore_vk_swapchain_available(size_t handle) { EncoreVkSwapchain *s = (EncoreVkSwapchain *)handle; return s != NULL && s->available; }
bool encore_vk_swapchain_set_present_mode(size_t handle, bool immediate) {
    EncoreVkSwapchain *swapchain = (EncoreVkSwapchain *)handle;
    if (swapchain == NULL || !swapchain->available || swapchain->offscreen)
        return false;
    uint32_t requested = immediate
        ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    if (swapchain->requested_present_mode != requested) {
        swapchain->requested_present_mode = requested;
        swapchain->needs_rebuild = true;
    }
    return true;
}
encore_str encore_vk_swapchain_error(size_t handle) { EncoreVkSwapchain *s = (EncoreVkSwapchain *)handle; return encore_string(s == NULL ? "Invalid swapchain" : s->error); }
uint32_t encore_vk_swapchain_width(size_t handle) { EncoreVkSwapchain *s = (EncoreVkSwapchain *)handle; return s == NULL ? 0 : s->width; }
uint32_t encore_vk_swapchain_height(size_t handle) { EncoreVkSwapchain *s = (EncoreVkSwapchain *)handle; return s == NULL ? 0 : s->height; }
uint32_t encore_vk_swapchain_frames_in_flight(size_t handle) { return handle == 0 ? 0 : 2; }

uint64_t encore_vk_target_memory_bytes(size_t handle) {
    EncoreVkSwapchain *target=(EncoreVkSwapchain *)handle;
    return target && target->available ? target->attachment_bytes : 0;
}

/* Readiness of the next upload/command slot, rather than completion of the
 * entire previous presented frame. No acquisition or driver wait occurs. */
bool encore_vk_swapchain_frame_ready(size_t handle) {
    EncoreVkSwapchain *s=(EncoreVkSwapchain *)handle;
    if(!s || !s->available || !s->owner || s->frame_active)return false;
    uint32_t slot=s->frame_index;
    if(s->owner->get_fence_status(s->owner->device,s->fences[slot])!=VK_SUCCESS)return false;
    if(!s->offscreen && s->owner->get_fence_status(s->owner->device,s->present_fences[slot])!=VK_SUCCESS)return false;
    return true;
}
