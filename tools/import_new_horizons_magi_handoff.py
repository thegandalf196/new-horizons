#!/usr/bin/env python3
"""Stage the approved Magi handoff in a private New Horizons mount tree.

The importer copies the handoff's PNG bytes unchanged into an ignored build/
candidate. It never edits the handoff, the live Mods tree, or game configuration.
The root GRAPHICS_PATCH.json is a merge contract for the integration owner, not
a game-loaded config file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HANDOFF = ROOT / "output/handoff-20261007-refreshed/magi/magi-vcmi-complete-v1"
DEFAULT_OUTPUT = ROOT / "build/nh-magi-complete-handoff-20261007"
EXPECTED_HANDOFF_MANIFEST_SHA256 = "6f580b5e02f93636198d4c511638730024bfd8c007bdd9e2cb6fe450edb740b6"

PACKAGE_RELATIVE = PurePosixPath("package/magi-vcmi-complete")
SPRITE_SUBTREE = PurePosixPath("Content/sprites")
MOUNT_ROOT = PurePosixPath("Mods/new-horizons")
PNG_COUNT = 345

BATTLE_GROUPS: dict[int, tuple[str, int]] = {
    0: ("walk", 8),
    1: ("fidget", 11),
    2: ("idle", 8),
    3: ("hit", 6),
    4: ("defend", 11),
    5: ("death", 8),
    7: ("turn-left", 2),
    8: ("turn-right", 2),
    9: ("turn-left-alt", 2),
    10: ("turn-right-alt", 2),
    11: ("melee-up", 10),
    12: ("melee", 10),
    13: ("melee-down", 10),
    14: ("shoot-up", 13),
    15: ("shoot", 13),
    16: ("shoot-down", 13),
    20: ("move-start", 2),
    21: ("move-end", 2),
}
DESCRIPTOR_PATHS = (
    PurePosixPath("AVWmage0.json"),
    PurePosixPath("AVWmagx0.json"),
    PurePosixPath("magi-vcmi-complete/magi/NH_CMAGE.json"),
    PurePosixPath("magi-vcmi-complete/archmagi/NH_CAMAGE.json"),
    PurePosixPath("magi-vcmi-complete/projectile/NH_PMAGEX.json"),
)
ICON_FILES = (
    "magi-vcmi-complete/icons/magi-small-32.png",
    "magi-vcmi-complete/icons/magi-portrait-58x64.png",
    "magi-vcmi-complete/icons/magi-portrait-foreground-58x64.png",
    "magi-vcmi-complete/icons/magi-map-attack-68.png",
    "magi-vcmi-complete/icons/magi-map-attack-69.png",
    "magi-vcmi-complete/icons/archmagi-small-32.png",
    "magi-vcmi-complete/icons/archmagi-portrait-58x64.png",
    "magi-vcmi-complete/icons/archmagi-portrait-foreground-58x64.png",
    "magi-vcmi-complete/icons/archmagi-map-attack-70.png",
    "magi-vcmi-complete/icons/archmagi-map-attack-71.png",
)
ARCHMAGE_REMOVALS = ["map", "mapMask", "mapAttackFromLeft", "mapAttackFromRight"]


class ImportValidationError(ValueError):
    """Invalid, unpinned, incomplete, or unsafe handoff input/output."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _safe_relative(value: str, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ImportValidationError(f"{label} must be a nonempty POSIX-relative path")
    relative = PurePosixPath(value)
    if relative.is_absolute() or any(part in {".", ".."} for part in relative.parts):
        raise ImportValidationError(f"unsafe {label}: {value}")
    return relative


def _checked_file(root: Path, relative: PurePosixPath, label: str) -> Path:
    if root.is_symlink() or not root.is_dir():
        raise ImportValidationError(f"{label} root must be an existing non-symlink directory")
    current = root
    for component in relative.parts:
        current = current / component
        if current.is_symlink():
            raise ImportValidationError(f"{label} must not use symlinks: {relative}")
    resolved = current.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise ImportValidationError(f"{label} escaped its root: {relative}") from error
    if not resolved.is_file():
        raise ImportValidationError(f"missing {label}: {relative}")
    return resolved


