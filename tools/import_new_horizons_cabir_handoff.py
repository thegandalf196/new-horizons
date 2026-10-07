#!/usr/bin/env python3
"""Build a private, incomplete VCMI overlay candidate from the Cabir handoff.

The tool never edits the handoff package, the live New Horizons module, or game
configuration. It copies the supplied walk/melee/ranged/icon/projectile pixels
into a detached build/ tree and emits descriptors for only the supported
front-facing groups. Missing battle groups are listed in candidate-manifest.json;
this output is not a complete or playable creature installation.
"""

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import sys

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACKAGE = ROOT / "output/handoff-20261007/cabir"
DEFAULT_OUTPUT = ROOT / "build/nh-cabir-handoff-overlay-v1-reviewed"
HANDOFF_MANIFEST_SHA256 = "b70669b1595c3cb4a8d5624e82ad28b2f5f5f769d622746c081309ccebe30350"

WALK_SOURCE_CANVAS = (128, 112)
BATTLE_CANVAS = (450, 400)
WALK_SOURCE_BASELINE_Y = 86
BATTLE_BASELINE_Y = 268
BATTLE_ROOT_X = 196.5
MELEE_CANVAS = (450, 400)
RANGED_CANVAS = (450, 400)
ICON_SIZES = {
    "large": (58, 64),
    "small": (32, 32),
    "portrait-cutout": (58, 64),
}
PROJECTILE_CANVAS = (50, 50)
PROJECTILE_ANGLES = (90, 72, 45, 27, 0, -27, -45, -72, -90)

FORMS = {
    "cabir": {
        "package": "cabir",
        "battleDescriptor": "NH_CabirHandoff.json",
        "mapDescriptor": "NH_CabirHandoffMap.json",
        "battleBase": "cabir-handoff/cabir/battle/",
        "mapBase": "cabir-handoff/cabir/map/",
        "icons": {
            "large": "cabir-large-58x64.png",
            "small": "cabir-small-32x32.png",
            "portrait-cutout": "cabir-portrait-cutout-58x64.png",
        },
        "runtimeIcons": {
            "large": "NH_cabir_handoff_icon_large.png",
            "small": "NH_cabir_handoff_icon_small.png",
            "portrait-cutout": "NH_cabir_handoff_portrait_cutout.png",
        },
    },
    "cabir-master": {
        "package": "cabir-master",
        "battleDescriptor": "NH_CabirMasterHandoff.json",
        "mapDescriptor": "NH_CabirMasterHandoffMap.json",
        "battleBase": "cabir-handoff/cabir-master/battle/",
        "mapBase": "cabir-handoff/cabir-master/map/",
        "icons": {
            "large": "cabir-master-large-58x64.png",
            "small": "cabir-master-small-32x32.png",
            "portrait-cutout": "cabir-master-portrait-cutout-58x64.png",
        },
        "runtimeIcons": {
            "large": "NH_cabir_master_handoff_icon_large.png",
            "small": "NH_cabir_master_handoff_icon_small.png",
            "portrait-cutout": "NH_cabir_master_handoff_portrait_cutout.png",
        },
    },
}

MOD_PREFIX = Path("overlay/Mods/new-horizons")
CONTENT_PREFIX = MOD_PREFIX / "Content"
IMAGE_PREFIX = MOD_PREFIX / "Images"
PROJECTILE_DESCRIPTOR = "NH_CabirHandoffFireball.json"
PROJECTILE_BASE = "cabir-handoff/projectile/"
MISSING_BATTLE_GROUPS = {
    "1": "mouse-over animation not supplied",
    "3": "hit/reaction animation not supplied",
    "4": "defence animation not supplied",
    "5": "death animation not supplied",
    "6": "alternate ranged-death animation not supplied",
    "7": "turn-left animation not supplied",
    "8": "turn-right animation not supplied",
    "11": "upward melee animation not supplied",
    "13": "downward melee animation not supplied",
    "14": "upward shooting animation not supplied",
    "16": "downward shooting animation not supplied",
    "17": "upward special/cast animation not supplied",
    "18": "front special/repair animation not supplied",
    "19": "downward special/cast animation not supplied",
    "20": "movement-start animation not supplied",
    "21": "movement-end animation not supplied",
    "22": "dead-state animation not supplied",
    "23": "alternate ranged-dead-state animation not supplied",
    "24": "resurrection animation not supplied",
    "25": "frozen/petrified animation not supplied",
    "30": "upward cast animation not supplied",
    "31": "front cast/repair animation not supplied",
    "32": "downward cast animation not supplied",
    "40": "upward group-attack animation not supplied",
    "41": "front group-attack animation not supplied",
    "42": "downward group-attack animation not supplied",
    "50": "teleport-start animation not supplied",
    "51": "teleport-end animation not supplied",
}


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _read_json(path: Path, description: str) -> dict:
    if path.is_symlink():
        raise ValueError(f"{description} must not be a symlink")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {description}: {path}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{description} must be a JSON object")
    return value


