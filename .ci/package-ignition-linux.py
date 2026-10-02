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
import re
import shutil
import subprocess
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


def package_of(path):
    try:
        return run("dpkg", "-S", str(path)).split(":", 1)[0].strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def symbol_versions(path, prefix):
    found = set(re.findall(prefix + r"_([0-9.]+)", run("objdump", "-T", str(path))))
    return max(found, key=lambda v: tuple(int(x) for x in v.split(".")), default=None)


def copy_licences(output, source_root, packages, sources):
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
    # Libraries from outside the package manager (Qt, in this image) carry
    # their licences in their own prefix.
    for source in sources:
        prefix = source.parent.parent
        for name in ("LICENSES", "licenses"):
            folder = prefix / name
            if folder.is_dir():
                target = licences / "prefix" / prefix.name
                if not target.exists():
                    shutil.copytree(folder, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path, help="New bundle directory")
    parser.add_argument("--source-root", type=Path, required=True)
    args = parser.parse_args()

    module = args.module.resolve()
    output = args.output
    output.mkdir(parents=True, exist_ok=False)
    (output / "lib").mkdir()

    libraries = closure(module)
    bundled, system = {}, {}
    for name, path in sorted(libraries.items()):
        (system if path is None or is_system(name) else bundled)[name] = path

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

    # The bundle must resolve from itself: nothing outside SYSTEM may remain.
    escaped = {name: str(path) for name, path in closure(target).items()
               if path is not None and not is_system(name) and output.resolve() not in path.resolve().parents}
    if escaped:
        raise RuntimeError(f"Resolved outside the bundle: {escaped}")

    exports = sorted(line.split()[-1] for line in run("nm", "-D", "--defined-only", str(target)).splitlines())
    if not exports or any(not name.startswith("ignition_ps3_") for name in exports):
        raise RuntimeError(f"Unexpected exports: {exports}")

    header = (args.source_root / "rpcs3/ignition/ignition_ps3.h").read_text()
    abi = int(re.search(r"#define IGNITION_PS3_ABI_VERSION (\d+)", header).group(1))
    binaries = [target, *(output / "lib").iterdir()]
    source_commit = run("git", "-C", str(args.source_root), "rev-parse", "HEAD").strip()

    copy_licences(output, args.source_root,
                  {entry["package"] for entry in inventory.values() if entry["package"]},
                  [path.resolve() for path in bundled.values()])

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
    for name in bundled:
        print(f"  bundled {name}")
    for name in system:
        print(f"  system  {name}")
    print(f"{archive.name} {archive.stat().st_size} bytes sha256 {digest}")


if __name__ == "__main__":
    main()
