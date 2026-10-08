#!/usr/bin/env python3
"""Bundle a CI-built macOS RPCS3 module and its non-system dependencies.

ignition-release.yml runs this in the job that built the module, so the
Homebrew libraries it copies are the ones the module was linked against.
This never compiles RPCS3. It repairs install names and signs copied binaries.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def dependencies(path):
    return [line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", str(path)).splitlines()[2:]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path, help="New bundle directory")
    parser.add_argument("--source-root", type=Path, required=True, help="RPCS3 checkout matching the CI artifact")
    parser.add_argument("--molten-vk", type=Path, default=Path("/opt/homebrew/opt/molten-vk/lib/libMoltenVK.dylib"))
    parser.add_argument("--icd", type=Path, required=True,
                        help="librpcs3_ignition_icd.dylib, the driver that forwards to the host's MoltenVK")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "lib").mkdir()
    copied = {}
    originals = {}
    formulae = set()
    queue = [(args.module.resolve(), args.output / "librpcs3_ignition.dylib"),
             (args.molten_vk.resolve(), args.output / "lib/libMoltenVK.dylib"),
             (args.icd.resolve(), args.output / "lib/librpcs3_ignition_icd.dylib")]
    while queue:
        source, target = queue.pop(0)
        if target in originals:
            if originals[target] != source:
                raise RuntimeError(f"Conflicting libraries named {target.name}")
            continue
        if not source.is_file():
            raise FileNotFoundError(source)
        originals[target] = source
        shutil.copy2(source, target)
        target.chmod(0o755)
        if "/Cellar/" in str(source):
            cellar, rest = str(source).split("/Cellar/", 1)
            formula, version, *_ = rest.split("/")
            formulae.add(Path(cellar) / "Cellar" / formula / version)
        rewritten = []
        for dep in dependencies(source):
            if dep.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dep.startswith("@loader_path/"):
                resolved = source.parent / dep.removeprefix("@loader_path/")
            elif dep.startswith("@rpath/"):
                suffix = dep.removeprefix("@rpath/")
                rpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", run("otool", "-l", str(source)))
                candidates = [Path(rpath.replace("@loader_path", str(source.parent))) / suffix for rpath in rpaths]
                resolved = next((candidate for candidate in candidates if candidate.is_file()), None)
                if resolved is None:
                    raise RuntimeError(f"Unresolved rpath dependency {dep} in {source}: {rpaths}")
            elif dep.startswith("/"):
                resolved = Path(dep)
            else:
                raise RuntimeError(f"Unresolved dependency {dep} in {source}")
            resolved = resolved.resolve()
            bundled = args.output / "lib" / resolved.name
            queue.append((resolved, bundled))
            replacement = "@loader_path/" + ("lib/" if target.parent == args.output else "") + bundled.name
            run("install_name_tool", "-change", dep, replacement, str(target))
            rewritten.append(replacement)
        run("install_name_tool", "-id", "@loader_path/" + target.name, str(target))
        copied[str(target.relative_to(args.output))] = {"source": str(source), "dependencies": rewritten}
    # The Vulkan loader also finds its driver at run time, outside dyld's
    # dependency list. The host points VK_DRIVER_FILES at this manifest while
    # an instance exists, so a Homebrew installation is not required to find it.
    # The driver it names forwards to the host's own MoltenVK, and loads the
    # bundled libMoltenVK.dylib only in a host that has none.
    (args.output / "MoltenVK_icd.json").write_text(json.dumps({
        "file_format_version": "1.0.0", "ICD": {
            "library_path": "lib/librpcs3_ignition_icd.dylib", "api_version": "1.4.0",
            "is_portability_driver": True}}, indent=2) + "\n")
    for formula in sorted(formulae):
        for file in formula.rglob("*"):
            if file.is_file() and re.match(r"^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)", file.name, re.I):
                target = args.output / "licenses" / formula.parent.name / file.relative_to(formula)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, target)
    source_commit = run("git", "-C", str(args.source_root), "rev-parse", "HEAD").strip()
    source_licenses = args.output / "licenses/rpcs3"
    source_licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.source_root / "LICENSE", source_licenses / "LICENSE")
    for file in (args.source_root / "3rdparty").rglob("*"):
        if file.is_file() and re.match(r"^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)", file.name, re.I):
            target = source_licenses / file.relative_to(args.source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    (args.output / "release.json").write_text(json.dumps({
        "abi": 4, "source_commit": source_commit,
        "source": f"https://github.com/ignition-emu/rpcs3/tree/{source_commit}",
        "input_module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    for file in originals:
        run("codesign", "--force", "--sign", "-", str(file))
        run("codesign", "--verify", str(file))
        if any(dep.startswith("/opt/") for dep in dependencies(file)):
            raise RuntimeError(f"External dependency remains in {file}")
    (args.output / "bundle.json").write_text(json.dumps(copied, indent=2) + "\n")
    archive = args.output.parent / "librpcs3_ignition.dylib.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for file in sorted(args.output.rglob("*")):
            if file.is_file():
                zipped.write(file, str(file.relative_to(args.output)))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (args.output.parent / "SHA256SUMS").write_text(f"{digest}  {archive.name}\n")
    print(f"{len(originals)} binaries bundled; sha256 {digest}")


if __name__ == "__main__":
    main()
