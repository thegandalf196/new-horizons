#!/usr/bin/env python3
"""Package an installed NH Windows client; never collect a user's game-data directory.

Run from a clean, recursively checked-out source tree in an MSVC developer shell.
Only this script's output directory is created; the install tree is read-only.
"""

import argparse
import hashlib
import json
import ntpath
import os
import posixpath
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


SYSTEM_DLLS = set("""
advapi32 avrt bcrypt bcryptprimitives cfgmgr32 combase comctl32 comdlg32
crypt32 cryptbase cryptsp d3d9 d3d11 dbgeng dbghelp dinput8 dnsapi dsound dwmapi dxgi dxva2
gdi32 glu32 hid imagehlp imm32 iphlpapi kernel32 kernelbase mf mfplat mfreadwrite
mpr msacm32 msimg32 msvcrt mswsock netapi32 normaliz ntdll ntmarta ole32 oleaut32
opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi strmiids
ucrtbase urlmon user32 userenv usp10 uxtheme version winhttp wininet winmm
wintrust wldap32 ws2_32 wtsapi32
""".split())
SYSTEM_DLLS = {name + ".dll" for name in SYSTEM_DLLS} | {"winspool.drv"}
FORBIDDEN_ASSETS = {".lod", ".snd", ".vid", ".h3m", ".h3c", ".vsgm1", ".vcgm1"}


def run(*command):
    return subprocess.check_output(command, text=True, encoding="utf-8", errors="replace").strip()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def media_runtime_roots(graph_path):
    """Conservatively retain codec DLLs that SDL/FFmpeg may load by name, not PE import."""
    nodes = json.loads(graph_path.read_text(encoding="utf-8"))["graph"]["nodes"]
    roots = [key for key, node in nodes.items() if (node.get("ref") or "").split("/")[0] in {
        "sdl_image", "sdl_mixer", "sdl-image", "sdl-mixer", "ffmpeg",
    }]
    pending = list(roots)
    visited = set()
    retained = set()
    provenance = []
    while pending:
        key = str(pending.pop())
        if key in visited:
            continue
        visited.add(key)
        node = nodes[key]
        if node.get("context") == "build":
            continue
        folder = Path(node.get("package_folder") or "__missing__")
        dlls = sorted({p.name.lower() for p in folder.rglob("*.dll")}) if folder.is_dir() else []
        retained.update(dlls)
        provenance.append({"reference": node.get("ref"), "options": node.get("options"), "retained_dlls": dlls})
        pending.extend(node.get("dependencies", {}).keys())
    return retained, provenance


def audit_pe_tree(package, runtime_roots=()):
    """Check every shipped PE, including transitive/delay imports, without executing it."""
    tool = shutil.which("dumpbin")
    if not tool:
        raise RuntimeError("dumpbin is required: run in the MSVC x64 developer environment")
    binaries = sorted(p for p in package.iterdir() if p.suffix.lower() in {".exe", ".dll"})
    by_name = {p.name.lower(): p for p in binaries}
    if len(by_name) != len(binaries):
        raise RuntimeError("Case-insensitive PE filename collision")
    report = {}
    pending = ["vcmi_client.exe", "vcmi_lib.dll", *runtime_roots]
    missing = set(pending) - by_name.keys()
    if missing:
        raise RuntimeError("Declared runtime-loaded media DLLs absent from install: " + ", ".join(sorted(missing)))
    while pending:
        key = pending.pop()
        binary = by_name[key]
        if binary.name in report:
            continue
        headers = run(tool, "/nologo", "/headers", str(binary))
        if not re.search(r"\b8664 machine\b", headers, re.IGNORECASE):
            raise RuntimeError(f"Not an AMD64 PE: {binary.name}")
        output = run(tool, "/nologo", "/dependents", str(binary))
        imports = sorted(set(re.findall(r"^\s+([\w.+-]+\.(?:dll|drv))\s*$", output, re.MULTILINE | re.IGNORECASE)))
        exports = run(tool, "/nologo", "/exports", str(binary))
        forwarders = re.findall(r"\(forwarded to ([\w-]+)\.", exports, re.IGNORECASE)
        imports = sorted(set(imports) | {name + ".dll" for name in forwarders})
        resolved = {}
        for dependency in imports:
            key = dependency.lower()
            if key in by_name:
                resolved[dependency] = "bundled"
                pending.append(key)
            elif key in SYSTEM_DLLS or key.startswith(("api-ms-win-", "ext-ms-win-")):
                resolved[dependency] = "Windows system/API contract"
            else:
                raise RuntimeError(f"Unresolved non-system PE import: {binary.name} -> {dependency}")
        report[binary.name] = resolved
    # Only remove unused DLLs from our temporary stage, never from the installed tree.
    for binary in binaries:
        if binary.name not in report:
            binary.unlink()
    return report


