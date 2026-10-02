#!/usr/bin/env python3
"""Bundle the Linux Ignition embed module with its non-system dependencies.

The Linux counterpart of Ignition's scripts/package-ps3-module.py (macOS). Runs
where the module was built, so every library it resolves is the one it linked:
copies the module and its dependency closure, points each copy's RUNPATH at the
bundle, and writes the inventory, licences and a zip with its SHA-256.

What stays with the host is the set every desktop already has and that must
match the running system rather than the build image: the C runtime, the GPU
and display stack (the Vulkan loader finds the host's drivers), audio, and the
other system services. bundle.json lists them, so a missing one is a named
requirement rather than a load failure.
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

MODULE = "librpcs3_ignition.so"
ARCHIVE = "librpcs3_ignition-linux-x86_64.zip"

# Sonames left to the host. Ordered by family; the comment says why.
SYSTEM = [
    # C/C++ runtime: one per process, already loaded by the host.
    r"ld-linux.*", r"linux-vdso\.so.*", r"libc\.so.*", r"libm\.so.*", r"libdl\.so.*",
    r"libpthread\.so.*", r"librt\.so.*", r"libresolv\.so.*", r"libutil\.so.*",
    r"libgcc_s\.so.*", r"libstdc\+\+\.so.*", r"libatomic\.so.*",
    # GPU and display: must be the running system's, as upstream's AppImage
    # also removes libvulkan and libwayland-client.
    r"libvulkan\.so.*", r"libGL\.so.*", r"libGLX\.so.*", r"libGLdispatch\.so.*",
    r"libEGL\.so.*", r"libOpenGL\.so.*", r"libgbm\.so.*", r"libdrm\.so.*",
    r"libX11\.so.*", r"libX11-xcb\.so.*", r"libxcb.*", r"libXau\.so.*", r"libXdmcp\.so.*",
    r"libXext\.so.*", r"libXrender\.so.*", r"libXi\.so.*", r"libXrandr\.so.*",
    r"libXfixes\.so.*", r"libXcursor\.so.*", r"libXinerama\.so.*", r"libxkbcommon.*",
    r"libwayland-.*",
    # Video-decode loaders, which find the host's drivers. (The OpenCL ICD
    # loader is bundled instead: it finds the host's drivers from
    # /etc/OpenCL/vendors either way, and not every desktop installs it.)
    r"libva\.so.*", r"libva-.*", r"libvdpau\.so.*",
    # Audio and system services.
    r"libasound\.so.*", r"libpulse.*", r"libjack\.so.*", r"libpipewire.*",
    r"libdbus-1\.so.*", r"libudev\.so.*", r"libsystemd\.so.*",
    # Font stack and GLib, which Qt reaches for and every desktop carries.
    r"libfontconfig\.so.*", r"libfreetype\.so.*", r"libexpat\.so.*", r"libz\.so.*",
    r"libglib-2\.0\.so.*", r"libgthread-2\.0\.so.*", r"libgobject-2\.0\.so.*",
    r"libgio-2\.0\.so.*", r"libgmodule-2\.0\.so.*", r"libpcre2-8\.so.*", r"libffi\.so.*",
    r"libmount\.so.*", r"libblkid\.so.*", r"libselinux\.so.*", r"libuuid\.so.*",
    # The system TLS/HTTP stack: ABI-stable sonames tied to the host's certificates.
    r"libcurl\.so.*", r"libssl\.so.*", r"libcrypto\.so.*", r"libgnutls\.so.*",
]


def run(*args):
    return subprocess.check_output(args, text=True)


def is_system(name):
    return any(re.fullmatch(pattern, name) for pattern in SYSTEM)


def closure(module):
    """Every library the dynamic loader would map for the module, by soname."""
    libraries = {}
    for line in run("ldd", str(module)).splitlines():
        line = line.strip()
        if "=>" in line:
            name, rest = (part.strip() for part in line.split("=>", 1))
            path = rest.split(" (", 1)[0].strip()
            if path == "not found":
                raise RuntimeError(f"{module} needs {name}, which does not resolve here")
            libraries[name] = Path(path) if path else None
        elif line:
            name = line.split(" (", 1)[0].strip()
            libraries[Path(name).name] = None
    return libraries


def needed(path):
    """The sonames an ELF file names itself (DT_NEEDED), not its closure."""
    return re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", run("readelf", "-d", str(path)))


def split(module, libraries):
    """Walk DT_NEEDED from the module. A system library is left to the host
    and not descended into: what it needs is the host's business too, and
    resolves from the host's own paths whatever the bundle carries."""
    bundled, system = {}, {}
    queue = [module]
    while queue:
        for name in needed(queue.pop()):
            if name in bundled or name in system:
                continue
            path = libraries.get(name)
            if path is None or is_system(name):
                system[name] = path
            else:
                bundled[name] = path
                queue.append(path.resolve())
    return bundled, system


