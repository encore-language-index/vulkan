# vulkan

Standalone Vulkan resource and command API for Encore. No Luma, SDL, window
creation, input handling or UI shaders are included. A Vulkan loader and driver
are required at runtime; the package loads the loader dynamically.

`native_create(false)` creates a headless context. Check `native_available`
and `native_error`, select an adapter, then create a device and resources.
Destroy resources before their device, and devices before their context.
All handles, including failed-but-nonzero creation results, must be destroyed.

For presentation, the application owns its window and event loop:

1. Pass platform extension names to `native_create_with_extensions`.
2. Create a native Vulkan surface using the resulting instance.
3. Wrap it with `native_surface_borrow` and create a presentation device/swapchain.
4. Call `native_surface_resize` with pixel dimensions on resize events.
5. Destroy the swapchain and wrapper before destroying the surface and instance.

The C interface in `vulkan_api.h` uses opaque handles, not backend layouts.
`encore_vk_surface_attach` additionally accepts application-owned resize and
release hooks. Hooks run synchronously on the calling thread; their user data
must remain valid until the wrapper is destroyed. A failed attachment leaves
surface ownership with the caller. Buffer mapping must be paired with unmapping
and externally synchronized with GPU use. Borrowed textures own their image
view but never the external image or memory.

## Migration from 0.0.0

The breaking separation is versioned `0.1.0`. `direct_*` UI helpers, SDL window
creation/input APIs and UI pixel upload are removed. Surface APIs replace the
remaining `native_window_*` operations. The native ABI uses `encore_vk_*` instead
of application-specific prefixes. Luma owns its compositor and SDL adapter.

## Native smoke test

```sh
mkdir -p target
cc -std=c11 tests/native_boundary.c runtime.c -ldl -o target/native-boundary
target/native-boundary
```

This test requires a working Vulkan device, but no display server or SDL.

The Encore example in `examples/offscreen` consumes the published `vulkan`
package. Run `encore run` in that directory; when using a custom registry, set
`ENCORE_INDEX_URL` to its index URL first. It renders without a display server.
