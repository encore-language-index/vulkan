/* Public opaque C interface for vulkan 0.1.0. No backend layouts exposed. */
#ifndef ENCORE_VULKAN_API_H
#define ENCORE_VULKAN_API_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifndef ENCORE_NATIVE_STRING_ABI
#define ENCORE_NATIVE_STRING_ABI
typedef struct { size_t ref_count; size_t len; char data[]; } encore_str_object;
typedef struct { encore_str_object *object; } encore_str;
#endif
extern void *encore_str_from_cstr(const char *);
size_t encore_vk_create_named_extensions(bool debug, const encore_str *names, size_t count);
size_t encore_vk_surface_borrow(size_t context, uint64_t surface, uint32_t width, uint32_t height);
bool encore_vk_surface_resize(size_t handle, uint32_t width, uint32_t height);
typedef void EncoreVkBuffer;
typedef void EncoreVkSubmission;
typedef void EncoreVkTexture;
typedef void EncoreVkBindGroup;
typedef void EncoreVkDevice;
typedef void EncoreVkSwapchain;
typedef void EncoreVkShader;
typedef void EncoreVkBindGroupLayout;
typedef void EncoreVkSampler;
typedef void EncoreVkPipeline;
typedef void EncoreVkEncoder;
typedef void EncoreVkFrame;
enum {
    VK_SUCCESS = 0,
    VK_INCOMPLETE = 5,
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18,
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 19,
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 20,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO = 25,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26,
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO = 27,
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28,
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO = 29,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO = 31,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 37,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO = 38,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO = 43,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER = 45,
    VK_STRUCTURE_TYPE_MEMORY_BARRIER = 46,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 = 1000059000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES = 51,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES = 53,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR = 1000150013,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT = 1000328000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR = 1000348013,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR = 1000203000,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO = 1000060000,
    VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO = 1000244001,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR = 1000150000,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR = 1000150002,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR = 1000150004,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR = 1000150005,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR = 1000150006,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR = 1000150017,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR = 1000150020,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR = 1000150007,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT = 1000275000,
    VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT = 1000275001,
    VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
    VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
    ENCORE_MAX_ADAPTERS = 16
};
enum {
    ENCORE_GPU_FEATURE_DESCRIPTOR_INDEXING = 1u << 0,
    ENCORE_GPU_FEATURE_BUFFER_DEVICE_ADDRESS = 1u << 1,
    ENCORE_GPU_FEATURE_TIMELINE_SEMAPHORE = 1u << 2,
    ENCORE_GPU_FEATURE_SYNCHRONIZATION_2 = 1u << 3,
    ENCORE_GPU_FEATURE_DYNAMIC_RENDERING = 1u << 4,
    ENCORE_GPU_FEATURE_MESH_SHADER = 1u << 5,
    ENCORE_GPU_FEATURE_RAY_QUERY = 1u << 6,
    ENCORE_GPU_FEATURE_DRAW_INDIRECT_COUNT = 1u << 7,
    ENCORE_GPU_FEATURE_SHADER_INT64_ATOMICS = 1u << 8,
    ENCORE_GPU_FEATURE_FRAGMENT_SHADER_BARYCENTRIC = 1u << 9
};
enum {
    VK_SUBOPTIMAL_KHR = 1000001003,
    VK_ERROR_INCOMPATIBLE_DRIVER = -9,
    VK_ERROR_OUT_OF_DATE_KHR = -1000001004,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_FORMAT_B8G8R8A8_SRGB = 50,
    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0,
    VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    VK_PRESENT_MODE_MAILBOX_KHR = 1,
    VK_PRESENT_MODE_FIFO_KHR = 2,
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR = 1,
    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR = 1,
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 1,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT = 2,
    VK_IMAGE_USAGE_SAMPLED_BIT = 4,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 16,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002
};
enum {
    VK_SHADER_STAGE_VERTEX_BIT = 1,
    VK_SHADER_STAGE_FRAGMENT_BIT = 16,
    VK_SHADER_STAGE_COMPUTE_BIT = 32,
    VK_SHADER_STAGE_TASK_BIT_EXT = 64,
    VK_SHADER_STAGE_MESH_BIT_EXT = 128,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3,
    VK_POLYGON_MODE_FILL = 0,
    VK_CULL_MODE_NONE = 0,
    VK_CULL_MODE_FRONT_BIT = 1,
    VK_CULL_MODE_BACK_BIT = 2,
    VK_FRONT_FACE_COUNTER_CLOCKWISE = 0,
    VK_FRONT_FACE_CLOCKWISE = 1,
    VK_BLEND_FACTOR_ZERO = 0,
    VK_BLEND_FACTOR_ONE = 1,
    VK_BLEND_FACTOR_SRC_ALPHA = 6,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7,
    VK_BLEND_OP_ADD = 0,
    VK_SAMPLE_COUNT_1_BIT = 1,
    VK_COMPARE_OP_LESS = 1,
    VK_COLOR_COMPONENT_RGBA_BITS = 15,
    VK_ATTACHMENT_LOAD_OP_LOAD = 0,
    VK_ATTACHMENT_LOAD_OP_CLEAR = 1,
    VK_ATTACHMENT_STORE_OP_STORE = 0,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2,
    VK_ATTACHMENT_STORE_OP_DONT_CARE = 1,
    VK_PIPELINE_BIND_POINT_GRAPHICS = 0,
    VK_PIPELINE_BIND_POINT_COMPUTE = 1,
    VK_INDEX_TYPE_UINT32 = 1,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7,
    VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR = 1000150000,
    VK_SUBPASS_EXTERNAL = UINT32_MAX,
    VK_SUBPASS_CONTENTS_INLINE = 0
};
enum { VK_DYNAMIC_STATE_VIEWPORT = 0, VK_DYNAMIC_STATE_SCISSOR = 1 };
enum { VK_FILTER_LINEAR = 1, VK_SAMPLER_MIPMAP_MODE_LINEAR = 1,
    VK_SAMPLER_ADDRESS_MODE_REPEAT = 0, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2 };
