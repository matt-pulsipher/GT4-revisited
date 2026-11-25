# The Linux build environment for GT4 Revisited (tools/linux/build.sh builds this image).
# BASE: ubuntu:24.04 (GCC 13, Clang 18) by default; ubuntu:22.04 (GCC 11, Clang 14) for an older glibc.
# The package list is the one the shared engine's Linux build uses for SDL's backends
# (engine/gt3r/docs/dev/PLATFORMS.md, "Linux").
ARG BASE=ubuntu:24.04
FROM ${BASE}
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential clang lld git ca-certificates cmake ninja-build pkg-config file \
      libasound2-dev libpulse-dev libpipewire-0.3-dev \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
      libwayland-dev libdecor-0-dev libxkbcommon-dev libegl1-mesa-dev libdrm-dev libgbm-dev \
      libudev-dev libdbus-1-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
