#!/usr/bin/env python3
"""Mechanically register approved Academy map-material masters on native canvases.

This tool performs only the existing masterSolidBox -> sourceSolidBox crop/fit
registration. It does not repaint, recolor, sharpen, or touch runtime assets.

Usage:
    python3 tools/export_new_horizons_academy_map_v2.py --name fort
    python3 tools/export_new_horizons_academy_map_v2.py --check --name fort
    python3 tools/export_new_horizons_academy_map_v2.py  # all available bodies
"""

from __future__ import annotations

import argparse
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw
import import_new_horizons_academy_assets as academy_importer


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = academy_importer.SOURCE_ROOT
REVISION_ROOT = academy_importer.MAP_REVISION_ROOT

# Coordinates are the prior handoff's reviewed map-body registrations. The
# destination sourceSolidBox is intentionally unchanged; each edit uses its
# exact corresponding masterSolidBox, which excludes incidental cast shadow.
REGISTRATIONS = {
    name: {
        "master": record["master"],
        "baselineMaster": record["sourceMaster"],
        "masterSolidBox": tuple(record["masterSolidBox"][key] for key in ("left", "top", "width", "height")),
        "sourceSolidBox": tuple(record["sourceSolidBox"][key] for key in ("left", "top", "width", "height")),
        "masterSize": record["masterSize"],
    }
    for name, record in academy_importer.MAP_REVISION_SLOTS.items()
}


def png_bytes(image: Image.Image) -> bytes:
    output = BytesIO()
    image.save(output, format="PNG", optimize=False)
    return output.getvalue()


def register_master(master_path: Path, registration: dict) -> tuple[Image.Image, list[int]]:
    destination = registration["sourceSolidBox"]
    crop = registration["masterSolidBox"]

    with Image.open(master_path) as opened:
        master = opened.convert("RGBA")

    crop_box = (crop[0], crop[1], crop[0] + crop[2], crop[1] + crop[3])
    if list(master.size) != registration["masterSize"]:
        raise ValueError(f"Unexpected master size for {master_path}: {master.size}, expected {registration['masterSize']}")
    if crop_box[2] > master.width or crop_box[3] > master.height:
        raise ValueError(f"Registered masterSolidBox exceeds {master_path}: {crop_box} vs {master.size}")

    fitted = master.crop(crop_box).resize((destination[2], destination[3]), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
    canvas.alpha_composite(fitted, (destination[0], destination[1]))
    return canvas, list(master.size)


def export_body(name: str) -> tuple[Image.Image, Image.Image, dict]:
    registration = REGISTRATIONS[name]
    destination = registration["sourceSolidBox"]
    crop = registration["masterSolidBox"]
    master_path = ROOT / REVISION_ROOT / registration["master"]
    baseline_path = ROOT / SOURCE_ROOT / registration["baselineMaster"]
    canvas, master_size = register_master(master_path, registration)
    baseline, baseline_master_size = register_master(baseline_path, registration)

    return canvas, baseline, {
        "name": name,
        "master": registration["master"],
        "masterSize": master_size,
        "baselineMaster": str(SOURCE_ROOT / registration["baselineMaster"]),
        "baselineMasterSize": baseline_master_size,
        "masterSolidBox": {"left": crop[0], "top": crop[1], "width": crop[2], "height": crop[3]},
        "sourceSolidBox": {
            "left": destination[0], "top": destination[1],
            "width": destination[2], "height": destination[3],
        },
        "canvasSize": [192, 192],
        "resampling": "LANCZOS",
        "runtimeAcceptance": False,
    }


def comparison_bytes(baseline: Image.Image, candidate: Image.Image) -> tuple[bytes, dict]:
    old = baseline.convert("RGBA")
    new = candidate.convert("RGBA")
    if old.size != (192, 192) or new.size != old.size:
        raise ValueError(f"Native map comparison requires 192x192 canvases, got {old.size} and {new.size}")

    comparison = Image.new("RGBA", (384, 192), (48, 48, 48, 255))
    comparison.alpha_composite(old, (0, 0))
    comparison.alpha_composite(new, (192, 0))
    ImageDraw.Draw(comparison).line((191, 0, 191, 191), fill=(255, 255, 255, 255), width=1)

    difference = ImageChops.difference(old, new)
    pixels = difference.get_flattened_data()
    channel_sum = sum(sum(pixel) for pixel in pixels)
    changed_pixels = sum(1 for pixel in pixels if any(pixel))
    metrics = {
        "comparison": "left=registered v1 source baseline; right=mechanical v2 export",
        "dimensions": [192, 192],
        "changedPixels": changed_pixels,
        "pixelCount": old.width * old.height,
        "meanAbsoluteChannelDifference": channel_sum / (old.width * old.height * 4),
        "runtimeAcceptance": False,
    }
    return png_bytes(comparison), metrics


def write_or_check(path: Path, payload: bytes, check_only: bool) -> None:
    if check_only:
        if not path.is_file() or path.read_bytes() != payload:
            raise RuntimeError(f"Generated map v2 file is missing or stale: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


def process(name: str, check_only: bool) -> None:
    candidate, baseline, registration = export_body(name)
    export_path = ROOT / REVISION_ROOT / "exports" / f"NH_academy_{name}_body.png"
    comparison_path = ROOT / REVISION_ROOT / "comparisons" / f"{name}-map-body-native-before-after.png"
    metrics_path = ROOT / REVISION_ROOT / "comparisons" / f"{name}-map-body-native-before-after.json"
    comparison, metrics = comparison_bytes(baseline, candidate)
    metrics["registration"] = registration
    metrics["candidateExport"] = str(export_path.relative_to(ROOT))

    write_or_check(export_path, png_bytes(candidate), check_only)
    write_or_check(comparison_path, comparison, check_only)
    write_or_check(metrics_path, (json.dumps(metrics, indent="\t") + "\n").encode("utf-8"), check_only)
    print(f"{name}: {export_path.relative_to(ROOT)}; {comparison_path.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", choices=sorted(REGISTRATIONS), action="append", help="body to export; repeatable")
    parser.add_argument("--check", action="store_true", help="verify exports and comparisons without writing")
    args = parser.parse_args()
    names = args.name or list(REGISTRATIONS)
    try:
        for name in names:
            process(name, args.check)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f"Academy map v2 export failed: {error}\n")
    print("PASS: Academy map v2 exports preserve prior registered map-body geometry")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