enum {
    VK_IMAGE_LAYOUT_UNDEFINED = 0,
    VK_IMAGE_LAYOUT_GENERAL = 1,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 5,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 7,
    VK_IMAGE_ASPECT_COLOR_BIT = 1,
    VK_IMAGE_ASPECT_DEPTH_BIT = 2,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT = 1,
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT = 4,
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT = 256,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT = 128,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT = 2048,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 1024,
    VK_PIPELINE_STAGE_TRANSFER_BIT = 4096,
    VK_PIPELINE_STAGE_HOST_BIT = 16384,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT = 65536,
    VK_ACCESS_INDIRECT_COMMAND_READ_BIT = 1,
    VK_ACCESS_SHADER_READ_BIT = 32,
    VK_ACCESS_INDEX_READ_BIT = 2,
    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT = 4,
    VK_ACCESS_SHADER_WRITE_BIT = 64,
    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT = 128,
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT = 256,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT = 1024,
    VK_ACCESS_TRANSFER_READ_BIT = 2048,
    VK_ACCESS_TRANSFER_WRITE_BIT = 4096,
    VK_ACCESS_HOST_READ_BIT = 8192,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 1,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT = 2,
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT = 256,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 32,
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT = 0x00020000,
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR = 0x00080000,
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR = 0x00100000,
    VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT = 2,
    VK_FORMAT_R32G32B32_SFLOAT = 106,
    VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR = 0,
    VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR = 1,
    VK_GEOMETRY_TYPE_TRIANGLES_KHR = 0,
    VK_GEOMETRY_TYPE_INSTANCES_KHR = 2,
    VK_GEOMETRY_OPAQUE_BIT_KHR = 1,
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR = 4,
    VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR = 0,
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR = 1,
    VK_QUEUE_FAMILY_IGNORED = UINT32_MAX
};
enum {
    VK_QUEUE_GRAPHICS_BIT = 1,
    VK_QUEUE_COMPUTE_BIT = 2,
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 2,
    VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0,
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 1,
    VK_FENCE_CREATE_SIGNALED_BIT = 1
};
enum {
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 1,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 2,
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 4
};
size_t encore_vk_create_extensions(bool debug, const char *const *extensions, uint32_t extension_count);
bool encore_vk_destroy(size_t handle);
size_t encore_vk_borrow(size_t instance_handle, size_t physical_device_handle);
size_t encore_vk_create(bool debug);
size_t encore_vk_surface_attach(size_t context_handle, uint64_t surface,
    uint32_t width, uint32_t height, void *user,
    void (*refresh)(void *, uint32_t *, uint32_t *),
    void (*release)(void *, void *, uint64_t));