def _checked_package_file(package_root: Path, relative: str, pins: dict) -> tuple[Path, bytes, str]:
    rel_path = Path(relative)
    if rel_path.is_absolute() or ".." in rel_path.parts:
        raise ValueError(f"unsafe package path: {relative}")
    current = package_root
    for component in rel_path.parts:
        current = current / component
        if current.is_symlink():
            raise ValueError(f"package input must not use symlinks: {relative}")
    resolved = current.resolve()
    try:
        resolved.relative_to(package_root.resolve())
    except ValueError as error:
        raise ValueError(f"package input escaped its root: {relative}") from error
    if not resolved.is_file():
        raise ValueError(f"required package asset is missing: {relative}")
    expected = pins.get(relative)
    if not isinstance(expected, str) or len(expected) != 64:
        raise ValueError(f"handoff manifest has no SHA-256 pin for {relative}")
    data = resolved.read_bytes()
    actual = _sha256(data)
    if actual != expected:
        raise ValueError(f"handoff asset hash mismatch for {relative}: expected {expected}, got {actual}")
    return resolved, data, actual


def _validate_handoff_contract(
    package_root: Path,
    expected_manifest_sha256: str | None = HANDOFF_MANIFEST_SHA256,
) -> tuple[dict, dict, dict, str]:
    if package_root.is_symlink() or not package_root.is_dir():
        raise ValueError("package must be an existing non-symlink directory")
    package_root = package_root.resolve()
    manifest_path = package_root / "HANDOFF_MANIFEST.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError("handoff manifest is missing or is a symlink")
    manifest_bytes = manifest_path.read_bytes()
    manifest_sha256 = _sha256(manifest_bytes)
    if expected_manifest_sha256 is not None and manifest_sha256 != expected_manifest_sha256:
        raise ValueError(
            "handoff manifest identity changed: expected "
            f"{expected_manifest_sha256}, got {manifest_sha256}"
        )
    try:
        handoff = json.loads(manifest_bytes)
    except json.JSONDecodeError as error:
        raise ValueError("handoff manifest is not valid JSON") from error
    if not isinstance(handoff, dict):
        raise ValueError("handoff manifest must be a JSON object")
    if handoff.get("approval") != "User approved Cabir forms/ember projectile 2026-10-07":
        raise ValueError("handoff manifest is not the reviewed Cabir approval package")
    coverage = handoff.get("coverage")
    if not isinstance(coverage, dict) or coverage.get("nativeFrames") != 38 or coverage.get("icons") != 6 or coverage.get("projectileAngles") != 9:
        raise ValueError("handoff manifest coverage does not match the reviewed 38-frame/6-icon/9-angle package")
    pins = handoff.get("sha256")
    if not isinstance(pins, dict):
        raise ValueError("handoff manifest is missing its SHA-256 file map")

    integration = _read_json(package_root / "integration-map.json", "integration map")
    animations = integration.get("animations", {})
    expected_animations = {
        "walk": {"group": 0, "countPerForm": 7, "canvas": [128, 112], "feetBaselineY": 86},
        "melee": {"group": 12, "countPerForm": 6, "canvas": [450, 400], "feetBaselineY": 268, "rootX": 196.5},
        "ranged": {
            "group": 15,
            "countPerForm": 6,
            "canvas": [450, 400],
            "feetBaselineY": 268,
            "rootX": 196.5,
            "releasePoseIndex": 3,
            "mouthCanvasXY": [230, 223],
        },
    }
    for family, expected in expected_animations.items():
        actual = animations.get(family)
        if not isinstance(actual, dict) or any(actual.get(key) != value for key, value in expected.items()):
            raise ValueError(f"handoff integration-map {family} contract changed")
    expected_icons = {
        "small": list(ICON_SIZES["small"]),
        "large": list(ICON_SIZES["large"]),
        "portraitCutout": list(ICON_SIZES["portrait-cutout"]),
    }
    if integration.get("icons") != expected_icons:
        raise ValueError("handoff icon canvas contract changed")
    projectile = integration.get("projectile", {})
    if projectile.get("frames") != 9 or projectile.get("canvas") != [50, 50] or projectile.get("horizontalAngleFrame") != 4:
        raise ValueError("handoff projectile contract changed")
    return handoff, integration, pins, manifest_sha256


