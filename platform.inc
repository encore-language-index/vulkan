/* Vulkan loader, externally owned surfaces and adapter discovery. */
static encore_str encore_string(const char *value) {
    encore_str result = {(encore_str_object *)encore_str_from_cstr(value == NULL ? "" : value)};
    return result;
}

static bool encore_vk_trace_enabled(void) { return getenv("ENCORE_VK_TRACE") != NULL; }

static char *encore_c_string(encore_str value) {
    size_t length = value.object == NULL ? 0 : value.object->len;
    char *result = (char *)malloc(length + 1);
    if (result == NULL) return NULL;
    if (length > 0) memcpy(result, value.object->data, length);
    result[length] = '\0';
    return result;
}

static void encore_error(EncoreVkContext *context, const char *value) {
    if (context == NULL) return;
    const char *message = value == NULL ? "unknown Vulkan error" : value;
    size_t length = strlen(message);
    if (length >= sizeof(context->error)) length = sizeof(context->error) - 1;
    memcpy(context->error, message, length);
    context->error[length] = '\0';
}

static uint32_t encore_vk_version(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 22) | (minor << 12) | patch;
}

static void *encore_open_vulkan(void) {
#if defined(_WIN32)
    return (void *)LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
    const char *names[] = {"libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib", NULL};
    for (size_t i = 0; names[i] != NULL; ++i) {
        void *library = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (library != NULL) return library;
    }
    return NULL;
#else
    void *library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    return library != NULL ? library : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *encore_symbol(void *library, const char *name) {
#if defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)library, name);
#else
    return dlsym(library, name);
#endif
}

static void encore_close_vulkan(void *library) {
    if (library == NULL) return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

static bool encore_has_extension(VkPhysicalDevice device,
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions, const char *name) {
    uint32_t count = 0;
    if (enumerate_extensions(device, NULL, &count, NULL) != VK_SUCCESS || count == 0) return false;
    VkExtensionProperties *properties = (VkExtensionProperties *)calloc(count, sizeof(VkExtensionProperties));
    if (properties == NULL) return false;
    VkResult result = enumerate_extensions(device, NULL, &count, properties);
    bool found = false;
    if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(properties[i].extensionName, name) == 0) { found = true; break; }
        }
    }
    free(properties);
    return found;
}

static void encore_read_adapter(EncoreVkAdapter *adapter, VkPhysicalDevice device,
    PFN_vkGetPhysicalDeviceProperties get_properties,
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions) {
    unsigned char properties[1024];
    memset(properties, 0, sizeof(properties));
    get_properties(device, properties);
    adapter->handle = device;
    memcpy(&adapter->api_version, properties + 0, sizeof(uint32_t));
    memcpy(&adapter->vendor_id, properties + 8, sizeof(uint32_t));
    memcpy(&adapter->device_id, properties + 12, sizeof(uint32_t));
    memcpy(&adapter->kind, properties + 16, sizeof(uint32_t));
    memcpy(adapter->name, properties + 20, 255);
    adapter->name[255] = '\0';
    uint32_t major = adapter->api_version >> 22;
    uint32_t minor = (adapter->api_version >> 12) & 0x3ffu;
    adapter->tier = major > 1 || (major == 1 && minor >= 3) ? 1u : 0u;
    adapter->ray_query = encore_has_extension(device, enumerate_extensions, "VK_KHR_ray_query") &&
        encore_has_extension(device, enumerate_extensions, "VK_KHR_acceleration_structure");
    adapter->ray_pipeline = adapter->ray_query &&
        encore_has_extension(device, enumerate_extensions, "VK_KHR_ray_tracing_pipeline");
    adapter->mesh_shader = encore_has_extension(device, enumerate_extensions, "VK_EXT_mesh_shader");
    if (adapter->mesh_shader) adapter->tier = 2u;
    if (adapter->ray_query || adapter->ray_pipeline) adapter->tier = 3u;
}