bool encore_vk_surface_available(size_t handle);
encore_str encore_vk_surface_error(size_t handle);
uint32_t encore_vk_surface_width(size_t handle);
uint32_t encore_vk_surface_height(size_t handle);
bool encore_vk_surface_destroy(size_t handle);
bool encore_vk_available(size_t handle);
encore_str encore_vk_error(size_t handle);
uint32_t encore_vk_api_version(size_t handle);
size_t encore_vk_instance(size_t handle);
size_t encore_vk_physical_device(size_t handle, size_t index);
size_t encore_vk_adapter_count(size_t handle);
encore_str encore_vk_adapter_name(size_t h, size_t i);
uint32_t encore_vk_adapter_vendor(size_t h, size_t i);
uint32_t encore_vk_adapter_device(size_t h, size_t i);
uint32_t encore_vk_adapter_kind(size_t h, size_t i);
uint32_t encore_vk_adapter_api_version(size_t h, size_t i);
uint32_t encore_vk_adapter_tier(size_t h, size_t i);
bool encore_vk_adapter_ray_query(size_t h, size_t i);
bool encore_vk_adapter_ray_pipeline(size_t h, size_t i);
bool encore_vk_adapter_mesh_shader(size_t h, size_t i);
size_t encore_vk_device_create(size_t context_handle, size_t adapter_index);
size_t encore_vk_device_create_features(size_t context_handle, size_t adapter_index,
    uint32_t required_features, uint32_t optional_features);
size_t encore_vk_present_device_create(size_t context_handle, size_t adapter_index, size_t window_handle);
size_t encore_vk_present_device_create_features(size_t context_handle,
    size_t adapter_index, size_t window_handle, uint32_t required_features,
    uint32_t optional_features);
size_t encore_vk_device_borrow(size_t context_handle, size_t device_handle, uint32_t queue_family);
bool encore_vk_device_destroy(size_t handle);
bool encore_vk_device_available(size_t handle);
bool encore_vk_device_independent_blend(size_t handle);
encore_str encore_vk_device_error(size_t handle);
uint32_t encore_vk_device_queue_family(size_t handle);
size_t encore_vk_device_handle(size_t handle);
bool encore_vk_device_graphics(size_t handle);
bool encore_vk_device_compute(size_t handle);
uint32_t encore_vk_device_requested_features(size_t handle);
uint32_t encore_vk_device_enabled_features(size_t handle);
size_t encore_vk_shader_create(size_t device_handle,
    const uint32_t *words, size_t word_count, uint32_t stage);
bool encore_vk_shader_available(size_t handle);
encore_str encore_vk_shader_error(size_t handle);
uint32_t encore_vk_shader_stage(size_t handle);
bool encore_vk_shader_destroy(size_t handle);
size_t encore_vk_bind_group_layout_create_entries(size_t device_handle,
    const uint32_t *bindings, size_t binding_count,
    const uint32_t *types, size_t type_count,
    const uint32_t *stages, size_t stage_count);
size_t encore_vk_bind_group_layout_create(size_t device_handle, uint32_t binding, uint32_t stage);
bool encore_vk_bind_group_layout_available(size_t handle);
encore_str encore_vk_bind_group_layout_error(size_t handle);
bool encore_vk_bind_group_layout_destroy(size_t handle);
size_t encore_vk_bind_group_create(size_t device_handle, size_t layout_handle, size_t buffer_handle,
    uint64_t offset, uint64_t range);
size_t encore_vk_storage_bind_group_create(size_t device_handle, size_t layout_handle, size_t buffer_handle,
    uint64_t offset, uint64_t range);
size_t encore_vk_storage_bind_group_create_many(size_t device_handle, size_t layout_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count);
size_t encore_vk_buffer_bind_group_create_many(size_t device_handle,
    size_t layout_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count);
size_t encore_vk_buffer_texture_bind_group_create_many(size_t device_handle,
    size_t layout_handle, const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count, uint32_t texture_binding,
    size_t texture_handle, size_t sampler_handle);
