#!/usr/bin/env python3
"""Build provisional native previews from already-separated Cabir poses.

This helper contains only the shared per-group scale, ground-contact pivot,
native-canvas export, and review-sheet steps needed by the pose extractor. It
does not identify components, edit source art, install runtime resources, or
claim that separate action groups have calibrated cross-group scale.
"""

from dataclasses import dataclass
import io
import json
import math

from PIL import Image


LOGICAL_CANVAS = (450, 400)
TARGET_PIVOT = (196.5, 268.0)
REFERENCE_HEIGHT = 60.0
ALPHA_THRESHOLD = 16
CONTACT_BAND_FRACTION = 0.03
MIN_CONTACT_BAND = 8
MATTE = (62, 55, 52, 255)
GIF_DURATION_MS = 150


@dataclass
class NativePose:
    """One cropped, source-preserving pose passed to the preview builder."""

    name: str
    image: Image.Image
    source_path: str
    source_sha256: str
    source_origin: tuple[int, int]
    extraction: dict


def _alpha_bbox(image: Image.Image, threshold: int = 0) -> tuple[int, int, int, int]:
    alpha = image.getchannel("A")
    if threshold:
        alpha = alpha.point(lambda value: 255 if value >= threshold else 0)
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("pose has no visible pixels at the requested alpha threshold")
    return bounds


def _contact_pivot(image: Image.Image) -> tuple[tuple[float, float], dict]:
    """Estimate a grounded pivot from the lower alpha band, not the bbox center."""
    x0, y0, x1, y1 = _alpha_bbox(image, ALPHA_THRESHOLD)
    core_height = y1 - y0
    band_height = max(MIN_CONTACT_BAND, math.ceil(core_height * CONTACT_BAND_FRACTION))
    band_top = max(y0, y1 - band_height)
    alpha = image.getchannel("A").load()
    contact_x = [
        x
        for y in range(band_top, y1)
        for x in range(x0, x1)
        if alpha[x, y] >= ALPHA_THRESHOLD
    ]
    if not contact_x:
        raise ValueError("pose has no alpha>=16 pixels in its lower contact band")
    left = min(contact_x)
    right = max(contact_x)
    pivot = ((left + right + 1) / 2.0, float(y1))
    return pivot, {
        "method": "center of alpha>=16 pixels in lower 3% band; pivotY is the alpha>=16 lower edge",
        "alpha16BBox": [x0, y0, x1, y1],
        "bandTop": band_top,
        "bandBottomExclusive": y1,
        "contactXBounds": [left, right + 1],
        "sourcePivotLocal": list(pivot),
    }


def _png_bytes(image: Image.Image) -> bytes:
    output = io.BytesIO()
    image.save(output, format="PNG", optimize=False)
    return output.getvalue()


