#!/usr/bin/env python3
"""Losslessly separate the four reviewed poses from two Cabir atlas sheets.

This offline tool identifies whole-sheet connected silhouettes instead of
cropping generated quadrants. It preserves source RGBA inside each selected
component's reviewed dilation, clears only faint exterior alpha, and records
the original source-space placement. It does not resize or bind runtime art.
"""

import argparse
from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT


def _require_external_path(path: Path) -> Path:
    requested = path.expanduser()
    resolved = requested.resolve()
    if (requested.absolute().is_relative_to(CHECKOUT)
            or resolved.is_relative_to(CHECKOUT) or CHECKOUT.is_relative_to(resolved)):
        raise ValueError("authoring requires an explicit external workspace outside the checkout")
    if any(parent.is_symlink() for parent in (requested, *requested.parents)):
        raise ValueError("authoring paths must not traverse symlinks")
    return resolved


def configure_private_root(private_root: Path) -> None:
    """Rebase project-shaped authoring paths without changing the checkout."""
    global ROOT, SOURCES
    resolved = _require_external_path(private_root)
    previous = ROOT
    for name, value in list(globals().items()):
        if name not in ("ROOT", "CHECKOUT") and isinstance(value, Path) and value.is_absolute():
            if value.is_relative_to(previous):
                globals()[name] = resolved / value.relative_to(previous)
    SOURCES = {
        action: replace(spec,
                        source_path=resolved / spec.source_path.relative_to(previous),
                        output_dir=resolved / spec.output_dir.relative_to(previous),
                        allowed_source_root=resolved / spec.allowed_source_root.relative_to(previous))
        for action, spec in SOURCES.items()
    }
    ROOT = resolved

CABIR_ROOT = ROOT / "assets/new-horizons/creatures/cabir"
V2_ROOT = CABIR_ROOT / "v2"
V3_ROOT = CABIR_ROOT / "v3"
MAX_SOURCE_SIDE = 8192
MAX_SOURCE_PIXELS = 8 * 1024 * 1024
POSE_COUNT = 4
DILATION_RADIUS = 4
MAX_REMOVED_ALPHA = 10


@dataclass(frozen=True)
class PoseSource:
    """Pinned properties for one generated source sheet."""

    action: str
    source_path: Path
    output_dir: Path
    source_sha256: str
    seed_alpha_threshold: int
    major_component_min_area: int = 50_000
    allowed_source_root: Path | None = None


SOURCES = {
    "melee": PoseSource(
        action="melee",
        source_path=V2_ROOT / "melee-v1/candidate-01.png",
        output_dir=V2_ROOT / "melee-v1/separated",
        source_sha256="148b3a365e5456c72935ca280afb6e36db61f5f0f3d0be395145d16c5403a257",
        seed_alpha_threshold=16,
        allowed_source_root=V2_ROOT / "melee-v1",
    ),
    "reactions": PoseSource(
        action="reactions",
        source_path=V2_ROOT / "reactions-v1/candidate-02.png",
        output_dir=V2_ROOT / "reactions-v1/separated",
        source_sha256="744691e95d2bcfcb0ba7632ed158f9c24f1ad021edaba9eb13cd78b5dec49349",
        seed_alpha_threshold=128,
        allowed_source_root=V2_ROOT / "reactions-v1",
    ),
    "melee-front-v3": PoseSource(
        action="melee-front-v3",
        source_path=V3_ROOT / "melee-front-v1/candidate-01.png",
        output_dir=V3_ROOT / "melee-front-v1/separated-v1",
        source_sha256="b58a766ea7512b10315b8b82f0aa85b6a161534434745b402706da9547935efb",
        seed_alpha_threshold=16,
        allowed_source_root=V3_ROOT / "melee-front-v1",
    ),
    "reactions-v3": PoseSource(
        action="reactions-v3",
        source_path=V3_ROOT / "reactions-v1/candidate-01.png",
        output_dir=V3_ROOT / "reactions-v1/separated-v1",
        source_sha256="e42feeba9a6019b3685c756280cedf72173842fa2a652ed2ba59a33094ddaa56",
        seed_alpha_threshold=16,
        allowed_source_root=V3_ROOT / "reactions-v1",
    ),
}

