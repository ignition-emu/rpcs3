#!/bin/sh -ex

# Builds the Ignition embed module (rpcs3_ignition.dll) for Windows x64 under
# MSYS2 clang64, with build-windows-clang.sh's options plus WITH_IGNITION, and
# builds only the module. The DLL and the MSYS2 DLLs it needs are gathered
# flat into $1 (the host loads it with the DLL's own directory searched),
# then zipped beside it with a SHA-256.

OUT="${1:?bundle directory}"

git config --global --add safe.directory '*'

# Same submodule set as build-windows-clang.sh.
# shellcheck disable=SC2046
git submodule -q update --init $(awk '/path/ && !/llvm/ && !/opencv/ && !/ffmpeg/ && !/curl/ && !/FAudio/ && !/zlib/ { print $3 }' .gitmodules)

mkdir -p build && cd build || exit 1

export CC="clang"
export CXX="clang++"
export LINKER=lld
export LINKER_FLAG="-fuse-ld=${LINKER}"
export AR="llvm-ar"
export RANLIB="llvm-ranlib"

cmake ..                                               \
    -DCMAKE_PREFIX_PATH=/clang64                       \
    -DCMAKE_INSTALL_PREFIX=/usr                        \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAG}"          \
    -DCMAKE_MODULE_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_SHARED_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_AR="$AR"                                   \
    -DCMAKE_RANLIB="$RANLIB"                           \
    -DUSE_SYSTEM_CURL=ON                               \
    -DUSE_FAUDIO=OFF                                   \
    -DUSE_SDL=ON                                       \
    -DUSE_SYSTEM_SDL=OFF                               \
    -DUSE_SYSTEM_FFMPEG=ON                             \
    -DUSE_SYSTEM_OPENCV=ON                             \
    -DUSE_SYSTEM_OPENAL=OFF                            \
    -DUSE_DISCORD_RPC=ON                               \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DWITH_LLVM=ON                                     \
    -DLLVM_DIR=/clang64/lib/cmake/llvm                 \
    -DVulkan_LIBRARY=/clang64/lib/libvulkan-1.dll.a    \
    -DSTATIC_LINK_LLVM=ON                              \
    -DBUILD_RPCS3_TESTS=OFF                            \
    -DRUN_RPCS3_TESTS=OFF                              \
    -DWITH_IGNITION=ON                                 \
    -G Ninja

ninja rpcs3_ignition

cd ..

# MinGW names the target librpcs3_ignition.dll; ship it under the name the
# host looks for.
MODULE=$(find build -name 'librpcs3_ignition.dll' -o -name 'rpcs3_ignition.dll' | head -1)
test -n "$MODULE"
mkdir -p "$OUT"
cp "$MODULE" "$OUT/rpcs3_ignition.dll"

# The MSYS2 DLLs it needs, next to it. vulkan-1.dll is the system's loader,
# which finds the system's drivers; the bundle must not carry its own.
cmake -DMSYS2_CLANG_BIN="$(cygpath -w /clang64/bin)" -DMSYS2_USR_BIN="$(cygpath -w /usr/bin)" \
    -Dexe="$OUT/rpcs3_ignition.dll" -P buildfiles/cmake/CopyRuntimeDependencies.cmake
rm -f "$OUT/vulkan-1.dll"

mkdir -p "$OUT/licenses/rpcs3"
cp LICENSE "$OUT/licenses/rpcs3/LICENSE"
git rev-parse HEAD > "$OUT/source_commit.txt"

ARCHIVE="$(dirname "$OUT")/rpcs3_ignition-windows-x64.zip"
(cd "$OUT" && 7z a -tzip -mx9 "$ARCHIVE" ./*)
(cd "$(dirname "$OUT")" && sha256sum "$(basename "$ARCHIVE")" > SHA256SUMS-windows)
ls -l "$OUT"
cat "$(dirname "$OUT")/SHA256SUMS-windows"
