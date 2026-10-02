#!/usr/bin/env python3
"""Print the FFmpeg configure options for the Ignition embed: ffmpeg-core's
(the vcpkg port's base options and the lines its ffmpeg.patch appends,
3rdparty/ffmpeg) plus extra-options.txt. One list for Linux and Windows."""
import re
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
patch = (here.parent.parent / "3rdparty/ffmpeg/ffmpeg.patch").read_text()
options = ["--enable-pic", "--disable-doc", "--enable-runtime-cpudetect", "--disable-autodetect"]
for line in patch.splitlines():
    match = re.match(r'^\+string\(APPEND OPTIONS "(.*)"\)$', line)
    if match:
        options += match.group(1).split()
options += ["--disable-openssl"]
options += (here / "extra-options.txt").read_text().split()
if len(sys.argv) > 1 and sys.argv[1] == "--extra":
    print(" ".join((here / "extra-options.txt").read_text().split()))
else:
    print(" ".join(options))
