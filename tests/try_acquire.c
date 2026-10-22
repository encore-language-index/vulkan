/* No driver/window: exercise the real acquisition state machine with WSI mocks. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *value) { return (void *)value; }
static VkResult fence_result, image_result;
static VkResult retired_result = VK_NOT_READY;
static unsigned waits, images, resets;
static VkResult wait_fences(void *d, uint32_t n, void *const *f, uint32_t all, uint64_t timeout) {
    (void)d; (void)n; (void)f; (void)all;
    assert(timeout == UINT64_MAX); ++waits; return VK_SUCCESS;
}
static VkResult fence_status(void *d, void *f) {
    (void)d;
    return f ? retired_result : (fence_result == VK_TIMEOUT ? VK_NOT_READY : fence_result);
}
static VkResult acquire_image(void *d, uint64_t s, uint64_t timeout, void *semaphore, void *fence, uint32_t *index) {
    (void)d; (void)s; (void)semaphore; (void)fence;
    assert(timeout == 0); ++images; *index = 0; return image_result;
}
static VkResult reset_command(void *buffer, uint32_t flags) { (void)buffer; (void)flags; ++resets; return 0; }
static VkResult begin_command(void *buffer, const VkCommandBufferBeginInfo *info) { (void)buffer; (void)info; return 0; }
static VkResult end_command(void *buffer) { (void)buffer; return VK_SUCCESS; }
static VkResult reset_fences(void *d, uint32_t n, void *const *f) { (void)d; (void)n; (void)f; return VK_SUCCESS; }
static VkResult submit(void *q, uint32_t n, const VkSubmitInfo *info, void *f) { (void)q; (void)n; (void)info; (void)f; return VK_SUCCESS; }
static VkResult present(void *q, const VkPresentInfoKHR *info) { (void)q; (void)info; return VK_ERROR_OUT_OF_DATE_KHR; }
static void check(EncoreVkSwapchain *swapchain, uint32_t expected) {
    size_t handle = encore_vk_frame_try_acquire((size_t)swapchain);
    assert(handle && encore_vk_frame_acquire_status(handle) == expected);
    EncoreVkFrame *frame = (EncoreVkFrame *)handle;
    assert(frame->recording == (expected == 1));
    if (expected != 1 && swapchain) assert(!swapchain->frame_active);
    /* This mock has no command queue to submit to. */
    frame->recording = false;
    if (swapchain) swapchain->frame_active = false;
    assert(encore_vk_frame_destroy(handle));
}
int main(void) {
    EncoreVkDevice device = {0};
    device.wait_for_fences = wait_fences; device.get_fence_status = fence_status;
    device.reset_command_buffer = reset_command; device.begin_command_buffer = begin_command;
    EncoreVkSurface surface = {0}; surface.width = 800; surface.height = 600;
    EncoreVkSwapchain swapchain = {0};
    swapchain.available = true; swapchain.owner = &device; swapchain.window = &surface;
    swapchain.width = 800; swapchain.height = 600; swapchain.acquire_next_image = acquire_image;
    check(NULL, 0);
    fence_result = 2; check(&swapchain, 2); assert(images == 0 && resets == 0);
    fence_result = -4; check(&swapchain, 0); assert(images == 0);
    fence_result = 0; image_result = 1; check(&swapchain, 2); assert(resets == 0);
    image_result = 2; check(&swapchain, 2); assert(resets == 0);
    image_result = -4; check(&swapchain, 0); assert(resets == 0);
    image_result = VK_ERROR_OUT_OF_DATE_KHR; check(&swapchain, 2);
    assert(swapchain.needs_rebuild); swapchain.needs_rebuild = false;
    surface.width = 0; check(&swapchain, 3); surface.width = 800;
    image_result = VK_SUCCESS; check(&swapchain, 1); assert(resets == 1);
    image_result = VK_SUBOPTIMAL_KHR; check(&swapchain, 1); assert(resets == 2);
    EncoreVkSwapchain old = {0}; old.owner = &device; old.fences[0] = (void *)1;
    swapchain.retired_next = &old;
    unsigned before = images; check(&swapchain, 2); assert(images == before);
    EncoreVkSwapchain older = old; old.retired_next = &older;
    check(&swapchain, 2); assert(images == before);
    retired_result = -4; check(&swapchain, 0); assert(images == before);
    swapchain.retired_next = NULL;
    assert(waits == 0); /* Nonblocking acquisition must not enter driver waits. */
    fence_result = VK_SUCCESS;
    assert(encore_vk_frame_wait_fence(&device, NULL, false) == VK_SUCCESS && waits == 0);
    fence_result = VK_TIMEOUT;
    assert(encore_vk_frame_wait_fence(&device, NULL, false) == VK_SUCCESS && waits == 1);
    fence_result = -4;
    assert(encore_vk_frame_wait_fence(&device, NULL, false) == -4 && waits == 1);
    /* OUT_OF_DATE after submission is recoverable and must not invalidate
       this frame's owner by rebuilding while the frame is still alive. */
    device.end_command_buffer = end_command; device.queue_submit = submit;
    swapchain.reset_fences = reset_fences; swapchain.queue_present = present;
    EncoreVkFrame frame = {0}; frame.owner = &swapchain; frame.recording = true;
    swapchain.needs_rebuild = false; swapchain.frame_active = true;
    assert(encore_vk_frame_present((size_t)&frame));
    assert(swapchain.needs_rebuild && !swapchain.frame_active && frame.complete && !frame.recording);
    assert(!encore_vk_frame_constants(0, NULL, 33));
    puts("try_acquire: pending, ready, suspended, out-of-date, device loss and retired slots passed");
}