size_t encore_vk_buffer_textures_bind_group_create_many(size_t device_handle,
    size_t layout_handle, const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count,
    const uint32_t *texture_bindings, size_t texture_binding_count,
    const size_t *texture_handles, size_t texture_count,
    const size_t *sampler_handles, size_t sampler_count);
size_t encore_vk_ray_query_bind_group_create(size_t device_handle,
    size_t layout_handle, uint32_t acceleration_binding,
    size_t acceleration_handle,
    const uint32_t *bindings, size_t binding_count,
    const size_t *buffer_handles, size_t buffer_count,
    const uint64_t *offsets, size_t offset_count,
    const uint64_t *ranges, size_t range_count);
size_t encore_vk_compute_pipeline_create(size_t device_handle, size_t shader_handle, size_t layout_handle);
bool encore_vk_compute_pipeline_available(size_t handle);
encore_str encore_vk_compute_pipeline_error(size_t handle);
bool encore_vk_compute_pipeline_destroy(size_t handle);
bool encore_vk_bind_group_available(size_t handle);
encore_str encore_vk_bind_group_error(size_t handle);
bool encore_vk_bind_group_destroy(size_t handle);
size_t encore_vk_texture_bind_group_create(size_t device_handle, size_t layout_handle,
    size_t texture_handle, size_t sampler_handle);
size_t encore_vk_sampler_create(size_t device_handle);
size_t encore_vk_sampler_create_repeat(size_t device_handle);
bool encore_vk_sampler_available(size_t handle);
encore_str encore_vk_sampler_error(size_t handle);
bool encore_vk_sampler_destroy(size_t handle);
bool encore_vk_bind_group_update_buffer(size_t bind_group_handle,
    uint32_t binding, size_t buffer_handle, uint64_t offset, uint64_t range);
size_t encore_vk_material_bind_group_create(size_t device_handle, size_t layout_handle,
    uint32_t uniform_binding, size_t buffer_handle, uint64_t offset, uint64_t range,
    uint32_t texture_binding, size_t texture_handle, size_t sampler_handle);
size_t encore_vk_scene_bind_group_create(size_t device_handle, size_t layout_handle,
    uint32_t uniform_binding, size_t uniform_handle, uint64_t uniform_range,
    uint32_t storage_binding, size_t storage_handle, uint64_t storage_range);
size_t encore_vk_pipeline_create(size_t device_handle, size_t swapchain_handle, size_t vertex_handle, size_t fragment_handle,
    size_t bind_group_layout_handle, bool requested_depth_attachment,
    bool depth_test, bool depth_write, uint32_t cull_mode, bool alpha_blend,
    bool additive_blend, bool load_color,
    uint32_t vertex_stride,
    const uint32_t *locations, size_t location_count,
    const uint32_t *formats, size_t format_count,
    const uint32_t *offsets, size_t offset_count);
bool encore_vk_pipeline_available(size_t handle);
encore_str encore_vk_pipeline_error(size_t handle);
bool encore_vk_pipeline_destroy(size_t handle);
size_t encore_vk_swapchain_create(size_t device_handle, size_t window_handle);
size_t encore_vk_offscreen_create(size_t device_handle, uint32_t width, uint32_t height);
size_t encore_vk_shared_offscreen_create(size_t device_handle, uint32_t width, uint32_t height);
void encore_vk_frame_timing_mark(size_t frame_handle, uint32_t label, bool begin);
size_t encore_vk_frame_acquire(size_t swapchain_handle);
/* Try-acquire never waits for a fence/image. Destroy every returned handle,
   including pending/suspended/error handles. Only READY owns an image. */
enum { ENCORE_VK_FRAME_FAILED = 0, ENCORE_VK_FRAME_READY = 1,
    ENCORE_VK_FRAME_PENDING = 2, ENCORE_VK_FRAME_SUSPENDED = 3 };
size_t encore_vk_frame_try_acquire(size_t swapchain_handle);
uint32_t encore_vk_frame_acquire_status(size_t handle);
/* Graphics pipelines expose a compatible 128-byte push-constant range. */
bool encore_vk_frame_constants(size_t handle, const float *values, size_t count);
bool encore_vk_frame_available(size_t handle);
uint32_t encore_vk_frame_slot(size_t handle);
encore_str encore_vk_frame_error(size_t handle);
bool encore_vk_frame_dispatch_compute(size_t frame_handle,
    size_t pipeline_handle, size_t bind_group_handle,
    uint32_t x, uint32_t y, uint32_t z);
