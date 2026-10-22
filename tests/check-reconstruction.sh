#!/bin/sh
# Usage: tests/check-reconstruction.sh /path/to/runtime/libencore_reconstruction.so
set -eu
cd "$(dirname "$0")/.."
: "${1:?Pass the built reconstruction plugin path}"
export ENCORE_RECONSTRUCTION_LIBRARY="$1"
mkdir -p target/reconstruction-tests
glslc --target-env=vulkan1.2 tests/reconstruction.vert -o target/reconstruction-tests/input.vert.spv
glslc --target-env=vulkan1.2 tests/reconstruction.frag -o target/reconstruction-tests/input.frag.spv
c++ -std=c++17 -O2 tests/reconstruction_camera.cpp -o target/reconstruction-tests/camera
target/reconstruction-tests/camera
cc -std=c11 -O2 tests/reconstruction.c runtime.c -ldl -pthread -lm -o target/reconstruction-tests/gpu
target/reconstruction-tests/gpu target/reconstruction-tests/input.vert.spv target/reconstruction-tests/input.frag.spv 0
target/reconstruction-tests/gpu target/reconstruction-tests/input.vert.spv target/reconstruction-tests/input.frag.spv 2