def _load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ImportValidationError(f"cannot read {label}: {path}") from error
    if not isinstance(value, dict):
        raise ImportValidationError(f"{label} must be a JSON object")
    return value


def _walk_files(root: Path) -> list[Path]:
    if root.is_symlink() or not root.is_dir():
        raise ImportValidationError(f"sprite source must be an existing non-symlink directory: {root}")
    result = []
    for current, directories, filenames in os.walk(root, followlinks=False):
        current_path = Path(current)
        for directory in directories:
            if (current_path / directory).is_symlink():
                raise ImportValidationError(f"sprite source contains a symlink directory: {current_path / directory}")
        for filename in filenames:
            path = current_path / filename
            if path.is_symlink():
                raise ImportValidationError(f"sprite source contains a symlink file: {path}")
            if path.is_file():
                result.append(path)
    return sorted(result)


def _read_pinned(
    handoff_root: Path,
    package_root: Path,
    relative: PurePosixPath,
    pins: dict[str, Any],
) -> bytes:
    try:
        package_root.resolve().relative_to(handoff_root.resolve())
    except ValueError as error:
        raise ImportValidationError("package input escaped the handoff root") from error
    source = _checked_file(package_root, relative, "package asset")
    manifest_key = (PACKAGE_RELATIVE / relative).as_posix()
    expected = pins.get(manifest_key)
    if not isinstance(expected, str) or len(expected) != 64:
        raise ImportValidationError(f"handoff SHA256.json has no SHA-256 for {manifest_key}")
    data = source.read_bytes()
    actual = _sha256(data)
    if actual != expected.lower():
        raise ImportValidationError(f"handoff hash mismatch for {manifest_key}: expected {expected}, got {actual}")
    return data


def _load_pins(handoff_root: Path, expected_manifest_sha256: str | None) -> dict[str, Any]:
    manifest_path = _checked_file(handoff_root, PurePosixPath("SHA256.json"), "handoff checksum manifest")
    manifest_data = manifest_path.read_bytes()
    actual_manifest = _sha256(manifest_data)
    if expected_manifest_sha256 is not None and actual_manifest != expected_manifest_sha256:
        raise ImportValidationError(
            "handoff SHA256.json identity changed: expected "
            f"{expected_manifest_sha256}, got {actual_manifest}"
        )
    try:
        pins = json.loads(manifest_data)
    except json.JSONDecodeError as error:
        raise ImportValidationError("handoff SHA256.json is invalid") from error
    if not isinstance(pins, dict) or not pins:
        raise ImportValidationError("handoff SHA256.json must contain a nonempty path-to-hash object")
    return pins


def _resource_path(resource: str, *, animation: bool = False) -> PurePosixPath:
    relative = _safe_relative(resource, "creature resource")
    if animation or relative.suffix.lower() == ".def":
        relative = relative.with_suffix(".json")
    return relative


def _read_descriptor(
    handoff_root: Path,
    package_root: Path,
    pins: dict[str, Any],
    relative: PurePosixPath,
) -> tuple[dict[str, Any], bytes]:
    data = _read_pinned(handoff_root, package_root, relative, pins)
    try:
        descriptor = json.loads(data)
    except json.JSONDecodeError as error:
        raise ImportValidationError(f"invalid sprite descriptor JSON: {relative}") from error
    if not isinstance(descriptor, dict):
        raise ImportValidationError(f"sprite descriptor must be a JSON object: {relative}")
    return descriptor, data