bool encore_vk_frame_compute_barrier(size_t frame_handle);
bool encore_vk_frame_fill_buffer(size_t frame_handle, size_t buffer_handle,
    uint64_t offset, uint64_t size, uint32_t value);
bool encore_vk_frame_transfer_storage_barrier(size_t frame_handle);
bool encore_vk_frame_fragment_storage_barrier(size_t frame_handle);
bool encore_vk_frame_copy_buffer(size_t frame_handle,
    size_t source_handle, size_t destination_handle,
    uint64_t source_offset, uint64_t destination_offset, uint64_t size);
bool encore_vk_frame_begin_render_pass(size_t frame_handle, size_t pipeline_handle,
    float red, float green, float blue, float alpha);
bool encore_vk_frame_bind_group(size_t frame_handle, size_t bind_group_handle);
bool encore_vk_frame_bind_pipeline(size_t frame_handle, size_t pipeline_handle);
bool encore_vk_frame_bind_vertex_buffer(size_t frame_handle, size_t buffer_handle);
bool encore_vk_frame_bind_index_buffer(size_t frame_handle, size_t buffer_handle);
bool encore_vk_frame_draw_indexed(size_t frame_handle, uint32_t index_count,
    uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
bool encore_vk_frame_draw_indexed_indirect_count(size_t frame_handle,
    size_t commands_handle, uint64_t commands_offset, size_t count_handle,
    uint64_t count_offset, uint32_t maximum_draw_count, uint32_t stride);
bool encore_vk_frame_draw_mesh_tasks(size_t frame_handle,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
bool encore_vk_frame_draw_mesh_tasks_indirect(size_t frame_handle,
    size_t commands_handle, uint64_t offset);
bool encore_vk_frame_end_render_pass(size_t frame_handle);
bool encore_vk_frame_set_viewport(size_t frame_handle, uint32_t x, uint32_t y,
    uint32_t width, uint32_t height);

bool encore_vk_frame_present(size_t frame_handle);
size_t encore_vk_swapchain_pixels(size_t handle);
uint32_t encore_vk_swapchain_pitch(size_t handle);
size_t encore_vk_swapchain_external_image(size_t handle);
uint32_t encore_vk_swapchain_external_layout(size_t handle);
bool encore_vk_frame_destroy(size_t handle);
bool encore_vk_swapchain_clear(size_t handle, float red, float green, float blue, float alpha);
bool encore_vk_swapchain_draw_pipeline(size_t handle, size_t pipeline_handle,
    float red, float green, float blue, float alpha);
bool encore_vk_swapchain_draw_indexed(size_t handle, size_t pipeline_handle, size_t vertex_handle,
    size_t index_handle, uint32_t index_count, float red, float green, float blue, float alpha);
bool encore_vk_swapchain_draw_indexed_bound(size_t handle, size_t pipeline_handle, size_t vertex_handle,
    size_t index_handle, size_t bind_group_handle, uint32_t index_count,
    float red, float green, float blue, float alpha);
bool encore_vk_swapchain_destroy(size_t handle);
bool encore_vk_swapchain_wait(size_t handle);
bool encore_vk_swapchain_last_frame_complete(size_t handle);
bool encore_vk_swapchain_destroy_retired(size_t handle);
bool encore_vk_swapchain_available(size_t handle);
bool encore_vk_swapchain_set_present_mode(size_t handle, bool immediate);
encore_str encore_vk_swapchain_error(size_t handle);
uint32_t encore_vk_swapchain_width(size_t handle);
uint32_t encore_vk_swapchain_height(size_t handle);
uint32_t encore_vk_swapchain_frames_in_flight(size_t handle);
size_t encore_vk_encoder_create(size_t device_handle);
bool encore_vk_encoder_available(size_t handle);
encore_str encore_vk_encoder_error(size_t handle);
bool encore_vk_encoder_bind_compute_pipeline(size_t encoder_handle, size_t pipeline_handle);
bool encore_vk_encoder_bind_compute_group(size_t encoder_handle, size_t pipeline_handle, size_t group_handle);
bool encore_vk_encoder_dispatch(size_t handle, uint32_t x, uint32_t y, uint32_t z);
bool encore_vk_encoder_storage_host_barrier(size_t handle);
bool encore_vk_encoder_transfer_host_barrier(size_t handle);
bool encore_vk_encoder_transfer_geometry_barrier(size_t handle);
bool encore_vk_encoder_copy_buffer(size_t encoder_handle, size_t source_handle, size_t destination_handle,
    uint64_t source_offset, uint64_t destination_offset, uint64_t size);
bool encore_vk_encoder_submit_wait(size_t handle);
size_t encore_vk_encoder_submit(size_t handle);
bool encore_vk_submission_complete(size_t handle);
bool encore_vk_submission_available(size_t handle);
bool encore_vk_submission_wait(size_t handle, uint64_t timeout_ns);
encore_str encore_vk_submission_error(size_t handle);
bool encore_vk_submission_destroy(size_t handle);
bool encore_vk_encoder_destroy(size_t handle);
size_t encore_vk_buffer_create(size_t device_handle, uint64_t size, uint32_t usage, bool host_visible);
bool encore_vk_buffer_available(size_t handle);
encore_str encore_vk_buffer_error(size_t handle);
uint64_t encore_vk_buffer_size(size_t handle);
bool encore_vk_buffer_host_visible(size_t handle);
bool encore_vk_buffer_write_u32(size_t handle, uint64_t offset, uint32_t value);
bool encore_vk_buffer_write_f32(size_t handle, uint64_t offset, float value);
bool encore_vk_buffer_write_u32s(size_t handle, uint64_t offset,
    const uint32_t *values, size_t count);
bool encore_vk_buffer_write_f32s(size_t handle, uint64_t offset,
    const float *values, size_t count);
uint32_t encore_vk_buffer_read_u32(size_t handle, uint64_t offset);
float encore_vk_buffer_read_f32(size_t handle, uint64_t offset);
bool encore_vk_buffer_read_f32s(size_t handle, uint64_t offset, float *values, size_t count);
bool encore_vk_buffer_destroy(size_t handle);
size_t encore_vk_blas_create_triangles(size_t device_handle,
    size_t vertex_handle, uint64_t vertex_stride, uint32_t vertex_count,
    size_t index_handle, uint32_t index_count);
size_t encore_vk_tlas_create_instances(size_t device_handle,
    const size_t *blas_handles, size_t blas_count,
    const float *transforms, size_t transform_count);
bool encore_vk_acceleration_structure_available(size_t handle);
encore_str encore_vk_acceleration_structure_error(size_t handle);
bool encore_vk_acceleration_structure_destroy(size_t handle);
size_t encore_vk_texture_create(size_t device_handle, uint32_t width, uint32_t height,
    uint32_t mip_levels, uint32_t format, uint32_t usage);
bool encore_vk_texture_available(size_t handle);
encore_str encore_vk_texture_error(size_t handle);
uint32_t encore_vk_texture_width(size_t handle);
uint32_t encore_vk_texture_height(size_t handle);
uint32_t encore_vk_texture_mip_levels(size_t handle);
uint32_t encore_vk_texture_format(size_t handle);
uint32_t encore_vk_texture_layout(size_t handle);
bool encore_vk_texture_destroy(size_t handle);
bool encore_vk_encoder_transition_texture(size_t encoder_handle, size_t texture_handle, uint32_t new_layout);
bool encore_vk_encoder_copy_buffer_to_texture(size_t encoder_handle, size_t buffer_handle,
    size_t texture_handle, uint32_t width, uint32_t height, uint32_t mip_level);
bool encore_vk_encoder_copy_buffer_to_texture_region(size_t encoder_handle,
    size_t buffer_handle, size_t texture_handle, uint32_t width,
    uint32_t height, uint32_t mip_level, uint32_t x, uint32_t y);
void *encore_vk_buffer_map(size_t handle, uint64_t bytes);
void encore_vk_buffer_unmap(size_t handle);
size_t encore_vk_swapchain_device(size_t handle);
bool encore_vk_swapchain_prepare(size_t handle,size_t pipeline);
const char *encore_vk_swapchain_error_cstr(size_t handle);
const char *encore_vk_pipeline_error_cstr(size_t handle);
const char *encore_vk_frame_error_cstr(size_t handle);
size_t encore_vk_frame_swapchain(size_t handle);
bool encore_vk_frame_in_pass(size_t handle);
bool encore_vk_swapchain_present_mode(size_t handle,uint32_t mode);
bool encore_vk_frame_scissor(size_t handle,int32_t x,int32_t y,uint32_t width,uint32_t height);
size_t encore_vk_texture_borrow(size_t device,uint64_t image,uint32_t width,uint32_t height,uint32_t format,uint32_t layout);
bool encore_vk_frame_sampled_color_barrier(size_t frame_handle);

/* Configurable offscreen attachments. Color output is always resolved. */
uint32_t encore_vk_target_sample_counts(size_t device, uint32_t format);
size_t encore_vk_render_target_create(size_t device, uint32_t width, uint32_t height, uint32_t format, uint32_t samples, uint32_t auxiliary_format, bool sampled_depth);
size_t encore_vk_target_attachment(size_t target, uint32_t attachment);
uint64_t encore_vk_target_render_signature(size_t target);

uint64_t encore_vk_target_memory_bytes(size_t target);
size_t encore_vk_target_readback(size_t target, uint32_t attachment, uintptr_t destination, size_t capacity);
/* Optional native packet executor; configure before logical-device creation. */
bool encore_vk_parallel_configure(uint32_t workers);
uint64_t encore_vk_parallel_stat(size_t device, uint32_t index);
void encore_vk_parallel_frame_begin(size_t device, uint64_t frame_id);
uint64_t encore_vk_parallel_frame_value(size_t device, uint64_t frame_id,
    uint32_t thread, uint32_t field);
bool encore_vk_swapchain_frame_ready(size_t swapchain);
bool encore_vk_frame_update_f32s(size_t frame, size_t buffer, uint64_t offset,
    const float *values, size_t count);
bool encore_vk_frame_dispatch_compute_indirect(size_t frame, size_t pipeline,
    size_t group, size_t buffer, uint64_t offset);

/* Caller owns every returned/clone reference and releases it exactly once.
 * Status: 0 invalid, 1 pending, 2 complete, 3 never submitted, 4 polling fault.
 * Polling faults do not authorize resource reclamation. Tokens outlive targets.
 */
size_t encore_vk_frame_completion(size_t frame);
size_t encore_vk_completion_clone(size_t completion);
bool encore_vk_completion_destroy(size_t completion);
uint32_t encore_vk_completion_status(size_t completion);
bool encore_vk_completion_submitted(size_t completion);

bool encore_vk_frame_update_u32s(size_t frame,size_t buffer,uint64_t offset,const uint32_t *values,size_t count);

/* Optional temporal SDK runtime. Capability bits: FSR, DLSS, DLAA, FSR FG,
 * DLSS FG, NVIDIA 3x, NVIDIA 4x. Absent runtime returns zero. */
uint32_t encore_vk_reconstruction_capabilities(size_t device);
uint64_t encore_vk_reconstruction_resolution(size_t device,uint32_t method,uint32_t width,uint32_t height,uint32_t quality);
size_t encore_vk_reconstruction_create(size_t device,uint32_t method,uint32_t iw,uint32_t ih,uint32_t ow,uint32_t oh,uint32_t quality);
bool encore_vk_reconstruction_available(size_t context);
encore_str encore_vk_reconstruction_error(size_t context);
bool encore_vk_reconstruction_record(size_t context,size_t frame,size_t source,size_t output,const float *values,size_t count);
bool encore_vk_reconstruction_record_images(size_t context,size_t frame,size_t source,size_t output,size_t color,size_t reactive,size_t composition,const float *values,size_t count);
bool encore_vk_reconstruction_destroy(size_t context);
size_t encore_vk_texture_image(size_t texture);
size_t encore_vk_texture_readback(size_t texture,uintptr_t destination,size_t capacity);
void encore_vk_presentation_yield(void);

#endif

/* Records an on-demand frame color copy; returns the Vulkan color format. */
uint32_t encore_vk_frame_copy_color(size_t frame, size_t buffer);
