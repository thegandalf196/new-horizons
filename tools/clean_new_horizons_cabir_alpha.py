#!/usr/bin/env python3
"""Remove reviewed faint external alpha debris from one Cabir walk atlas.

This is a narrow, reproducible transparency cleanup, not a general sprite
matting tool. It preserves each source RGBA pixel inside a two-pixel dilation
of that cell's largest 8-connected alpha>=16 component. Outside the retained
region only alpha is cleared, and the operation fails closed if it would remove
any pixel with alpha above 10. The original sheet is never modified.
"""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

import export_new_horizons_cabir_animation as animation_exporter


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
    global ROOT
    resolved = _require_external_path(private_root)
    previous = ROOT
    for name, value in list(globals().items()):
        if name not in ("ROOT", "CHECKOUT") and isinstance(value, Path) and value.is_absolute():
            if value.is_relative_to(previous):
                globals()[name] = resolved / value.relative_to(previous)
    ROOT = resolved

WALK_ASSETS = ROOT / "assets/new-horizons/creatures/cabir/v2/walk-v2"
SOURCE_ATLAS = WALK_ASSETS / "candidate-02.png"
OUTPUT_DIRECTORY = WALK_ASSETS / "cleaned"
OUTPUT_ATLAS_NAME = "candidate-02-alpha-cleaned.png"
RECEIPT_NAME = "cleanup-receipt.json"
PINNED_SOURCE_SHA256 = "06ba5ad74fdb2436337b175346ce3bc63e048b07ba04c6c006a2378750f9754a"
ROWS = 2
COLUMNS = 2
CORE_ALPHA_THRESHOLD = 16
DILATION_RADIUS = 2
MAX_DILATION_RADIUS = 4
MAX_REMOVED_ALPHA = 10
BODY_HEIGHT = 60


def _connected_components(alpha: Image.Image, threshold: int) -> tuple[bytearray, list[dict]]:
    """Return the largest 8-connected threshold component and component facts."""
    width, height = alpha.size
    values = alpha.tobytes()
    visited = bytearray(width * height)
    components: list[dict] = []
    largest_pixels: list[int] = []
    largest_area = 0
    largest_bbox: tuple[int, int, int, int] | None = None

    for start in range(width * height):
        if visited[start] or values[start] < threshold:
            continue

        visited[start] = 1
        pending = [start]
        component_pixels: list[int] = []
        min_x = width
        min_y = height
        max_x = -1
        max_y = -1
        peak_alpha = 0

        while pending:
            index = pending.pop()
            component_pixels.append(index)
            x = index % width
            y = index // width
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
            peak_alpha = max(peak_alpha, values[index])

            for neighbor_y in range(max(0, y - 1), min(height, y + 2)):
                row_start = neighbor_y * width
                for neighbor_x in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = row_start + neighbor_x
                    if not visited[neighbor] and values[neighbor] >= threshold:
                        visited[neighbor] = 1
                        pending.append(neighbor)

        area = len(component_pixels)
        bbox = (min_x, min_y, max_x + 1, max_y + 1)
        components.append({"area": area, "bbox": list(bbox), "maxAlpha": peak_alpha})
        # The scan is deterministic in row-major order; ties retain the first.
        if area > largest_area:
            largest_pixels = component_pixels
            largest_area = area
            largest_bbox = bbox

    if not largest_pixels or largest_bbox is None:
        raise ValueError(f"cell has no alpha>={threshold} silhouette seed")

    components.sort(key=lambda component: (-component["area"], component["bbox"][1], component["bbox"][0]))
    seed = bytearray(width * height)
    for index in largest_pixels:
        seed[index] = 1
    return seed, components


def _dilate_square(mask: bytearray, width: int, height: int, radius: int) -> bytearray:
    """Apply deterministic Chebyshev-radius dilation without modifying input."""
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


def _alpha_edge_counts(alpha: Image.Image) -> dict[str, int]:
    width, height = alpha.size
    pixels = alpha.load()
    return {
        "left": sum(pixels[0, y] > 0 for y in range(height)),
        "right": sum(pixels[width - 1, y] > 0 for y in range(height)),
        "top": sum(pixels[x, 0] > 0 for x in range(width)),
        "bottom": sum(pixels[x, height - 1] > 0 for x in range(width)),
    }