def _validate_descriptor(
    descriptor: dict[str, Any],
    descriptor_path: PurePosixPath,
    *,
    expected_groups: dict[int, tuple[str, int]],
    image_names: dict[str, bytes],
    handoff_root: Path,
    package_root: Path,
    pins: dict[str, Any],
    pattern: str,
    selection_overlay: bool,
) -> tuple[dict[str, Any], int]:
    basepath = descriptor.get("basepath")
    if not isinstance(basepath, str):
        raise ImportValidationError(f"descriptor has no string basepath: {descriptor_path}")
    safe_base = _safe_relative(basepath.rstrip("/"), "sprite basepath")
    sequences = descriptor.get("sequences")
    if not isinstance(sequences, list):
        raise ImportValidationError(f"descriptor has no sequences array: {descriptor_path}")

    indexed: dict[int, dict[str, Any]] = {}
    for sequence in sequences:
        if not isinstance(sequence, dict) or not isinstance(sequence.get("group"), int):
            raise ImportValidationError(f"malformed sequence in {descriptor_path}")
        group = sequence["group"]
        if group in indexed:
            raise ImportValidationError(f"duplicate group {group} in {descriptor_path}")
        if sequence.get("generateShadow") != 0:
            raise ImportValidationError(f"sequence {group} must preserve supplied shadows (generateShadow: 0)")
        indexed[group] = sequence
    if set(indexed) != set(expected_groups):
        raise ImportValidationError(
            f"unexpected groups in {descriptor_path}: expected {sorted(expected_groups)}, got {sorted(indexed)}"
        )

    frame_total = 0
    for group, (action, expected_count) in expected_groups.items():
        sequence = indexed[group]
        frames = sequence.get("frames")
        if not isinstance(frames, list) or len(frames) != expected_count:
            raise ImportValidationError(f"wrong frame count for {descriptor_path}, group {group}")
        expected_names = [pattern.format(action=action, index=index) for index in range(expected_count)]
        if frames != expected_names:
            raise ImportValidationError(f"frame order/name mismatch for {descriptor_path}, group {group}")
        if selection_overlay:
            if "generateOverlay" not in sequence:
                sequence["generateOverlay"] = 1
            elif sequence["generateOverlay"] != 1:
                raise ImportValidationError(f"battle group {group} must enable the VCMI selection overlay")

        for frame in frames:
            safe_frame = _safe_relative(frame, "sprite frame")
            image_relative = safe_base / safe_frame
            image_key = image_relative.as_posix()
            if image_key not in image_names:
                source_relative = SPRITE_SUBTREE / image_relative
                image_data = _read_pinned(handoff_root, package_root, source_relative, pins)
                if source_relative.suffix.lower() != ".png":
                    raise ImportValidationError(f"sprite frame does not reference a PNG: {image_key}")
                image_names[image_key] = image_data
            frame_total += 1
    return descriptor, frame_total


