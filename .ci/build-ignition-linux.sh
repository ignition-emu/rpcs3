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

python3 .ci/package-ignition-linux.py build/rpcs3/ignition/librpcs3_ignition.so \
    "${ARTDIR:-/root/artifacts}/bundle" --source-root .
