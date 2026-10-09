#!/usr/bin/env python3
"""Mechanically export a row-major Cabir frame grid to battle canvases.

Every frame uses one union alpha extent, one scale, and one fixed placement so
unequal animation silhouettes do not pump or drift. The source atlas is read
only; this tool does not create or install runtime bindings.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
LOGICAL_CANVAS = (450, 400)
ANCHOR_X = 196.5
FEET_BASELINE_Y = 268
MAX_SOURCE_SIDE = 8192
MAX_SOURCE_PIXELS = 64 * 1024 * 1024
MAX_COLUMNS = 32
MAX_ROWS = 32
MAX_BODY_HEIGHT = FEET_BASELINE_Y
GIF_BACKGROUND = (42, 31, 34, 255)


def _within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def validate_new_output_directory(input_path: Path, output_dir: Path) -> Path:
    """Return a resolved new output path without permitting source/runtime writes."""
    source = input_path.expanduser().resolve()
    output = output_dir.expanduser().resolve()

    if output == source.parent:
        raise ValueError("output directory must not be the source atlas parent")
    if _within(source, output):
        raise ValueError("output directory must not contain the source atlas")
    if _within(output, ROOT.resolve()) or _within(output_dir.expanduser().absolute(), ROOT.absolute()):
        raise ValueError(f"output directory must be outside the checkout: {output}")
    if output_dir.is_symlink() or output.exists():
        raise FileExistsError(f"output directory must be new: {output}")
    return output


def _edge_alpha_count(alpha: Image.Image, side: str) -> int:
    width, height = alpha.size
    pixels = alpha.load()
    if side == "left":
        return sum(pixels[0, y] > 0 for y in range(height))
    if side == "right":
        return sum(pixels[width - 1, y] > 0 for y in range(height))
    if side == "top":
        return sum(pixels[x, 0] > 0 for x in range(width))
    return sum(pixels[x, height - 1] > 0 for x in range(width))


def _grid_boundaries(dimension: int, parts: int) -> list[int]:
    """Partition a dimension proportionally, assigning each source pixel once."""
    return [round(index * dimension / parts) for index in range(parts + 1)]


def _validate_dimensions(width: int, height: int, columns: int, rows: int = 1) -> None:
    if columns < 1 or columns > MAX_COLUMNS:
        raise ValueError(f"columns must be between 1 and {MAX_COLUMNS}")
    if rows < 1 or rows > MAX_ROWS:
        raise ValueError(f"rows must be between 1 and {MAX_ROWS}")
    if width < columns * 2 or height < rows * 2:
        raise ValueError("atlas dimensions are too small for the requested panels")
    if width > MAX_SOURCE_SIDE or height > MAX_SOURCE_SIDE or width * height > MAX_SOURCE_PIXELS:
        raise ValueError("atlas dimensions exceed the safe source limit")
    # Preserve the established one-row horizontal-atlas contract. Multirow
    # generated grids may differ by a pixel; their cells are proportionally
    # partitioned and transparently padded after source-edge validation.
    if rows == 1 and width % columns:
        raise ValueError(f"atlas width {width} is not divisible into {columns} equal columns")
    x_bounds = _grid_boundaries(width, columns)
    y_bounds = _grid_boundaries(height, rows)
    if min(b - a for a, b in zip(x_bounds, x_bounds[1:])) < 2 or min(
        b - a for a, b in zip(y_bounds, y_bounds[1:])
    ) < 2:
        raise ValueError("atlas panels are too small")


def load_atlas(input_path: Path, columns: int = 4, rows: int = 1) -> tuple[Image.Image, list[Image.Image], tuple[int, int, int, int]]:
    """Read an RGBA row-major atlas and compute one shared visible extent."""
    try:
        opened = Image.open(input_path)
    except (OSError, ValueError) as error:
        raise ValueError(f"could not open source atlas: {input_path}") from error

    with opened:
        width, height = opened.size
        # Check the header dimensions before copy() forces pixel decoding/allocation.
        _validate_dimensions(width, height, columns, rows)
        if opened.mode != "RGBA":
            raise ValueError("source atlas must contain a real RGBA channel")
        if opened.format == "GIF" or getattr(opened, "is_animated", False):
            raise ValueError("source must be one static atlas image")
        atlas = opened.copy()
    return load_atlas_from_image(atlas, columns, rows)


def _centered_left(width: int) -> int:
    # Integer canvas pixels cannot represent every half-pixel placement. This
    # keeps the requested 196.5 anchor exact for odd widths and rounds down for
    # even widths (at most a half-pixel difference).
    return math.floor(ANCHOR_X - width / 2)


def render_frames(atlas: Image.Image, columns: int, height: int, rows: int = 1) -> tuple[list[Image.Image], dict]:
    if height < 1 or height > MAX_BODY_HEIGHT:
        raise ValueError(f"body height must be between 1 and {MAX_BODY_HEIGHT}")

    atlas, source_frames, bounds = load_atlas_from_image(atlas, columns, rows)
    x0, y0, x1, y1 = bounds
    shared_width = x1 - x0
    shared_height = y1 - y0
    x_boundaries = _grid_boundaries(atlas.width, columns)
    y_boundaries = _grid_boundaries(atlas.height, rows)
    widths = [right - left for left, right in zip(x_boundaries, x_boundaries[1:])]
    heights = [bottom - top for top, bottom in zip(y_boundaries, y_boundaries[1:])]
    scale = height / shared_height
    scaled_width = max(1, round(shared_width * scale))
    left = _centered_left(scaled_width)
    top = FEET_BASELINE_Y - height
    if left < 0 or left + scaled_width > LOGICAL_CANVAS[0] or top < 0:
        raise ValueError("scaled atlas body does not fit the battle canvas")

    rendered: list[Image.Image] = []
    for frame in source_frames:
        # All panels receive the identical crop and resize dimensions. Unequal
        # silhouettes therefore keep their original relative scale and pose.
        body = frame.crop(bounds).resize((scaled_width, height), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", LOGICAL_CANVAS, (0, 0, 0, 0))
        canvas.alpha_composite(body, (left, top))
        rendered.append(canvas)

    metadata = {
        "columns": columns,
        "rows": rows,
        "sourceFrameCount": rows * columns,
        "frameOrder": "row-major",
        "sourcePanelSize": [max(widths), max(heights)],
        "sourceCellWidths": widths,
        "sourceCellHeights": heights,
        "sourceCellBoundaryPolicy": "round(i * dimension / part_count); transparent right/bottom padding",
        "sharedSourceBounds": list(bounds),
        "sharedSourceBodySize": [shared_width, shared_height],
        "scale": scale,
        "resizedBodySize": [scaled_width, height],
        "logicalCanvas": list(LOGICAL_CANVAS),
        "anchorXRequested": ANCHOR_X,
        "anchorXActual": left + scaled_width / 2,
        "feetBaselineY": FEET_BASELINE_Y,
        "placement": [left, top],
        "resampling": "LANCZOS",
        "alphaPolicy": "preserved; no thresholding or cleanup",
    }
    return rendered, metadata


def load_atlas_from_image(atlas: Image.Image, columns: int, rows: int = 1) -> tuple[Image.Image, list[Image.Image], tuple[int, int, int, int]]:
    """Validate an already opened atlas using the same geometry contract."""
    width, height = atlas.size
    _validate_dimensions(width, height, columns, rows)
    if atlas.mode != "RGBA":
        raise ValueError("source atlas must contain a real RGBA channel")

    x_bounds = _grid_boundaries(width, columns)
    y_bounds = _grid_boundaries(height, rows)
    widths = [right - left for left, right in zip(x_bounds, x_bounds[1:])]
    heights = [bottom - top for top, bottom in zip(y_bounds, y_bounds[1:])]
    padded_width = max(widths)
    padded_height = max(heights)
    alpha = atlas.getchannel("A")
    alpha_min, alpha_max = alpha.getextrema()
    if alpha_min == 255 or alpha_max == 0:
        raise ValueError("atlas needs visible art and transparent alpha pixels")

    frames: list[Image.Image] = []
    frame_bounds: list[tuple[int, int, int, int]] = []
    for row in range(rows):
        for column in range(columns):
            index = row * columns + column
            frame = atlas.crop((
                x_bounds[column],
                y_bounds[row],
                x_bounds[column + 1],
                y_bounds[row + 1],
            ))
            frame_alpha = frame.getchannel("A")
            bbox = frame_alpha.getbbox()
            if bbox is None:
                raise ValueError(f"atlas panel {index} is empty")
            for edge in ("left", "right", "top", "bottom"):
                if _edge_alpha_count(frame_alpha, edge) > 1:
                    raise ValueError(
                        f"atlas panel {index} has more than one visible pixel on its {edge} edge; "
                        "the source may be clipped"
                    )
            if frame.size != (padded_width, padded_height):
                padded = Image.new("RGBA", (padded_width, padded_height), (0, 0, 0, 0))
                # Paste without a mask so even transparent source pixels retain
                # their original RGBA values. Padding adds only transparent pixels.
                padded.paste(frame, (0, 0))
                frame = padded
            frames.append(frame)
            frame_bounds.append(bbox)

    bounds = (
        min(box[0] for box in frame_bounds),
        min(box[1] for box in frame_bounds),
        max(box[2] for box in frame_bounds),
        max(box[3] for box in frame_bounds),
    )
    return atlas, frames, bounds


def _review_crop_bounds(frames: list[Image.Image], margin: int = 8) -> tuple[int, int, int, int]:
    boxes = [frame.getchannel("A").getbbox() for frame in frames]
    visible = [box for box in boxes if box is not None]
    if not visible:
        raise ValueError("rendered atlas has no visible pixels")
    x0 = max(0, min(box[0] for box in visible) - margin)
    y0 = max(0, min(box[1] for box in visible) - margin)
    x1 = min(LOGICAL_CANVAS[0], max(box[2] for box in visible) + margin)
    y1 = min(LOGICAL_CANVAS[1], max(box[3] for box in visible) + margin)
    return x0, y0, x1, y1


def _contact_sheet(frames: list[Image.Image], crop_bounds: tuple[int, int, int, int]) -> Image.Image:
    scale = 4
    sheet_columns = min(2, len(frames))
    sheet_rows = math.ceil(len(frames) / sheet_columns)
    crop_width = crop_bounds[2] - crop_bounds[0]
    crop_height = crop_bounds[3] - crop_bounds[1]
    sheet = Image.new(
        "RGBA",
        (sheet_columns * crop_width * scale, sheet_rows * crop_height * scale),
        (0, 0, 0, 0),
    )
    for index, frame in enumerate(frames):
        enlarged = frame.crop(crop_bounds).resize((crop_width * scale, crop_height * scale), Image.Resampling.NEAREST)
        x = (index % sheet_columns) * enlarged.width
        y = (index // sheet_columns) * enlarged.height
        sheet.alpha_composite(enlarged, (x, y))
    return sheet


def _animation_gif(frames: list[Image.Image], crop_bounds: tuple[int, int, int, int]) -> list[Image.Image]:
    # GIF is only a compact review proof. Composite it over a neutral matte;
    # the frame PNGs and contact sheet retain their original alpha.
    gif_frames = []
    crop_size = (crop_bounds[2] - crop_bounds[0], crop_bounds[3] - crop_bounds[1])
    background = Image.new("RGBA", crop_size, GIF_BACKGROUND)
    for frame in frames:
        preview = background.copy()
        preview.alpha_composite(frame.crop(crop_bounds))
        gif_frames.append(preview.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT))
    return gif_frames


def export_animation(input_path: Path, output_dir: Path, columns: int = 4, height: int = 60, rows: int = 1) -> Path:
    output = validate_new_output_directory(input_path, output_dir)
    atlas, _frames, _bounds = load_atlas(input_path, columns, rows)
    source_sha256 = hashlib.sha256(input_path.read_bytes()).hexdigest()
    frames, metadata = render_frames(atlas, columns, height, rows)
    review_bounds = _review_crop_bounds(frames)
    metadata["sourceName"] = input_path.name
    metadata["sourceSha256"] = source_sha256
    metadata["reviewCropBounds"] = list(review_bounds)

    output.mkdir(parents=True, exist_ok=False)
    for index, frame in enumerate(frames):
        frame.save(output / f"frame-{index:02d}.png")

    _contact_sheet(frames, review_bounds).save(output / "contact-sheet-4x.png")
    gif_frames = _animation_gif(frames, review_bounds)
    gif_frames[0].save(
        output / "animation-proof.gif",
        save_all=True,
        append_images=gif_frames[1:],
        duration=140,
        loop=0,
        disposal=2,
        optimize=False,
    )
    (output / "export.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    if hashlib.sha256(input_path.read_bytes()).hexdigest() != source_sha256:
        raise RuntimeError("source atlas changed during export")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="static RGBA frame atlas")
    parser.add_argument("--output-dir", type=Path, required=True, help="new export directory (must not exist)")
    parser.add_argument("--columns", type=int, default=4, help="equal-width grid columns (default: 4)")
    parser.add_argument("--rows", type=int, default=1, help="equal-height atlas rows read top-to-bottom (default: 1)")
    parser.add_argument("--height", type=int, default=60, help="shared body height in pixels (default: 60)")
    args = parser.parse_args()
    try:
        output = export_animation(args.input, args.output_dir, args.columns, args.height, args.rows)
    except (FileExistsError, OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Created mechanical Cabir animation exports: {output}")


if __name__ == "__main__":
    main()