def clean_atlas(
    image: Image.Image,
    source_sha256: str | None = None,
    *,
    dilation_radius: int = DILATION_RADIUS,
) -> tuple[Image.Image, dict]:
    """Return one alpha-cleaned copy and an auditable per-cell receipt.

    The optional radius lets a separately pinned generated sheet preserve a
    slightly broader antialias fringe without changing the established v2
    default policy.
    """
    if image.mode != "RGBA":
        raise ValueError("source atlas must contain a real RGBA channel")
    if not isinstance(dilation_radius, int) or not 0 <= dilation_radius <= MAX_DILATION_RADIUS:
        raise ValueError(f"dilation radius must be between 0 and {MAX_DILATION_RADIUS}")

    width, height = image.size
    animation_exporter._validate_dimensions(width, height, COLUMNS, ROWS)
    x_bounds = animation_exporter._grid_boundaries(width, COLUMNS)
    y_bounds = animation_exporter._grid_boundaries(height, ROWS)
    input_pixels = image.tobytes()
    output_pixels = bytearray(input_pixels)
    cell_receipts = []
    total_removed = 0
    total_histogram: dict[str, int] = {}
    max_removed = 0

    for row in range(ROWS):
        for column in range(COLUMNS):
            cell_index = row * COLUMNS + column
            bounds = (x_bounds[column], y_bounds[row], x_bounds[column + 1], y_bounds[row + 1])
            cell = image.crop(bounds)
            cell_alpha = cell.getchannel("A")
            cell_width, cell_height = cell.size
            seed, components = _connected_components(cell_alpha, CORE_ALPHA_THRESHOLD)
            retained = _dilate_square(seed, cell_width, cell_height, dilation_radius)
            alpha_values = cell_alpha.tobytes()

            removed_count = 0
            removed_peak = 0
            histogram: dict[str, int] = {}
            for local_index, alpha_value in enumerate(alpha_values):
                if retained[local_index] or alpha_value == 0:
                    continue
                removed_count += 1
                removed_peak = max(removed_peak, alpha_value)
                key = str(alpha_value)
                histogram[key] = histogram.get(key, 0) + 1
                global_x = bounds[0] + local_index % cell_width
                global_y = bounds[1] + local_index // cell_width
                output_pixels[(global_y * width + global_x) * 4 + 3] = 0

            if removed_peak > MAX_REMOVED_ALPHA:
                raise ValueError(
                    f"cell {cell_index} would remove alpha {removed_peak}, above the reviewed limit "
                    f"{MAX_REMOVED_ALPHA}; manual review is required"
                )

            total_removed += removed_count
            max_removed = max(max_removed, removed_peak)
            for key, count in histogram.items():
                total_histogram[key] = total_histogram.get(key, 0) + count

            # Count the four unpadded source-cell edges after applying the same
            # local retention mask, for a focused clipping diagnosis.
            retained_alpha = bytearray(alpha_values)
            for local_index, keep in enumerate(retained):
                if not keep:
                    retained_alpha[local_index] = 0
            cleaned_cell_alpha = Image.frombytes("L", cell.size, bytes(retained_alpha))
            cell_receipts.append({
                "index": cell_index,
                "row": row,
                "column": column,
                "sourceBounds": list(bounds),
                "size": [cell_width, cell_height],
                "alpha16Components": components,
                "seedArea": components[0]["area"],
                "seedBounds": components[0]["bbox"],
                "sourceEdgeAlphaCounts": _alpha_edge_counts(cell_alpha),
                "cleanedEdgeAlphaCounts": _alpha_edge_counts(cleaned_cell_alpha),
                "removedPixelCount": removed_count,
                "maximumRemovedAlpha": removed_peak,
                "removedAlphaHistogram": histogram,
            })

    cleaned = Image.frombytes("RGBA", image.size, bytes(output_pixels))
    # Preflight against the frozen row-major exporter. The cleanup may not
    # bypass its clipping or geometry checks.
    _atlas, _frames, _bounds = animation_exporter.load_atlas_from_image(cleaned, COLUMNS, ROWS)
    rendered, export_metadata = animation_exporter.render_frames(cleaned, COLUMNS, BODY_HEIGHT, ROWS)

    receipt = {
        "status": "provisional generated-sheet transparency cleanup; visual review required",
        "sourceSha256": source_sha256,
        "sourceSize": [width, height],
        "sourcePath": str(SOURCE_ATLAS.relative_to(ROOT)),
        "grid": {
            "rows": ROWS,
            "columns": COLUMNS,
            "order": "row-major",
            "xBoundaries": x_bounds,
            "yBoundaries": y_bounds,
        },
        "policy": {
            "seed": f"largest 8-connected alpha>={CORE_ALPHA_THRESHOLD} component in each source cell",
            "dilation": f"{dilation_radius}-pixel square/Chebyshev-radius dilation within each cell",
            "retainedPixels": "original RGBA bytes are preserved inside the dilated seed",
            "removedPixels": "outside the dilated seed, nonzero alpha is set to zero; RGB bytes are preserved",
            "maximumRemovedAlphaAllowed": MAX_REMOVED_ALPHA,
            "noGlobalThreshold": True,
            "noRecoloringOrWarping": True,
        },
        "cells": cell_receipts,
        "removedPixelCount": total_removed,
        "maximumRemovedAlpha": max_removed,
        "removedAlphaHistogram": total_histogram,
        "export": {
            "frameCount": len(rendered),
            "bodyHeight": BODY_HEIGHT,
            "canvas": list(animation_exporter.LOGICAL_CANVAS),
            "sharedSourceBounds": export_metadata["sharedSourceBounds"],
            "sharedSourceBodySize": export_metadata["sharedSourceBodySize"],
            "scale": export_metadata["scale"],
            "placement": export_metadata["placement"],
            "feetBaselineY": export_metadata["feetBaselineY"],
            "alphaPolicy": export_metadata["alphaPolicy"],
        },
        "sourceFileModified": False,
    }
    return cleaned, receipt


