/* Link this consumer against runtime.c: only the public opaque API is visible. */
#include "../vulkan_api.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *encore_str_from_cstr(const char *value) {
    size_t length = strlen(value);
    encore_str_object *s = malloc(sizeof(*s) + length + 1);
    assert(s);
    s->ref_count = 1; s->len = length;
    memcpy(s->data, value, length + 1);
    return s;
}

int main(void) {
    assert(!encore_vk_buffer_map(0, 16));
    assert(!encore_vk_surface_attach(0, 1, 32, 32, NULL, NULL, NULL));
    assert(!encore_vk_frame_scissor(0, 0, 0, 1, 1));
    assert(!encore_vk_frame_sampled_color_barrier(0));
    assert(!encore_vk_texture_borrow(0, 1, 32, 32, 44, 5));
    size_t context = encore_vk_create(false);
    assert(encore_vk_available(context));
    assert(encore_vk_adapter_count(context) > 0);
    size_t device = encore_vk_device_create(context, 0);
    assert(encore_vk_device_available(device));
    size_t buffer = encore_vk_buffer_create(device, 64, 1, true);
    assert(encore_vk_buffer_available(buffer));
    assert(!encore_vk_buffer_map(buffer, 65));
    unsigned char *mapped = encore_vk_buffer_map(buffer, 64);
    assert(mapped); memset(mapped, 42, 64);
    encore_vk_buffer_unmap(buffer);
    mapped = encore_vk_buffer_map(buffer, 64);
    assert(mapped && mapped[63] == 42);
    encore_vk_buffer_unmap(buffer);
    assert(encore_vk_buffer_destroy(buffer));
    /* Single-channel data uploads use their actual texel size. */
    for (int sample_bytes = 1; sample_bytes <= 2; ++sample_bytes) {
        size_t samples = encore_vk_buffer_create(device, 4 * sample_bytes, 1, true);
        assert(encore_vk_buffer_available(samples));
        void *data = encore_vk_buffer_map(samples, 4 * sample_bytes);
        assert(data); memset(data, 127, 4 * sample_bytes);
        encore_vk_buffer_unmap(samples);
        size_t image = encore_vk_texture_create(device, 2, 2, 1,
            sample_bytes == 1 ? 9 : 70, 6);
        size_t upload = encore_vk_encoder_create(device);
        assert(encore_vk_texture_available(image));
        assert(encore_vk_encoder_transition_texture(upload, image, 7));
        assert(encore_vk_encoder_copy_buffer_to_texture(upload, samples, image, 2, 2, 0));
        assert(encore_vk_encoder_transition_texture(upload, image, 5));
        assert(encore_vk_encoder_submit_wait(upload));
        assert(encore_vk_encoder_destroy(upload));
        assert(encore_vk_texture_destroy(image));
        assert(encore_vk_buffer_destroy(samples));
    }
    size_t target = encore_vk_offscreen_create(device, 32, 32);
    assert(encore_vk_swapchain_available(target));
    assert(encore_vk_swapchain_device(target) == device);
    for (int i = 0; i < 5; ++i) {
        assert(encore_vk_swapchain_clear(target, 1, 0, 0, 1));
        assert(encore_vk_swapchain_wait(target));
        unsigned char *pixels = (unsigned char *)encore_vk_swapchain_pixels(target);
        assert(pixels && pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255 && pixels[3] == 255);
    }
    assert(encore_vk_swapchain_width(target) == 32);
    assert(encore_vk_swapchain_destroy(target));
    size_t shared = encore_vk_shared_offscreen_create(device, 32, 32);
    assert(encore_vk_swapchain_available(shared));
    for (int i = 0; i < 5; ++i) {
        assert(encore_vk_swapchain_clear(shared, 1, 1, 1, 1));
        assert(encore_vk_swapchain_wait(shared));
        size_t image = encore_vk_swapchain_external_image(shared);
        assert(image);
        size_t view = encore_vk_texture_borrow(device, image, 32, 32, 44,
            encore_vk_swapchain_external_layout(shared));
        assert(encore_vk_texture_available(view));
        assert(encore_vk_texture_destroy(view));
        /* Destroying a borrowed view must leave the producer image usable. */
        assert(encore_vk_swapchain_available(shared));
    }
    assert(encore_vk_swapchain_destroy(shared));
    assert(encore_vk_device_destroy(device));
    assert(encore_vk_destroy(context));
    puts("Opaque Vulkan API: buffer mapping, offscreen rendering and destruction passed");
}
