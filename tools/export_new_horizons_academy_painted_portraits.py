#!/usr/bin/env python3
"""Mechanically export the authored Academy portrait-painting preview."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
REVISION_ROOT = ROOT / "assets/new-horizons/academy/portrait-revisions/v2"
SOURCE = REVISION_ROOT / "masters/stoneGargoyle-painted.png"
PROMPT = REVISION_ROOT / "masters/stoneGargoyle-painted.prompt.txt"
OUTPUT = REVISION_ROOT / "exports/stoneGargoyle.png"
MANIFEST = REVISION_ROOT / "exports/stoneGargoyle.manifest.json"
PREVIEW_DIR = ROOT / "build/nh-up239-validation/stoneGargoyle-painted"
SIZE = (58, 64)
EXPECTED_SOURCE_SHA256 = "0d23df17ec4daa51b24c5a54cbf0f0c32442649622a9fce101b0da953cb750b9"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def png_bytes(image: Image.Image) -> bytes:
    output = io.BytesIO()
    image.save(output, format="PNG", optimize=False)
    return output.getvalue()


def render_export() -> tuple[Image.Image, dict[str, object]]:
    source_bytes = SOURCE.read_bytes()
    source_sha = sha256(source_bytes)
    if source_sha != EXPECTED_SOURCE_SHA256:
        raise ValueError(f"source master SHA256 changed: {SOURCE}")

    with Image.open(io.BytesIO(source_bytes)) as opened:
        source = opened.copy()
        source_size = source.size
        has_alpha = "A" in source.getbands() or "transparency" in opened.info
        source = source.convert("RGBA" if has_alpha else "RGB")

    source_ratio = source_size[0] / source_size[1]
    target_ratio = SIZE[0] / SIZE[1]
    aspect_error = abs(source_ratio / target_ratio - 1.0)
    if aspect_error > 0.005:
        raise ValueError(
            f"source aspect ratio differs from {SIZE[0]}x{SIZE[1]} by "
            f"{aspect_error:.3%}; refusing crop or stretch"
        )

    resized = source.resize(SIZE, Image.Resampling.LANCZOS)
    manifest: dict[str, object] = {
        "asset": "Academy Stone Gargoyle painted portrait preview",
        "runtime_authorized": False,
        "source": SOURCE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha,
        "source_size_px": list(source_size),
        "prompt": PROMPT.relative_to(ROOT).as_posix(),
        "prompt_sha256": sha256(PROMPT.read_bytes()),
        "export": OUTPUT.relative_to(ROOT).as_posix(),
        "export_sha256": sha256(png_bytes(resized)),
        "export_size_px": list(SIZE),
        "resampling": "Pillow Image.Resampling.LANCZOS",
        "crop": "none",
        "repaint": "none",
        "source_to_export_aspect_error_percent": round(aspect_error * 100, 6),
    }
    return resized, manifest


def render_private_previews(native: Image.Image) -> dict[str, bytes]:
    native_bytes = png_bytes(native)
    enlarged = native.resize((SIZE[0] * 8, SIZE[1] * 8), Image.Resampling.NEAREST)
    enlarged_bytes = png_bytes(enlarged)

    scale = 4
    native_display = native.resize((SIZE[0] * scale, SIZE[1] * scale), Image.Resampling.NEAREST)
    sheet = Image.new("RGB", (760, 570), (39, 34, 30))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    draw.text((24, 18), "Authored Stone Gargoyle portrait - reduction preview only", fill=(239, 226, 202), font=font)
    draw.text((24, 46), "58 x 64 export, shown at 4x nearest", fill=(220, 203, 178), font=font)
    draw.text((286, 46), "8x nearest (464 x 512)", fill=(220, 203, 178), font=font)
    sheet.paste(native_display, (24, 68))
    sheet.paste(enlarged, (286, 68))
    return {
        "stoneGargoyle-native.png": native_bytes,
        "stoneGargoyle-8x-nearest.png": enlarged_bytes,
        "stoneGargoyle-native-and-8x.png": png_bytes(sheet),
    }


def write_outputs(check: bool, parser: argparse.ArgumentParser) -> None:
    if not SOURCE.is_file() or not PROMPT.is_file():
        parser.error("the authored master and its prompt must both be present")

    try:
        native, manifest = render_export()
    except (OSError, ValueError) as error:
        parser.error(str(error))

    expected_export = png_bytes(native)
    expected_manifest = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    previews = render_private_previews(native)

    if check:
        if not OUTPUT.is_file() or OUTPUT.read_bytes() != expected_export:
            parser.error(f"painted portrait export differs from its authored source: {OUTPUT}")
        if not MANIFEST.is_file() or MANIFEST.read_bytes() != expected_manifest:
            parser.error(f"portrait export manifest differs from its authored source: {MANIFEST}")
        # Private comparison files are ignored build outputs, not prerequisites
        # for checking a fresh source checkout's committed exports.
        for name, expected in previews.items():
            path = PREVIEW_DIR / name
            if path.is_file() and path.read_bytes() != expected:
                parser.error(f"private preview is stale: {path}")
        print("PASS: painted portrait export and source hashes match")
        return

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(expected_export)
    MANIFEST.write_bytes(expected_manifest)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    for name, contents in previews.items():
        (PREVIEW_DIR / name).write_bytes(contents)
    print(f"Wrote preview-only export: {OUTPUT.relative_to(ROOT)} ({SIZE[0]}x{SIZE[1]})")
    print(f"Wrote authored-only native/8x comparison: {PREVIEW_DIR}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify exports and private previews without writing")
    args = parser.parse_args()
    write_outputs(args.check, parser)
    return 0


if __name__ == "__main__":
    sys.exit(main())