def package_of(path):
    # dpkg records merged-/usr files under either /usr/lib or /lib.
    candidates = [path]
    if str(path).startswith("/usr/lib/"):
        candidates.append(Path(str(path)[len("/usr"):]))
    for candidate in candidates:
        try:
            return run("dpkg", "-S", str(candidate)).split(":", 1)[0].strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return None


# Components the image builds outside the package manager, and so has no
# copyright file for: their licence texts, fetched from their own sources.
UPSTREAM_LICENCES = [
    (r"libQt6.*", "qt", ["https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/LGPL-3.0-only.txt",
                         "https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/GPL-2.0-only.txt",
                         "https://raw.githubusercontent.com/qt/qtbase/dev/LICENSES/GPL-3.0-only.txt"]),
    (r"libSDL3.*", "sdl3", ["https://raw.githubusercontent.com/libsdl-org/SDL/main/LICENSE.txt"]),
    (r"libopencv_.*", "opencv", ["https://raw.githubusercontent.com/opencv/opencv/4.x/LICENSE"]),
]
# Linked statically into the module itself.
STATIC_LICENCES = [
    ("llvm", ["https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/LICENSE.TXT"]),
    # The LGPL build .ci/build-ffmpeg-ignition-linux.sh makes.
    ("ffmpeg", ["https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/LICENSE.md",
                "https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/COPYING.LGPLv2.1"]),
]


def ffmpeg_info(report):
    if report is None:
        return None
    text = report.read_text()
    if "all required components present" not in text or "NOT LGPL" in text:
        raise RuntimeError(f"FFmpeg check failed:\n{text}")
    return dict(re.findall(r"^(license|configuration): (.*)$", text, re.M))


