#!/bin/sh -ex

# Builds the Ignition embed module (rpcs3_ignition) for Linux x86_64 and bundles
# it with its dependencies. Runs inside the rpcs3-ci-jammy image, with
# build-linux.sh's toolchain and options plus WITH_IGNITION, and builds only the
# module: no app, no AppImage. The bundle lands in $ARTDIR.

cd rpcs3 || exit 1

git config --global --add safe.directory '*'

# Same submodule set as build-linux.sh.
# shellcheck disable=SC2046
git submodule -q update --init $(awk '/path/ && !/llvm/ && !/opencv/ && !/libsdl-org/ && !/curl/ && !/zlib/ { print $3 }' .gitmodules)

# FFmpeg: an LGPL static build with the codecs RPCS3 uses (see
# .ci/ignition-ffmpeg), in place of the image's.
FFMPEG_PREFIX=/rpcs3/build-ffmpeg/install
.ci/build-ffmpeg-ignition-linux.sh "$FFMPEG_PREFIX"
# libatomic, which FFmpeg's link line asks for, is taken static like the C++
# runtime, so the host need not have it.
FFMPEG_LIBS=$(PKG_CONFIG_PATH="$FFMPEG_PREFIX/lib/pkgconfig" pkg-config --static --libs \
    libavformat libavcodec libswscale libswresample libavutil | sed 's/-latomic/-l:libatomic.a/g' | tr ' ' ';')

mkdir -p build && cd build || exit 1

export CC="${CLANG_BINARY}"
export CXX="${CLANGXX_BINARY}"
export LINKER=lld
export AR=/usr/bin/llvm-ar-"$LLVMVER"
export RANLIB=/usr/bin/llvm-ranlib-"$LLVMVER"
export LINKER_FLAG="-fuse-ld=${LINKER}"

cmake ..                                               \
    -DCMAKE_INSTALL_PREFIX=/usr                        \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAG}"          \
    -DCMAKE_MODULE_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_SHARED_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_AR="$AR"                                   \
    -DCMAKE_RANLIB="$RANLIB"                           \
    -DUSE_SYSTEM_CURL=ON                               \
    -DUSE_SDL=ON                                       \
    -DUSE_SYSTEM_SDL=ON                                \
    -DUSE_SYSTEM_FFMPEG=ON                             \
    -DFFMPEG_INCLUDE_DIR="$FFMPEG_PREFIX/include"      \
    -DFFMPEG_LIBRARIES="$FFMPEG_LIBS"                  \
    -DUSE_SYSTEM_OPENAL=OFF                            \
    -DUSE_SYSTEM_OPENCV=ON                             \
    -DUSE_DISCORD_RPC=ON                               \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DLLVM_DIR=/opt/llvm/lib/cmake/llvm                \
    -DSTATIC_LINK_LLVM=ON                              \
    -DBUILD_RPCS3_TESTS=OFF                            \
    -DRUN_RPCS3_TESTS=OFF                              \
    -DWITH_IGNITION=ON                                 \
    -G Ninja

ninja rpcs3_ignition

cd ..

command -v patchelf > /dev/null || { apt-get update -q && apt-get install -y -q patchelf; }
command -v python3 > /dev/null || { apt-get update -q && apt-get install -y -q python3; }

# The image's own OpenAL is built against its newer libstdc++; the in-tree one
# (USE_SYSTEM_OPENAL=OFF above) links into the module with the static runtime.
# The limits are Ubuntu 22.04's: glibc 2.35 and GCC 12's libstdc++.
python3 .ci/package-ignition-linux.py build/rpcs3/ignition/librpcs3_ignition.so \
    "${ARTDIR:-/root/artifacts}/bundle" --source-root . \
    --max-glibc 2.35 --max-glibcxx 3.4.30 --ffmpeg-check "$FFMPEG_PREFIX/check_ffmpeg.txt"