size_t encore_vk_create_extensions(bool debug, const char *const *extensions, uint32_t extension_count) {
    (void)debug;
    EncoreVkContext *context = (EncoreVkContext *)calloc(1, sizeof(EncoreVkContext));
    if (context == NULL) return 0;
    context->window_support = extension_count > 0;
    if (extension_count > 0 && extensions == NULL) {
        encore_error(context, "Missing instance extensions"); return (size_t)context;
    }
    context->owns_instance = true;
    context->library = encore_open_vulkan();
    if (context->library == NULL) { encore_error(context, "Vulkan loader was not found"); return (size_t)context; }
    context->get_proc = (PFN_vkGetInstanceProcAddr)encore_symbol(context->library, "vkGetInstanceProcAddr");
    if (context->get_proc == NULL) { encore_error(context, "vkGetInstanceProcAddr was not found"); return (size_t)context; }
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)context->get_proc(NULL, "vkEnumerateInstanceVersion");
    context->api_version = encore_vk_version(1, 0, 0);
    if (enumerate_version != NULL) {
        uint32_t loader_version = encore_vk_version(1, 0, 0);
        if (enumerate_version(&loader_version) == VK_SUCCESS) context->api_version = loader_version;
    }
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)context->get_proc(NULL, "vkCreateInstance");
    if (create_instance == NULL) { encore_error(context, "vkCreateInstance was not found"); return (size_t)context; }
    uint32_t requested = context->api_version < encore_vk_version(1, 3, 0)
        ? context->api_version : encore_vk_version(1, 3, 0);
    VkApplicationInfo application = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "Encore", encore_vk_version(0, 1, 0),
        "Encore", encore_vk_version(0, 1, 0), requested
    };
    VkInstanceCreateInfo create_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &application, 0, NULL, extension_count, extensions
    };
    VkResult result = create_instance(&create_info, NULL, &context->instance);
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER && requested > encore_vk_version(1, 0, 0)) {
        application.apiVersion = encore_vk_version(1, 0, 0);
        context->api_version = application.apiVersion;
        result = create_instance(&create_info, NULL, &context->instance);
    }
    if (result != VK_SUCCESS) {
        snprintf(context->error, sizeof(context->error), "Unable to create Vulkan instance (result %d)", result);
        return (size_t)context;
    }
    context->destroy_instance = (PFN_vkDestroyInstance)context->get_proc(context->instance, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate_devices =
        (PFN_vkEnumeratePhysicalDevices)context->get_proc(context->instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_properties =
        (PFN_vkGetPhysicalDeviceProperties)context->get_proc(context->instance, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)context->get_proc(context->instance, "vkEnumerateDeviceExtensionProperties");
    if (enumerate_devices == NULL || get_properties == NULL || enumerate_extensions == NULL) {
        encore_error(context, "Required Vulkan instance functions were not found"); return (size_t)context;
    }
    uint32_t count = 0;
    if (enumerate_devices(context->instance, &count, NULL) != VK_SUCCESS || count == 0) {
        encore_error(context, "No Vulkan physical devices were found"); return (size_t)context;
    }
    if (count > ENCORE_MAX_ADAPTERS) count = ENCORE_MAX_ADAPTERS;
    VkPhysicalDevice devices[ENCORE_MAX_ADAPTERS];
    if (enumerate_devices(context->instance, &count, devices) != VK_SUCCESS) {
        encore_error(context, "Unable to enumerate Vulkan physical devices"); return (size_t)context;
    }
    context->adapter_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        encore_read_adapter(&context->adapters[i], devices[i], get_properties, enumerate_extensions);
    }
    context->available = true;
    return (size_t)context;
}

bool encore_vk_destroy(size_t handle) {
    EncoreVkContext *context = (EncoreVkContext *)handle;
    if (context == NULL) return false;
    if (context->owns_instance && context->instance != NULL && context->destroy_instance != NULL)
        context->destroy_instance(context->instance, NULL);
    encore_close_vulkan(context->library);
    free(context);
    return true;
}