NATIVE_GROUPS = {
    "melee-front-v3": {
        "group": "base-melee-front-v3",
        "poseNames": ["ready", "windup", "thrust", "recovery"],
    },
    "reactions-v3": {
        "group": "base-reactions-v3",
        "poseNames": ["hit", "brace", "dying", "dead"],
    },
}


def _within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _validate_source_location(spec: PoseSource, source: Path) -> None:
    """Reject any input outside the source action root pinned for this export."""
    if spec.allowed_source_root is None or not _within(source.resolve(), spec.allowed_source_root.resolve()):
        raise ValueError("source is outside its pinned Cabir action directory")


def _check_dimensions(width: int, height: int) -> None:
    if width < 2 or height < 2:
        raise ValueError("source pose sheet dimensions are too small")
    if width > MAX_SOURCE_SIDE or height > MAX_SOURCE_SIDE or width * height > MAX_SOURCE_PIXELS:
        raise ValueError("source pose sheet dimensions exceed the safe limit")


def _connected_components(alpha: Image.Image, threshold: int) -> tuple[list[dict], list[tuple[int, int, int, int]]]:
    """Find 8-connected alpha components by run-length rows, without NumPy."""
    width, height = alpha.size
    values = alpha.tobytes()
    parents: list[int] = []
    ranks: list[int] = []
    runs: list[tuple[int, int, int, int, int]] = []
    previous: list[tuple[int, int, int]] = []

    def make_label() -> int:
        label = len(parents)
        parents.append(label)
        ranks.append(0)
        return label

    def find(label: int) -> int:
        while parents[label] != label:
            parents[label] = parents[parents[label]]
            label = parents[label]
        return label

    def union(left: int, right: int) -> None:
        left_root = find(left)
        right_root = find(right)
        if left_root == right_root:
            return
        if ranks[left_root] < ranks[right_root]:
            left_root, right_root = right_root, left_root
        parents[right_root] = left_root
        if ranks[left_root] == ranks[right_root]:
            ranks[left_root] += 1

    for y in range(height):
        row_start = y * width
        current: list[tuple[int, int, int]] = []
        x = 0
        while x < width:
            while x < width and values[row_start + x] < threshold:
                x += 1
            if x == width:
                break
            start = x
            peak_alpha = 0
            while x < width and values[row_start + x] >= threshold:
                peak_alpha = max(peak_alpha, values[row_start + x])
                x += 1
            end = x
            label = make_label()
            # Adjacent rows merge when their runs overlap or touch diagonally.
            for previous_start, previous_end, previous_label in previous:
                if previous_start <= end and previous_end >= start:
                    union(label, previous_label)
            runs.append((label, start, end, y, peak_alpha))
            current.append((start, end, label))
        previous = current

    stats: dict[int, dict] = {}
    for label, start, end, y, peak_alpha in runs:
        root_label = find(label)
        component = stats.setdefault(root_label, {
            "area": 0,
            "bbox": [width, height, 0, 0],
            "maxAlpha": 0,
            "firstPixel": y * width + start,
        })
        component["area"] += end - start
        component["bbox"][0] = min(component["bbox"][0], start)
        component["bbox"][1] = min(component["bbox"][1], y)
        component["bbox"][2] = max(component["bbox"][2], end)
        component["bbox"][3] = max(component["bbox"][3], y + 1)
        component["maxAlpha"] = max(component["maxAlpha"], peak_alpha)

    ordered = sorted(
        stats.items(),
        key=lambda pair: (-pair[1]["area"], pair[1]["firstPixel"]),
    )
    components = []
    for root_label, component in ordered:
        components.append({
            "label": root_label,
            "area": component["area"],
            "bbox": component["bbox"],
            "maxAlpha": component["maxAlpha"],
            "firstPixel": component["firstPixel"],
        })
    # Runs keep their provisional labels while unions are discovered. Return
    # canonical roots so mask assembly maps every run to its final component.
    canonical_runs = [(find(label), start, end, y, peak_alpha) for label, start, end, y, peak_alpha in runs]
    return components, canonical_runs


