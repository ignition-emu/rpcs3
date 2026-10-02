#!/usr/bin/env python3
"""Bundle the Windows Ignition embed module with its non-system DLLs.

The Windows counterpart of package-ignition-linux.py, run on the MSVC CI image
after msbuild: copies rpcs3_ignition.dll and every DLL it imports, directly or
not, from the build's own dependency folders (Qt, OpenCV) beside it, flat. The
host loads the module with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, so they resolve
from there. Everything else must be the system's: Windows itself, and the
Vulkan loader, which finds the host's drivers and must not be bundled.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
import urllib.request
import zipfile
from pathlib import Path

import pefile

MODULE = "rpcs3_ignition.dll"
ARCHIVE = "rpcs3_ignition-windows-x64.zip"
SYSTEM32 = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
# Left to the host even if the build image has a copy somewhere.
HOST = {"vulkan-1.dll"}

UPSTREAM_LICENCES = [
    (r"qt6.*\.dll", "qt", ["https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/LGPL-3.0-only.txt",
                           "https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/GPL-2.0-only.txt",
                           "https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/GPL-3.0-only.txt"]),
    (r"opencv_.*\.dll", "opencv", ["https://raw.githubusercontent.com/opencv/opencv/4.x/LICENSE"]),
]
# Linked statically into the module itself. FFmpeg is the LGPL build of
# ffmpeg-core's recipe CI makes (.ci/ignition-ffmpeg); ffmpeg-core's copyright
# file is copied from the submodule as well.
STATIC_LICENCES = [
    ("llvm", ["https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/LICENSE.TXT"]),
    ("ffmpeg", ["https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/LICENSE.md",
                "https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/COPYING.LGPLv2.1"]),
]


def imports(path):
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    names = [entry.dll.decode() for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])]
    names += [entry.dll.decode() for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", [])]
    exports = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        exports = [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name]
    pe.close()
    return names, exports


def is_system(name):
    lower = name.lower()
    return lower.startswith(("api-ms-win-", "ext-ms-")) or (SYSTEM32 / name).is_file()


def ffmpeg_info(report):
    if report is None:
        return None
    text = report.read_text(errors="replace")
    if "all required components present" not in text or "NOT LGPL" in text:
        raise RuntimeError(f"FFmpeg check failed:\n{text}")
    return dict(re.findall(r"^(license|configuration): (.*?)\r?$", text, re.M))


def fetch(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except OSError:
            if attempt == 2:
                raise
            time.sleep(10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path, help="New bundle directory")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--search", type=Path, action="append", default=[],
                        help="A folder the build's own DLLs come from")
    parser.add_argument("--ffmpeg-check", type=Path, help="check_ffmpeg's report on the FFmpeg linked in")
    args = parser.parse_args()

    output = args.output
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.module, output / MODULE)

    bundled, system, queue = {}, set(), [args.module]
    while queue:
        names, _ = imports(queue.pop())
        for name in names:
            lower = name.lower()
            if lower in bundled or name in system:
                continue
            if lower in HOST:
                system.add(name)
                continue
            found = next((folder / name for folder in args.search if (folder / name).is_file()), None)
            if found is not None:
                bundled[lower] = found
                shutil.copy2(found, output / found.name)
                queue.append(found)
            elif is_system(name):
                system.add(name)
            else:
                raise RuntimeError(f"{name} is neither the build's nor the system's")

    # Besides the ABI, emucore's OpenGL.cpp exports the hybrid-GPU hints
    # drivers read from an executable; from a DLL they are inert.
    _, exports = imports(output / MODULE)
    inert = {"NvOptimusEnablement", "AmdPowerXpressRequestHighPerformance"}
    if not exports or any(not e.startswith("ignition_ps3_") and e not in inert for e in exports):
        raise RuntimeError(f"Unexpected exports: {exports}")

    licences = output / "licenses"
    rpcs3 = licences / "rpcs3"
    rpcs3.mkdir(parents=True)
    shutil.copy2(args.source_root / "LICENSE", rpcs3 / "LICENSE")
    pattern = re.compile(r"^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)", re.I)
    for file in (args.source_root / "3rdparty").rglob("*"):
        if file.is_file() and pattern.match(file.name):
            target = rpcs3 / file.relative_to(args.source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    ffmpeg_copyright = args.source_root / "3rdparty/ffmpeg/copyright"
    if ffmpeg_copyright.is_file():
        (licences / "upstream/ffmpeg").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ffmpeg_copyright, licences / "upstream/ffmpeg/copyright")
    unlicensed = [n for n in bundled if not any(re.fullmatch(p, n) for p, _, _ in UPSTREAM_LICENCES)]
    if unlicensed:
        raise RuntimeError(f"No licence source for {unlicensed}")
    for folder, urls in [*[(f, u) for p, f, u in UPSTREAM_LICENCES if any(re.fullmatch(p, n) for n in bundled)],
                         *STATIC_LICENCES]:
        target = licences / "upstream" / folder
        target.mkdir(parents=True, exist_ok=True)
        for url in urls:
            (target / url.rsplit("/", 1)[1]).write_bytes(fetch(url))

    header = (args.source_root / "rpcs3/ignition/ignition_ps3.h").read_text()
    abi = int(re.search(r"#define IGNITION_PS3_ABI_VERSION (\d+)", header).group(1))
    source_commit = subprocess.check_output(["git", "-C", str(args.source_root), "rev-parse", "HEAD"], text=True).strip()
    (output / "bundle.json").write_text(json.dumps({
        "bundled": {path.name: {"source": str(path)} for path in bundled.values()},
        "system": sorted(system),
    }, indent=2) + "\n")
    (output / "release.json").write_text(json.dumps({
        "abi": abi,
        "source_commit": source_commit,
        "source": f"https://github.com/ignition-emu/rpcs3/tree/{source_commit}",
        "input_module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "toolchain": "MSVC, static CRT",
        "ffmpeg": ffmpeg_info(args.ffmpeg_check),
        "exports": sorted(exports),
    }, indent=2) + "\n")

    archive = output.parent / ARCHIVE
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for file in sorted(output.rglob("*")):
            if file.is_file():
                zipped.write(file, file.relative_to(output).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output.parent / "SHA256SUMS-windows").write_text(f"{digest}  {archive.name}\n")

    print(f"{len(exports)} exports; {len(bundled)} DLLs bundled, {len(system)} left to the host")
    for path in bundled.values():
        print(f"  bundled {path.name}  ({path})")
    for name in sorted(system):
        print(f"  system  {name}")
    print(f"{archive.name} {archive.stat().st_size} bytes sha256 {digest}")


if __name__ == "__main__":
    main()