def _verify_package_contract(
    handoff_root: Path,
    package_root: Path,
    pins: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    mod_data = _read_pinned(handoff_root, package_root, PurePosixPath("mod.json"), pins)
    try:
        mod = json.loads(mod_data)
    except json.JSONDecodeError as error:
        raise ImportValidationError("package mod manifest is invalid JSON") from error
    if not isinstance(mod, dict):
        raise ImportValidationError("package mod manifest must be an object")
    if mod.get("modType") != "Graphical" or not isinstance(mod.get("depends"), list) or "new-horizons" not in mod["depends"]:
        raise ImportValidationError("Magi package must be a graphical overlay depending on new-horizons")
    if mod.get("creatures") != ["config/creatures/tower.json"]:
        raise ImportValidationError("Magi package must register only its Tower graphics patch")

    config_relative = PurePosixPath("Content/config/creatures/tower.json")
    config_data = _read_pinned(handoff_root, package_root, config_relative, pins)
    try:
        creatures = json.loads(config_data)
    except json.JSONDecodeError as error:
        raise ImportValidationError("package Tower graphics patch is invalid JSON") from error
    if not isinstance(creatures, dict) or set(creatures) != {"core:mage", "core:archMage"}:
        raise ImportValidationError("package must patch exactly core:mage and core:archMage")

    allowed_graphics = {
        "animation", "iconSmall", "iconLarge", "mapAttackFromRight", "mapAttackFromLeft", "missile"
    }
    graphics: dict[str, dict[str, Any]] = {}
    for creature_id in ("core:mage", "core:archMage"):
        entry = creatures[creature_id]
        if not isinstance(entry, dict) or set(entry) != {"graphics"}:
            raise ImportValidationError(f"{creature_id} override must be graphics-only")
        values = entry["graphics"]
        if not isinstance(values, dict) or set(values) != allowed_graphics:
            raise ImportValidationError(f"{creature_id} graphics keys differ from the approved package")
        for key in allowed_graphics:
            if key in values:
                if key != "missile" and not isinstance(values[key], str):
                    raise ImportValidationError(f"{creature_id} graphics.{key} must be a resource string")
                if key != "missile":
                    _safe_relative(values[key], f"{creature_id} graphics.{key}")
        graphics[creature_id] = values

    mage_missile = graphics["core:mage"].get("missile")
    arch_missile = graphics["core:archMage"].get("missile")
    if not isinstance(mage_missile, dict) or set(mage_missile) != {"projectile"}:
        raise ImportValidationError("Mage package patch must replace projectile art only")
    if (
        not isinstance(arch_missile, dict)
        or set(arch_missile) != {"attackClimaxFrame", "ray"}
        or arch_missile.get("attackClimaxFrame") != 8
        or not isinstance(arch_missile.get("ray"), list)
        or len(arch_missile["ray"]) != 5
    ):
        raise ImportValidationError("Archmage package must retain the approved five-color ray and climax frame 8")

    ray_relative = PurePosixPath("inputs/runtime-support/archmagi-red-ray.json")
    ray_data = _checked_file(handoff_root, ray_relative, "approved Archmage ray")
    ray_key = ray_relative.as_posix()
    expected_ray_hash = pins.get(ray_key)
    if not isinstance(expected_ray_hash, str) or _sha256(ray_data.read_bytes()) != expected_ray_hash.lower():
        raise ImportValidationError("approved Archmage ray does not match the handoff SHA256.json")
    ray_source = _load_json(ray_data, "approved Archmage ray")
    expected_ray = ray_source.get("archMage", {}).get("graphics", {}).get("missile")
    if arch_missile != expected_ray:
        raise ImportValidationError("package Archmage ray differs from the approved runtime-support ray")

    binding_relative = PurePosixPath("resource-bindings.json")
    bindings_data = _read_pinned(handoff_root, package_root, binding_relative, pins)
    try:
        bindings = json.loads(bindings_data)
    except json.JSONDecodeError as error:
        raise ImportValidationError("package resource-bindings.json is invalid") from error
    map_bindings = bindings.get("nativeMapOverlay")
    if not isinstance(map_bindings, dict):
        raise ImportValidationError("package resource-bindings.json is missing native map overlays")
    if map_bindings.get("magi", {}).get("resource") != "AVWmage0.json" or map_bindings.get("archmagi", {}).get("resource") != "AVWmagx0.json":
        raise ImportValidationError("native map overlays must keep AVWmage0/AVWmagx0 basenames")
    if any("NH_academy_" in Path(graphics[creature]["iconLarge"]).name for creature in graphics):
        raise ImportValidationError("large portraits must avoid generated New Horizons portrait aliases")
    return graphics, bindings


def build_overlay(
    handoff_root: Path,
    *,
    expected_manifest_sha256: str | None = EXPECTED_HANDOFF_MANIFEST_SHA256,
) -> tuple[dict[str, bytes], dict[str, int]]:
    """Return mount-tree files and counts without writing anything."""
    if handoff_root.is_symlink() or not handoff_root.is_dir():
        raise ImportValidationError("handoff root must be an existing non-symlink directory")
    handoff_root = handoff_root.resolve()
    package_root = handoff_root / PACKAGE_RELATIVE.as_posix()
    if package_root.is_symlink() or not package_root.is_dir():
        raise ImportValidationError("approved package directory is missing or is a symlink")
    package_root = package_root.resolve()
    try:
        package_root.relative_to(handoff_root)
    except ValueError as error:
        raise ImportValidationError("package directory escaped handoff root") from error

    pins = _load_pins(handoff_root, expected_manifest_sha256)
    graphics, _bindings = _verify_package_contract(handoff_root, package_root, pins)

    sprite_root = package_root / SPRITE_SUBTREE.as_posix()
    files = _walk_files(sprite_root)
    png_sources: dict[str, Path] = {}
    descriptor_sources: set[str] = set()
    for path in files:
        relative = PurePosixPath(path.relative_to(sprite_root).as_posix())
        suffix = relative.suffix.lower()
        if suffix == ".png":
            png_sources[relative.as_posix()] = path
        elif suffix == ".json":
            descriptor_sources.add(relative.as_posix())
        else:
            raise ImportValidationError(f"unexpected non-PNG/non-descriptor file in sprite package: {relative}")
    expected_descriptors = {path.as_posix() for path in DESCRIPTOR_PATHS}
    if descriptor_sources != expected_descriptors:
        raise ImportValidationError(
            "unexpected sprite descriptor inventory: "
            f"expected {sorted(expected_descriptors)}, got {sorted(descriptor_sources)}"
        )
    if len(png_sources) != PNG_COUNT:
        raise ImportValidationError(f"expected {PNG_COUNT} approved PNGs, found {len(png_sources)}")
    for creature_id, values in graphics.items():
        animation_json = _resource_path(values["animation"], animation=True)
        if animation_json.as_posix() not in descriptor_sources:
            raise ImportValidationError(f"unresolved {creature_id} animation descriptor: {values['animation']}")
        for key in ("iconSmall", "iconLarge", "mapAttackFromRight", "mapAttackFromLeft"):
            resource = _resource_path(values[key])
            if resource.as_posix() not in png_sources:
                raise ImportValidationError(f"unresolved {creature_id} graphics.{key}: {values[key]}")
    mage_projectile = _resource_path(graphics["core:mage"]["missile"]["projectile"])
    if mage_projectile.as_posix() not in descriptor_sources:
        raise ImportValidationError("unresolved Magi projectile descriptor")

    outputs: dict[str, bytes] = {}
    image_names: dict[str, bytes] = {}
    for relative in sorted(png_sources):
        rel = PurePosixPath(relative)
        data = _read_pinned(handoff_root, package_root, SPRITE_SUBTREE / rel, pins)
        image_names[relative] = data
        outputs[(MOUNT_ROOT / "Images" / rel).as_posix()] = data

    descriptor_counts = {
        PurePosixPath("magi-vcmi-complete/magi/NH_CMAGE.json"): (BATTLE_GROUPS, "{action}/{index:03d}.png", True),
        PurePosixPath("magi-vcmi-complete/archmagi/NH_CAMAGE.json"): (BATTLE_GROUPS, "{action}/{index:03d}.png", True),
        PurePosixPath("AVWmage0.json"): ({0: ("map", 30)}, "{index:03d}.png", False),
        PurePosixPath("AVWmagx0.json"): ({0: ("map", 30)}, "{index:03d}.png", False),
        PurePosixPath("magi-vcmi-complete/projectile/NH_PMAGEX.json"): ({0: ("projectile", 9)}, "red-magi-angle-{index}.png", False),
    }
    frame_counts: dict[str, int] = {}
    for relative, (expected_groups, pattern, selection_overlay) in descriptor_counts.items():
        source_relative = SPRITE_SUBTREE / relative
        descriptor, _ = _read_descriptor(handoff_root, package_root, pins, source_relative)
        descriptor, total = _validate_descriptor(
            descriptor,
            relative,
            expected_groups=expected_groups,
            image_names=image_names,
            handoff_root=handoff_root,
            package_root=package_root,
            pins=pins,
            pattern=pattern,
            selection_overlay=selection_overlay,
        )
        output_relative = (MOUNT_ROOT / "Content" / "sprites" / relative).as_posix()
        outputs[output_relative] = _json_bytes(descriptor)
        frame_counts[relative.as_posix()] = total

    # The approved package's resources are already expressed relative to the
    # New Horizons mod. Moving PNGs from Content/sprites to Images therefore
    # leaves descriptor basepaths and creature resource references unchanged.
    patch = {
        "creatures": {
            "core:mage": {
                "set": graphics["core:mage"],
                "remove": [],
            },
            "core:archMage": {
                "set": graphics["core:archMage"],
                "remove": ARCHMAGE_REMOVALS,
            },
        }
    }
    outputs["GRAPHICS_PATCH.json"] = _json_bytes(patch)
    stats = {
        "pngCount": len(png_sources),
        "descriptorCount": len(descriptor_sources),
        "copiedFileCount": len(outputs) - 1,
        "magiBattleFrames": frame_counts["magi-vcmi-complete/magi/NH_CMAGE.json"],
        "archmagiBattleFrames": frame_counts["magi-vcmi-complete/archmagi/NH_CAMAGE.json"],
        "mapFrames": frame_counts["AVWmage0.json"] + frame_counts["AVWmagx0.json"],
        "projectileAngles": frame_counts["magi-vcmi-complete/projectile/NH_PMAGEX.json"],
    }
    if stats["magiBattleFrames"] != 133 or stats["archmagiBattleFrames"] != 133 or stats["mapFrames"] != 60 or stats["projectileAngles"] != 9:
        raise ImportValidationError("approved handoff animation inventory is incomplete")
    return outputs, stats


def _safe_output(output_dir: Path) -> Path:
    build_root = ROOT / "build"
    if build_root.is_symlink():
        raise ImportValidationError("repository build directory must not be a symlink")
    requested = output_dir.absolute()
    if requested.is_symlink():
        raise ImportValidationError("private output directory must not be a symlink")
    build_absolute = build_root.absolute()
    try:
        relative = requested.relative_to(build_absolute)
    except ValueError as error:
        raise ImportValidationError("private Magi output must stay below repository build/") from error
    if not relative.parts or any(part in {".", ".."} for part in relative.parts):
        raise ImportValidationError("invalid private output path")
    current = build_absolute
    for part in relative.parts[:-1]:
        current = current / part
        if current.exists() and current.is_symlink():
            raise ImportValidationError("private output path contains a symlink directory")
    resolved = requested.resolve()
    try:
        resolved.relative_to(build_root.resolve())
    except ValueError as error:
        raise ImportValidationError("resolved output escaped repository build/") from error
    return resolved


def write_overlay(
    handoff_root: Path,
    output_dir: Path,
    *,
    check: bool = False,
    expected_manifest_sha256: str | None = EXPECTED_HANDOFF_MANIFEST_SHA256,
) -> dict[str, int]:
    outputs, stats = build_overlay(
        handoff_root,
        expected_manifest_sha256=expected_manifest_sha256,
    )
    destination = _safe_output(output_dir)
    if check:
        if destination.is_symlink() or not destination.is_dir():
            raise ImportValidationError("--check requires an existing private output directory")
        actual_paths: set[str] = set()
        for path in destination.rglob("*"):
            if path.is_symlink():
                raise ImportValidationError(f"private output contains a symlink: {path.relative_to(destination)}")
            if path.is_file():
                actual_paths.add(path.relative_to(destination).as_posix())
        expected_paths = set(outputs)
        if actual_paths != expected_paths:
            missing = sorted(expected_paths - actual_paths)
            extra = sorted(actual_paths - expected_paths)
            raise ImportValidationError(f"private output set differs; missing={missing}, extra={extra}")
        for relative, expected in outputs.items():
            if (destination / relative).read_bytes() != expected:
                raise ImportValidationError(f"private output differs from pinned package: {relative}")
        return stats

    if destination.exists():
        raise ImportValidationError(f"refusing to overwrite existing output {destination}; use --check to verify it")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".nh-magi-handoff-", dir=destination.parent) as temp_name:
        staging = Path(temp_name)
        for relative, data in sorted(outputs.items()):
            path = staging / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if destination.exists():
            raise ImportValidationError(f"refusing to replace output created during staging: {destination}")
        staging.rename(destination)
    return stats


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--handoff", type=Path, default=DEFAULT_HANDOFF, help="extracted approved Magi handoff root")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="new private output directory under build/")
    parser.add_argument("--check", action="store_true", help="verify an existing private output without modifying it")
    args = parser.parse_args(argv)
    try:
        stats = write_overlay(args.handoff, args.output, check=args.check)
    except (OSError, ImportValidationError) as error:
        print(f"Magi handoff import error: {error}", file=sys.stderr)
        return 2
    verb = "Verified" if args.check else "Imported"
    print(
        f"{verb} private Magi handoff: {stats['pngCount']} PNGs, "
        f"{stats['descriptorCount']} descriptors, {stats['magiBattleFrames'] + stats['archmagiBattleFrames']} battle frames; "
        f"output {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
