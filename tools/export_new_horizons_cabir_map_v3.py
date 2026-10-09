#!/usr/bin/env python3
"""Build pinned 64x64 offline map sprites for the barehanded Cabir v3 art.

This is a mechanical derivative exporter only: it uses the already reviewed
generated standing and four-pose walk sources, the battle export's pinned
alpha cleanup/component separation, one shared scale per source sheet, a fixed
ground pivot, and an optional horizontal mirror for the opposite encounter
facing. It does not read, copy, or modify original game sprite pixels and does
not install runtime bindings.
"""

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = ROOT / "assets/new-horizons/creatures"
sys.path.insert(0, str(ROOT / "tools"))

import export_new_horizons_cabir_battle_v3 as battle_export  # noqa: E402
import export_new_horizons_cabir_pose_preview as preview  # noqa: E402


CANVAS = (64, 64)
TARGET_PIVOT = (32.0, 60.0)
TARGET_BODY_HEIGHT = 42.0
ALPHA_THRESHOLD = 16
MATTE = (56, 49, 43, 255)
GIF_DURATION_MS = 160


@dataclass(frozen=True)
class MapFamily:
    key: str
    source_family: str
    output_relative: str
    descriptor_name: str
    descriptor_basepath: str
    encounter_left: str
    encounter_right: str


FAMILIES = {
    "cabir": MapFamily(
        key="cabir",
        source_family="cabir",
        output_relative="assets/new-horizons/creatures/cabir/v3/map-export-v1",
        descriptor_name="NH_CabirMap.json",
        descriptor_basepath="NH_cabir_v3_map/",
        encounter_left="NH_CabirEncounterLeft.png",
        encounter_right="NH_CabirEncounterRight.png",
    ),
    "cabir-master": MapFamily(
        key="cabir-master",
        source_family="cabir-master",
        output_relative="assets/new-horizons/creatures/cabir-master/v3/map-export-v1",
        descriptor_name="NH_CabirMasterMap.json",
        descriptor_basepath="NH_cabir_master_v3_map/",
        encounter_left="NH_CabirMasterEncounterLeft.png",
        encounter_right="NH_CabirMasterEncounterRight.png",
    ),
}


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _png_bytes(image: Image.Image) -> bytes:
    output = io.BytesIO()
    image.save(output, format="PNG", optimize=False)
    return output.getvalue()


def _alpha_bbox(image: Image.Image, threshold: int = ALPHA_THRESHOLD) -> tuple[int, int, int, int]:
    alpha = image.getchannel("A")
    if threshold:
        alpha = alpha.point(lambda value: 255 if value >= threshold else 0)
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("map pose has no visible alpha at the selected threshold")
    return bounds


def _round_nearest(value: float) -> int:
    # Match the existing preview helper's positive-coordinate placement rule.
    return math.floor(value + 0.5)