def _read_rgba(path: Path, data: bytes, expected_size: tuple[int, int], label: str) -> Image.Image:
    try:
        with Image.open(BytesIO(data)) as opened:
            if opened.format != "PNG" or opened.mode != "RGBA" or getattr(opened, "is_animated", False):
                raise ValueError(f"{label} must be a static RGBA PNG")
            if opened.size != expected_size:
                raise ValueError(f"{label} expected {expected_size[0]}x{expected_size[1]}, got {opened.width}x{opened.height}")
            opened.load()
            return opened.copy()
    except (OSError, ValueError) as error:
        if isinstance(error, ValueError):
            raise
        raise ValueError(f"could not decode {label}") from error


def _source_asset(package_root: Path, pins: dict, relative: str, expected_size: tuple[int, int]) -> tuple[bytes, str, Image.Image]:
    path, data, digest = _checked_package_file(package_root, relative, pins)
    image = _read_rgba(path, data, expected_size, relative)
    return data, digest, image


def _walk_translation(first_walk: Image.Image) -> tuple[tuple[int, int], dict]:
    alpha = first_walk.getchannel("A")
    frame_box = alpha.getbbox()
    if frame_box is None or frame_box[3] != WALK_SOURCE_BASELINE_Y:
        raise ValueError("walk frame 00 must retain the authored foot baseline y=86")
    row_y = WALK_SOURCE_BASELINE_Y - 1
    row_box = alpha.crop((0, row_y, WALK_SOURCE_CANVAS[0], row_y + 1)).getbbox()
    if row_box is None:
        raise ValueError("walk frame 00 has no visible foot pixels on baseline row y=85")
    source_anchor_x = (row_box[0] + row_box[2]) / 2.0
    dx = BATTLE_ROOT_X - source_anchor_x
    dy = BATTLE_BASELINE_Y - WALK_SOURCE_BASELINE_Y
    if not dx.is_integer():
        raise ValueError(f"walk root alignment requires a fractional pixel shift ({dx}); no resampling is allowed")
    offset = (int(dx), int(dy))
    if offset[0] < 0 or offset[1] < 0 or offset[0] + WALK_SOURCE_CANVAS[0] > BATTLE_CANVAS[0] or offset[1] + WALK_SOURCE_CANVAS[1] > BATTLE_CANVAS[1]:
        raise ValueError("fixed walk translation would clip the authored source canvas")
    return offset, {
        "method": "translate only; one offset for all frames, no scaling or per-frame recentering",
        "sourceCanvas": list(WALK_SOURCE_CANVAS),
        "targetCanvas": list(BATTLE_CANVAS),
        "sourceFootBaselineY": WALK_SOURCE_BASELINE_Y,
        "targetFootBaselineY": BATTLE_BASELINE_Y,
        "referenceFrame": 0,
        "referenceFootRowY": row_y,
        "referenceFootRowBounds": [row_box[0], row_y, row_box[2], row_y + 1],
        "referenceAnchor": [source_anchor_x, float(WALK_SOURCE_BASELINE_Y)],
        "targetAnchor": [BATTLE_ROOT_X, float(BATTLE_BASELINE_Y)],
        "offsetXY": list(offset),
        "pixelResampling": "none",
    }


