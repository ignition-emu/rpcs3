#!/usr/bin/env python3
"""Add extra-options.txt to a vcpkg ffmpeg port already patched with
ffmpeg-core's ffmpeg.patch, after the options that patch appends, so the
Windows build configures with the same list options.py gives Linux.
Usage: vcpkg_port.py <vcpkg>/ports/ffmpeg/portfile.cmake"""
import sys
from pathlib import Path

portfile = Path(sys.argv[1])
text = portfile.read_text()
anchor = 'string(APPEND OPTIONS " --enable-bsf=mjpeg2jpeg")\n'
if text.count(anchor) != 1:
    sys.exit(f"{portfile}: ffmpeg-core's options not found")
extra = " ".join((Path(__file__).resolve().parent / "extra-options.txt").read_text().split())
text = text.replace(anchor, anchor + f'string(APPEND OPTIONS " {extra}")\n')
portfile.write_text(text)
print(f"added to {portfile}: {extra}")
