#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p target
cc -std=c11 tests/native_boundary.c runtime.c -ldl -o target/native-boundary
env -u DISPLAY -u WAYLAND_DISPLAY target/native-boundary
if nm target/native-boundary | grep -E 'graphene_|luma_|SDL_' >/dev/null; then
    echo 'Application-specific symbols leaked into vulkan' >&2
    exit 1
fi
# Luma vendors only the public, opaque header, never private backend layouts.
if test -f ../ui_native/vulkan_api.h; then
    cmp vulkan_api.h ../ui_native/vulkan_api.h
fi

if test -f ../Luma/workspace/ui_native/vulkan_api.h; then
    cmp vulkan_api.h ../Luma/workspace/ui_native/vulkan_api.h
fi