def _translate_walk(image: Image.Image, offset: tuple[int, int]) -> bytes:
    canvas = Image.new("RGBA", BATTLE_CANVAS, (0, 0, 0, 0))
    # paste() performs only the authored integer translation and preserves all
    # RGBA bytes, including RGB values under transparent source pixels.
    canvas.paste(image, offset)
    stream = BytesIO()
    canvas.save(stream, format="PNG", optimize=False)
    return stream.getvalue()


def _add_output(outputs: dict[str, bytes], relative: str, data: bytes) -> None:
    if relative in outputs:
        raise ValueError(f"duplicate overlay output path: {relative}")
    outputs[relative] = data


def _sprite_sequence(group: int, frames: list[str]) -> dict:
    return {"group": group, "generateOverlay": 1, "frames": frames}


def build_overlay(
    package_dir: Path,
    *,
    expected_manifest_sha256: str | None = HANDOFF_MANIFEST_SHA256,
) -> tuple[dict[str, bytes], dict]:
    """Return all output files and provenance metadata without writing them."""
    if package_dir.is_symlink() or not package_dir.is_dir():
        raise ValueError("package must be an existing non-symlink directory")
    package_root = package_dir.resolve()
    _handoff, _integration, pins, handoff_manifest_sha256 = _validate_handoff_contract(
        package_root, expected_manifest_sha256
    )
    outputs: dict[str, bytes] = {}
    source_hashes: dict[str, str] = {}
    direct_source_destinations: dict[str, str] = {}
    walk_transforms: dict[str, dict] = {}

    for form_key, form in FORMS.items():
        source_form = form["package"]
        walk_records = []
        walk_images = []
        for index in range(7):
            source_relative = f"animations/{source_form}/walk/frame-{index:02}.png"
            data, digest, image = _source_asset(package_root, pins, source_relative, WALK_SOURCE_CANVAS)
            box = image.getchannel("A").getbbox()
            if box is None or box[3] != WALK_SOURCE_BASELINE_Y:
                raise ValueError(f"{source_relative} does not preserve the handoff foot baseline y=86")
            walk_records.append((source_relative, data, digest))
            walk_images.append(image)
        offset, transform = _walk_translation(walk_images[0])
        for index, (source_relative, data, digest) in enumerate(walk_records):
            battle_relative = f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/battle/walk/frame-{index:02}.png"
            map_relative = f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/map/walk/frame-{index:02}.png"
            _add_output(outputs, battle_relative, _translate_walk(walk_images[index], offset))
            _add_output(outputs, map_relative, data)
            source_hashes[source_relative] = digest
            direct_source_destinations[source_relative] = map_relative
        walk_transforms[form_key] = transform

        group_files: dict[str, list[str]] = {"walk": [], "holding": [], "melee": [], "ranged": []}
        group_files["walk"] = [f"walk/frame-{index:02}.png" for index in range(7)]
        for action, count in (("melee", 6), ("ranged", 6)):
            for index in range(count):
                source_relative = f"animations/{source_form}/{action}/frame-{index:02}.png"
                expected_size = MELEE_CANVAS if action == "melee" else RANGED_CANVAS
                data, digest, _image = _source_asset(package_root, pins, source_relative, expected_size)
                output_relative = f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/battle/{action}/frame-{index:02}.png"
                _add_output(outputs, output_relative, data)
                source_hashes[source_relative] = digest
                direct_source_destinations[source_relative] = output_relative
                group_files[action].append(f"{action}/frame-{index:02}.png")

        # The supplied melee's neutral first pose is the only supplied still
        # holding pose. Reuse it as group 2; no rejected/legacy frame is borrowed.
        neutral_bytes = outputs[
            f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/battle/melee/frame-00.png"
        ]
        neutral_output = f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/battle/holding/frame-00.png"
        _add_output(outputs, neutral_output, neutral_bytes)
        group_files["holding"] = ["holding/frame-00.png"]

        battle_descriptor = {
            "basepath": form["battleBase"],
            "sequences": [
                _sprite_sequence(0, group_files["walk"]),
                _sprite_sequence(2, group_files["holding"]),
                _sprite_sequence(12, group_files["melee"]),
                _sprite_sequence(15, group_files["ranged"]),
            ],
        }
        battle_descriptor_path = f"{CONTENT_PREFIX}/sprites/{form['battleDescriptor']}"
        _add_output(outputs, battle_descriptor_path, _json_bytes(battle_descriptor))

        map_descriptor = {
            "basepath": form["mapBase"],
            "sequences": [{"group": 0, "frames": [f"walk/frame-{index:02}.png" for index in range(7)]}],
        }
        map_descriptor_path = f"{CONTENT_PREFIX}/sprites/{form['mapDescriptor']}"
        _add_output(outputs, map_descriptor_path, _json_bytes(map_descriptor))

        for icon_role, source_name in form["icons"].items():
            source_relative = f"icons/{source_name}"
            data, digest, _image = _source_asset(package_root, pins, source_relative, ICON_SIZES[icon_role])
            output_relative = f"{IMAGE_PREFIX}/cabir-handoff/{form_key}/icons/{form['runtimeIcons'][icon_role]}"
            _add_output(outputs, output_relative, data)
            source_hashes[source_relative] = digest
            direct_source_destinations[source_relative] = output_relative

    if walk_transforms["cabir"]["offsetXY"] != walk_transforms["cabir-master"]["offsetXY"]:
        raise ValueError("base and Master walk inputs require different offsets; shared positioning is not established")

    projectile_frames = []
    for index in range(9):
        source_relative = f"projectile/cabir-fire-angle-{index:02}.png"
        data, digest, _image = _source_asset(package_root, pins, source_relative, PROJECTILE_CANVAS)
        output_relative = f"{IMAGE_PREFIX}/cabir-handoff/projectile/frame-{index:02}.png"
        _add_output(outputs, output_relative, data)
        source_hashes[source_relative] = digest
        direct_source_destinations[source_relative] = output_relative
        projectile_frames.append({"group": 0, "frame": index, "file": f"frame-{index:02}.png"})

    projectile_descriptor = {
        "basepath": PROJECTILE_BASE,
        "images": projectile_frames,
    }
    projectile_descriptor_path = f"{CONTENT_PREFIX}/sprites/{PROJECTILE_DESCRIPTOR}"
    _add_output(outputs, projectile_descriptor_path, _json_bytes(projectile_descriptor))

    # This manifest intentionally describes a private overlay fragment rather
    # than a complete creature module/config patch.
    metadata = {
        "schemaVersion": 1,
        "status": "PRIVATE PARTIAL RESOURCE CANDIDATE; NOT INSTALLED OR PLAYABLE",
        "sourcePackage": "output/handoff-20261007/cabir",
        "handoffManifestSha256": handoff_manifest_sha256,
        "sourcePackageApproval": "2026-10-07 Cabir handoff, selected assets hash-pinned to HANDOFF_MANIFEST.json",
        "distributionNote": "Derived game-asset visuals remain private review outputs; no approval to redistribute is implied.",
        "sourceHashes": dict(sorted(source_hashes.items())),
        "directCopyDestinations": dict(sorted(direct_source_destinations.items())),
        "spriteResources": {
            form_key: {
                "battleDescriptor": form["battleDescriptor"],
                "mapDescriptor": form["mapDescriptor"],
                "supportedBattleGroups": [0, 2, 12, 15],
                "group2Source": "supplied melee frame-00 reused as a neutral holding pose",
                "icons": form["runtimeIcons"],
            }
            for form_key, form in FORMS.items()
        },
        "supportedGroups": {
            "0": "7 supplied walk frames; one constant integer translation for battle canvas",
            "2": "supplied front-melee frame-00 reused as one neutral holding frame",
            "12": "6 supplied front-facing melee frames only",
            "15": "6 supplied front-facing ranged frames only",
        },
        "missingBattleGroups": dict(sorted(MISSING_BATTLE_GROUPS.items(), key=lambda item: int(item[0]))),
        "walkTransform": walk_transforms,
        "rangedRelease": {
            "handoffPoseIndexZeroBased": 3,
            "engineAttackClimaxFrameOneBased": 4,
            "mouthOnBattleCanvas": [230, 223],
            "projectileFramesAreAnglesNotTime": True,
            "frameAnglesDegrees": list(PROJECTILE_ANGLES),
            "horizontalAngleFrameZeroBased": 4,
            "frontMiddleProjectileOffset": [33, -42],
            "frontOffsetCalculation": "battle origin (222,265) to mouth (230,223); x = -25 + middleX, y = middleY",
            "upperAndLowerProjectileOrigins": "not supplied; do not bind directional shooting yet",
        },
        "moduleConfigChanged": False,
        "gameplayChanged": False,
        "installationReady": False,
    }
    metadata["outputHashes"] = {path: _sha256(data) for path, data in sorted(outputs.items())}
    outputs["candidate-manifest.json"] = _json_bytes(metadata)
    return outputs, metadata