def _align_pose(
    name: str,
    pose: preview.NativePose,
    scale: float,
    *,
    source_role: str,
) -> tuple[Image.Image, dict]:
    source_bbox = _alpha_bbox(pose.image)
    source_body_height = source_bbox[3] - source_bbox[1]
    if source_body_height <= 0:
        raise ValueError(f"{name} has an empty alpha-{ALPHA_THRESHOLD} body bound")
    source_pivot, contact = preview._contact_pivot(pose.image)
    resized_size = (
        max(1, round(pose.image.width * scale)),
        max(1, round(pose.image.height * scale)),
    )
    left = _round_nearest(TARGET_PIVOT[0] - source_pivot[0] * scale)
    top = _round_nearest(TARGET_PIVOT[1] - source_pivot[1] * scale)
    if left < 0 or top < 0 or left + resized_size[0] > CANVAS[0] or top + resized_size[1] > CANVAS[1]:
        raise ValueError(
            f"{name} exceeds the {CANVAS[0]}x{CANVAS[1]} map canvas at uniform scale {scale:.8f}; "
            "refusing to crop or stretch the sprite"
        )
    resized = pose.image.resize(resized_size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", CANVAS, (0, 0, 0, 0))
    canvas.alpha_composite(resized, (left, top))
    output_bbox = _alpha_bbox(canvas)
    visible_height = output_bbox[3] - output_bbox[1]
    if not (40 <= visible_height <= 44):
        raise ValueError(f"{name} visible body height {visible_height}px is outside the reviewed 40–44px range")
    details = {
        "pose": name,
        "sourceRole": source_role,
        "sourcePath": pose.source_path,
        "sourceSha256": pose.source_sha256,
        "sourceOrigin": list(pose.source_origin),
        "sourceImageSize": list(pose.image.size),
        "sourceAlphaThresholdBBox": list(source_bbox),
        "sourceBodyHeight": source_body_height,
        "sourceContactPivot": list(source_pivot),
        "contactLandmark": contact,
        "uniformScaleForSourceSheet": scale,
        "targetVisibleBodyHeight": TARGET_BODY_HEIGHT,
        "resizedSourceSize": list(resized_size),
        "targetRootPivot": list(TARGET_PIVOT),
        "outputPlacement": [left, top],
        "outputAlphaThresholdBBox": list(output_bbox),
        "outputVisibleBodyHeight": visible_height,
        "outputCanvas": list(CANVAS),
        "sourceArtResizedWith": "Pillow LANCZOS; alpha retained",
    }
    return canvas, details


def _contact_sheet(frames: list[tuple[str, Image.Image]], scale: int) -> bytes:
    if scale not in (1, 4):
        raise ValueError("map review contacts support native 1x or nearest 4x only")
    columns = 2
    rows = math.ceil(len(frames) / columns)
    tile = CANVAS[0] * scale
    label_height = max(14, 14 * scale // 2)
    sheet = Image.new("RGBA", (columns * tile, rows * (tile + label_height)), MATTE)
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default()
    except OSError:
        font = None
    for index, (label, frame) in enumerate(frames):
        if frame.size != CANVAS:
            raise ValueError(f"contact frame {label} is not the native map canvas")
        x = (index % columns) * tile
        y = (index // columns) * (tile + label_height)
        resized = frame if scale == 1 else frame.resize((tile, tile), Image.Resampling.NEAREST)
        sheet.alpha_composite(resized, (x, y))
        draw.text((x + 3, y + tile + 2), label, fill=(243, 226, 196, 255), font=font)
    return _png_bytes(sheet)


def _gif_bytes(frames: list[Image.Image]) -> bytes:
    if not frames:
        raise ValueError("cannot animate an empty map walk")
    output_frames = []
    for frame in frames:
        background = Image.new("RGBA", CANVAS, MATTE)
        background.alpha_composite(frame)
        output_frames.append(background.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT))
    output = io.BytesIO()
    output_frames[0].save(
        output,
        format="GIF",
        save_all=True,
        append_images=output_frames[1:],
        duration=GIF_DURATION_MS,
        loop=0,
        disposal=2,
        optimize=False,
    )
    return output.getvalue()


def _build_family(family: str) -> dict[str, bytes]:
    try:
        spec = FAMILIES[family]
        source_config = battle_export.FAMILIES[spec.source_family]
    except KeyError as error:
        raise ValueError(f"unknown Cabir map family: {family}") from error

    standing_pose, standing_provenance = battle_export._extract_standing(
        source_config["standing"], source_config["standing_prompt"], family
    )
    walk_atlas = battle_export._extract_atlas(source_config["atlases"]["walk"])
    walk_poses = battle_export._atlas_poses(walk_atlas)
    if len(walk_poses) != 4:
        raise ValueError(f"{family} map walk requires four separated source frames; found {len(walk_poses)}")

    standing_height = _alpha_bbox(standing_pose.image)[3] - _alpha_bbox(standing_pose.image)[1]
    walk_reference_height = _alpha_bbox(walk_poses[0].image)[3] - _alpha_bbox(walk_poses[0].image)[1]
    if standing_height <= 0 or walk_reference_height <= 0:
        raise ValueError(f"{family} map scale calibration has an empty body")
    standing_scale = TARGET_BODY_HEIGHT / standing_height
    walk_scale = TARGET_BODY_HEIGHT / walk_reference_height

    standing_right, standing_details = _align_pose(
        "encounter-right-standing", standing_pose, standing_scale, source_role="pinned standing master"
    )
    standing_left = ImageOps.mirror(standing_right)
    left_details = {
        **standing_details,
        "pose": "encounter-left-standing",
        "orientation": "mechanical horizontal mirror of encounter-right-standing; no redrawing",
    }
    right_details = {
        **standing_details,
        "orientation": "unmirrored generated standing pose, facing the authored rightward direction",
    }

    walk_frames = []
    walk_details = []
    for index, pose in enumerate(walk_poses):
        frame, details = _align_pose(
            f"walk-{index:02d}", pose, walk_scale, source_role="pinned generated four-pose walk sheet"
        )
        walk_frames.append(frame)
        walk_details.append(details)

    descriptor = {
        "basepath": spec.descriptor_basepath,
        "sequences": [{
            "group": 0,
            "frames": [f"walk/frame-{index:02d}.png" for index in range(4)],
        }],
    }
    artifacts: dict[str, bytes] = {
        spec.descriptor_name: (json.dumps(descriptor, indent=2) + "\n").encode("utf-8"),
        f"{spec.descriptor_basepath}walk/frame-00.png": _png_bytes(walk_frames[0]),
        f"{spec.descriptor_basepath}walk/frame-01.png": _png_bytes(walk_frames[1]),
        f"{spec.descriptor_basepath}walk/frame-02.png": _png_bytes(walk_frames[2]),
        f"{spec.descriptor_basepath}walk/frame-03.png": _png_bytes(walk_frames[3]),
        spec.encounter_left: _png_bytes(standing_left),
        spec.encounter_right: _png_bytes(standing_right),
        "review/map-walk-contact-1x.png": _contact_sheet(
            [(f"walk {index}", frame) for index, frame in enumerate(walk_frames)], 1
        ),
        "review/map-walk-contact-4x-nearest.png": _contact_sheet(
            [(f"walk {index}", frame) for index, frame in enumerate(walk_frames)], 4
        ),
        "review/encounter-facing-contact-1x.png": _contact_sheet(
            [("left (mirrored)", standing_left), ("right", standing_right)], 1
        ),
        "review/encounter-facing-contact-4x-nearest.png": _contact_sheet(
            [("left (mirrored)", standing_left), ("right", standing_right)], 4
        ),
        "review/map-walk-preview.gif": _gif_bytes(walk_frames),
        "review/alignment.json": (json.dumps({
            "status": "offline provisional map-size raster review; no runtime install or motion acceptance",
            "family": family,
            "canvas": list(CANVAS),
            "targetRootPivot": list(TARGET_PIVOT),
            "targetVisibleBodyHeight": TARGET_BODY_HEIGHT,
            "alphaThreshold": ALPHA_THRESHOLD,
            "standingScale": standing_scale,
            "walkSheetScale": walk_scale,
            "standingSource": standing_provenance,
            "encounterPoses": {
                spec.encounter_left: left_details,
                spec.encounter_right: right_details,
            },
            "walkPoses": walk_details,
            "cleanupAndSeparation": walk_atlas.receipt,
            "encounterMirrorRule": "left is a mechanical horizontal mirror; right uses the pinned generated standing pose",
            "limitations": [
                "map walk is the existing four-frame generated draft, not a claim of complete loop quality",
                "standing and walk sheets are calibrated independently to the same 42px body height",
                "no original game sprite pixels are copied; original resource IDs below are metadata references only",
            ],
        }, indent=2) + "\n").encode("utf-8"),
    }
    manifest = {
        "format": 1,
        "status": "offline provisional original Cabir map-size derivatives; not installed or visually accepted",
        "family": family,
        "descriptor": spec.descriptor_name,
        "descriptorLoaderShape": "basepath plus numeric sequences[group].frames, matching the custom animation JSON route",
        "descriptorBasepath": spec.descriptor_basepath,
        "mapGroup": 0,
        "mapWalkFrames": 4,
        "mapCanvas": list(CANVAS),
        "targetRootPivot": list(TARGET_PIVOT),
        "targetVisibleBodyHeight": TARGET_BODY_HEIGHT,
        "encounterResources": {
            "left": spec.encounter_left,
            "right": spec.encounter_right,
            "canvas": list(CANVAS),
        },
        "sourcePins": {
            "standing": {
                "path": source_config["standing"].path,
                "sha256": source_config["standing"].sha256,
                "promptPath": source_config["standing_prompt"].path,
                "promptSha256": source_config["standing_prompt"].sha256,
            },
            "walk": {
                "path": source_config["atlases"]["walk"].source.path,
                "sha256": source_config["atlases"]["walk"].source.sha256,
                "promptPath": source_config["atlases"]["walk"].prompt.path,
                "promptSha256": source_config["atlases"]["walk"].prompt.sha256,
                "cleanupReceiptPath": (
                    source_config["atlases"]["walk"].cleanup_receipt.path
                    if source_config["atlases"]["walk"].cleanup_receipt else None
                ),
                "cleanupReceiptSha256": (
                    source_config["atlases"]["walk"].cleanup_receipt.sha256
                    if source_config["atlases"]["walk"].cleanup_receipt else None
                ),
            },
        },
        "referenceOnlyOriginalResourceMetadata": {
            "mapAnimation": {"resource": "AVWgrem0/AVWgrex0", "group": 0, "frameCount": 8, "canvas": [64, 64]},
            "encounter": {
                "resource": "AvWattak",
                "frameIndices": {
                    "right": 56 if family == "cabir" else 58,
                    "left": 57 if family == "cabir" else 59,
                },
            },
            "provenanceNote": "Original resource names/geometry metadata only; no purchaser game pixel data is included.",
        },
        "sourceFilesModified": False,
        "gameplayOrRuntimeBindingIncluded": False,
        "files": {path: _sha256_bytes(data) for path, data in sorted(artifacts.items())},
    }
    artifacts["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    return artifacts


def _external_workspace(workspace_root: Path | None) -> Path:
    if workspace_root is None:
        raise ValueError("explicit external workspace root is required")
    resolved = Path(workspace_root).resolve()
    if resolved == ROOT or ROOT in resolved.parents:
        raise ValueError("map authoring workspace must be outside the checkout")
    return resolved


def _output_path(family: str, workspace_root: Path | None = None) -> Path:
    workspace = _external_workspace(workspace_root)
    try:
        return workspace / FAMILIES[family].output_relative
    except KeyError as error:
        raise ValueError(f"unknown Cabir map family: {family}") from error


def _validate_output_path(output: Path) -> None:
    resolved = output.resolve()
    if resolved == ROOT or ROOT in resolved.parents:
        raise ValueError("map export output must be outside the checkout")
    if output.is_symlink() or output.exists():
        raise FileExistsError(f"refusing to replace existing Cabir map export: {output}")


def _write_artifacts(output: Path, artifacts: dict[str, bytes]) -> None:
    _validate_output_path(output)
    output.mkdir(parents=True, exist_ok=False)
    for relative, data in sorted(artifacts.items()):
        target = output / relative
        if not target.resolve().is_relative_to(output.resolve()):
            raise ValueError(f"map export path escapes its new output directory: {relative}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def _check_artifacts(output: Path, artifacts: dict[str, bytes]) -> None:
    if output.is_symlink() or not output.is_dir():
        raise FileNotFoundError(f"pinned map export directory is missing or symlinked: {output}")
    expected = set(artifacts)
    actual = {
        path.relative_to(output).as_posix()
        for path in output.rglob("*")
        if path.is_file() and not path.is_symlink()
    }
    if actual != expected:
        raise ValueError(f"map export file set differs; missing={sorted(expected-actual)} extra={sorted(actual-expected)}")
    for relative, data in artifacts.items():
        path = output / relative
        if path.is_symlink() or _sha256(path) != _sha256_bytes(data):
            raise ValueError(f"map export artifact differs from its pinned source/reduction: {relative}")


def build_family(family: str, workspace_root: Path | None = None) -> tuple[Path, dict[str, bytes]]:
    if family not in FAMILIES:
        raise ValueError(f"unknown Cabir map family: {family}")
    workspace = _external_workspace(workspace_root)
    # Reuse the pinned extraction implementation on an explicit external source
    # mirror. Its global root is restored even when a pin or geometry fails.
    previous_source_root = battle_export.ROOT
    try:
        battle_export.ROOT = workspace
        return _output_path(family, workspace), _build_family(family)
    finally:
        battle_export.ROOT = previous_source_root


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", choices=("cabir", "cabir-master", "all"), default="all")
    parser.add_argument("--check", action="store_true", help="recompute and verify exact existing output bytes")
    parser.add_argument("--workspace-root", type=Path, required=True,
                        help="External authoring mirror for source inputs and staged map exports")
    args = parser.parse_args()
    names = tuple(FAMILIES) if args.family == "all" else (args.family,)
    try:
        workspace = _external_workspace(args.workspace_root)
        built = [build_family(name, workspace) for name in names]
        if args.check:
            for output, artifacts in built:
                _check_artifacts(output, artifacts)
        else:
            for output, _artifacts in built:
                _validate_output_path(output)
            for output, artifacts in built:
                _write_artifacts(output, artifacts)
    except (FileExistsError, FileNotFoundError, OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))
    for output, _artifacts in built:
        print(f"{'Verified' if args.check else 'Created'} offline Cabir map export: {output.relative_to(workspace)}")


if __name__ == "__main__":
    main()