def collect_microsoft_notices(package):
    """Retain installed toolchain/SDK redistribution terms, not just a license label."""
    candidates = []
    for variable, subdirectories in (
        ("VSINSTALLDIR", ("Licenses", "VC/Redist/MSVC")),
        ("WindowsSdkDir", ("",)),
    ):
        root = Path(os.environ.get(variable) or "__missing__")
        for subdirectory in subdirectories:
            folder = root / subdirectory
            if not folder.is_dir():
                continue
            # SDK's root EULA is sufficient; do not walk its entire include/library tree.
            entries = folder.glob("*") if variable == "WindowsSdkDir" else folder.rglob("*")
            for path in entries:
                if path.is_file() and path.name.lower().startswith(("license", "eula", "redist")) and path.suffix.lower() in {".txt", ".rtf", ".htm", ".html"}:
                    candidates.append((variable, root, path))
    if not any(path.name.lower().startswith(("license", "eula")) for _, _, path in candidates):
        raise RuntimeError("Microsoft toolchain/SDK license texts not found; do not publish CRT without notices")
    for variable, root, path in candidates:
        destination = package / "licenses" / "Microsoft" / variable / path.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)


def build_provenance(build_directory):
    cache = build_directory / "CMakeCache.txt"
    if not cache.is_file():
        raise RuntimeError("CMakeCache.txt is required for build provenance")
    selected = {}
    safe_keys = {"CMAKE_BUILD_TYPE", "CMAKE_GENERATOR", "CMAKE_CXX_STANDARD", "CMAKE_MSVC_RUNTIME_LIBRARY"}
    for line in cache.read_text(encoding="utf-8").splitlines():
        match = re.match(r"([^:#]+):[^=]+=(.*)$", line)
        if match:
            key, value = match.groups()
            if (key.startswith("ENABLE_") and value in {"ON", "OFF", "TRUE", "FALSE", "0", "1"}) or key in safe_keys:
                selected[key] = value
    compiler_versions = []
    for path in (build_directory / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"):
        match = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([\d.]+)"\)', path.read_text(encoding="utf-8"))
        if match:
            compiler_versions.append(match.group(1))
    return {"cache_options": selected, "msvc_compiler_versions": sorted(set(compiler_versions))}


def collect_notices(graph_path, package, source_output):
    graph = json.loads(graph_path.read_text(encoding="utf-8"))
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict) or not nodes:
        raise RuntimeError("Conan JSON must contain graph.nodes from conan install --format=json")
    notices = package / "licenses" / "dependencies"
    notices.mkdir(parents=True)
    dependencies = []
    missing = []
    for node in nodes.values():
        reference = node.get("ref")
        if not reference or not node.get("package_folder") or node.get("context") == "build" or reference.startswith("qt/"):
            continue
        # Include notices for static dependencies too: their code can be inside VCMI_lib.dll.
        name = re.sub(r"[^a-zA-Z0-9_.-]", "_", reference.split("#")[0])
        copied = []
        for folder_key in ("package_folder", "recipe_folder"):
            folder = Path(node.get(folder_key) or "__missing__")
            candidates = list((folder / "licenses").rglob("*")) if (folder / "licenses").is_dir() else []
            candidates += [p for p in folder.glob("*") if p.is_file() and p.name.lower().startswith(("license", "copying", "copyright", "notice"))]
            for source in candidates:
                if not source.is_file() or source.is_symlink():
                    continue
                relative = source.relative_to(folder)
                destination = notices / name / folder_key / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                copied.append(destination.relative_to(package).as_posix())
        metadata = {
            "reference": reference, "license": node.get("license"), "homepage": node.get("homepage"),
            "package_id": node.get("package_id"), "package_revision": node.get("prev"),
            "settings": node.get("settings"), "options": node.get("options"),
            "notices": sorted(set(copied)),
        }
        dependencies.append(metadata)
        if not copied:
            missing.append(reference)
    if not dependencies:
        raise RuntimeError("No Conan host dependency notices collected")
    if missing:
        raise RuntimeError("Missing dependency license texts (do not publish): " + ", ".join(missing))
    # Fetch through each exact cached recipe's source() implementation, retaining its
    # exported patches/build scripts as well as upstream source. A homepage is NOT
    # a substitute for corresponding source. Failure prevents binary publication.
    by_reference = {node.get("ref"): node for node in nodes.values()}
    with tarfile.open(source_output, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        for dependency in dependencies:
            reference = dependency["reference"]
            node = by_reference[reference]
            with tempfile.TemporaryDirectory(prefix="nh-dependency-source-") as temporary:
                source = Path(temporary) / "recipe"
                shutil.copytree(node["recipe_folder"], source)
                located = subprocess.run(["conan", "cache", "path", reference, "--folder=export_source"], text=True, capture_output=True)
                if located.returncode:
                    # Only a specifically absent export-source folder is optional. An
                    # unknown recipe, broken cache, or any other Conan failure is fatal.
                    absent = re.search(r"(?i)(export.source|exported.source).*(not exist|not found|missing)", located.stderr)
                    if not absent:
                        raise RuntimeError(f"Cannot locate exported sources for {reference}: {located.stderr}")
                else:
                    exported = Path(located.stdout.strip())
                    if exported.is_dir():
                        shutil.copytree(exported, source, dirs_exist_ok=True)
                # Local source command works with a copy, never writes into Conan's binary cache.
                name_version, _, user_channel = reference.split("#")[0].partition("@")
                name, version = name_version.split("/", 1)
                command = ["conan", "source", str(source), "--name", name, "--version", version]
                if user_channel:
                    user, channel = user_channel.split("/", 1)
                    command += ["--user", user, "--channel", channel]
                subprocess.run(command, check=True)
                archive_name = re.sub(r"[^a-zA-Z0-9_.-]", "_", name_version)
                for path in sorted(source.rglob("*")):
                    # Cache manifest files and .git metadata are not corresponding source.
                    if ".git" in path.parts or path.name in {"conanmanifest.txt", "conaninfo.txt"}:
                        continue
                    info = archive.gettarinfo(str(path), arcname=f"{archive_name}/{path.relative_to(source).as_posix()}")
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    validate_archive_link(info, archive_name)
                    if info.isfile():
                        with path.open("rb") as stream:
                            archive.addfile(info, stream)
                    else:
                        archive.addfile(info)
                dependency["source_archive_directory"] = archive_name
    write_json(package / "DEPENDENCIES.json", dependencies)


def validate_archive_link(info, archive_root):
    if not (info.issym() or info.islnk()):
        return
    target = info.linkname.replace("\\", "/")
    if ntpath.splitdrive(target)[0] or posixpath.isabs(target):
        raise RuntimeError(f"Absolute/drive/UNC archive link: {info.name}")
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(info.name), target)) if info.issym() else posixpath.normpath(target)
    if resolved != archive_root and not resolved.startswith(archive_root + "/"):
        raise RuntimeError(f"Escaping archive link: {info.name}")


