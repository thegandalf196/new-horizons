#!/usr/bin/env python3
"""Deterministic, explicit-input NHART v1 packing and self-contained verification."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile

MAGIC = b"NHART\r\n\x1a"
HEADER = struct.Struct("<8sIIQQ")
RECORD = struct.Struct("<IIQQ32s")
MAX_ENTRIES = 1_000_000
MAX_INDEX = 256 * 1024 * 1024
MAX_NAME = 4096
MANIFEST = ".nhart/manifest.json"
CHUNK = 1024 * 1024
SELECTED_TYPES = frozenset({"IMAGE", "ANIMATION", "MASK", "PALETTE", "BMP_FONT", "TTF_FONT", "JSON", "TEXT"})

# Keep aligned with EResTypeHelper::getTypeFromExtension in ResourcePath.cpp.
EXTENSIONS = {}
for _kind, _extensions in {
    "TEXT": "TXT MD", "JSON": "JSON", "ANIMATION": "DEF", "MASK": "MSK MSG",
    "CAMPAIGN": "H3C VCMP", "MAP": "H3M TUT VMAP", "BMP_FONT": "FNT",
    "TTF_FONT": "TTF", "IMAGE": "BMP GIF JPG PCX PNG TGA",
    "SOUND": "WAV 82M MP3 OGG FLAC", "VIDEO_LOW_QUALITY": "SMK",
    "VIDEO": "BIK OGV WEBM MPG MJPG", "ARCHIVE_ZIP": "ZIP",
    "ARCHIVE_LOD": "LOD PAC", "ARCHIVE_VID": "VID", "ARCHIVE_SND": "SND",
    "ARCHIVE_PAK": "PAK", "ARCHIVE_NHART": "NHART", "PALETTE": "PAL", "SAVEGAME": "VSGM1",
    "LUA_SCRIPT": "LUA", "AI_MODEL": "ONNX",
}.items():
    EXTENSIONS.update({"." + ext: _kind for ext in _extensions.split()})


class NHArtError(ValueError):
    pass


def ascii_upper(value):
    return value.translate(str.maketrans("abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"))


def validate_name(name, metadata=False):
    if not isinstance(name, str):
        raise NHArtError("Resource/source name must be a string")
    try:
        encoded = name.encode("utf-8", errors="strict")
    except UnicodeError as error:
        raise NHArtError("Invalid UTF-8 name") from error
    if not 1 <= len(encoded) <= MAX_NAME or any(c in name for c in "\\\x00:"):
        raise NHArtError("Invalid resource/source name")
    parts = name.split("/")
    if any(not part or part in (".", "..") for part in parts):
        raise NHArtError("Absolute or traversal names are forbidden")
    if not metadata and resource_identity(name) == resource_identity(MANIFEST):
        raise NHArtError("Reserved manifest resource")
    return encoded


def resource_identity(name):
    # ResourcePath recognizes a final extension even when it is the whole
    # filename (for example .PNG). pathlib.suffix intentionally does not.
    dot = name.rfind(".")
    extension = name[dot:] if dot > name.rfind("/") else ""
    kind = EXTENSIONS.get(ascii_upper(extension), "OTHER")
    stem = name[:-len(extension)] if extension and kind != "OTHER" else name
    return ascii_upper(stem), kind


def _portable_text(value):
    if not isinstance(value, str) or not value.strip() or "\x00" in value:
        return False
    try:
        value.encode("utf-8", errors="strict")
    except UnicodeError:
        return False
    return not re.search(r"(?i)(/home/|/users/|[a-z]:[\\/]|\\\\)", value)


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise NHArtError("Duplicate JSON member")
        result[key] = value
    return result


def read_json(data):
    try:
        return json.loads(data, object_pairs_hook=_unique_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(NHArtError("Nonfinite JSON number")))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise NHArtError("Invalid manifest JSON") from error


def validate_manifest(manifest):
    if not isinstance(manifest, dict) or type(manifest.get("format")) is not int or manifest["format"] != 1:
        raise NHArtError("Manifest format must be 1")
    entries = manifest.get("entries")
    required = manifest.get("requiredFamilies")
    counts = manifest.get("requiredFamilyCounts")
    if not isinstance(entries, list) or not entries or len(entries) >= MAX_ENTRIES:
        raise NHArtError("Manifest requires 1..999999 selected entries")
    if not isinstance(required, list) or any(not _portable_text(x) for x in required) or len(set(required)) != len(required):
        raise NHArtError("Invalid requiredFamilies")
    if not isinstance(counts, dict) or any(not _portable_text(key) or type(value) is not int or value < 1 for key, value in counts.items()):
        raise NHArtError("Invalid requiredFamilyCounts")
    if set(counts) != set(required):
        raise NHArtError("requiredFamilyCounts must cover exactly requiredFamilies")
    identities = {resource_identity(MANIFEST)}
    families = set()
    actual_counts = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise NHArtError("Invalid manifest entry")
        validate_name(entry.get("resource"))
        validate_name(entry.get("source"))
        identity = resource_identity(entry["resource"])
        if identity[1] not in SELECTED_TYPES:
            raise NHArtError("Selected payload is not a visual/support resource type")
        if identity in identities:
            raise NHArtError("ResourcePath name/type collision")
        identities.add(identity)
        if type(entry.get("size")) is not int or not 0 <= entry["size"] < 2**64:
            raise NHArtError("Invalid selected payload size")
        if not isinstance(entry.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise NHArtError("Invalid selected SHA256")
        for field in ("family", "origin", "selection", "approval"):
            value = entry.get(field)
            if not _portable_text(value):
                raise NHArtError("Missing explicit provenance: " + field)
        families.add(entry["family"])
        actual_counts[entry["family"]] = actual_counts.get(entry["family"], 0) + 1
    if not set(required) <= families:
        raise NHArtError("Required family has no selected payload")
    if any(actual_counts.get(family) != count for family, count in counts.items()):
        raise NHArtError("Required family selected count mismatch")
    # Reject undeclared fields, avoiding accidental timestamp/private-path metadata.
    if set(manifest) != {"format", "entries", "requiredFamilies", "requiredFamilyCounts"}:
        raise NHArtError("Unknown manifest field")
    expected = {"resource", "source", "size", "sha256", "family", "origin", "selection", "approval"}
    if any(set(entry) != expected for entry in entries):
        raise NHArtError("Unknown/missing manifest entry field")
    return {"format": 1, "requiredFamilies": sorted(required), "requiredFamilyCounts": dict(sorted(counts.items())),
            "entries": sorted(entries, key=lambda entry: entry["resource"].encode("utf-8"))}


def _digest(stream, length, destination=None):
    digest = hashlib.sha256()
    remaining = length
    while remaining:
        data = stream.read(min(CHUNK, remaining))
        if not data:
            raise NHArtError("Truncated payload")
        digest.update(data)
        if destination is not None:
            destination.write(data)
        remaining -= len(data)
    return digest.hexdigest()


def verify(path, manifest=None):
    """Verify container and embedded inventory, without consulting input sources."""
    with Path(path).open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        header = stream.read(HEADER.size)
        if len(header) != HEADER.size:
            raise NHArtError("Truncated header")
        magic, version, flags, index_offset, count = HEADER.unpack(header)
        if magic != MAGIC or version != 1 or flags != 0:
            raise NHArtError("Unsupported NHART header")
        index_size = size - index_offset
        if not 1 <= count <= MAX_ENTRIES or not HEADER.size <= index_offset <= size:
            raise NHArtError("Invalid index bounds/count")
        if index_size > MAX_INDEX or count * (RECORD.size + 1) > index_size:
            raise NHArtError("Invalid index size")
        stream.seek(index_offset)
        index = stream.read(index_size)
        cursor = 0
        rows = []
        identities = set()
        for _ in range(count):
            if cursor + RECORD.size > len(index):
                raise NHArtError("Truncated index record")
            name_length, reserved, offset, length, digest = RECORD.unpack_from(index, cursor)
            cursor += RECORD.size
            if reserved or not 1 <= name_length <= MAX_NAME or cursor + name_length > len(index):
                raise NHArtError("Invalid index name/reserved field")
            try:
                name = index[cursor:cursor + name_length].decode("utf-8", errors="strict")
            except UnicodeError as error:
                raise NHArtError("Invalid index UTF-8") from error
            cursor += name_length
            validate_name(name, metadata=True)
            identity = resource_identity(name)
            if identity in identities:
                raise NHArtError("ResourcePath name/type collision")
            identities.add(identity)
            if offset < HEADER.size or offset > index_offset or length > index_offset - offset:
                raise NHArtError("Payload outside data region")
            rows.append({"resource": name, "offset": offset, "size": length, "sha256": digest.hex()})
        if cursor != len(index):
            raise NHArtError("Trailing index bytes")
        end = HEADER.size
        for row in sorted(rows, key=lambda row: (row["offset"], row["size"])):
            if row["offset"] < end:
                raise NHArtError("Overlapping payloads")
            end = row["offset"] + row["size"]
        for row in rows:
            stream.seek(row["offset"])
            if _digest(stream, row["size"]) != row["sha256"]:
                raise NHArtError("Payload SHA256 mismatch")
        metadata = next((row for row in rows if row["resource"] == MANIFEST), None)
        if metadata is None or metadata["size"] > MAX_INDEX:
            raise NHArtError("Missing/oversized embedded manifest")
        stream.seek(metadata["offset"])
        expected_manifest = None if manifest is None else validate_manifest(
            read_json(Path(manifest).read_bytes()) if isinstance(manifest, (str, Path)) else manifest)
        manifest = validate_manifest(read_json(stream.read(metadata["size"])))
        if expected_manifest is not None and manifest != expected_manifest:
            raise NHArtError("Embedded manifest differs from expected selected manifest")
        actual = {row["resource"]: (row["size"], row["sha256"]) for row in rows if row != metadata}
        expected = {entry["resource"]: (entry["size"], entry["sha256"]) for entry in manifest["entries"]}
        if actual != expected:
            raise NHArtError("Embedded manifest does not match archive inventory")
        return {"format": 1, "size": size, "indexOffset": index_offset,
                "entryCount": count, "manifest": manifest, "entries": rows}


def pack(manifest, input_root, output):
    """Pack only explicit selections; replace output only after staged verification."""
    manifest = validate_manifest(manifest)
    root = Path(input_root).resolve(strict=True)
    if not root.is_dir():
        raise NHArtError("Input root must be a directory")
    output = Path(output)
    sources = {}
    for entry in manifest["entries"]:
        source = root / entry["source"]
        component = root
        for part in entry["source"].split("/"):
            component = component / part
            if component.is_symlink():
                raise NHArtError("Selected source file/directory symlinks are forbidden")
        resolved = source.resolve(strict=True)
        if not resolved.is_relative_to(root) or not resolved.is_file():
            raise NHArtError("Selected source is not a regular file under input root")
        if resolved == output.resolve():
            raise NHArtError("Output cannot replace a selected source")
        with resolved.open("rb") as stream:
            if os.fstat(stream.fileno()).st_size != entry["size"] or _digest(stream, entry["size"]) != entry["sha256"]:
                raise NHArtError("Selected source size/SHA256 mismatch")
        sources[entry["resource"]] = resolved
    metadata = json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if len(metadata) > MAX_INDEX:
        raise NHArtError("Embedded manifest too large")
    names = sorted([MANIFEST, *sources], key=lambda name: name.encode("utf-8"))
    if sum(RECORD.size + len(name.encode("utf-8")) for name in names) > MAX_INDEX:
        raise NHArtError("Index too large")
    entries = {entry["resource"]: entry for entry in manifest["entries"]}
    staging = None
    try:
        with tempfile.NamedTemporaryFile(mode="w+b", prefix=".nhart-stage-", dir=output.parent, delete=False) as stream:
            staging = Path(stream.name)
            stream.write(bytes(HEADER.size))
            records = []
            for name in names:
                offset = stream.tell()
                if name == MANIFEST:
                    stream.write(metadata)
                    length, digest = len(metadata), hashlib.sha256(metadata).hexdigest()
                else:
                    entry = entries[name]
                    length = entry["size"]
                    with sources[name].open("rb") as source:
                        if os.fstat(source.fileno()).st_size != length:
                            raise NHArtError("Selected source changed during packing")
                        digest = _digest(source, length, stream)
                    if digest != entry["sha256"]:
                        raise NHArtError("Selected source changed during packing")
                encoded = name.encode("utf-8")
                records.append(RECORD.pack(len(encoded), 0, offset, length, bytes.fromhex(digest)) + encoded)
            index_offset = stream.tell()
            for record in records:
                stream.write(record)
            stream.seek(0)
            stream.write(HEADER.pack(MAGIC, 1, 0, index_offset, len(records)))
            stream.flush()
            os.fsync(stream.fileno())
        result = verify(staging)
        os.replace(staging, output)
        return result
    finally:
        if staging is not None and staging.exists():
            staging.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    packing = subcommands.add_parser("pack")
    packing.add_argument("--manifest", required=True, type=Path)
    packing.add_argument("--input-root", required=True, type=Path)
    packing.add_argument("--output", required=True, type=Path)
    for command in ("verify", "inspect"):
        inspection = subcommands.add_parser(command)
        inspection.add_argument("archive", type=Path)
        inspection.add_argument("--manifest", type=Path, help="Require exact canonical selected manifest match")
    args = parser.parse_args(argv)
    try:
        if args.command == "pack":
            if args.output.resolve() == args.manifest.resolve():
                raise NHArtError("Output cannot replace input manifest")
            result = pack(read_json(args.manifest.read_bytes()), args.input_root, args.output)
        else:
            result = verify(args.archive, args.manifest)
        print(json.dumps(result if args.command == "inspect" else {
            "verified": True, "entryCount": result["entryCount"], "size": result["size"]}, sort_keys=True))
        return 0
    except (NHArtError, OSError) as error:
        # Do not disclose absolute input paths through filesystem exception text.
        parser.exit(2, "NHART: " + (str(error) if isinstance(error, NHArtError) else "File operation failed") + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
