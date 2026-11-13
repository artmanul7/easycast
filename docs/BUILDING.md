# Building EasyCast

EasyCast is cross-compiled on a Linux PC (WSL works) for aarch64 glibc. The release binary is built in two passes
with profile guided optimization (PGO).

## What you need

- Toolchain: Bootlin `aarch64--glibc--stable-2025.08-1` (GCC 14.3), https://toolchains.bootlin.com
- This repository, branch `easycast`, with the submodules of Flycast v2.7 (`git submodule update --init --recursive`)
- From the target system (BananaOS): SDL2 headers and library, libdrm headers, libcurl headers and library.
  The patched SDL2 2.32.10 used by EasyCast is in `easycast-<version>-gpl-sources.tar.gz` of every release.
- GLES/EGL headers (Khronos) for the OpenGL part of Flycast

## Configure

```bash
cmake -S . -B build \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=$TC/bin/aarch64-buildroot-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=$TC/bin/aarch64-buildroot-linux-gnu-g++ \
  -DCMAKE_C_FLAGS="-mtune=cortex-a55 -I$SYSROOT/include -I$SYSROOT/include/libdrm" \
  -DCMAKE_CXX_FLAGS="-mtune=cortex-a55 -I$SYSROOT/include -I$SYSROOT/include/libdrm" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$SYSROOT/lib -Wl,-rpath-link,$SYSROOT/lib" \
  -DCMAKE_BUILD_TYPE=Release -DLIBRETRO=OFF \
  -DUSE_VULKAN=ON -DUSE_OPENGL=ON -DUSE_GLES=ON -DUSE_GLES2=OFF \
  -DUSE_HOST_SDL=ON -DSDL2_DIR=$SYSROOT/cmake-sdl \
  -DCURL_INCLUDE_DIR=$SYSROOT/include -DCURL_LIBRARY=$SYSROOT/lib/libcurl.so \
  -DUSE_ALSA=OFF -DUSE_PULSEAUDIO=OFF -DUSE_OSS=OFF -DUSE_LIBAO=OFF \
  -DUSE_BREAKPAD=OFF -DUSE_DISCORD=OFF -DUSE_LUA=OFF -DUSE_OPENMP=OFF -DLIBUSB_ENABLE_UDEV=OFF
cmake --build build -j$(nproc)
```

Sound goes through SDL. `$TC` is the toolchain folder, `$SYSROOT` a folder with the target headers and libraries.

## Profile guided build (PGO)

1. Configure with `-fprofile-generate -fprofile-update=atomic` added to the C, C++ and linker flags, build.
2. Run that binary on the handheld and play several games for a few minutes (we used Sonic Adventure 2,
   Dead or Alive 2 and Crazy Taxi, about 9 minutes). Set `GCOV_PREFIX` so the `.gcda` files land in a folder you
   can copy back.
3. Copy the `.gcda` files into the same build folder and reconfigure with
   `-fprofile-use -fprofile-partial-training -Wno-missing-profile -fprofile-correction -Wno-error=coverage-mismatch`,
   build again. Use the same build folder in both steps so the object paths match.

## The GPU kernel module

`mali_kbase.ko` is built from [ROCKNIX/mali_kbase](https://github.com/ROCKNIX/mali_kbase) (commit 39da994) with the
ROCKNIX patches against the kernel of the target OS. The release asset `easycast-<version>-gpl-sources.tar.gz`
contains the patched module source, the BananaOS kernel config and the build scripts (`kernel.sh`, `kbase.sh`).
For another OS, build the module against that OS's kernel (same version and config) and replace
`runtime/<version>/mali_kbase.ko`.

## Package

A release archive holds `runtime/<version>/` (flycast, `lib/`, `mali_kbase.ko`, `defaults/`, `licenses/`, `run.py`,
`compat.py`, `easycast_update.py`, `ui.py`, `logcapture.py`, `VERSION`), `tiles/EasyCast.sh` and `media/easycast.png`,
plus a `.sha256` file. The installer and the updater take the newest GitHub release that has such an archive.
