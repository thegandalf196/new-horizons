#!/usr/bin/env python3
"""Build a private full-state Cabir preview overlay from the reviewed handoff.

The output is for an isolated preview copy only. It preserves supplied walk,
melee, ranged, map, icon, and projectile pixels; unsupported battle groups alias
one of those supplied poses and are listed explicitly in the manifest. It does
not alter the live New Horizons module or Cabir gameplay data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CANDIDATE = ROOT / "build/nh-cabir-handoff-overlay-v1-reviewed"
DEFAULT_TOWER_CONFIG = ROOT / "Mods/new-horizons/Content/config/creatures/tower.json"
DEFAULT_OUTPUT = ROOT / "build/nh-cabir-wisp-preview-overlay/cabir"
EXPECTED_CANDIDATE_MANIFEST_SHA256 = "df236c112cec14f26f5396651fbca6f6e4154028e6669d908e8c5ebcdf0ee220"
EXPECTED_HANDOFF_MANIFEST_SHA256 = "b70669b1595c3cb4a8d5624e82ad28b2f5f5f769d622746c081309ccebe30350"

MOD_ROOT = "Mods/new-horizons"
PARTIAL_PREFIX = "overlay/Mods/new-horizons/"
CREATURES_RELATIVE = "Content/config/creatures/tower.json"
PREVIEW_MANIFEST_NAME = "CABIR_PREVIEW_MANIFEST.json"
CREATURE_GROUPS = (
    (0, "MOVING"),
    (1, "MOUSEON"),
    (2, "HOLDING"),
    (3, "HITTED"),
    (4, "DEFENCE"),
    (5, "DEATH"),
    (6, "DEATH_RANGED"),
    (7, "TURN_L"),
    (8, "TURN_R"),
    (11, "ATTACK_UP"),
    (12, "ATTACK_FRONT"),
    (13, "ATTACK_DOWN"),
    (14, "SHOOT_UP"),
    (15, "SHOOT_FRONT"),
    (16, "SHOOT_DOWN"),
    (17, "SPECIAL_UP"),
    (18, "SPECIAL_FRONT"),
    (19, "SPECIAL_DOWN"),
    (20, "MOVE_START"),
    (21, "MOVE_END"),
    (22, "DEAD"),
    (23, "DEAD_RANGED"),
    (24, "RESURRECTION"),
    (25, "FROZEN"),
    (30, "CAST_UP"),
    (31, "CAST_FRONT"),
    (32, "CAST_DOWN"),
    (40, "GROUP_ATTACK_UP"),
    (41, "GROUP_ATTACK_FRONT"),
    (42, "GROUP_ATTACK_DOWN"),
    (50, "TELEPORT_START"),
    (51, "TELEPORT_END"),
)

# Each unsupported group reuses a named, supplied group/frame. These aliases
# only let the isolated preview load; they are not authored animation coverage.
GROUP_ALIASES = {
    1: {"sourceGroup": 2, "reason": "mouse-over state aliases supplied neutral holding pose"},
    3: {"sourceGroup": 2, "reason": "hit reaction aliases supplied neutral holding pose"},
    4: {"sourceGroup": 2, "reason": "defence aliases supplied neutral holding pose"},
    5: {"sourceGroup": 2, "reason": "death aliases supplied neutral holding pose; no corpse pose supplied"},
    6: {"sourceGroup": 2, "reason": "ranged death aliases supplied neutral holding pose; no corpse pose supplied"},
    7: {"sourceGroup": 2, "reason": "turn-left aliases supplied neutral holding pose"},
    8: {"sourceGroup": 2, "reason": "turn-right aliases supplied neutral holding pose"},
    11: {"sourceGroup": 12, "reason": "upward melee aliases supplied front melee sequence"},
    13: {"sourceGroup": 12, "reason": "downward melee aliases supplied front melee sequence"},
    14: {"sourceGroup": 15, "reason": "upward shooting aliases supplied front spit sequence"},
    16: {"sourceGroup": 15, "reason": "downward shooting aliases supplied front spit sequence"},
    17: {"sourceGroup": 2, "reason": "upward special aliases supplied neutral holding pose"},
    18: {"sourceGroup": 2, "reason": "front special/repair aliases supplied neutral holding pose"},
    19: {"sourceGroup": 2, "reason": "downward special aliases supplied neutral holding pose"},
    20: {"sourceGroup": 0, "sourceFrame": 0, "reason": "movement-start aliases first supplied walk frame"},
    21: {"sourceGroup": 0, "sourceFrame": -1, "reason": "movement-end aliases last supplied walk frame"},
    22: {"sourceGroup": 2, "reason": "dead-state aliases supplied neutral holding pose"},
    23: {"sourceGroup": 2, "reason": "ranged dead-state aliases supplied neutral holding pose"},
    24: {"sourceGroup": 2, "reason": "resurrection aliases supplied neutral holding pose"},
    25: {"sourceGroup": 2, "reason": "frozen state aliases supplied neutral holding pose"},
    30: {"sourceGroup": 2, "reason": "upward cast aliases supplied neutral holding pose"},
    31: {"sourceGroup": 2, "reason": "front cast/repair aliases supplied neutral holding pose"},
    32: {"sourceGroup": 2, "reason": "downward cast aliases supplied neutral holding pose"},
    40: {"sourceGroup": 12, "reason": "upward group attack aliases supplied front melee sequence"},
    41: {"sourceGroup": 12, "reason": "front group attack aliases supplied front melee sequence"},
    42: {"sourceGroup": 12, "reason": "downward group attack aliases supplied front melee sequence"},
    50: {"sourceGroup": 0, "sourceFrame": 0, "reason": "teleport-start aliases first supplied walk frame"},
    51: {"sourceGroup": 0, "sourceFrame": -1, "reason": "teleport-end aliases last supplied walk frame"},
}

FORM_BINDINGS = {
    "cabir": {
        "creatureId": "core:gremlin",
        "battleDescriptor": "NH_CabirHandoff.def",
        "mapDescriptor": "NH_CabirHandoffMap.def",
        "mapAttack": "NH_CabirHandoffMap:0:0",
        "iconLarge": "cabir-handoff/cabir/icons/NH_cabir_handoff_icon_large.png",
        "iconSmall": "cabir-handoff/cabir/icons/NH_cabir_handoff_icon_small.png",
        "projectile": "NH_CabirHandoffFireball.def",
    },
    "cabir-master": {
        "creatureId": "core:masterGremlin",
        "battleDescriptor": "NH_CabirMasterHandoff.def",
        "mapDescriptor": "NH_CabirMasterHandoffMap.def",
        "mapAttack": "NH_CabirMasterHandoffMap:0:0",
        "iconLarge": "cabir-handoff/cabir-master/icons/NH_cabir_master_handoff_icon_large.png",
        "iconSmall": "cabir-handoff/cabir-master/icons/NH_cabir_master_handoff_icon_small.png",
        "projectile": "NH_CabirHandoffFireball.def",
    },
}

FRAME_ANGLES = [90, 72, 45, 27, 0, -27, -45, -72, -90]
FRONT_MOUTH_OFFSET = [33, -42]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _safe_relative(path: str) -> PurePosixPath:
    rel = PurePosixPath(path)
    if rel.is_absolute() or ".." in rel.parts or not rel.parts:
        raise ValueError(f"unsafe relative path: {path}")
    return rel


def _no_symlink_path(root: Path, relative: PurePosixPath) -> Path:
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"input contains a symlink: {relative.as_posix()}")
    resolved = current.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError(f"input escaped its root: {relative.as_posix()}") from error
    return resolved


def _parse_jsonc(data: bytes, label: str) -> dict:
    try:
        text = data.decode("utf-8")
        # Current tower override uses a standalone explanatory // comment.
        text = re.sub(r"(?m)^\s*//[^\n]*(?:\n|$)", "\n", text)
        value = json.loads(text)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{label} is not valid JSON/JSONC") from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def load_reviewed_candidate(candidate_root: Path) -> tuple[dict[str, bytes], dict]:
    candidate_root = Path(candidate_root)
    if candidate_root.is_symlink() or not candidate_root.is_dir():
        raise ValueError("reviewed Cabir candidate must be an existing non-symlink directory")
    manifest_path = candidate_root / "candidate-manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError("reviewed Cabir candidate manifest is missing or symlinked")
    manifest_bytes = manifest_path.read_bytes()
    manifest_sha = _sha256(manifest_bytes)
    if manifest_sha != EXPECTED_CANDIDATE_MANIFEST_SHA256:
        raise ValueError("reviewed Cabir candidate manifest identity changed")
    try:
        manifest = json.loads(manifest_bytes)
    except json.JSONDecodeError as error:
        raise ValueError("reviewed Cabir candidate manifest is invalid JSON") from error
    if (
        manifest.get("status") != "PRIVATE PARTIAL RESOURCE CANDIDATE; NOT INSTALLED OR PLAYABLE"
        or manifest.get("handoffManifestSha256") != EXPECTED_HANDOFF_MANIFEST_SHA256
        or manifest.get("moduleConfigChanged") is not False
        or manifest.get("gameplayChanged") is not False
        or manifest.get("installationReady") is not False
    ):
        raise ValueError("reviewed Cabir candidate does not match the expected partial handoff contract")
    output_hashes = manifest.get("outputHashes")
    if not isinstance(output_hashes, dict) or len(output_hashes) != 74:
        raise ValueError("reviewed Cabir candidate has an unexpected file inventory")

    overlay_files: dict[str, bytes] = {}
    for stored_path, expected_hash in sorted(output_hashes.items()):
        rel = _safe_relative(stored_path)
        if not stored_path.startswith(PARTIAL_PREFIX):
            raise ValueError(f"candidate file is outside its New Horizons overlay: {stored_path}")
        path = _no_symlink_path(candidate_root, rel)
        if not path.is_file():
            raise ValueError(f"candidate asset is missing: {stored_path}")
        data = path.read_bytes()
        if _sha256(data) != expected_hash:
            raise ValueError(f"reviewed candidate asset hash mismatch: {stored_path}")
        overlay_files[stored_path[len("overlay/"):]] = data
    return overlay_files, {
        "candidateManifestSha256": manifest_sha,
        "handoffManifestSha256": manifest["handoffManifestSha256"],
        "sourcePackage": manifest["sourcePackage"],
        "inputOutputHashCount": len(output_hashes),
    }


def _descriptor_groups(files: dict[str, bytes], relative: str) -> tuple[dict, dict[int, list[str]]]:
    try:
        descriptor = json.loads(files[relative])
    except (KeyError, json.JSONDecodeError) as error:
        raise ValueError(f"partial candidate is missing a valid descriptor: {relative}") from error
    sequences = descriptor.get("sequences")
    if not isinstance(sequences, list):
        raise ValueError(f"descriptor has no sequences: {relative}")
    groups: dict[int, list[str]] = {}
    for sequence in sequences:
        group = sequence.get("group")
        frames = sequence.get("frames")
        if not isinstance(group, int) or group in groups or not isinstance(frames, list) or not frames:
            raise ValueError(f"invalid sequence in descriptor: {relative}")
        groups[group] = frames
    if set(groups) != {0, 2, 12, 15}:
        raise ValueError(f"partial descriptor groups changed unexpectedly: {relative}")
    return descriptor, groups


def _aliased_frames(group: int, groups: dict[int, list[str]]) -> list[str]:
    alias = GROUP_ALIASES[group]
    frames = groups[alias["sourceGroup"]]
    if "sourceFrame" not in alias:
        return list(frames)
    index = alias["sourceFrame"]
    if index < 0:
        index += len(frames)
    if index < 0 or index >= len(frames):
        raise ValueError(f"placeholder source frame is invalid for group {group}")
    return [frames[index]]


def _full_state_descriptor(files: dict[str, bytes], descriptor_relative: str, label: str) -> tuple[bytes, dict]:
    descriptor, supplied_groups = _descriptor_groups(files, descriptor_relative)
    sequences = []
    group_aliases = {}
    for group, _name in CREATURE_GROUPS:
        if group in supplied_groups:
            frames = list(supplied_groups[group])
            source = "supplied"
        else:
            frames = _aliased_frames(group, supplied_groups)
            source = f"alias of group {GROUP_ALIASES[group]['sourceGroup']}"
            group_aliases[str(group)] = {
                "sourceGroup": GROUP_ALIASES[group]["sourceGroup"],
                "sourceFrameSelection": (
                    "first" if GROUP_ALIASES[group].get("sourceFrame") == 0 else
                    "last" if GROUP_ALIASES[group].get("sourceFrame") == -1 else
                    "all"
                ),
                "reason": GROUP_ALIASES[group]["reason"],
            }
        sequences.append({"group": group, "generateOverlay": 1, "frames": frames})

    completed = {"basepath": descriptor["basepath"], "sequences": sequences}
    return _json_bytes(completed), {
        "descriptor": descriptor_relative,
        "form": label,
        "supportedGroups": [0, 2, 12, 15],
        "aliasGroups": group_aliases,
        "completeGroupCountForPreviewLoading": len(sequences),
    }


def _patch_tower_config(base_tower_bytes: bytes) -> tuple[bytes, dict]:
    tower = _parse_jsonc(base_tower_bytes, "baseline Tower creature config")
    original = json.loads(json.dumps(tower))
    graphics_changes = {}
    for form, binding in FORM_BINDINGS.items():
        creature_id = binding["creatureId"]
        entry = tower.get(creature_id)
        if not isinstance(entry, dict) or not isinstance(entry.get("graphics"), dict):
            raise ValueError(f"baseline Tower config lacks graphics for {creature_id}")
        graphics = entry["graphics"]
        missile = graphics.get("missile")
        if not isinstance(missile, dict) or missile.get("frameAngles") != FRAME_ANGLES:
            raise ValueError(f"baseline projectile angle contract changed for {creature_id}")

        changed = {
            "animation": binding["battleDescriptor"],
            "map": binding["mapDescriptor"],
            "mapAttackFromLeft": binding["mapAttack"],
            "mapAttackFromRight": binding["mapAttack"],
            "iconLarge": binding["iconLarge"],
            "iconSmall": binding["iconSmall"],
        }
        graphics.update(changed)
        missile["projectile"] = binding["projectile"]
        missile["attackClimaxFrame"] = 4
        missile["offset"] = {
            "upperX": FRONT_MOUTH_OFFSET[0],
            "upperY": FRONT_MOUTH_OFFSET[1],
            "middleX": FRONT_MOUTH_OFFSET[0],
            "middleY": FRONT_MOUTH_OFFSET[1],
            "lowerX": FRONT_MOUTH_OFFSET[0],
            "lowerY": FRONT_MOUTH_OFFSET[1],
        }
        graphics_changes[creature_id] = {
            "graphicsFields": sorted(changed),
            "missileFields": ["projectile", "attackClimaxFrame", "offset"],
            "projectileOffsetAllDirections": list(FRONT_MOUTH_OFFSET),
            "projectileOffsetReason": "front-only spit art is aliased for all shot directions in this isolated preview",
        }

    _assert_only_preview_graphics_changed(original, tower)
    return _json_bytes(tower), graphics_changes


def _assert_only_preview_graphics_changed(before: dict, after: dict) -> None:
    if set(before) != set(after):
        raise ValueError("preview tower patch changed creature registration")
    changed_ids = set(FORM_BINDINGS[key]["creatureId"] for key in FORM_BINDINGS)
    for creature_id in before:
        if creature_id not in changed_ids:
            if before[creature_id] != after[creature_id]:
                raise ValueError(f"preview tower patch changed unrelated creature {creature_id}")
            continue
        left = json.loads(json.dumps(before[creature_id]))
        right = json.loads(json.dumps(after[creature_id]))
        left_graphics = left.pop("graphics", None)
        right_graphics = right.pop("graphics", None)
        if left != right:
            raise ValueError(f"preview tower patch changed gameplay data for {creature_id}")
        if not isinstance(left_graphics, dict) or not isinstance(right_graphics, dict):
            raise ValueError(f"preview tower patch lost graphics for {creature_id}")
        # No non-preview graphic settings may change.
        allowed_top = {"animation", "map", "mapAttackFromLeft", "mapAttackFromRight", "iconLarge", "iconSmall", "missile"}
        if {key: value for key, value in left_graphics.items() if key not in allowed_top} != {
            key: value for key, value in right_graphics.items() if key not in allowed_top
        }:
            raise ValueError(f"preview tower patch changed unrelated graphics for {creature_id}")
        left_missile = left_graphics.get("missile", {})
        right_missile = right_graphics.get("missile", {})
        allowed_missile = {"projectile", "attackClimaxFrame", "offset"}
        if {key: value for key, value in left_missile.items() if key not in allowed_missile} != {
            key: value for key, value in right_missile.items() if key not in allowed_missile
        }:
            raise ValueError(f"preview tower patch changed unrelated missile settings for {creature_id}")


def _validate_graphics_references(files: dict[str, bytes]) -> None:
    tower_path = f"{MOD_ROOT}/{CREATURES_RELATIVE}"
    try:
        tower = json.loads(files[tower_path])
    except (KeyError, json.JSONDecodeError) as error:
        raise ValueError("generated Tower graphics config is missing or invalid") from error
    for binding in FORM_BINDINGS.values():
        graphics = tower[binding["creatureId"]]["graphics"]
        for field in ("animation", "map", "missile"):
            descriptor_name = (
                graphics[field] if field != "missile" else graphics[field]["projectile"]
            ).replace(".def", ".json")
            descriptor_path = f"{MOD_ROOT}/Content/sprites/{descriptor_name}"
            if descriptor_path not in files:
                raise ValueError(f"graphics {field} descriptor is not in the preview overlay: {descriptor_path}")
        for field in ("iconLarge", "iconSmall"):
            image_path = f"{MOD_ROOT}/Images/{graphics[field]}"
            if image_path not in files:
                raise ValueError(f"graphics {field} is not in the preview overlay: {image_path}")
        for field in ("mapAttackFromLeft", "mapAttackFromRight"):
            parts = graphics[field].split(":")
            if len(parts) != 3:
                raise ValueError(f"preview {field} must identify a descriptor group and frame")
            descriptor_path = f"{MOD_ROOT}/Content/sprites/{parts[0]}.json"
            try:
                group_id, frame_index = int(parts[1]), int(parts[2])
                descriptor = json.loads(files[descriptor_path])
                sequence = next(item for item in descriptor["sequences"] if item["group"] == group_id)
                frame = sequence["frames"][frame_index]
            except (KeyError, IndexError, StopIteration, ValueError, json.JSONDecodeError) as error:
                raise ValueError(f"preview {field} references a missing map frame") from error
            image_path = f"{MOD_ROOT}/Images/{descriptor['basepath']}{frame}"
            if image_path not in files:
                raise ValueError(f"preview {field} image is missing: {image_path}")


def build_preview_overlay(partial_files: dict[str, bytes], baseline_tower_bytes: bytes) -> tuple[dict[str, bytes], dict]:
    """Transform a verified partial handoff into a full-state isolated overlay."""
    files = dict(partial_files)
    forms = {}
    for key, binding in FORM_BINDINGS.items():
        descriptor_relative = f"{MOD_ROOT}/Content/sprites/{binding['battleDescriptor'].replace('.def', '.json')}"
        descriptor_bytes, form_report = _full_state_descriptor(files, descriptor_relative, key)
        files[descriptor_relative] = descriptor_bytes
        forms[key] = form_report

    tower_bytes, graphics_changes = _patch_tower_config(baseline_tower_bytes)
    files[f"{MOD_ROOT}/{CREATURES_RELATIVE}"] = tower_bytes

    # Validate every descriptor's frame references against the emitted Images tree.
    for relative, data in files.items():
        if not relative.startswith(f"{MOD_ROOT}/Content/sprites/") or not relative.endswith(".json"):
            continue
        descriptor = json.loads(data)
        basepath = descriptor.get("basepath")
        if not isinstance(basepath, str):
            raise ValueError(f"sprite descriptor has no basepath: {relative}")
        if "sequences" in descriptor:
            for sequence in descriptor["sequences"]:
                for frame in sequence["frames"]:
                    image_path = f"{MOD_ROOT}/Images/{basepath}{frame}"
                    if image_path not in files:
                        raise ValueError(f"sprite frame is not supplied in overlay: {image_path}")
        elif "images" in descriptor:
            for image in descriptor["images"]:
                image_path = f"{MOD_ROOT}/Images/{basepath}{image['file']}"
                if image_path not in files:
                    raise ValueError(f"projectile frame is not supplied in overlay: {image_path}")
    _validate_graphics_references(files)

    report = {
        "schemaVersion": 1,
        "status": "PRIVATE ISOLATED VISUAL PREVIEW OVERLAY; NOT NORMAL GAME CONTENT",
        "handoffManifestSha256": EXPECTED_HANDOFF_MANIFEST_SHA256,
        "candidateManifestSha256": None,
        "normalPlayableChanged": False,
        "gameplayChanged": False,
        "portraitBindings": "supplied 58x64 and 32x32 handoff icons are bound directly; portrait-cutout artifacts are retained but unused",
        "projectileBindings": {
            "descriptor": "NH_CabirHandoffFireball.def",
            "attackClimaxFrameOneBased": 4,
            "angleFrames": FRAME_ANGLES,
            "allOffsets": list(FRONT_MOUTH_OFFSET),
            "directionalPoseNote": "shoot-up/down groups and their origin are front-pose placeholders",
        },
        "creatures": forms,
        "towerGraphicsPatch": graphics_changes,
        "limitations": [
            "The supplied walk, front melee, front spit, map walk, icons, and ember projectile are preserved without pixel changes.",
            "Missing creature groups alias supplied poses only to make an isolated full-state descriptor load.",
            "Death/dead groups remain standing holding-pose placeholders; no corpse animation is supplied.",
            "Directional melee and shooting are front-pose aliases, not authored directions.",
            "Master repair/special groups show a neutral holding-pose placeholder, not a repair animation.",
            "A borrowed-Wisp-stat preview is composed separately; this overlay changes Cabir graphics bindings only.",
            "This overlay is not installed in the normal New Horizons module and is not an art or gameplay completion claim.",
        ],
        "outputFileHashes": {relative: _sha256(data) for relative, data in sorted(files.items())},
    }
    return files, report


def _validate_output_path(output: Path) -> Path:
    lexical = Path(os.path.abspath(output))
    resolved = lexical.resolve()
    if lexical != resolved:
        raise ValueError("preview output path must not contain symlinks")
    try:
        resolved.relative_to((ROOT / "build").resolve())
    except ValueError as error:
        raise ValueError("private preview output must stay below repository build/") from error
    if resolved == ROOT / "build" or resolved == ROOT / "Mods/new-horizons":
        raise ValueError("refusing protected preview output path")
    return resolved


def _write_files(files: dict[str, bytes], report: dict, destination: Path) -> None:
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(f"refusing to overwrite preview output: {destination}")
    for relative, data in sorted(files.items()):
        rel = _safe_relative(relative)
        path = destination.joinpath(*rel.parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (destination / PREVIEW_MANIFEST_NAME).write_bytes(_json_bytes(report))


def write_preview(candidate: Path, baseline_tower: Path, destination: Path, *, check: bool = False) -> dict:
    candidate_files, source = load_reviewed_candidate(candidate)
    if baseline_tower.is_symlink() or not baseline_tower.is_file():
        raise ValueError("baseline tower config must be a regular file")
    baseline_bytes = baseline_tower.read_bytes()
    files, report = build_preview_overlay(candidate_files, baseline_bytes)
    report["candidateManifestSha256"] = source["candidateManifestSha256"]
    report["sourcePackage"] = source["sourcePackage"]
    report["sourceCandidateOutputHashCount"] = source["inputOutputHashCount"]
    output = _validate_output_path(destination)
    expected = dict(files)
    expected[PREVIEW_MANIFEST_NAME] = _json_bytes(report)
    if check:
        if output.is_symlink() or not output.is_dir():
            raise ValueError("preview output is missing or symlinked")
        actual = {}
        for path in output.rglob("*"):
            if path.is_symlink():
                raise ValueError("preview output contains a symlink")
            if path.is_file():
                actual[path.relative_to(output).as_posix()] = path.read_bytes()
        if actual != expected:
            raise ValueError("private Cabir preview output differs from the reproducible handoff transform")
        return report
    _write_files(files, report, output)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="create a new private full-state overlay below build/")
    mode.add_argument("--check", action="store_true", help="verify an existing generated overlay exactly")
    parser.add_argument("--candidate", type=Path, default=DEFAULT_CANDIDATE)
    parser.add_argument("--baseline-tower", type=Path, default=DEFAULT_TOWER_CONFIG)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    try:
        report = write_preview(args.candidate, args.baseline_tower, args.output, check=args.check)
    except (OSError, ValueError, FileExistsError) as error:
        parser.error(str(error))
    print(
        f"PASS: {report['status']}; {len(report['outputFileHashes'])} overlay files, "
        f"32 groups per Cabir form, source handoff {report['handoffManifestSha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
