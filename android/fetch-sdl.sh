#!/bin/sh
# Fetches the SDL3 source used for both the Java glue and the native build, at the release the
# shared engine pins for Android (engine/gt3r/android/fetch-sdl.sh), into android/third_party/SDL.
#   android/fetch-sdl.sh        SDL 3.4 for the phone/tablet APK (third_party/SDL)
#   android/fetch-sdl.sh --xr   SDL's development branch for a Meta Quest APK (third_party/SDL-xr)
set -e
cd "$(dirname "$0")"
engine=../engine/gt3r/android/fetch-sdl.sh
TAG=$(sed -n 's/^TAG=//p' "$engine")
XR_COMMIT=$(sed -n 's/^XR_COMMIT=//p' "$engine")
if [ -z "$TAG" ] || [ -z "$XR_COMMIT" ]; then
  echo "Could not read SDL's version from $engine (git submodule update --init?)" >&2
  exit 1
fi
if [ "$1" = "--xr" ]; then
  if [ ! -d third_party/SDL-xr ]; then
    git init -q third_party/SDL-xr
    git -C third_party/SDL-xr fetch -q --depth 1 https://github.com/libsdl-org/SDL.git "$XR_COMMIT"
    git -C third_party/SDL-xr checkout -q FETCH_HEAD
  fi
  exit 0
fi
if [ ! -d third_party/SDL ]; then
  git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git third_party/SDL
fi