def _dilate_region(mask: bytearray, width: int, height: int, radius: int) -> bytearray:
    """Dilate a binary source-sized mask with a square/Chebyshev radius."""
    current = mask
    for _ in range(radius):
        expanded = bytearray(width * height)
        for index, value in enumerate(current):
            if not value:
                continue
            x = index % width
            y = index // width
            for neighbor_y in range(max(0, y - 1), min(height, y + 2)):
                row_start = neighbor_y * width
                for neighbor_x in range(max(0, x - 1), min(width, x + 2)):
                    expanded[row_start + neighbor_x] = 1
        current = expanded
    return current


def _pose_order_key(component: dict, image_height: int) -> tuple[int, float, int]:
    x0, y0, x1, y1 = component["bbox"]
    center_y = (y0 + y1) / 2
    center_x = (x0 + x1) / 2
    return (0 if center_y < image_height / 2 else 1, center_x, component["firstPixel"])


def separate_poses(image: Image.Image, spec: PoseSource, source_sha256: str | None = None) -> tuple[list[Image.Image], dict]:
    """Separate four whole-sheet poses onto one shared, unscaled source canvas."""
    if image.mode != "RGBA":
        raise ValueError("source pose sheet must contain a real RGBA channel")
    width, height = image.size
    _check_dimensions(width, height)
    alpha = image.getchannel("A")
    components, runs = _connected_components(alpha, spec.seed_alpha_threshold)
    major_components = [
        component for component in components
        if component["area"] >= spec.major_component_min_area
    ]
    if len(major_components) != POSE_COUNT:
        raise ValueError(
            f"{spec.action} source requires exactly {POSE_COUNT} major silhouettes; "
            f"found {len(major_components)} at alpha>={spec.seed_alpha_threshold} "
            f"and area>={spec.major_component_min_area}"
        )

    row_counts = [
        sum((component["bbox"][1] + component["bbox"][3]) / 2 < height / 2 for component in major_components),
        sum((component["bbox"][1] + component["bbox"][3]) / 2 >= height / 2 for component in major_components),
    ]
    if row_counts != [2, 2]:
        raise ValueError(f"{spec.action} source does not contain exactly two pose silhouettes in each source row")
    ordered_components = sorted(major_components, key=lambda component: _pose_order_key(component, height))
    seed_masks = [bytearray(width * height) for _ in ordered_components]
    label_to_pose = {component["label"]: pose_index for pose_index, component in enumerate(ordered_components)}
    for label, start, end, y, _peak_alpha in runs:
        pose_index = label_to_pose.get(label)
        if pose_index is None:
            continue
        row_start = y * width
        seed_masks[pose_index][row_start + start:row_start + end] = b"\x01" * (end - start)

    dilated_masks = [_dilate_region(mask, width, height, DILATION_RADIUS) for mask in seed_masks]
    coverage = bytearray(width * height)
    for mask in dilated_masks:
        for index, included in enumerate(mask):
            if included:
                coverage[index] += 1

    alpha_bytes = alpha.tobytes()
    removed_count = 0
    removed_peak = 0
    removed_histogram: dict[str, int] = {}
    for index, alpha_value in enumerate(alpha_bytes):
        if alpha_value == 0 or coverage[index] == 1:
            continue
        if alpha_value > MAX_REMOVED_ALPHA:
            x = index % width
            y = index // width
            reason = "overlapping pose dilations" if coverage[index] > 1 else "outside all pose dilations"
            raise ValueError(
                f"{spec.action} source has alpha {alpha_value} at ({x},{y}) {reason}; "
                "refusing to discard a potentially meaningful pixel"
            )
        removed_count += 1
        removed_peak = max(removed_peak, alpha_value)
        key = str(alpha_value)
        removed_histogram[key] = removed_histogram.get(key, 0) + 1

    if removed_peak > MAX_REMOVED_ALPHA:
        raise ValueError("removed alpha exceeds the reviewed faint-pixel limit")

    expanded_bboxes = []
    for component in ordered_components:
        x0, y0, x1, y1 = component["bbox"]
        expanded_bboxes.append((
            max(0, x0 - DILATION_RADIUS),
            max(0, y0 - DILATION_RADIUS),
            min(width, x1 + DILATION_RADIUS),
            min(height, y1 + DILATION_RADIUS),
        ))
    common_bbox = (
        min(box[0] for box in expanded_bboxes),
        min(box[1] for box in expanded_bboxes),
        max(box[2] for box in expanded_bboxes),
        max(box[3] for box in expanded_bboxes),
    )
    common_width = common_bbox[2] - common_bbox[0]
    common_height = common_bbox[3] - common_bbox[1]
    source_pixels = image.tobytes()
    frames = []
    pose_receipts = []

    for pose_index, (component, mask, expanded_bbox) in enumerate(
        zip(ordered_components, dilated_masks, expanded_bboxes)
    ):
        frame_pixels = bytearray(common_width * common_height * 4)
        pose_x0, pose_y0, pose_x1, pose_y1 = expanded_bbox
        for y in range(pose_y0, pose_y1):
            source_row_start = y * width
            destination_y = y - common_bbox[1]
            for x in range(pose_x0, pose_x1):
                source_index = source_row_start + x
                if not mask[source_index]:
                    continue
                # Low-alpha pixels in overlapping halos are deliberately not
                # copied to either pose; the fail-closed limit above caps loss.
                if coverage[source_index] > 1 and alpha_bytes[source_index] > 0:
                    continue
                destination_x = x - common_bbox[0]
                destination_index = (destination_y * common_width + destination_x) * 4
                source_index *= 4
                frame_pixels[destination_index:destination_index + 4] = source_pixels[source_index:source_index + 4]

        frame = Image.frombytes("RGBA", (common_width, common_height), bytes(frame_pixels))
        frames.append(frame)
        x0, y0, x1, y1 = component["bbox"]
        pose_receipts.append({
            "frame": f"pose-{pose_index:02d}.png",
            "row": 0 if (y0 + y1) / 2 < height / 2 else 1,
            "column": pose_index % 2,
            "seedThreshold": spec.seed_alpha_threshold,
            "seedArea": component["area"],
            "seedMaxAlpha": component["maxAlpha"],
            "sourceGlobalBBox": [x0, y0, x1, y1],
            "paddedSourceBBox": list(expanded_bbox),
            "sourceOffsetOnCommonCanvas": [expanded_bbox[0] - common_bbox[0], expanded_bbox[1] - common_bbox[1]],
        })

    receipt = {
        "status": "offline lossless pose separation; alignment/native review pending",
        "action": spec.action,
        "sourcePath": str(spec.source_path.relative_to(ROOT)),
        "sourceSha256": source_sha256,
        "sourceSize": [width, height],
        "sourceComponentCountAtThreshold": len(components),
        "majorComponentAreaMinimum": spec.major_component_min_area,
        "selectedPoseCount": len(frames),
        "componentConnectivity": "8-connected",
        "ordering": "row-major by component bbox center; top/bottom split at source height/2, then x center",
        "commonSourceCanvas": {
            "globalBBox": list(common_bbox),
            "size": [common_width, common_height],
            "scale": 1.0,
            "resized": False,
            "sourceOffsetsPreserved": True,
        },
        "policy": {
            "seed": f"four largest-area components meeting area minimum at alpha>={spec.seed_alpha_threshold}",
            "dilationRadius": DILATION_RADIUS,
            "dilationShape": "square/Chebyshev radius, clipped to source bounds",
            "retainedPixels": "original source RGBA bytes copied unchanged",
            "removedPixels": "only nonzero source pixels outside a unique pose dilation; alpha must be <=10; no recoloring",
            "maximumRemovedAlphaAllowed": MAX_REMOVED_ALPHA,
            "independentResize": False,
            "quadrantCropping": False,
        },
        "removedPixelCount": removed_count,
        "maximumRemovedAlpha": removed_peak,
        "removedAlphaHistogram": removed_histogram,
        "poses": pose_receipts,
        "sourceFileModified": False,
    }
    return frames, receipt