def _safe_output(output_dir: Path) -> Path:
    requested = output_dir.absolute()
    build_root = (ROOT / "build").resolve()
    if requested.is_symlink():
        raise ValueError("output directory must not be a symlink")
    resolved = requested.resolve()
    try:
        relative = resolved.relative_to(build_root)
    except ValueError as error:
        raise ValueError("private overlay output must stay below the repository build/ directory") from error
    if not relative.parts:
        raise ValueError("refusing to use the whole build/ directory as output")
    for part in relative.parts:
        if part in {".", ".."}:
            raise ValueError("invalid output directory path")
    current = build_root
    for part in relative.parts[:-1]:
        current = current / part
        if current.exists() and current.is_symlink():
            raise ValueError("output path contains a symlink directory")
    if resolved == (ROOT / "Mods").resolve() or (ROOT / "Mods").resolve() in resolved.parents:
        raise ValueError("overlay must not write into the live Mods tree")
    return resolved


def write_overlay(
    package_dir: Path,
    output_dir: Path,
    *,
    check: bool = False,
    expected_manifest_sha256: str | None = HANDOFF_MANIFEST_SHA256,
) -> dict:
    outputs, metadata = build_overlay(package_dir, expected_manifest_sha256=expected_manifest_sha256)
    destination = _safe_output(output_dir)
    if check:
        if destination.is_symlink() or not destination.is_dir():
            raise ValueError("--check requires an existing private output directory")
        actual_paths = set()
        for path in destination.rglob("*"):
            if path.is_symlink():
                raise ValueError(f"private output contains a symlink: {path.relative_to(destination)}")
            if path.is_file():
                actual_paths.add(path.relative_to(destination).as_posix())
        expected_paths = set(outputs)
        if actual_paths != expected_paths:
            missing = sorted(expected_paths - actual_paths)
            extra = sorted(actual_paths - expected_paths)
            raise ValueError(f"private overlay file set differs; missing={missing}, extra={extra}")
        for relative, expected in outputs.items():
            actual = (destination / relative).read_bytes()
            if actual != expected:
                raise ValueError(f"private overlay differs from pinned source: {relative}")
        return metadata

    if destination.exists():
        raise ValueError(f"refusing to overwrite existing output directory: {destination}; use --check to verify it")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    for relative, data in sorted(outputs.items()):
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return metadata


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE, help="extracted approved handoff directory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="new private output directory under build/")
    parser.add_argument("--check", action="store_true", help="verify an existing output without modifying it")
    args = parser.parse_args(argv)
    try:
        metadata = write_overlay(args.package, args.output, check=args.check)
    except (OSError, ValueError) as error:
        print(f"Cabir handoff overlay error: {error}", file=sys.stderr)
        return 2
    print(f"{metadata['status']}: {len(metadata['outputHashes'])} overlay files; output {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
