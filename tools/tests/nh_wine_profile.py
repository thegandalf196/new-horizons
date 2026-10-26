# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit purchaser-content admission and staging for the private Wine owner.

Only manifest-listed originals are hashed; no installation discovery or copying.
The engine may rewrite settings, but original assets and the chosen map remain
immutable. This module neither launches processes nor authorizes input.
"""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat


class Refusal(Exception):
    pass


METADATA_LIMIT = 1024 * 1024
MAP_LIMIT = 16 * 1024 * 1024
SAVE_LIMIT = 128 * 1024 * 1024
FILE_COUNT_LIMIT = 4096


def _sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _plain(value, *, directory=False):
    if not isinstance(value, (str, Path)):
        raise Refusal("Content path must be absolute")
    path = Path(value)
    if not path.is_absolute() or ".." in path.parts:
        raise Refusal("Content path must be absolute without traversal")
    for item in (path, *path.parents):
        if item.is_symlink():
            raise Refusal("Content path has symlink ancestor")
    mode = path.lstat().st_mode
    if not (stat.S_ISDIR(mode) if directory else stat.S_ISREG(mode)):
        raise Refusal("Content path has wrong type")
    return path


def _digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def _file(row, *, limit=None):
    if not isinstance(row, dict) or not _sha(row.get("sha256")):
        raise Refusal("Invalid content file pin")
    path = _plain(row.get("path"))
    if limit is not None and path.stat().st_size > limit:
        raise Refusal("Content file exceeds bounded size")
    if _digest(path) != row["sha256"]:
        raise Refusal("Content file SHA256 mismatch")
    return path


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise Refusal("Duplicate content JSON field")
        result[key] = value
    return result


def _json(path):
    if path.stat().st_size > METADATA_LIMIT:
        raise Refusal("Content metadata exceeds bounded size")
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique,
                          parse_constant=lambda value: (_ for _ in ()).throw(Refusal("Nonfinite JSON")))
    except (ValueError, UnicodeError, RecursionError) as error:
        raise Refusal("Invalid content JSON") from error


def _fields(row, fields):
    if not isinstance(row, dict) or set(row) != set(fields):
        raise Refusal("Content JSON has missing/unknown fields")


def _relative(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or any(ord(c) < 32 for c in name) or name.startswith("/")
            or any(p in ("", ".", "..") for p in name.split("/"))):
        raise Refusal("Unsafe relative original filename")
    return PurePosixPath(name)


def verify_original_content(content):
    """Full declared original-byte check before launch and after owned cleanup."""
    if "inputSave" in content:
        _file(content["inputSave"], limit=SAVE_LIMIT)
    for group in ("data", "music"):
        row = content["original"][group]
        directory = _plain(row["path"], directory=True)
        for name, pin in row["files"].items():
            _file({"path": str(directory / _relative(name)), "sha256": pin})


def load_content_manifest(path, sha):
    """Read-only admission: exact bounded schema and pins; creates nothing."""
    manifest_path = _file({"path": path, "sha256": sha}, limit=METADATA_LIMIT)
    content = _json(manifest_path)
    fields = ("version", "original", "map", "settings", "modPreset")
    _fields(content, fields + (("inputSave",) if isinstance(content, dict) and "inputSave" in content else ()))
    if type(content["version"]) is not int or content["version"] != 1:
        raise Refusal("Unsupported content manifest version")
    _fields(content["original"], ("data", "music"))
    count = 0
    for group in ("data", "music"):
        row = content["original"][group]
        _fields(row, ("path", "files"))
        row["path"] = str(_plain(row["path"], directory=True))
        if not isinstance(row["files"], dict) or not row["files"]:
            raise Refusal("Original file inventories must be nonempty")
        count += len(row["files"])
        if count > FILE_COUNT_LIMIT:
            raise Refusal("Original inventory exceeds bounded count")
        for name, pin in row["files"].items():
            _relative(name)
            if not _sha(pin):
                raise Refusal("Invalid original SHA256")
    _fields(content["map"], ("path", "sha256", "filename"))
    name = content["map"]["filename"]
    _relative(name)
    if "/" in name or Path(name).suffix.lower() not in (".vmap", ".h3m"):
        raise Refusal("Selected map must have a plain vmap/h3m basename")
    content["map"]["path"] = str(_file(content["map"], limit=MAP_LIMIT))
    if "inputSave" in content:
        row = content["inputSave"]
        _fields(row, ("path", "sha256", "filename"))
        name = row["filename"]
        _relative(name)
        if "/" in name or Path(name).suffix.lower() != ".vsgm1":
            raise Refusal("Selected input save must have a plain vsgm1 basename")
        row["path"] = str(_file(row, limit=SAVE_LIMIT))
    for key in ("settings", "modPreset"):
        _fields(content[key], ("path", "sha256"))
        content[key]["path"] = str(_file(content[key], limit=METADATA_LIMIT))
    if not isinstance(_json(Path(content["settings"]["path"])), dict):
        raise Refusal("Settings must be a JSON object")
    preset = _json(Path(content["modPreset"]["path"]))
    if preset != {"activePreset": "default", "presets": {"default": {
            "mods": ["vcmi", "core", "new-horizons"], "settings": {}}}}:
        raise Refusal("Only the curated default mod preset is admitted")
    verify_original_content(content)
    content["manifest"] = {"path": str(manifest_path), "sha256": sha}
    return content


def _private_directory(path):
    if path.exists() or path.is_symlink():
        _plain(path, directory=True)
        info = path.stat()
        if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
            raise Refusal("Staging directory must be owned0700")
    else:
        _private_directory(path.parent)
        path.mkdir(mode=0o700)


def _destinations(root):
    root = _plain(root, directory=True)
    if root.stat().st_uid != os.getuid() or stat.S_IMODE(root.stat().st_mode) != 0o700:
        raise Refusal("Staging root must remain owned0700")
    profile = root / "localappdata" / "HeroesIII-NewHorizons"
    return profile / "content", profile / "config", root / "localappdata/new-horizons/Saves"


def _copy(row, destination, limit):
    source = _file(row, limit=limit)
    payload = source.read_bytes()
    if len(payload) > limit or hashlib.sha256(payload).hexdigest() != row["sha256"]:
        raise Refusal("Source changed before copy")
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(payload)


def stage_content(content, root):
    """Owner-only staging after fresh-root admission, before PE fork.

    Original directories remain external symlink targets, never copied wholesale.
    Existing destination files/links are never overwritten.
    """
    content_dir, config_dir, saves_dir = _destinations(Path(root))
    _file(content["manifest"], limit=METADATA_LIMIT)
    for group in ("data", "music"):
        _plain(content["original"][group]["path"], directory=True)
    for directory in (content_dir, content_dir / "Maps", config_dir):
        _private_directory(directory)
    for link, group in (("Data", "data"), ("Mp3", "music")):
        (content_dir / link).symlink_to(content["original"][group]["path"], target_is_directory=True)
    _copy(content["map"], content_dir / "Maps" / content["map"]["filename"], MAP_LIMIT)
    if "inputSave" in content:
        _private_directory(saves_dir)
        _copy(content["inputSave"], saves_dir / content["inputSave"]["filename"], SAVE_LIMIT)
    _copy(content["settings"], config_dir / "settings.json", METADATA_LIMIT)
    _copy(content["modPreset"], config_dir / "modSettings.json", METADATA_LIMIT)
    verify_staged_content(content, root)


def verify_staged_content(content, root, *, verify_copy_bytes=True):
    """Map/save/link integrity each input; configs are byte-checked only before fork."""
    content_dir, config_dir, saves_dir = _destinations(Path(root))
    for directory in (content_dir, content_dir / "Maps", config_dir):
        _plain(directory, directory=True)
        info = directory.stat()
        if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
            raise Refusal("Staged directory no longer owned0700")
    for link, group in (("Data", "data"), ("Mp3", "music")):
        target = _plain(content["original"][group]["path"], directory=True)
        path = content_dir / link
        if not path.is_symlink() or os.readlink(path) != str(target):
            raise Refusal("Staged original-content link changed")
    _file({"path": str(content_dir / "Maps" / content["map"]["filename"]),
           "sha256": content["map"]["sha256"]}, limit=MAP_LIMIT)
    if "inputSave" in content:
        row = content["inputSave"]
        _file(row, limit=SAVE_LIMIT)
        saves = _plain(saves_dir, directory=True)
        if saves.stat().st_uid != os.getuid() or stat.S_IMODE(saves.stat().st_mode) != 0o700:
            raise Refusal("Staged Saves directory no longer owned0700")
        _file({"path": str(saves / row["filename"]), "sha256": row["sha256"]}, limit=SAVE_LIMIT)
    if verify_copy_bytes:
        for name, key in (("settings.json", "settings"), ("modSettings.json", "modPreset")):
            _file({"path": str(config_dir / name), "sha256": content[key]["sha256"]}, limit=METADATA_LIMIT)
    else:
        for name in ("settings.json", "modSettings.json"):
            _plain(config_dir / name)
