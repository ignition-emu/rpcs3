#!/bin/sh -ex

# Builds the FFmpeg the Linux Ignition embed links statically: ffmpeg-core's
# version and configure options plus .ci/ignition-ffmpeg/extra-options.txt
# (see options.py), LGPL, in the jammy image so it asks no more of glibc than
# the module does. Installs to $1 and checks it with check_ffmpeg.c. Skipped
# when $1 already holds a build (restored from the CI cache).
# Runs from the repository root inside the image.

PREFIX="${1:?install prefix}"
VERSION=8.1.1
# vcpkg 2026.06.01's ports/ffmpeg SHA512 for this tag, the source ffmpeg-core builds.
SHA512=e858e92e5eb08d562302cde371af55917df6e1fe53994e18462a3c929a40ede1828c2bd53c2a7d65a2cfd791782ead3cd94efb2def904f49cb5dd8ab5cd4256f

command -v pkg-config > /dev/null || { apt-get update -q && apt-get install -y -q pkg-config; }

if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
    command -v nasm > /dev/null || { apt-get update -q && apt-get install -y -q nasm; }
    WORK=$(mktemp -d)
    curl -fsSL --retry 3 -o "$WORK/ffmpeg.tar.gz" "https://github.com/FFmpeg/FFmpeg/archive/n$VERSION.tar.gz"
    echo "$SHA512  $WORK/ffmpeg.tar.gz" | sha512sum -c
    tar xzf "$WORK/ffmpeg.tar.gz" -C "$WORK"
    OPTIONS=$(python3 .ci/ignition-ffmpeg/options.py)
    (
        cd "$WORK/FFmpeg-n$VERSION"
        # shellcheck disable=SC2086
        ./configure --prefix="$PREFIX" --cc="${CLANG_BINARY:-cc}" --enable-static --disable-shared \
            --disable-programs --disable-avdevice --disable-avfilter $OPTIONS
        make -j"$(nproc)"
        make install
    )
    rm -rf "$WORK"
fi

PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" "${CLANG_BINARY:-cc}" -o "$PREFIX/check_ffmpeg" .ci/ignition-ffmpeg/check_ffmpeg.c \
    $(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --cflags --static --libs libavformat libavcodec libavutil)
"$PREFIX/check_ffmpeg" .ci/ignition-ffmpeg/required.txt | tee "$PREFIX/check_ffmpeg.txt"