def fetch(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except OSError:
            if attempt == 2:
                raise
            time.sleep(10)


def version_key(version):
    return tuple(int(x) for x in version.split("."))


def symbol_versions(path, prefix):
    found = set(re.findall(prefix + r"_([0-9.]+)\b", run("objdump", "-T", str(path))))
    return max(found, key=version_key, default=None)


def copy_licences(output, source_root, packages, sources, names):
    licences = output / "licenses"
    rpcs3 = licences / "rpcs3"
    rpcs3.mkdir(parents=True)
    shutil.copy2(source_root / "LICENSE", rpcs3 / "LICENSE")
    pattern = re.compile(r"^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)", re.I)
    for file in (source_root / "3rdparty").rglob("*"):
        if file.is_file() and pattern.match(file.name):
            target = rpcs3 / file.relative_to(source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    for package in sorted(packages):
        copyright_file = Path("/usr/share/doc") / package / "copyright"
        if copyright_file.is_file():
            target = licences / "packages" / package / "copyright"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(copyright_file, target)
    # The C++ runtime the module links statically: its package's copyright,
    # which carries the GCC Runtime Library Exception.
    for archive in ("libstdc++.a", "libgcc.a", "libgcc_eh.a"):
        path = Path(run(os.environ.get("CXX", "c++"), f"-print-file-name={archive}").strip())
        package = package_of(path.resolve()) if path.is_file() else None
        if package is None:
            raise RuntimeError(f"No package owns {archive} ({path})")
        packages = set(packages) | {package}
    for package in sorted(packages):
        copyright_file = Path("/usr/share/doc") / package / "copyright"
        if copyright_file.is_file():
            target = licences / "packages" / package / "copyright"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(copyright_file, target)
        else:
            raise RuntimeError(f"No copyright file for {package}")
    needed_upstream = [(folder, urls) for pattern, folder, urls in UPSTREAM_LICENCES
                       if any(re.fullmatch(pattern, name) for name in names)]
    unlicensed = [name for name, source in zip(names, sources) if package_of(source) is None
                  and not any(re.fullmatch(pattern, name) for pattern, _, _ in UPSTREAM_LICENCES)]
    if unlicensed:
        raise RuntimeError(f"No licence source for {unlicensed}")
    for folder, urls in [*needed_upstream, *STATIC_LICENCES]:
        target = licences / "upstream" / folder
        target.mkdir(parents=True, exist_ok=True)
        for url in urls:
            (target / url.rsplit("/", 1)[1]).write_bytes(fetch(url))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path, help="New bundle directory")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--max-glibc", help="Oldest host glibc to load on, e.g. 2.35")
    parser.add_argument("--max-glibcxx", help="Oldest host libstdc++ to load on, e.g. 3.4.30")
    parser.add_argument("--ffmpeg-check", type=Path, help="check_ffmpeg's report on the FFmpeg linked in")
    args = parser.parse_args()

    module = args.module.resolve()
    output = args.output
    output.mkdir(parents=True, exist_ok=False)
    (output / "lib").mkdir()

    bundled, system = split(module, closure(module))

    inventory = {}
    target = output / MODULE
    shutil.copy2(module, target)
    for name, path in bundled.items():
        copy = output / "lib" / name
        shutil.copy2(path.resolve(), copy)
        copy.chmod(0o755)
        inventory[f"lib/{name}"] = {"source": str(path.resolve()), "package": package_of(path.resolve())}

    for file in [target, *(output / "lib").iterdir()]:
        run("strip", "--strip-debug", str(file))
        run("patchelf", "--set-rpath", "$ORIGIN/lib" if file == target else "$ORIGIN", str(file))

    # The bundle must resolve from itself: every non-system library the module
    # or a bundled library names must load from the bundle.
    resolved = closure(target)
    escaped = {name: str(resolved.get(name))
               for file in [target, *(output / "lib").iterdir()] for name in needed(file)
               if not is_system(name) and (resolved.get(name) is None
                                           or output.resolve() not in resolved[name].resolve().parents)}
    if escaped:
        raise RuntimeError(f"Resolved outside the bundle: {escaped}")

    exports = sorted(line.split()[-1] for line in run("nm", "-D", "--defined-only", str(target)).splitlines())
    if not exports or any(not name.startswith("ignition_ps3_") for name in exports):
        raise RuntimeError(f"Unexpected exports: {exports}")

    header = (args.source_root / "rpcs3/ignition/ignition_ps3.h").read_text()
    abi = int(re.search(r"#define IGNITION_PS3_ABI_VERSION (\d+)", header).group(1))
    binaries = [target, *(output / "lib").iterdir()]
    # Every binary must load against the oldest host the bundle claims.
    for prefix, limit in (("GLIBC", args.max_glibc), ("GLIBCXX", args.max_glibcxx)):
        if not limit:
            continue
        over = {b.name: v for b in binaries if (v := symbol_versions(b, prefix))
                and version_key(v) > version_key(limit)}
        if over:
            raise RuntimeError(f"{prefix} newer than {limit} required by {over}")
    source_commit = run("git", "-C", str(args.source_root), "rev-parse", "HEAD").strip()

    copy_licences(output, args.source_root,
                  {entry["package"] for entry in inventory.values() if entry["package"]},
                  [path.resolve() for path in bundled.values()], list(bundled))

    (output / "bundle.json").write_text(json.dumps({
        "bundled": inventory,
        "system": {name: str(path) if path else None for name, path in system.items()},
    }, indent=2) + "\n")
    (output / "release.json").write_text(json.dumps({
        "abi": abi,
        "source_commit": source_commit,
        "source": f"https://github.com/ignition-emu/rpcs3/tree/{source_commit}",
        "input_module_sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
        "exports": exports,
        # The statically linked FFmpeg's own avutil_license()/configuration().
        "ffmpeg": ffmpeg_info(args.ffmpeg_check),
        "requires": {
            "GLIBC": max(filter(None, (symbol_versions(b, "GLIBC") for b in binaries)),
                         key=lambda v: tuple(int(x) for x in v.split("."))),
            "GLIBCXX": max(filter(None, (symbol_versions(b, "GLIBCXX") for b in binaries)),
                           key=lambda v: tuple(int(x) for x in v.split(".")), default=None),
        },
    }, indent=2) + "\n")

    archive = output.parent / ARCHIVE
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for file in sorted(output.rglob("*")):
            if file.is_file():
                zipped.write(file, str(file.relative_to(output)))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output.parent / "SHA256SUMS").write_text(f"{digest}  {archive.name}\n")

    print(f"{len(exports)} exports; {len(bundled)} libraries bundled, {len(system)} left to the host")
    for name in sorted(bundled):
        print(f"  bundled {name}")
    for name in sorted(system):
        print(f"  system  {name}")
    print(f"{archive.name} {archive.stat().st_size} bytes sha256 {digest}")


if __name__ == "__main__":
    main()