def _read_source_atlas(source: Path) -> Image.Image:
    """Check image header bounds before copying or decoding source pixels."""
    try:
        opened = Image.open(source)
    except (OSError, ValueError) as error:
        raise ValueError(f"could not open source atlas: {source}") from error

    with opened:
        animation_exporter._validate_dimensions(opened.width, opened.height, COLUMNS, ROWS)
        if opened.mode != "RGBA" or opened.format == "GIF" or getattr(opened, "is_animated", False):
            raise ValueError("source must be one static RGBA atlas")
        return opened.copy()


def export_review_bundle(input_path: Path, output_dir: Path) -> Path:
    source = input_path.expanduser().resolve()
    requested_output = output_dir.expanduser()
    if requested_output.is_symlink():
        raise FileExistsError(f"refusing symlink cleanup output: {requested_output}")
    _require_external_path(output_dir)
    output = requested_output.resolve()
    if source != SOURCE_ATLAS.resolve():
        raise ValueError(f"this cleanup is pinned to {SOURCE_ATLAS.relative_to(ROOT)}")
    if output.exists():
        raise FileExistsError(f"refusing to replace existing cleanup output: {output}")

    image = _read_source_atlas(source)
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()
    if source_sha256 != PINNED_SOURCE_SHA256:
        raise ValueError("candidate-02 bytes differ from the reviewed source hash; manual review is required")
    cleaned, receipt = clean_atlas(image, source_sha256)
    cleaned_path = output / OUTPUT_ATLAS_NAME
    cleaned_sha256 = hashlib.sha256(cleaned.tobytes()).hexdigest()
    # Hash PNG bytes after save as well as canonical RGBA pixels for exact provenance.
    output.mkdir(parents=True, exist_ok=False)
    cleaned.save(cleaned_path, format="PNG")
    png_sha256 = hashlib.sha256(cleaned_path.read_bytes()).hexdigest()
    receipt["cleanedRgbaSha256"] = cleaned_sha256
    receipt["cleanedPngSha256"] = png_sha256
    receipt["cleanedPath"] = cleaned_path.name

    native_output = output / "native-export"
    animation_exporter.export_animation(cleaned_path, native_output, COLUMNS, BODY_HEIGHT, ROWS)
    receipt["nativeExportPath"] = native_output.name
    receipt["nativeFiles"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(native_output.iterdir())
        if path.is_file()
    }
    (output / RECEIPT_NAME).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path,
                        help="external project-shaped authoring workspace")
    parser.add_argument("--input", type=Path, help="pinned candidate-02 RGBA atlas")
    parser.add_argument("--output-dir", type=Path, help="new external cleaned review bundle directory")
    args = parser.parse_args()
    try:
        configure_private_root(args.project_root)
        output = export_review_bundle(args.input or SOURCE_ATLAS, args.output_dir or OUTPUT_DIRECTORY)
    except (FileExistsError, OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Created provisional Cabir alpha-cleanup review bundle: {output}")


if __name__ == "__main__":
    main()
