#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p target
glslc --target-env=vulkan1.2 tests/render_targets.vert -o target/render-targets.vert.spv
glslc --target-env=vulkan1.2 tests/render_targets.frag -o target/render-targets.frag.spv
cc -std=c11 tests/render_targets.c runtime.c -ldl -o target/render-targets
target/render-targets target/render-targets.vert.spv target/render-targets.frag.spv