def _export_native_alignment(frames: list[Image.Image], receipt: dict, spec: PoseSource, output: Path) -> dict[str, str]:
    _require_external_path(output)
    """Reuse the established Cabir 60px/ground-pivot alignment for v3 previews."""
    native = NATIVE_GROUPS.get(spec.action)
    if native is None:
        return {}

    # The existing aligner owns the shared 450x400 canvas, 60px reference
    # scale, foot-contact pivot, 196.5 center anchor, contact sheet and GIF.
    # Constructing its already-reviewed pose records here avoids grid crops,
    # which would clip the deliberately seam-crossing whole silhouettes.
    import export_new_horizons_cabir_pose_preview as preview

    common_x, common_y, _common_right, _common_bottom = receipt["commonSourceCanvas"]["globalBBox"]
    poses = []
    for pose_name, frame, pose in zip(native["poseNames"], frames, receipt["poses"]):
        x0, y0, x1, y1 = pose["paddedSourceBBox"]
        local_box = (x0 - common_x, y0 - common_y, x1 - common_x, y1 - common_y)
        crop = frame.crop(local_box)
        poses.append(preview.NativePose(
            name=pose_name,
            image=crop,
            source_path=spec.source_path.resolve().relative_to(ROOT).as_posix(),
            source_sha256=receipt["sourceSha256"],
            source_origin=(x0, y0),
            extraction={
                "kind": "pinned whole connected-component crop; no quadrant clipping",
                "sourceGlobalSeedBBox": pose["sourceGlobalBBox"],
                "sourceGlobalCrop": pose["paddedSourceBBox"],
                "componentArea": pose["seedArea"],
                "componentAlphaThreshold": pose["seedThreshold"],
                "separationFrame": pose["frame"],
            },
        ))

    artifacts, _metadata, _aligned = preview.build_preview_group(
        native["group"], poses, reference_index=0
    )
    prefix = f"exports/{native['group']}/"
    native_output = output / "native-export"
    native_output.mkdir(parents=False, exist_ok=False)
    hashes = {}
    for relative, data in sorted(artifacts.items()):
        if not relative.startswith(prefix):
            raise RuntimeError(f"unexpected native alignment artifact path: {relative}")
        target = native_output / relative[len(prefix):]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        hashes[target.relative_to(native_output).as_posix()] = hashlib.sha256(data).hexdigest()
    return hashes