def _align_pose(
    pose: NativePose,
    scale: float,
    target_pivot: tuple[float, float],
    logical_canvas: tuple[int, int],
) -> tuple[Image.Image, dict]:
    source_pivot, contact = _contact_pivot(pose.image)
    source_pivot_global = [
        pose.source_origin[0] + source_pivot[0],
        pose.source_origin[1] + source_pivot[1],
    ]
    resized_size = (
        max(1, round(pose.image.width * scale)),
        max(1, round(pose.image.height * scale)),
    )
    left = math.floor(target_pivot[0] - source_pivot[0] * scale + 0.5)
    top = math.floor(target_pivot[1] - source_pivot[1] * scale + 0.5)
    if left < 0 or top < 0 or left + resized_size[0] > logical_canvas[0] or top + resized_size[1] > logical_canvas[1]:
        raise ValueError(f"aligned pose {pose.name} exceeds the {logical_canvas[0]}x{logical_canvas[1]} battle canvas")
    resized = pose.image.resize(resized_size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", logical_canvas, (0, 0, 0, 0))
    canvas.alpha_composite(resized, (left, top))
    return canvas, {
        "pose": pose.name,
        "sourcePath": pose.source_path,
        "sourceSha256": pose.source_sha256,
        "sourceOrigin": list(pose.source_origin),
        "sourceSize": list(pose.image.size),
        "sourceExtraction": pose.extraction,
        "sourcePivotGlobal": source_pivot_global,
        "contactLandmark": contact,
        "uniformSheetScale": scale,
        "resizedSourceSize": list(resized_size),
        "targetPivot": list(target_pivot),
        "outputPlacement": [left, top],
        "outputPivotActual": [left + source_pivot[0] * scale, top + source_pivot[1] * scale],
        "logicalCanvas": list(logical_canvas),
    }


def _visible_union(frames: list[Image.Image], logical_canvas: tuple[int, int], margin: int = 8) -> tuple[int, int, int, int]:
    boxes = [frame.getchannel("A").getbbox() for frame in frames]
    visible = [box for box in boxes if box is not None]
    if not visible:
        raise ValueError("aligned group has no visible pixels")
    return (
        max(0, min(box[0] for box in visible) - margin),
        max(0, min(box[1] for box in visible) - margin),
        min(logical_canvas[0], max(box[2] for box in visible) + margin),
        min(logical_canvas[1], max(box[3] for box in visible) + margin),
    )


def _contact_sheet(frames: list[Image.Image], crop: tuple[int, int, int, int]) -> Image.Image:
    scale = 4
    columns = 2 if len(frames) > 1 else 1
    rows = math.ceil(len(frames) / columns)
    width, height = (crop[2] - crop[0]) * scale, (crop[3] - crop[1]) * scale
    sheet = Image.new("RGBA", (columns * width, rows * height), MATTE)
    for index, frame in enumerate(frames):
        tile = frame.crop(crop).resize((width, height), Image.Resampling.NEAREST)
        sheet.alpha_composite(tile, ((index % columns) * width, (index // columns) * height))
    return sheet


def _gif_bytes(frames: list[Image.Image], crop: tuple[int, int, int, int]) -> bytes:
    size = (crop[2] - crop[0], crop[3] - crop[1])
    gif_frames = []
    background = Image.new("RGBA", size, MATTE)
    for frame in frames:
        preview = background.copy()
        preview.alpha_composite(frame.crop(crop))
        gif_frames.append(preview.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT))
    output = io.BytesIO()
    gif_frames[0].save(
        output,
        format="GIF",
        save_all=True,
        append_images=gif_frames[1:],
        duration=GIF_DURATION_MS,
        loop=0,
        disposal=2,
        optimize=False,
    )
    return output.getvalue()


def build_preview_group(
    group_name: str,
    poses: list[NativePose],
    reference_index: int = 0,
    *,
    reference_height: float = REFERENCE_HEIGHT,
    target_pivot: tuple[float, float] = TARGET_PIVOT,
    logical_canvas: tuple[int, int] = LOGICAL_CANVAS,
) -> tuple[dict[str, bytes], dict, list[Image.Image]]:
    """Build one provisional group with a shared reference scale and ground pivot.

    Scale is calibrated independently for this group from the selected pose.
    This helper intentionally makes no cross-action body-scale claim.
    """
    if not poses:
        raise ValueError(f"alignment group is empty: {group_name}")
    if not 0 <= reference_index < len(poses):
        raise ValueError("reference pose index is outside the pose group")
    if reference_height <= 0 or logical_canvas[0] < 1 or logical_canvas[1] < 1:
        raise ValueError("preview dimensions and reference height must be positive")
    if any(pose.image.mode != "RGBA" for pose in poses):
        raise ValueError("native preview poses must be RGBA")

    reference = poses[reference_index]
    bounds = _alpha_bbox(reference.image, ALPHA_THRESHOLD)
    height = bounds[3] - bounds[1]
    if height <= 0:
        raise ValueError(f"invalid reference body height in {group_name}")
    scale = reference_height / height
    calibration = {
        "method": f"{reference_height:g} native pixels divided by reference pose alpha>=16 body/flame height",
        "referencePoseIndex": reference_index,
        "referencePose": reference.name,
        "referenceAlpha16BBox": list(bounds),
        "referenceBodyHeight": height,
        "targetBodyHeight": reference_height,
        "uniformScale": scale,
    }

    artifacts: dict[str, bytes] = {}
    aligned_frames: list[Image.Image] = []
    pose_details = []
    for index, pose in enumerate(poses):
        frame, details = _align_pose(pose, scale, target_pivot, logical_canvas)
        aligned_frames.append(frame)
        pose_details.append(details)
        artifacts[f"exports/{group_name}/frame-{index:02d}.png"] = _png_bytes(frame)

    crop = _visible_union(aligned_frames, logical_canvas)
    artifacts[f"exports/{group_name}/contact-sheet-4x.png"] = _png_bytes(_contact_sheet(aligned_frames, crop))
    if len(aligned_frames) > 1:
        artifacts[f"exports/{group_name}/animation-proof.gif"] = _gif_bytes(aligned_frames, crop)
    metadata = {
        "group": group_name,
        "status": "provisional native alignment for visual review; no runtime binding",
        "canvas": list(logical_canvas),
        "targetRootPivot": list(target_pivot),
        "alphaThresholdForScaleAndGround": ALPHA_THRESHOLD,
        "scaleCalibration": calibration,
        "perPosePivotsAreGroundContactNotOverallBBoxCenter": True,
        "contactBandRule": {
            "fraction": CONTACT_BAND_FRACTION,
            "minimumSourcePixels": MIN_CONTACT_BAND,
            "meaning": "lower contact band excludes raised/extended weapon from x-root estimate",
        },
        "uncertainty": None,
        "poses": pose_details,
        "reviewCropBounds": list(crop),
    }
    artifacts[f"exports/{group_name}/alignment.json"] = (json.dumps(metadata, indent=2) + "\n").encode("utf-8")
    return artifacts, metadata, aligned_frames
