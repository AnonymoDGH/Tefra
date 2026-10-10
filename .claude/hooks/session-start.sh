#!/bin/bash
# SessionStart hook for Claude Code cloud sessions: installs the toolchain Tefra needs to build and run
# its tests headless (lavapipe software Vulkan + validation layers). Idempotent; no-op outside the cloud.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

PKGS=(
  ninja-build cmake ccache clang-format
  glslang-tools glslc
  mesa-vulkan-drivers vulkan-validationlayers vulkan-tools libvulkan1
  g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev
  libwayland-dev wayland-protocols
)

missing=()
for p in "${PKGS[@]}"; do
  dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p")
done

if [ ${#missing[@]} -gt 0 ]; then
  echo "Installing: ${missing[*]}" >&2
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >&2
  apt-get install -y -qq "${missing[@]}" >&2
fi

# Configure the default debug build so `cmake --build --preset linux-debug` works right away.
cd "${CLAUDE_PROJECT_DIR:-$(pwd)}"
if [ ! -f build/linux-debug/build.ninja ]; then
  cmake --preset linux-debug >&2
fi
