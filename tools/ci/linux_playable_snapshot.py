#!/usr/bin/env python3
"""Freeze and select a local Linux play candidate.

The development build's resource links point back into the working tree. This
helper copies the client, matching library and curated resource trees into a
content-addressed, read-only snapshot so later source edits cannot alter a run.
It is a local development aid, not the audited public package staging tool.
Snapshots are retained after promotion because a running profile may still hold
runtime symlinks into an older one; see LINUX_PLAYABLE_SNAPSHOT.md for cleanup.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tempfile


FORMAT_VERSION = 1
SNAPSHOT_RE = re.compile(r"snapshot-([0-9a-f]{64})\Z")
PAYLOAD_ROOTS = ("vcmiclient", "libvcmi.so", "config", "scripts",
                 "Mods/vcmi", "Mods/new-horizons")
REQUIRED_FILES = ("config/filesystem.json", "config/newHorizonsCombat.json",
                  "config/newHorizonsMagic.json", "scripts/damage/damageCalculator.lua",
                  "Mods/vcmi/mod.json", "Mods/new-horizons/mod.json")


def fail(message):
    raise RuntimeError(message)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def payload_digest(files):
    return hashlib.sha256(canonical_bytes(files)).hexdigest()


def checked_tree_files(source, destination_root):
    """Map one resource tree to package-relative names; reject nested links."""
    if not source.exists() or not source.is_dir():
        fail(f"Missing resource tree: {source}")
    resolved = source.resolve(strict=True)
    files = {}
    for current, dirs, names in os.walk(resolved, followlinks=False):
        current_path = Path(current)
        for name in list(dirs):
            path = current_path / name
            if path.is_symlink() or not path.is_dir():
                fail(f"Resource tree contains a symlink or non-directory: {path}")
        for name in names:
            path = current_path / name
            if path.is_symlink() or not path.is_file():
                fail(f"Resource tree contains a symlink or non-regular file: {path}")
            relative = path.relative_to(resolved).as_posix()
            package_path = (PurePosixPath(destination_root) / relative).as_posix()
            files[package_path] = path
    return files


def source_payload(client, resources):
    client = Path(client)
    resources = Path(resources)
    if client.is_symlink() or not client.is_file() or not os.access(client, os.X_OK):
        fail("Client must be a regular executable file")
    if resources.is_symlink() or not resources.is_dir():
        fail("Resource root must be a regular directory")
    library = client.parent / "libvcmi.so"
    if library.is_symlink() or not library.is_file():
        fail("Matching libvcmi.so must be a regular file beside the client")
    files = {"vcmiclient": client, "libvcmi.so": library}
    for name in PAYLOAD_ROOTS[2:]:
        source = resources / name
        files.update(checked_tree_files(source, name))
    missing = [name for name in REQUIRED_FILES if name not in files]
    if missing:
        fail("Candidate is missing required curated resources: " + ", ".join(missing))
    images = "Mods/new-horizons/Images"
    if not any(name.startswith(images + "/") for name in files):
        fail("Candidate is missing curated New Horizons artwork")
    return files


def inventory(source_files):
    return {name: sha256_file(path) for name, path in sorted(source_files.items())}


def write_payload(source_files, target):
    for name, source in sorted(source_files.items()):
        destination = target / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        shutil.copystat(source, destination, follow_symlinks=False)


def package_files(root):
    actual = {}
    for current, dirs, names in os.walk(root, followlinks=False):
        current_path = Path(current)
        for name in list(dirs):
            path = current_path / name
            if path.is_symlink() or not path.is_dir():
                fail(f"Snapshot contains a symlink or non-directory: {path}")
        for name in names:
            path = current_path / name
            if path.is_symlink() or not path.is_file():
                fail(f"Snapshot contains a symlink or non-regular file: {path}")
            relative = path.relative_to(root).as_posix()
            if relative in ("SHA256SUMS", "SNAPSHOT.json"):
                continue
            actual[relative] = sha256_file(path)
    return actual


def write_metadata(root, files):
    digest = payload_digest(files)
    (root / "SHA256SUMS").write_text("".join(f"{value}  {name}\n" for name, value in sorted(files.items())))
    metadata = {"format": FORMAT_VERSION, "snapshot_sha256": digest,
                "created_utc": datetime.now(timezone.utc).isoformat(),
                "files": files}
    (root / "SNAPSHOT.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return digest


def verify_snapshot(root, expected_digest=None):
    if root.is_symlink() or not root.is_dir():
        fail("Selected snapshot directory is missing or is a symlink")
    metadata_path = root / "SNAPSHOT.json"
    sums_path = root / "SHA256SUMS"
    if metadata_path.is_symlink() or sums_path.is_symlink():
        fail("Snapshot metadata cannot be symlinks")
    try:
        metadata = json.loads(metadata_path.read_text())
        listed = {}
        for line in sums_path.read_text().splitlines():
            digest, name = line.split(maxsplit=1)
            name = name.strip()
            parsed = PurePosixPath(name)
            if parsed.is_absolute() or ".." in parsed.parts or name in listed:
                fail("Unsafe or duplicate checksum entry")
            listed[name] = digest
    except (OSError, ValueError, json.JSONDecodeError) as error:
        fail(f"Cannot read snapshot manifest: {error}")
    if metadata.get("format") != FORMAT_VERSION or not isinstance(metadata.get("files"), dict):
        fail("Unsupported local snapshot metadata")
    files = metadata["files"]
    if listed != files:
        fail("Snapshot checksum list differs from its identity")
    actual = package_files(root)
    if actual != files:
        fail("Snapshot inventory or checksum mismatch")
    digest = payload_digest(files)
    if metadata.get("snapshot_sha256") != digest:
        fail("Snapshot identity mismatch")
    if expected_digest is not None and digest != expected_digest:
        fail("Current pointer does not match selected snapshot")
    match = SNAPSHOT_RE.fullmatch(root.name)
    if not match or match.group(1) != digest:
        fail("Snapshot directory name does not match its contents")
    return metadata


@contextmanager
def store_lock(store, exclusive):
    if store.is_symlink():
        fail("Snapshot store cannot be a symlink")
    store.mkdir(parents=True, exist_ok=True)
    lock_path = store / ".snapshot.lock"
    if lock_path.is_symlink():
        fail("Snapshot lock cannot be a symlink")
    with lock_path.open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
        yield


def read_current(store):
    pointer = store / "current.json"
    if pointer.is_symlink() or not pointer.is_file():
        fail("No frozen Linux playable snapshot is selected; build and freeze one first")
    try:
        data = json.loads(pointer.read_text())
        if data.get("format") != FORMAT_VERSION:
            fail("Unsupported current playable snapshot pointer")
        name = data["snapshot"]
        digest = data["snapshot_sha256"]
        previous_name = data.get("previous_snapshot")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        fail(f"Cannot read current playable snapshot pointer: {error}")
    match = SNAPSHOT_RE.fullmatch(name)
    if not match or match.group(1) != digest:
        fail("Current playable snapshot pointer is invalid")
    if previous_name is not None:
        if not SNAPSHOT_RE.fullmatch(previous_name) or previous_name == name:
            fail("Previous playable snapshot pointer is invalid")
    return name, digest, previous_name


def set_current(store, name, digest, previous_name):
    pointer_data = {"format": FORMAT_VERSION, "snapshot": name,
                    "snapshot_sha256": digest, "previous_snapshot": previous_name}
    fd, temporary_name = tempfile.mkstemp(prefix=".current-", dir=store)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(pointer_data, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, store / "current.json")
        directory_fd = os.open(store, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)


def readonly_tree(root):
    for path in sorted(root.rglob("*"), reverse=True):
        if path.is_dir():
            path.chmod(0o555)
        else:
            path.chmod(path.stat().st_mode & ~0o222)
    root.chmod(0o555)


def make_removable(root):
    if root.is_symlink() or not root.is_dir():
        return
    root.chmod(0o755)
    for path in root.rglob("*"):
        if path.is_dir() and not path.is_symlink():
            path.chmod(0o755)
        elif not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)


def candidate_in_store(store, snapshot):
    store = store.resolve(strict=True)
    snapshot = Path(snapshot).absolute()
    if snapshot.is_symlink():
        fail("Candidate snapshot cannot be a symlink")
    try:
        resolved = snapshot.resolve(strict=True)
    except OSError as error:
        fail(f"Candidate snapshot does not exist: {error}")
    if resolved.parent != store or not SNAPSHOT_RE.fullmatch(resolved.name):
        fail("Candidate snapshot must be a generated direct child of the selected snapshot store")
    return resolved


def promote_snapshot(store, snapshot):
    candidate = candidate_in_store(store, snapshot)
    match = SNAPSHOT_RE.fullmatch(candidate.name)
    digest = match.group(1)
    verify_snapshot(candidate, digest)
    try:
        current_name, _, prior_name = read_current(store)
    except RuntimeError:
        current_name = None
        prior_name = None
    previous_name = current_name if current_name != candidate.name else prior_name
    set_current(store, candidate.name, digest, previous_name)
    return candidate


def freeze(args):
    store = Path(args.store).absolute()
    if store.is_symlink():
        fail("Snapshot store cannot be a symlink")
    with store_lock(store, exclusive=True):
        sources = source_payload(args.client, args.resources)
        before = inventory(sources)
        temporary = Path(tempfile.mkdtemp(prefix=".snapshot-", dir=store))
        try:
            write_payload(sources, temporary)
            after = inventory(source_payload(args.client, args.resources))
            if before != after:
                fail("Build or source resources changed while the snapshot was being copied")
            copied = package_files(temporary)
            if copied != before:
                fail("Frozen files differ from the build and resources selected at copy time")
            digest = write_metadata(temporary, copied)
            name = "snapshot-" + digest
            destination = store / name
            if destination.is_symlink():
                fail("Snapshot destination cannot be a symlink")
            if destination.exists():
                verify_snapshot(destination, digest)
                make_removable(temporary)
                shutil.rmtree(temporary)
            else:
                readonly_tree(temporary)
                os.replace(temporary, destination)
            verify_snapshot(destination, digest)
            if args.promote:
                promote_snapshot(store, destination)
            print(destination)
        except BaseException:
            if temporary.exists():
                make_removable(temporary)
                shutil.rmtree(temporary)
            raise


def resolve(args):
    store = Path(args.store).absolute()
    with store_lock(store, exclusive=False):
        name, digest, _ = read_current(store)
        candidate = store / name
        verify_snapshot(candidate, digest)
        print(candidate)


def promote(args):
    store = Path(args.store).absolute()
    with store_lock(store, exclusive=True):
        candidate = promote_snapshot(store, args.snapshot)
        print(candidate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    freeze_parser = subparsers.add_parser("freeze", help="copy a just-built candidate into an immutable local snapshot")
    freeze_parser.add_argument("--client", type=Path, required=True)
    freeze_parser.add_argument("--resources", type=Path, required=True,
                               help="build bin directory containing config/scripts/Mods resource links")
    freeze_parser.add_argument("--store", type=Path, required=True)
    freeze_parser.add_argument("--no-promote", dest="promote", action="store_false",
                               help="leave the candidate unselected for headless validation (default)")
    freeze_parser.add_argument("--promote", dest="promote", action="store_true",
                               help="select immediately after freezing; use only after candidate validation")
    freeze_parser.set_defaults(promote=False)
    freeze_parser.set_defaults(run=freeze)
    resolve_parser = subparsers.add_parser("resolve", help="verify and print the currently selected snapshot")
    resolve_parser.add_argument("--store", type=Path, required=True)
    resolve_parser.set_defaults(run=resolve)
    promote_parser = subparsers.add_parser("promote", help="select a previously frozen candidate after validation")
    promote_parser.add_argument("--snapshot", type=Path, required=True)
    promote_parser.add_argument("--store", type=Path, required=True)
    promote_parser.set_defaults(run=promote)
    args = parser.parse_args()
    try:
        args.run(args)
    except RuntimeError as error:
        parser.exit(2, f"Local Linux snapshot: {error}\n")


if __name__ == "__main__":
    main()