def _read_source(path: Path) -> Image.Image:
    try:
        opened = Image.open(path)
    except (OSError, ValueError) as error:
        raise ValueError(f"could not open source pose sheet: {path}") from error
    with opened:
        _check_dimensions(opened.width, opened.height)
        if opened.mode != "RGBA" or opened.format == "GIF" or getattr(opened, "is_animated", False):
            raise ValueError("source pose sheet must be one static RGBA image")
        return opened.copy()


def export_action(action: str, output_dir: Path | None = None) -> Path:
    try:
        spec = SOURCES[action]
    except KeyError as error:
        raise ValueError(f"unknown Cabir action source: {action}") from error

    source = spec.source_path.resolve()
    output = spec.output_dir if output_dir is None else output_dir
    _require_external_path(output)
    _validate_source_location(spec, source)
    if output.is_symlink() or output.exists():
        raise FileExistsError(f"refusing to replace existing separated output: {output}")

    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()
    if source_sha256 != spec.source_sha256:
        raise ValueError(f"{action} source hash differs from the reviewed input; manual review required")
    image = _read_source(source)
    frames, receipt = separate_poses(image, spec, source_sha256)

    output.mkdir(parents=True, exist_ok=False)
    for frame_index, frame in enumerate(frames):
        frame.save(output / f"pose-{frame_index:02d}.png", format="PNG")
    native_export_hashes = _export_native_alignment(frames, receipt, spec, output)
    receipt["outputFiles"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(output.glob("pose-*.png"))
    }
    if native_export_hashes:
        receipt["nativeExportFiles"] = native_export_hashes
    (output / "separation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_sha256:
        raise RuntimeError("source pose sheet changed during separation")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path,
                        help="external project-shaped authoring workspace")
    parser.add_argument("--output-dir", type=Path, help="new external pose bundle directory")
    parser.add_argument("--action", required=True, choices=sorted(SOURCES), help="pinned source action sheet")
    args = parser.parse_args()
    try:
        configure_private_root(args.project_root)
        output = export_action(args.action, args.output_dir)
    except (FileExistsError, OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))
    print(f"Created offline Cabir pose separation: {output}")


if __name__ == "__main__":
    main()
