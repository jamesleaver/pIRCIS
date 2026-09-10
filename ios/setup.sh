#!/bin/sh
# Fetch what the iOS build needs and generate the Xcode project.
#
#   ios/setup.sh            then open ios/build/pIRCIS.xcodeproj
#
# The dependencies land in third_party/, shared with the Linux build. Nothing
# here needs an Apple developer account: the simulator runs unsigned builds,
# and Xcode signs a build for your own phone with a free Apple ID.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LGFX_TAG="1.2.28"          # the version the PlatformIO build pins
SDL_TAG="release-2.32.10"

command -v cmake >/dev/null || { echo "cmake not found: brew install cmake"; exit 2; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode not found"; exit 2; }

mkdir -p "$ROOT/third_party"
if [ ! -d "$ROOT/third_party/LovyanGFX/src" ]; then
  echo "==> fetching LovyanGFX $LGFX_TAG"
  git clone -q --depth 1 --branch "$LGFX_TAG" https://github.com/lovyan03/LovyanGFX.git \
      "$ROOT/third_party/LovyanGFX"
fi
if [ ! -d "$ROOT/third_party/SDL/include" ]; then
  echo "==> fetching SDL $SDL_TAG"
  git clone -q --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git \
      "$ROOT/third_party/SDL"
fi

echo "==> generating the Xcode project"
cmake -S "$ROOT/ios" -B "$ROOT/ios/build" -G Xcode -DCMAKE_SYSTEM_NAME=iOS >/dev/null
echo
echo "open $ROOT/ios/build/pIRCIS.xcodeproj"
echo "  scheme pircis, pick a simulator or your phone, press Run"