def source_archive(root, output, revision):
    # git's recursive tracked-file list excludes build products, purchaser assets and .git credentials.
    tracked = subprocess.check_output(["git", "ls-files", "-z", "--recurse-submodules"], cwd=root).decode().split("\0")
    excluded = []
    with tarfile.open(output, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        for name in sorted(filter(None, tracked)):
            # Internal acceptance diaries contain local purchaser paths, not corresponding build source.
            if name == "CI/deploy_rsa.enc" or name.startswith("docs/NH_"):
                excluded.append(name)
                continue
            path = root / name
            if not path.is_file() and not path.is_symlink():
                raise RuntimeError(f"Missing tracked source file: {name}; initialize submodules")
            info = archive.gettarinfo(str(path), arcname=f"new-horizons-{revision}/{name}")
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            validate_archive_link(info, f"new-horizons-{revision}")
            if info.isfile():
                with path.open("rb") as stream:
                    archive.addfile(info, stream)
            else:
                archive.addfile(info)
    return excluded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--conan-graph", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(run("git", "rev-parse", "--show-toplevel"))
    revision = run("git", "rev-parse", "HEAD")
    if os.environ.get("GITHUB_SHA") and revision != os.environ["GITHUB_SHA"]:
        raise RuntimeError("Checkout does not match GITHUB_SHA")
    if run("git", "status", "--porcelain", "--untracked-files=no"):
        raise RuntimeError("Tracked source is dirty; package identity would be misleading")
    if args.output_dir.exists():
        raise RuntimeError("Output directory must not exist; refusing to modify prior artifacts")
    install = args.install_dir.resolve(strict=True)
    helpers = root / "tools" / "windows"
    for filename in ("Play-New-Horizons.cmd", "Start-New-Horizons.ps1", "README-New-Horizons.txt", "dirs.json"):
        if not (helpers / filename).is_file():
            raise RuntimeError(f"Frontend tools/windows/{filename} is required")
    args.output_dir.mkdir(parents=True)
    package_name = f"New-Horizons-Windows-x64-{revision[:12]}"
    with tempfile.TemporaryDirectory(prefix="nh-package-") as temporary:
        package = Path(temporary) / package_name
        package.mkdir()
        # Explicit allowlist: never copy a user profile, Data, Maps, Saves, demo or optional mods.
        for source in sorted(install.iterdir()):
            if source.is_file() and source.suffix.lower() in {".exe", ".dll"}:
                if source.suffix.lower() == ".exe" and source.name.lower() != "vcmi_client.exe":
                    raise RuntimeError(f"Unexpected executable in standalone install: {source.name}")
                shutil.copyfile(source, package / source.name)
        for filename in ("VCMI_client.exe", "VCMI_lib.dll"):
            if not (package / filename).is_file():
                raise RuntimeError(f"Required installed binary missing: {filename}")
        for directory in ("config", "scripts", "Mods/vcmi"):
            source = install / directory
            if not source.is_dir():
                raise RuntimeError(f"Required engine resources missing: {directory}")
            shutil.copytree(source, package / directory)
        # Never ship synthetic smoke tests or future development files alongside the launcher.
        for filename in ("Play-New-Horizons.cmd", "Start-New-Horizons.ps1", "README-New-Horizons.txt", "dirs.json"):
            source = helpers / filename
            destination = package / (Path("config/dirs.json") if filename == "dirs.json" else filename)
            if destination.exists() and filename != "dirs.json":
                raise RuntimeError(f"Frontend helper collides with package file: {destination.name}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        for filename in ("license.txt", "AUTHORS.h"):
            shutil.copyfile(root / filename, package / filename)
        embedded = package / "licenses" / "xBRZ"
        embedded.mkdir(parents=True)
        shutil.copyfile(root / "client/xBRZ/License.txt", embedded / "License.txt")
        xbrz = (root / "client/xBRZ/xbrz.cpp").read_text(encoding="utf-8")
        (embedded / "ATTRIBUTION.txt").write_text(xbrz.split('#include "xbrz.h"', 1)[0], encoding="utf-8")
        runtime_roots, media_provenance = media_runtime_roots(args.conan_graph)
        write_json(package / "MEDIA-RUNTIME.json", {
            "policy": "Retain SDL image/mixer and FFmpeg host dependency DLL closure, including non-PE-imported codecs",
            "dependencies": media_provenance,
            "limitation": "Graph options/provenance and PE imports do not prove every dynamic codec or Windows media playback",
        })
        write_json(package / "PE-IMPORTS.json", audit_pe_tree(package, runtime_roots))
        collect_microsoft_notices(package)
        dependency_source_name = package_name + "-dependency-sources.tar.gz"
        collect_notices(args.conan_graph, package, args.output_dir / dependency_source_name)
        source_name = package_name + "-source.tar.gz"
        excluded = source_archive(root, args.output_dir / source_name, revision)
        write_json(package / "BUILD-IDENTITY.json", {
            "project": "Heroes III: New Horizons", "source_commit": revision,
            "source_repository": "https://github.com/thegandalf196/vcmi",
            "source_archive": source_name, "source_archive_sha256": sha256(args.output_dir / source_name),
            "source_exclusions": excluded,
            "build_provenance": build_provenance(install.parent),
            "prebuilt_dependencies": {
                "url": "https://github.com/vcmi/vcmi-dependencies/releases/download/2026-09-01/dependencies-windows-x64.txz",
                "sha256": "6772d2e9f0a730329a195edce895a863d9115c4bba43101a9a6df3fefe6cfe43",
            },
            "dependency_source_archive": dependency_source_name,
            "dependency_source_sha256": sha256(args.output_dir / dependency_source_name),
            "submodules": run("git", "submodule", "status", "--recursive").splitlines(),
            "platform": "Windows x64", "configuration": "RelWithDebInfo",
            "run_id": os.environ.get("GITHUB_RUN_ID"), "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
            "compiler": "MSVC v142; Conan msvc-x64", "transport": "authoritative in-process simulation",
            "preset": "new-horizons-windows-x64", "render_backend": "SDL3",
            "engine_resource_scope": ["config", "scripts", "Mods/vcmi"],
            "acceptance": "Compile/package/import audit only; Windows graphical gameplay unverified",
            "proprietary_assets_included": False, "signed": False,
        })
        (package / "SOURCE-NOTICE.txt").write_text(
            "Heroes III: New Horizons is a VCMI-derived GPL-covered fork. Preserve license.txt and AUTHORS.h.\n"
            f"Exact source: https://github.com/thegandalf196/vcmi/tree/{revision}\n"
            f"Corresponding fork source and submodules accompany this ZIP as {source_name}.\n"
            "Third-party references, license texts and upstream homepages are in DEPENDENCIES.json and licenses/.\n"
            f"Dependency sources and exact Conan recipes/patches accompany this ZIP as {dependency_source_name}.\n"
            "Embedded xBRZ has GPLv3 terms and attribution in licenses/xBRZ; root license alone is not the entire inventory.\n"
            "MSVC/UCRT runtime files are Microsoft redistributables; installed redistribution terms are in licenses/Microsoft.\n"
            "MEDIA-RUNTIME.json records conservative dynamic-codec retention; runtime media behavior remains unverified.\n"
            "Original Heroes III Complete assets are required, external, and not redistributed here.\n"
            "Unsigned diagnostic preview; compilation is not Windows gameplay acceptance.\n", encoding="utf-8")
        for path in package.rglob("*"):
            if path.is_file() and path.suffix.lower() in FORBIDDEN_ASSETS:
                raise RuntimeError(f"Forbidden game asset/save in package: {path.relative_to(package)}")
        checksums = [f"{sha256(p)}  {p.relative_to(package).as_posix()}" for p in sorted(package.rglob("*")) if p.is_file()]
        (package / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
        with zipfile.ZipFile(args.output_dir / (package_name + ".zip"), "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(package.rglob("*")):
                if path.is_file():
                    archive.write(path, f"{package_name}/{path.relative_to(package).as_posix()}")
    (args.output_dir / "SHA256SUMS.txt").write_text("\n".join(
        f"{sha256(p)}  {p.name}" for p in sorted(args.output_dir.iterdir()) if p.is_file()
    ) + "\n", encoding="utf-8")
    print(f"READY: {package_name}.zip plus source archive and SHA256SUMS.txt")


if __name__ == "__main__":
    main()