size_t encore_vk_borrow(size_t instance_handle, size_t physical_device_handle) {
    EncoreVkContext *context = (EncoreVkContext *)calloc(1, sizeof(EncoreVkContext));
    if (context == NULL) return 0;
    context->instance = (VkInstance)instance_handle;
    context->library = encore_open_vulkan();
    if (context->instance == NULL || physical_device_handle == 0 || context->library == NULL) {
        encore_error(context, "Invalid borrowed Vulkan instance or physical device");
        return (size_t)context;
    }
    context->get_proc = (PFN_vkGetInstanceProcAddr)encore_symbol(context->library, "vkGetInstanceProcAddr");
    PFN_vkGetPhysicalDeviceProperties get_properties = context->get_proc == NULL ? NULL :
        (PFN_vkGetPhysicalDeviceProperties)context->get_proc(context->instance, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions = context->get_proc == NULL ? NULL :
        (PFN_vkEnumerateDeviceExtensionProperties)context->get_proc(
            context->instance, "vkEnumerateDeviceExtensionProperties");
    if (context->get_proc == NULL || get_properties == NULL || enumerate_extensions == NULL) {
        encore_error(context, "Required borrowed Vulkan instance functions were not found");
        return (size_t)context;
    }
    context->api_version = encore_vk_version(1, 2, 0);
    context->adapter_count = 1;
    encore_read_adapter(&context->adapters[0], (VkPhysicalDevice)physical_device_handle,
        get_properties, enumerate_extensions);
    context->available = true;
    return (size_t)context;
}

size_t encore_vk_create(bool debug) {
    return encore_vk_create_extensions(debug, NULL, 0);
}

/* The caller creates the native surface and supplies optional resize/release
 * hooks. No window toolkit or event loop is loaded by this library. */
size_t encore_vk_surface_attach(size_t context_handle, uint64_t surface,
    uint32_t width, uint32_t height, void *user,
    void (*refresh)(void *, uint32_t *, uint32_t *),
    void (*release)(void *, VkInstance, VkSurfaceKHR)) {
    EncoreVkContext *context = (EncoreVkContext *)context_handle;
    if (context == NULL || !context->available || surface == 0) return 0;
    EncoreVkSurface *window = calloc(1, sizeof(*window));
    if (window == NULL) return 0;
    window->owner = context; window->surface = surface;
    window->width = width; window->height = height;
    window->window = user; window->refresh = refresh; window->release = release;
    window->open = true;
    return (size_t)window;
}
static void encore_vk_surface_refresh(EncoreVkSurface *window) {
    if (window != NULL && window->refresh != NULL)
        window->refresh(window->window, &window->width, &window->height);
}
bool encore_vk_surface_available(size_t handle) {
    EncoreVkSurface *w = (EncoreVkSurface *)handle;
    return w != NULL && w->open && w->surface != 0;
}
encore_str encore_vk_surface_error(size_t handle) {
    EncoreVkSurface *w = (EncoreVkSurface *)handle;
    return encore_string(w == NULL ? "Invalid Vulkan surface" : w->error);
}
uint32_t encore_vk_surface_width(size_t handle) {
    EncoreVkSurface *w = (EncoreVkSurface *)handle;
    return w == NULL ? 0 : w->width;
}
uint32_t encore_vk_surface_height(size_t handle) {
    EncoreVkSurface *w = (EncoreVkSurface *)handle;
    return w == NULL ? 0 : w->height;
}
bool encore_vk_surface_destroy(size_t handle) {
    EncoreVkSurface *w = (EncoreVkSurface *)handle;
    if (w == NULL) return false;
    if (w->release != NULL) w->release(w->window, w->owner->instance, w->surface);
    free(w);
    return true;
}


static EncoreVkAdapter *encore_adapter(size_t handle, size_t index) {
    EncoreVkContext *context = (EncoreVkContext *)handle;
    return context != NULL && index < context->adapter_count ? &context->adapters[index] : NULL;
}

bool encore_vk_available(size_t handle) { EncoreVkContext *c = (EncoreVkContext *)handle; return c != NULL && c->available; }
encore_str encore_vk_error(size_t handle) { EncoreVkContext *c = (EncoreVkContext *)handle; return encore_string(c == NULL ? "Invalid Vulkan context" : c->error); }
uint32_t encore_vk_api_version(size_t handle) { EncoreVkContext *c = (EncoreVkContext *)handle; return c == NULL ? 0 : c->api_version; }
size_t encore_vk_instance(size_t handle) {
    EncoreVkContext *context = (EncoreVkContext *)handle;
    return context == NULL ? 0 : (size_t)(uintptr_t)context->instance;
}
size_t encore_vk_physical_device(size_t handle, size_t index) {
    EncoreVkAdapter *adapter = encore_adapter(handle, index);
    return adapter == NULL ? 0 : (size_t)(uintptr_t)adapter->handle;
}
size_t encore_vk_adapter_count(size_t handle) { EncoreVkContext *c = (EncoreVkContext *)handle; return c == NULL ? 0 : c->adapter_count; }
encore_str encore_vk_adapter_name(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return encore_string(a == NULL ? "" : a->name); }
uint32_t encore_vk_adapter_vendor(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a == NULL ? 0 : a->vendor_id; }
uint32_t encore_vk_adapter_device(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a == NULL ? 0 : a->device_id; }
uint32_t encore_vk_adapter_kind(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a == NULL ? 0 : a->kind; }
uint32_t encore_vk_adapter_api_version(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a == NULL ? 0 : a->api_version; }
uint32_t encore_vk_adapter_tier(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a == NULL ? 0 : a->tier; }
bool encore_vk_adapter_ray_query(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a != NULL && a->ray_query; }
bool encore_vk_adapter_ray_pipeline(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a != NULL && a->ray_pipeline; }
bool encore_vk_adapter_mesh_shader(size_t h, size_t i) { EncoreVkAdapter *a = encore_adapter(h, i); return a != NULL && a->mesh_shader; }
