#!/usr/bin/env python3
"""Export an offline, provisional Cabir v3 battle-animation bundle.

The tool uses pinned generated masters, the reviewed whole-component pose
splitter, and the shared native battle-canvas aligner. It never edits source
art, installs a module binding, or fills missing animation groups with fake
static frames.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = ROOT / "assets/new-horizons/creatures"
sys.path.insert(0, str(ROOT / "tools"))

import extract_new_horizons_cabir_poses as pose_splitter  # noqa: E402
import export_new_horizons_cabir_pose_preview as native_preview  # noqa: E402


ALPHA_SEED_THRESHOLD = 16
MAJOR_COMPONENT_MIN_AREA = 50_000
STANDALONE_DILATION_RADIUS = 4
MAX_DISCARDED_ALPHA = 10
SOURCE_SIDE_LIMIT = 8192
SOURCE_PIXEL_LIMIT = 8 * 1024 * 1024
MATTE = (62, 55, 52, 255)


@dataclass(frozen=True)
class PinnedFile:
    path: str
    sha256: str


@dataclass(frozen=True)
class AuxiliaryComponent:
    """An authored disconnected effect explicitly assigned to one atlas pose."""

    bbox: tuple[int, int, int, int]
    area: int
    max_alpha: int
    pose_index: int


@dataclass(frozen=True)
class AtlasSpec:
    name: str
    source: PinnedFile
    prompt: PinnedFile
    pose_names: tuple[str, str, str, str]
    auxiliary_components: tuple[AuxiliaryComponent, ...] = ()
    cleanup_pixels: tuple[tuple[int, int, int], ...] = ()  # x, y, expected alpha
    cleanup_receipt: PinnedFile | None = None


@dataclass
class ExtractedAtlas:
    spec: AtlasSpec
    frames: list[Image.Image]
    receipt: dict
    source_sha256: str


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _verify_pin(root: Path, pinned: PinnedFile, label: str) -> Path:
    unresolved = root / pinned.path
    if unresolved.is_symlink():
        raise ValueError(f"{label} must not be a symlink: {pinned.path}")
    path = unresolved.resolve()
    allowed_root = (root / "assets/new-horizons/creatures").resolve()
    try:
        path.relative_to(allowed_root)
    except ValueError as error:
        raise ValueError(f"{label} is outside the pinned generated-art roots") from error
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"{label} is missing or is a symlink: {pinned.path}")
    actual = _sha256(path)
    if actual != pinned.sha256:
        raise ValueError(f"{label} SHA-256 changed: expected {pinned.sha256}, got {actual}")
    return path


def _read_pinned_rgba(path: Path, label: str) -> Image.Image:
    try:
        with Image.open(path) as opened:
            if opened.format != "PNG" or opened.mode != "RGBA" or getattr(opened, "is_animated", False):
                raise ValueError(f"{label} must be a single-frame RGBA PNG")
            if (
                opened.width < 2
                or opened.height < 2
                or opened.width > SOURCE_SIDE_LIMIT
                or opened.height > SOURCE_SIDE_LIMIT
                or opened.width * opened.height > SOURCE_PIXEL_LIMIT
            ):
                raise ValueError(f"{label} dimensions exceed the reviewed safe limits")
            return opened.copy()
    except (OSError, ValueError) as error:
        raise ValueError(f"could not safely load {label}: {path}") from error


def _set_alpha_zero(image: Image.Image, x: int, y: int, expected: int, label: str) -> dict:
    pixel = image.getpixel((x, y))
    if pixel[3] != expected:
        raise ValueError(f"{label} cleanup pixel ({x},{y}) alpha changed: expected {expected}, got {pixel[3]}")
    # Only alpha is changed. RGB remains byte-identical for provenance.
    image.putpixel((x, y), (*pixel[:3], 0))
    return {"x": x, "y": y, "expectedAlpha": expected, "rgbPreserved": list(pixel[:3])}


def _component_runs_at(
    runs: list[tuple[int, int, int, int, int]],
    label: int,
) -> list[tuple[int, int, int, int, int]]:
    return [run for run in runs if run[0] == label]


def _source_cell_for_bbox(bbox: tuple[int, int, int, int], size: tuple[int, int]) -> int:
    x0, y0, x1, y1 = bbox
    width, height = size
    mid_x = (x0 + x1) / 2
    mid_y = (y0 + y1) / 2
    col = 0 if mid_x < width / 2 else 1
    row = 0 if mid_y < height / 2 else 1
    return row * 2 + col


def _main_component_coverage(image: Image.Image, major_components: list[dict], runs: list[tuple[int, int, int, int, int]]) -> bytearray:
    width, height = image.size
    covered = bytearray(width * height)
    for component in major_components:
        seed = bytearray(width * height)
        for label, x0, x1, y, _peak in runs:
            if label == component["label"]:
                seed[y * width + x0:y * width + x1] = b"\x01" * (x1 - x0)
        dilated = pose_splitter._dilate_region(seed, width, height, pose_splitter.DILATION_RADIUS)
        for index, included in enumerate(dilated):
            if included:
                covered[index] = 1
    return covered


def _extract_atlas(spec: AtlasSpec) -> ExtractedAtlas:
    path = _verify_pin(ROOT, spec.source, f"{spec.name} source")
    prompt_path = _verify_pin(ROOT, spec.prompt, f"{spec.name} generation prompt")
    source_hash = _sha256(path)
    original = _read_pinned_rgba(path, f"{spec.name} source")
    components, source_runs = pose_splitter._connected_components(original.getchannel("A"), ALPHA_SEED_THRESHOLD)
    major = [component for component in components if component["area"] >= MAJOR_COMPONENT_MIN_AREA]
    if len(major) != 4:
        raise ValueError(f"{spec.name} must have exactly four major source silhouettes; found {len(major)}")
    major_ordered = sorted(major, key=lambda component: pose_splitter._pose_order_key(component, original.height))
    if [_source_cell_for_bbox(tuple(item["bbox"]), original.size) for item in major_ordered] != [0, 1, 2, 3]:
        raise ValueError(f"{spec.name} major components are not the expected row-major 2x2 pose sheet")

    main_coverage = _main_component_coverage(original, major_ordered, source_runs)
    source_alpha = original.getchannel("A").tobytes()
    external_alpha = bytearray(original.width * original.height)
    for index, alpha_value in enumerate(source_alpha):
        if alpha_value and not main_coverage[index]:
            external_alpha[index] = alpha_value
    external_components, external_runs = pose_splitter._connected_components(
        Image.frombytes("L", original.size, bytes(external_alpha)), 1
    )

    cleanup_records = []
    cleanup_set = set()
    for x, y, expected_alpha in spec.cleanup_pixels:
        if not 0 <= x < original.width or not 0 <= y < original.height:
            raise ValueError(f"{spec.name} cleanup coordinate is outside the source")
        record = _set_alpha_zero(original.copy(), x, y, expected_alpha, spec.name)
        cleanup_records.append(record)
        cleanup_set.add((x, y))

    auxiliary_records = []
    for auxiliary in spec.auxiliary_components:
        matches = [
            component
            for component in external_components
            if tuple(component["bbox"]) == auxiliary.bbox
            and component["area"] == auxiliary.area
            and component["maxAlpha"] == auxiliary.max_alpha
        ]
        if len(matches) != 1:
            raise ValueError(
                f"{spec.name} authored auxiliary component pin changed: "
                f"bbox={auxiliary.bbox} area={auxiliary.area} maxAlpha={auxiliary.max_alpha}"
            )
        component = matches[0]
        if _source_cell_for_bbox(auxiliary.bbox, original.size) != auxiliary.pose_index:
            raise ValueError(f"{spec.name} auxiliary component is not safely assigned to its declared atlas pose")
        component_runs = _component_runs_at(external_runs, component["label"])
        if not component_runs:
            raise ValueError(f"{spec.name} pinned auxiliary component has no source pixels")
        auxiliary_records.append({
            "sourceGlobalBBox": list(auxiliary.bbox),
            "externalNonzeroAlphaPixelCount": auxiliary.area,
            "maximumAlpha": auxiliary.max_alpha,
            "assignedPoseIndex": auxiliary.pose_index,
            "assignedPoseName": spec.pose_names[auxiliary.pose_index],
            "reason": "explicitly retained authored detached hand/spark effect; restored byte-for-byte after silhouette separation",
        })

    work = original.copy()
    for x, y, expected_alpha in spec.cleanup_pixels:
        _set_alpha_zero(work, x, y, expected_alpha, spec.name)
    for auxiliary in spec.auxiliary_components:
        component = next(
            item for item in external_components
            if tuple(item["bbox"]) == auxiliary.bbox
            and item["area"] == auxiliary.area
            and item["maxAlpha"] == auxiliary.max_alpha
        )
        for _label, x0, x1, y, _peak in _component_runs_at(external_runs, component["label"]):
            for x in range(x0, x1):
                pixel = work.getpixel((x, y))
                work.putpixel((x, y), (*pixel[:3], 0))

    split_spec = pose_splitter.PoseSource(
        action=spec.name,
        source_path=path,
        output_dir=path.parent / ".offline-unused",
        source_sha256=source_hash,
        seed_alpha_threshold=ALPHA_SEED_THRESHOLD,
        major_component_min_area=MAJOR_COMPONENT_MIN_AREA,
    )
    frames, receipt = pose_splitter.separate_poses(work, split_spec, source_hash)

    common_x, common_y, common_right, common_bottom = receipt["commonSourceCanvas"]["globalBBox"]
    if spec.auxiliary_components:
        common_x = min(common_x, *(item.bbox[0] for item in spec.auxiliary_components))
        common_y = min(common_y, *(item.bbox[1] for item in spec.auxiliary_components))
        common_right = max(common_right, *(item.bbox[2] for item in spec.auxiliary_components))
        common_bottom = max(common_bottom, *(item.bbox[3] for item in spec.auxiliary_components))
        old_x, old_y, old_right, old_bottom = receipt["commonSourceCanvas"]["globalBBox"]
        if (common_x, common_y, common_right, common_bottom) != (old_x, old_y, old_right, old_bottom):
            expanded = []
            for frame in frames:
                canvas = Image.new("RGBA", (common_right - common_x, common_bottom - common_y), (0, 0, 0, 0))
                canvas.alpha_composite(frame, (old_x - common_x, old_y - common_y))
                expanded.append(canvas)
            frames = expanded
            receipt["commonSourceCanvas"]["globalBBox"] = [common_x, common_y, common_right, common_bottom]
            receipt["commonSourceCanvas"]["size"] = [common_right - common_x, common_bottom - common_y]
            receipt["commonSourceCanvas"]["expandedForPinnedAuxiliaryComponents"] = True
    for auxiliary in spec.auxiliary_components:
        pose_receipt = receipt["poses"][auxiliary.pose_index]
        x0, y0, x1, y1 = pose_receipt["paddedSourceBBox"]
        ax0, ay0, ax1, ay1 = auxiliary.bbox
        expanded_pose_bbox = [min(x0, ax0), min(y0, ay0), max(x1, ax1), max(y1, ay1)]
        pose_receipt["paddedSourceBBox"] = expanded_pose_bbox
        pose_receipt["sourceOffsetOnCommonCanvas"] = [expanded_pose_bbox[0] - common_x, expanded_pose_bbox[1] - common_y]
        pose_receipt.setdefault("assignedAuxiliarySourceBBoxes", []).append(list(auxiliary.bbox))
    for auxiliary in spec.auxiliary_components:
        ext_comp = next(
            value for value in external_components
            if tuple(value["bbox"]) == auxiliary.bbox
            and value["area"] == auxiliary.area
            and value["maxAlpha"] == auxiliary.max_alpha
        )
        component_runs = _component_runs_at(external_runs, ext_comp["label"])
        for _label, x0, x1, y, _peak in component_runs:
            for x in range(x0, x1):
                if (x, y) in cleanup_set:
                    continue
                px = x - common_x
                py = y - common_y
                if not (0 <= px < common_right - common_x and 0 <= py < common_bottom - common_y):
                    raise ValueError(f"{spec.name} assigned effect falls outside the common component canvas")
                frames[auxiliary.pose_index].putpixel((px, py), original.getpixel((x, y)))

    for x, y, _alpha in spec.cleanup_pixels:
        if any(0 <= x - common_x < frame.width and 0 <= y - common_y < frame.height and frame.getpixel((x - common_x, y - common_y))[3] for frame in frames):
            raise ValueError(f"{spec.name} explicit alpha cleanup was reintroduced into an exported pose")

    receipt["sourceComponentCountAtAlphaThreshold"] = len(components)
    receipt["sourceMajorComponentCount"] = len(major)
    receipt["promptPath"] = spec.prompt.path
    receipt["promptSha256"] = _sha256(prompt_path)
    receipt["inputPngIsModifiedInMemoryOnlyForPinnedOperations"] = bool(spec.cleanup_pixels or spec.auxiliary_components)
    receipt["authorizedAlphaOnlyCleanup"] = cleanup_records
    receipt["auxiliaryComponentsAssigned"] = auxiliary_records
    receipt["sourceFileModified"] = False
    if spec.cleanup_receipt:
        receipt_path = _verify_pin(ROOT, spec.cleanup_receipt, f"{spec.name} cleanup receipt")
        receipt["upstreamCleanupReceipt"] = {
            "path": spec.cleanup_receipt.path,
            "sha256": _sha256(receipt_path),
        }
    receipt["poses"] = [
        {**pose, "poseName": spec.pose_names[index]}
        for index, pose in enumerate(receipt["poses"])
    ]
    return ExtractedAtlas(spec, frames, receipt, source_hash)


def _extract_standing(pinned: PinnedFile, prompt: PinnedFile, family: str) -> tuple[native_preview.NativePose, dict]:
    path = _verify_pin(ROOT, pinned, f"{family} standing master")
    prompt_path = _verify_pin(ROOT, prompt, f"{family} standing prompt")
    source_hash = _sha256(path)
    image = _read_pinned_rgba(path, f"{family} standing master")
    components, runs = pose_splitter._connected_components(image.getchannel("A"), ALPHA_SEED_THRESHOLD)
    if not components or components[0]["area"] < MAJOR_COMPONENT_MIN_AREA:
        raise ValueError(f"{family} standing master has no dominant full-body component")
    main = components[0]
    width, height = image.size
    seed = bytearray(width * height)
    for label, x0, x1, y, _peak in runs:
        if label == main["label"]:
            seed[y * width + x0:y * width + x1] = b"\x01" * (x1 - x0)
    retained = pose_splitter._dilate_region(seed, width, height, STANDALONE_DILATION_RADIUS)
    alpha = image.getchannel("A").tobytes()
    removed = 0
    removed_histogram: dict[str, int] = {}
    pixels = bytearray(image.tobytes())
    for index, alpha_value in enumerate(alpha):
        if alpha_value == 0 or retained[index]:
            continue
        if alpha_value > MAX_DISCARDED_ALPHA:
            x, y = index % width, index // width
            raise ValueError(f"{family} standing has non-body alpha {alpha_value} at ({x},{y}); refusing cleanup")
        pixels[index * 4 + 3] = 0
        removed += 1
        removed_histogram[str(alpha_value)] = removed_histogram.get(str(alpha_value), 0) + 1
    cleaned = Image.frombytes("RGBA", image.size, bytes(pixels))
    details = {
        "status": "provisional standalone full-body pose; alpha-only exterior cleanup, no source edits",
        "sourcePath": pinned.path,
        "sourceSha256": source_hash,
        "promptPath": prompt.path,
        "promptSha256": _sha256(prompt_path),
        "sourceSize": list(image.size),
        "alphaSeedThreshold": ALPHA_SEED_THRESHOLD,
        "mainComponentArea": main["area"],
        "mainComponentAlpha16BBox": main["bbox"],
        "externalNonzeroAlphaRemoved": removed,
        "maximumRemovedAlpha": max((int(key) for key in removed_histogram), default=0),
        "removedAlphaHistogram": removed_histogram,
        "rule": "only nonzero alpha outside the 4px dilation of the largest body component is cleared; alpha must be <=10; RGB preserved",
        "sourceFileModified": False,
    }
    pose = native_preview.NativePose(
        name="holding",
        image=cleaned,
        source_path=pinned.path,
        source_sha256=source_hash,
        source_origin=(0, 0),
        extraction={
            "kind": "pinned standalone full-body generated master; no cropping or recoloring",
            "mainComponentAlpha16BBox": main["bbox"],
            "mainComponentArea": main["area"],
        },
    )
    return pose, details


def _atlas_poses(atlas: ExtractedAtlas) -> list[native_preview.NativePose]:
    common_x, common_y, _right, _bottom = atlas.receipt["commonSourceCanvas"]["globalBBox"]
    poses = []
    for index, (frame, item) in enumerate(zip(atlas.frames, atlas.receipt["poses"])):
        x0, y0, x1, y1 = item["paddedSourceBBox"]
        local_box = (x0 - common_x, y0 - common_y, x1 - common_x, y1 - common_y)
        poses.append(native_preview.NativePose(
            name=atlas.spec.pose_names[index],
            image=frame.crop(local_box),
            source_path=atlas.spec.source.path,
            source_sha256=atlas.source_sha256,
            source_origin=(x0, y0),
            extraction={
                "kind": "pinned whole connected-component crop; no quadrant clipping",
                "sourceGlobalSeedBBox": item["sourceGlobalBBox"],
                "sourceGlobalCrop": item["paddedSourceBBox"],
                "componentArea": item["seedArea"],
                "componentAlphaThreshold": item["seedThreshold"],
                "separationFrame": item["frame"],
                "assignedAuxiliaryEffects": [
                    value for value in atlas.receipt["auxiliaryComponentsAssigned"]
                    if value["assignedPoseIndex"] == index
                ],
            },
        ))
    return poses


def _font():
    try:
        return ImageFont.load_default()
    except OSError:
        return None


def _contact_master(frames: list[tuple[str, Image.Image]], factor: int = 2) -> bytes:
    """Make a labeled nearest-neighbor index of every emitted sprite frame."""
    import io
    import math

    cell_width, cell_height = 360, 200
    columns = 4
    rows = math.ceil(len(frames) / columns)
    sheet = Image.new("RGBA", (columns * cell_width, rows * cell_height), MATTE)
    draw = ImageDraw.Draw(sheet)
    font = _font()
    for index, (label, frame) in enumerate(frames):
        x, y = (index % columns) * cell_width, (index // columns) * cell_height
        draw.text((x + 4, y + 3), label, fill=(244, 231, 208, 255), font=font)
        bounds = frame.getchannel("A").getbbox()
        if bounds is None:
            raise ValueError(f"empty native frame in review contact: {label}")
        crop = frame.crop(bounds)
        available = (cell_width - 8, cell_height - 26)
        if crop.width * factor > available[0] or crop.height * factor > available[1]:
            raise ValueError(f"native preview crop exceeds the uniform {factor}x contact cell: {label}")
        resized = crop.resize((crop.width * factor, crop.height * factor), Image.Resampling.NEAREST)
        sheet.alpha_composite(resized, (x + 4 + (available[0] - resized.width) // 2, y + 22 + (available[1] - resized.height) // 2))
    buffer = io.BytesIO()
    sheet.save(buffer, format="PNG", optimize=False)
    return buffer.getvalue()


def _build_family(family: str) -> dict[str, bytes]:
    config = FAMILIES[family]
    stand_pose, stand_details = _extract_standing(config["standing"], config["standing_prompt"], family)
    atlases = {name: _extract_atlas(spec) for name, spec in config["atlases"].items()}
    atlas_poses = {name: _atlas_poses(value) for name, value in atlases.items()}

    groups: dict[str, dict] = {}

    def add_group(
        key: str,
        poses: list[native_preview.NativePose],
        *,
        reference_index: int = 0,
        calibration_pose: native_preview.NativePose | None = None,
        descriptor_slices: dict[int, tuple[int, int]] | None = None,
    ) -> None:
        helper_poses = ([calibration_pose] if calibration_pose else []) + poses
        helper_reference = 0 if calibration_pose else reference_index
        artifacts, metadata, aligned = native_preview.build_preview_group(
            key,
            helper_poses,
            reference_index=helper_reference,
        )
        frame_paths = []
        frame_start = 1 if calibration_pose else 0
        for index, frame in enumerate(aligned):
            relative = f"{config['descriptor_basepath']}frames/{key}/frame-{index:02d}.png"
            frame_paths.append(relative)
            frame_index = index - frame_start
            if frame_index >= 0:
                logical_frames.append((f"{key}:{poses[frame_index].name}", frame))
        groups[key] = {
            "artifacts": artifacts,
            "metadata": metadata,
            "descriptorFramePaths": frame_paths,
            "sequenceFrameStart": frame_start,
            "poseCount": len(poses),
            "descriptorSlices": descriptor_slices or {},
        }

    logical_frames: list[tuple[str, Image.Image]] = []
    add_group("holding", [stand_pose])
    add_group("moving", atlas_poses["walk"], reference_index=0)
    add_group("reaction", atlas_poses["reactions"], reference_index=1)
    add_group("melee-front", atlas_poses["melee_front"], reference_index=0)
    add_group(
        "melee-directions",
        atlas_poses["melee_directions"],
        calibration_pose=atlas_poses["melee_front"][0],
    )
    if family == "cabir-master":
        add_group("shoot-front", atlas_poses["shoot_front"], reference_index=0)
        add_group(
            "shoot-directions",
            atlas_poses["shoot_directions"],
            calibration_pose=atlas_poses["shoot_front"][0],
        )
        add_group("repair-front", atlas_poses["repair"], reference_index=0)

    sprite_files: dict[str, bytes] = {}
    review_files: dict[str, bytes] = {}
    alignment_index = {}
    for group_key, group in groups.items():
        prefix = f"exports/{group_key}/"
        for relative, data in group["artifacts"].items():
            if not relative.startswith(prefix):
                raise RuntimeError(f"unexpected native-preview artifact: {relative}")
            tail = relative[len(prefix):]
            if tail.startswith("frame-") and tail.endswith(".png"):
                target = f"{config['descriptor_basepath']}frames/{group_key}/{tail}"
                sprite_files[target] = data
            elif tail == "contact-sheet-4x.png":
                review_files[f"review/groups/{group_key}-contact-4x.png"] = data
            elif tail == "animation-proof.gif":
                review_files[f"review/groups/{group_key}-animation-proof.gif"] = data
            elif tail == "alignment.json":
                review_files[f"review/alignment/{group_key}.json"] = data
            else:
                raise RuntimeError(f"unrecognized preview artifact: {relative}")
        alignment_index[group_key] = group["metadata"]["scaleCalibration"]

    descriptor = _build_descriptor(family, groups, config)
    descriptor_bytes = (json.dumps(descriptor, indent=2) + "\n").encode("utf-8")
    review_files["review/contact-master-2x.png"] = _contact_master(logical_frames)
    review_files["review/family-index.json"] = (
        json.dumps({
            "status": "offline provisional preview; not installed or accepted",
            "family": family,
            "logicalCanvas": list(native_preview.LOGICAL_CANVAS),
            "targetPivot": list(native_preview.TARGET_PIVOT),
            "uniformContactNearestScale": 2,
            "frames": [label for label, _frame in logical_frames],
            "calibration": alignment_index,
        }, indent=2) + "\n"
    ).encode("utf-8")

    artifacts = {
        config["descriptor_filename"]: descriptor_bytes,
        **sprite_files,
        **review_files,
    }
    manifest = {
        "format": 1,
        "status": "offline provisional custom-animation export; no runtime binding, gameplay change, or visual acceptance",
        "family": family,
        "descriptor": config["descriptor_filename"],
        "descriptorBasepath": config["descriptor_basepath"],
        "descriptorLoaderShape": "RenderHandler initFromJson: basepath plus numeric sequences[group].frames file list",
        "canvas": list(native_preview.LOGICAL_CANVAS),
        "targetRootPivot": list(native_preview.TARGET_PIVOT),
        "referenceTargetHeightPixels": native_preview.REFERENCE_HEIGHT,
        "attackClimaxFrameZeroBased": 2,
        "directionalAttackSequence": "front ready, directional windup, directional release, shared front recovery",
        "standingSource": stand_details,
        "sourceSheets": {name: atlas.receipt for name, atlas in atlases.items()},
        "groups": {
            key: {
                **data["metadata"]["scaleCalibration"],
                "descriptorFrameStart": data["sequenceFrameStart"],
            }
            for key, data in groups.items()
        },
        "mappedGroups": descriptor["sequences"],
        "intentionalAliases": config["aliases"],
        "notProvided": config["not_provided"],
        "warnings": config["warnings"],
        "files": {
            path: hashlib.sha256(data).hexdigest()
            for path, data in sorted(artifacts.items())
        },
    }
    artifacts["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    return artifacts


def _descriptor_sequence(group: int, frame_paths: list[str]) -> dict:
    prefix = None
    # Keep the path relative to the descriptor basepath; this also rejects a
    # source/master path accidentally leaking into runtime JSON.
    relative = []
    for path in frame_paths:
        if prefix is None:
            prefix = path.split("frames/", 1)[0]
        if not path.startswith(prefix + "frames/"):
            raise ValueError("animation sequence includes a frame outside its pinned basepath")
        relative.append(path[len(prefix):])
    # RGBA sprites have no vanilla DEF palette-selection pixels. Ask both
    # render backends to derive the native contour from the alpha silhouette.
    return {"group": group, "generateOverlay": 1, "frames": relative}


def _build_descriptor(family: str, groups: dict[str, dict], config: dict) -> dict:
    def paths(key: str, start: int = 0, stop: int | None = None) -> list[str]:
        data = groups[key]
        all_paths = data["descriptorFramePaths"]
        begin = data["sequenceFrameStart"] + start
        end = None if stop is None else data["sequenceFrameStart"] + stop
        return all_paths[begin:end]

    def frame_path(key: str, index: int) -> str:
        return groups[key]["descriptorFramePaths"][index]

    sequences = [
        _descriptor_sequence(0, paths("moving")),
        _descriptor_sequence(2, paths("holding")),
        _descriptor_sequence(3, paths("reaction", 0, 1)),
        _descriptor_sequence(4, paths("reaction", 1, 2)),
        _descriptor_sequence(5, paths("reaction", 2, 4)),
        _descriptor_sequence(22, paths("reaction", 3, 4)),
        _descriptor_sequence(12, paths("melee-front")),
        _descriptor_sequence(11, [
            frame_path("melee-front", 0),
            frame_path("melee-directions", 1),
            frame_path("melee-directions", 2),
            frame_path("melee-front", 3),
        ]),
        _descriptor_sequence(13, [
            frame_path("melee-front", 0),
            frame_path("melee-directions", 3),
            frame_path("melee-directions", 4),
            frame_path("melee-front", 3),
        ]),
    ]
    if family == "cabir-master":
        sequences.extend([
            _descriptor_sequence(15, paths("shoot-front")),
            _descriptor_sequence(14, [
                frame_path("shoot-front", 0),
                frame_path("shoot-directions", 1),
                frame_path("shoot-directions", 2),
                frame_path("shoot-front", 3),
            ]),
            _descriptor_sequence(16, [
                frame_path("shoot-front", 0),
                frame_path("shoot-directions", 3),
                frame_path("shoot-directions", 4),
                frame_path("shoot-front", 3),
            ]),
            _descriptor_sequence(18, paths("repair-front")),
            _descriptor_sequence(30, paths("repair-front")),
            _descriptor_sequence(31, paths("repair-front")),
            _descriptor_sequence(32, paths("repair-front")),
        ])
    return {
        "basepath": config["descriptor_basepath"],
        "sequences": sequences,
    }


def _family_specs(root: Path) -> dict[str, dict]:
    def pin(path: str, digest: str) -> PinnedFile:
        return PinnedFile(path, digest)

    base = "cabir/v3"
    master = "cabir-master/v3"
    return {
        "cabir": {
            "asset_root": root / f"assets/new-horizons/creatures/{base}",
            "output": root / f"assets/new-horizons/creatures/{base}/battle-export-v1",
            "descriptor_filename": "NH_cabir_v3_battle.json",
            "descriptor_basepath": "NH_cabir_v3_battle/",
            "standing": pin(f"assets/new-horizons/creatures/{base}/standing-master.png", "e9e5cfacb4cafa8f55c0160a400a4f0c6ea3426d77f631c928de27eaa4033060"),
            "standing_prompt": pin(f"assets/new-horizons/creatures/{base}/standing.prompt.txt", "66a9aa4bf1be4f6f964ace0f038cde03c134556a756d87a7f6bb106a2be0a082"),
            "atlases": {
                "walk": AtlasSpec("base-walk", pin(f"assets/new-horizons/creatures/{base}/walk-v2/cleaned/candidate-01-alpha-cleaned.png", "112c20fb49091bcbf5efc5646b7ce1950f37ade03969a12c3ccedcffdecace2a"), pin(f"assets/new-horizons/creatures/{base}/walk-v2/candidate-01.prompt.txt", "255e5baf86275a2d7a91369841b054e896b2d9f006d940c2bc7b45a55d842272"), ("step-a", "step-b", "step-c", "step-d"), cleanup_receipt=pin(f"assets/new-horizons/creatures/{base}/walk-v2/cleaned/cleanup-receipt.json", "8e14fb7db2c7b2fc0d296a709c6e6b080219c68f6d8b0589092eb0937f2419a2")),
                "melee_front": AtlasSpec("base-melee-front", pin(f"assets/new-horizons/creatures/{base}/melee-front-v1/candidate-01.png", "b58a766ea7512b10315b8b82f0aa85b6a161534434745b402706da9547935efb"), pin(f"assets/new-horizons/creatures/{base}/melee-front-v1/candidate-01.prompt.txt", "012b9cdd479940915efed31bddf50a0aa31129790dd68f839c21e5086d29cb5b"), ("ready", "windup", "claw-swipe", "recovery")),
                "melee_directions": AtlasSpec("base-melee-directions", pin(f"assets/new-horizons/creatures/{base}/melee-directions-v1/candidate-01.png", "c6a7b5831041f977668a3cdb75ff58707dbf31c2fd2c4e5021974fc3c9acbbf6"), pin(f"assets/new-horizons/creatures/{base}/melee-directions-v1/candidate-01.prompt.txt", "0aa19633eeb907fce8b28e0c70f80a0d97aa47dfe662d770711d1ee6365a05e6"), ("up-windup", "up-release", "down-windup", "down-release")),
                "reactions": AtlasSpec("base-reactions", pin(f"assets/new-horizons/creatures/{base}/reactions-v1/candidate-01.png", "e42feeba9a6019b3685c756280cedf72173842fa2a652ed2ba59a33094ddaa56"), pin(f"assets/new-horizons/creatures/{base}/reactions-v1/candidate-01.prompt.txt", "10855fc06decadba78f3b5d33e234b257d8bc2c5533e298aa2df59eab338360d"), ("hit-recoil", "defend-brace", "dying", "collapsed-dead")),
            },
            "aliases": {},
            "not_provided": [1, 6, 7, 8, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 30, 31, 32, 40, 41, 42, 50, 51],
            "warnings": [
                "Walk atlas is an open-gait draft; it is exported as supplied and is not a completed loop-quality claim.",
                "The pre-cleaned walk input retains prior reviewed alpha-only output; no new movement artwork or interpolation was added.",
            ],
        },
        "cabir-master": {
            "asset_root": root / f"assets/new-horizons/creatures/{master}",
            "output": root / f"assets/new-horizons/creatures/{master}/battle-export-v1",
            "descriptor_filename": "NH_cabir_master_v3_battle.json",
            "descriptor_basepath": "NH_cabir_master_v3_battle/",
            "standing": pin(f"assets/new-horizons/creatures/{master}/standing-master.png", "c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15"),
            "standing_prompt": pin(f"assets/new-horizons/creatures/{master}/standing.prompt.txt", "3b97121db995db93b8ec57e775b147c3edfca9da5d0a3696dd471ff67390a5b7"),
            "atlases": {
                "walk": AtlasSpec("master-walk", pin(f"assets/new-horizons/creatures/{master}/walk-v1/cleaned-v1/candidate-01-alpha-cleaned.png", "66c9402eaa19e17a50840a0871150b2ed9db72062dad0511ba7b37c1e272d537"), pin(f"assets/new-horizons/creatures/{master}/walk-v1/candidate-01.prompt.txt", "088fe9244edce4bcb9401d173aa1b837c7efd31a3fed578e8f9d713721dc6731"), ("step-a", "step-b", "step-c", "step-d"), cleanup_pixels=((1078, 87, 19), (437, 1007, 17)), cleanup_receipt=pin(f"assets/new-horizons/creatures/{master}/walk-v1/cleaned-v1/cleanup-receipt.json", "16669a3e117ebea032c5759fef78d6129f6eab41d25a954ca8fa937010816111")),
                "melee_front": AtlasSpec("master-melee-front", pin(f"assets/new-horizons/creatures/{master}/melee-front-v1/candidate-01.png", "d3edb6e641348b06e30bcf4c6495c5eb8e18473317b7a300b441b28a2040bd6f"), pin(f"assets/new-horizons/creatures/{master}/melee-front-v1/candidate-01.prompt.txt", "f334c83ff3a2fdadcffe3e7594d087ed2178bbcfc591ff501a8487c8b5caaf3c"), ("ready", "windup", "claw-swipe", "recovery")),
                "melee_directions": AtlasSpec("master-melee-directions", pin(f"assets/new-horizons/creatures/{master}/melee-directions-v1/candidate-01.png", "758899e21dbec3c4417d1832aea4ea792e20073a1afc20af04f31d10f0969d91"), pin(f"assets/new-horizons/creatures/{master}/melee-directions-v1/candidate-01.prompt.txt", "0aa19633eeb907fce8b28e0c70f80a0d97aa47dfe662d770711d1ee6365a05e6"), ("up-windup", "up-release", "down-windup", "down-release")),
                "reactions": AtlasSpec("master-reactions", pin(f"assets/new-horizons/creatures/{master}/reactions-v1/candidate-01.png", "ed5bc5c94942d7c5c5c7fbc0b1712db52596ce00d94894608fdefa3eb887cf2f"), pin(f"assets/new-horizons/creatures/{master}/reactions-v1/candidate-01.prompt.txt", "d06c90701112dcbd0b4410d60c509fc7564a8568cbf179291a6c29f4ea13a58e"), ("hit-recoil", "defend-brace", "dying", "collapsed-dead")),
                "shoot_front": AtlasSpec("master-shoot-front", pin(f"assets/new-horizons/creatures/{master}/shoot-front-v1/candidate-01.png", "921c65c91a194dd56da044ed6f6299322e14f083357229f11534c73791e38dfa"), pin(f"assets/new-horizons/creatures/{master}/shoot-front-v1/candidate-01.prompt.txt", "2f3fa96c738c85f0f52df92348598fed824a44cb463c9c1cd0e9a5437d9bd20f"), ("ready", "spark-release", "palm-shot", "recovery"), auxiliary_components=(
                    AuxiliaryComponent((699, 127, 728, 208), 909, 216, 1),
                    AuxiliaryComponent((1219, 798, 1286, 889), 3023, 232, 3),
                ), cleanup_pixels=((713, 134, 14),)),
                "shoot_directions": AtlasSpec("master-shoot-directions", pin(f"assets/new-horizons/creatures/{master}/shoot-directions-v1/candidate-01.png", "a1525239c00440b83fed0c3570f4a48fe2b64868a39b5621e1e325365433a0e4"), pin(f"assets/new-horizons/creatures/{master}/shoot-directions-v1/candidate-01.prompt.txt", "7cd02c777c8e755eac9c8d84c39ebdd1d9305de32f6c514eac003ba409a9dcc9"), ("up-windup", "up-release", "down-windup", "down-release")),
                "repair": AtlasSpec("master-repair", pin(f"assets/new-horizons/creatures/{master}/repair-v1/candidate-01.png", "c6232b9f16eb4df4d42e53f8534f0e20053d91c16698b128fd7c0b6f5fb78bc8"), pin(f"assets/new-horizons/creatures/{master}/repair-v1/candidate-01.prompt.txt", "9db44d47b0ee986963e9eb9e98d34b3a416a3e9411c8419c45e0e0c4b22673d6"), ("repair-ready", "reach", "focus", "recovery"), auxiliary_components=(
                    AuxiliaryComponent((556, 841, 597, 906), 747, 239, 2),
                    AuxiliaryComponent((505, 983, 545, 1006), 407, 244, 2),
                )),
            },
            "aliases": {
                "18": [31],
                "30": [31],
                "32": [31],
            },
            "not_provided": [1, 6, 7, 8, 17, 19, 20, 21, 23, 24, 25, 40, 41, 42, 50, 51],
            "warnings": [
                "Master walk has two exact, documented single-pixel alpha removals (source coordinates (1078,87) alpha 19 and (437,1007) alpha 17); RGB bytes and source master are preserved.",
                "Repair spark components are assigned to the lower-left focus pose by pinned source geometry; their source RGBA is preserved.",
                "Master shooting detached spark components are assigned to their row-major authored poses; one isolated alpha-14 fringe pixel at (713,134) is cleared by alpha only.",
                "Repair groups 18/30/31/32 intentionally alias one close-front repair gesture; this does not claim separate directional repair art.",
            ],
        },
    }


FAMILIES = _family_specs(ROOT)


def _validate_detached_output(path: Path) -> None:
    resolved = path.resolve()
    assets_root = (ROOT / "assets/new-horizons/creatures").resolve()
    try:
        resolved.relative_to(assets_root)
    except ValueError as error:
        raise ValueError("battle export output must remain under the pinned Cabir generated-art tree") from error
    if path.is_symlink() or path.exists():
        raise FileExistsError(f"refusing to replace existing Cabir battle export: {path}")


def _write_artifacts(output: Path, artifacts: dict[str, bytes]) -> None:
    _validate_detached_output(output)
    output.mkdir(parents=True, exist_ok=False)
    for relative, data in sorted(artifacts.items()):
        target = output / relative
        if not target.resolve().is_relative_to(output.resolve()):
            raise ValueError(f"export path escapes its new output directory: {relative}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def _check_artifacts(output: Path, artifacts: dict[str, bytes]) -> None:
    if output.is_symlink() or not output.is_dir():
        raise FileNotFoundError(f"pinned battle export directory is missing or is a symlink: {output}")
    expected = set(artifacts)
    actual = {
        path.relative_to(output).as_posix()
        for path in output.rglob("*")
        if path.is_file() and not path.is_symlink()
    }
    if actual != expected:
        raise ValueError(f"battle export file set differs; missing={sorted(expected-actual)} extra={sorted(actual-expected)}")
    for relative, data in artifacts.items():
        path = output / relative
        if path.is_symlink() or _sha256(path) != hashlib.sha256(data).hexdigest():
            raise ValueError(f"battle export artifact differs from its pinned source/reduction: {relative}")


def _validate_previous_export(output: Path, family: str) -> None:
    """Allow refresh only when the existing bundle proves its own exact bytes."""
    if output.is_symlink() or not output.is_dir():
        raise FileNotFoundError(f"cannot refresh missing or symlinked export: {output}")
    for item in output.rglob("*"):
        if item.is_symlink():
            raise ValueError(f"cannot refresh export containing a symlink: {item.relative_to(output)}")
    manifest_path = output / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("cannot refresh an export without its integrity manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("family") != family or not manifest.get("status", "").startswith("offline provisional"):
        raise ValueError("existing export is not a known offline Cabir battle bundle")
    expected = manifest.get("files")
    if not isinstance(expected, dict):
        raise ValueError("existing export manifest has no artifact hash map")
    actual = {
        item.relative_to(output).as_posix()
        for item in output.rglob("*")
        if item.is_file() and item.name != "manifest.json"
    }
    if actual != set(expected):
        raise ValueError("existing export file set does not match its own manifest")
    for relative, digest in expected.items():
        candidate = output / relative
        if candidate.is_symlink() or not candidate.is_file() or _sha256(candidate) != digest:
            raise ValueError(f"existing export changed since its manifest was written: {relative}")


def _next_refresh_backup(backup_root: Path, family: str) -> Path:
    """Choose a fresh archival path without replacing any prior checkpoint."""
    candidate = backup_root / f"{family}-previous"
    revision = 2
    while candidate.exists() or candidate.is_symlink():
        candidate = backup_root / f"{family}-previous-{revision}"
        revision += 1
    return candidate


def _refresh_known_export(output: Path, artifacts: dict[str, bytes], family: str) -> Path:
    """Refresh a manifest-verified prior output, preserving it in ignored build/."""
    _validate_previous_export(output, family)
    stage = output.with_name(f".{output.name}.stage")
    backup_root = ROOT / "build/nh-cabir-battle-v3-refresh"
    backup = _next_refresh_backup(backup_root, family)
    if stage.exists() or stage.is_symlink() or backup.exists() or backup.is_symlink():
        raise FileExistsError("known-export refresh staging/backup path already exists")
    backup_root.mkdir(parents=True, exist_ok=True)
    _write_artifacts(stage, artifacts)
    try:
        os.replace(output, backup)
        os.replace(stage, output)
    except OSError:
        if not output.exists() and backup.exists():
            os.replace(backup, output)
        raise
    return backup


def build_family(family: str) -> tuple[Path, dict[str, bytes]]:
    if family not in FAMILIES:
        raise ValueError(f"unknown Cabir family: {family}")
    config = FAMILIES[family]
    return config["output"], _build_family(family)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", choices=("cabir", "cabir-master", "all"), default="all")
    parser.add_argument("--check", action="store_true", help="verify exact existing export bytes without writing")
    parser.add_argument("--refresh-known", action="store_true", help="refresh only manifest-verified outputs and preserve their prior bytes under ignored build/")
    args = parser.parse_args()
    if args.check and args.refresh_known:
        parser.error("--check and --refresh-known are mutually exclusive")
    names = tuple(FAMILIES) if args.family == "all" else (args.family,)
    try:
        built = [build_family(name) for name in names]
        if args.check:
            for output, artifacts in built:
                _check_artifacts(output, artifacts)
        elif args.refresh_known:
            for name, (output, _artifacts) in zip(names, built):
                _validate_previous_export(output, name)
                stage = output.with_name(f".{output.name}.stage")
                if stage.exists() or stage.is_symlink():
                    raise FileExistsError(f"refresh backup/staging path already exists for {name}")
            backups = []
            for name, (output, artifacts) in zip(names, built):
                backups.append(_refresh_known_export(output, artifacts, name))
        else:
            for output, _artifacts in built:
                _validate_detached_output(output)
            for output, artifacts in built:
                _write_artifacts(output, artifacts)
    except (FileExistsError, FileNotFoundError, OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))
    for output, _artifacts in built:
        print(f"{'Verified' if args.check else 'Created'} offline Cabir battle export: {output.relative_to(ROOT)}")
    if args.refresh_known:
        for backup in backups:
            print(f"Preserved previous generated export: {backup.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
