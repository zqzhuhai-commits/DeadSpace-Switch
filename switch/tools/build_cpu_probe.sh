#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${DEVKITPRO:?Set DEVKITPRO to devkitPro}"
: "${BOOST_ROOT:?Set BOOST_ROOT to a directory with include/boost (header-only use)}"
dynarmic_dir="$project_root/third_party/dynarmic"
pin=a41c380246d3d9f9874f0f792d234dc0cc17c180
if [[ "$(git -C "$dynarmic_dir" rev-parse HEAD)" != "$pin" ]]; then
  echo 'Unexpected Dynarmic revision; initialize the pinned git submodule.' >&2
  exit 1
fi
patch_path="$project_root/switch/patches/dynarmic-switch-jit.patch"
if git -C "$dynarmic_dir" apply --check "$patch_path" 2>/dev/null; then
  git -C "$dynarmic_dir" apply "$patch_path"
elif ! git -C "$dynarmic_dir" apply --reverse --check "$patch_path" 2>/dev/null; then
  echo 'Dynarmic patch does not match this checkout; refusing a partial patch.' >&2
  exit 1
fi
cpu_build="${CPU_BUILD_DIR:-$project_root/switch/build/cpu}"
cmake -S "$project_root/switch/runtime" -B "$cpu_build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$project_root/switch/runtime/switch-toolchain.cmake" \
  -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT="$BOOST_ROOT" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$cpu_build" --parallel "${BUILD_JOBS:-2}"
# A fresh main.o is needed when enabling the CPU probe compile definition.
make -C "$project_root/switch" -B -j"${BUILD_JOBS:-2}" CPU_PROBE=1 CPU_BUILD_DIR="$cpu_build"
