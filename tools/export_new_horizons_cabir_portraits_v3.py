#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build pinned transparent Cabir and Cabir Master portrait derivatives.

The only foreground sources are the two original generated v3 standing
masters. The outputs retain subject alpha rather than baking a background;
no purchaser creature frame is read.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import sys

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

# Reuse the established bounded Cabir alpha-cleanup primitives. Only pixels
# outside the largest alpha>=16 silhouette, dilated by four pixels, can have
# their alpha cleared; the same helper fails closed above alpha 10.
import clean_new_horizons_cabir_alpha as alpha_cleanup  # noqa: E402


LARGE_SIZE = (58, 64)
LARGE_CONTENT_BOX = (56, 62)
LARGE_MARGIN = (1, 1)
SMALL_SIZE = (32, 32)
SMALL_CONTENT_SIZE = (30, 30)
SMALL_AVATAR_CROP_SIZE = 800
LANCZOS = Image.Resampling.LANCZOS
NEAREST = Image.Resampling.NEAREST
RUNTIME_DIRECTORY = Path("Mods/new-horizons/Images")
PREVIOUS_RUNTIME_SHA256 = {
    "NH_cabir_icon_large.png": "09b3de803b4db2e29343346753ca72aefd3fee35f799429f340631d0994337b1",
    "NH_cabir_icon_small.png": "1fb6843cfb3ced0fb9c468d7b101817c87ac4ccd834500f09d6bf242071c1400",
    "NH_cabirMaster_icon_large.png": "a1cd9a491c52354aaa3182586542ae28661b8647a9c2b0e5182426a1fe481d93",
    "NH_cabirMaster_icon_small.png": "fb280dc34405bb184baadfc6078ab8ffaf41042499bd6d529e4f62870e815ab1",
}

CREATURES = {
    "cabir": {
        "source": Path("assets/new-horizons/creatures/cabir/v3/standing-master.png"),
        "sourceSha256": "e9e5cfacb4cafa8f55c0160a400a4f0c6ea3426d77f631c928de27eaa4033060",
        "prompt": Path("assets/new-horizons/creatures/cabir/v3/standing.prompt.txt"),
        "promptSha256": "66a9aa4bf1be4f6f964ace0f038cde03c134556a756d87a7f6bb106a2be0a082",
        "outputDirectory": Path("assets/new-horizons/creatures/cabir/v3/portrait-export-v2"),
        "largeName": "NH_cabir_icon_large.png",
        "smallName": "NH_cabir_icon_small.png",
    },
    "cabirMaster": {
        "source": Path("assets/new-horizons/creatures/cabir-master/v3/standing-master.png"),
        "sourceSha256": "c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15",
        "prompt": Path("assets/new-horizons/creatures/cabir-master/v3/standing.prompt.txt"),
        "promptSha256": "3b97121db995db93b8ec57e775b147c3edfca9da5d0a3696dd471ff67390a5b7",
        "outputDirectory": Path("assets/new-horizons/creatures/cabir-master/v3/portrait-export-v2"),
        "largeName": "NH_cabirMaster_icon_large.png",
        "smallName": "NH_cabirMaster_icon_small.png",
    },
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def png_bytes(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=False, compress_level=9)
    return buffer.getvalue()


def _read_pinned_rgba(relative_path: Path, expected_sha256: str, label: str) -> Image.Image:
    path = ROOT / relative_path
    raw = path.read_bytes()
    if sha256_bytes(raw) != expected_sha256:
        raise ValueError(f"pinned {label} bytes changed: {relative_path}")
    with Image.open(io.BytesIO(raw)) as opened:
        if opened.format != "PNG" or opened.mode != "RGBA" or opened.size != (1323, 1189):
            raise ValueError(f"expected pinned 1323x1189 RGBA PNG for {label}: {relative_path}")
        if getattr(opened, "is_animated", False):
            raise ValueError(f"animated image is not a valid {label}: {relative_path}")
        return opened.copy()


def _clean_master(source: Image.Image) -> tuple[Image.Image, dict]:
    if source.mode != "RGBA":
        raise ValueError("Cabir standing source must be RGBA")
    width, height = source.size
    alpha = source.getchannel("A")
    seed, components = alpha_cleanup._connected_components(alpha, 16)
    retained = alpha_cleanup._dilate_square(seed, width, height, 4)
    source_bytes = source.tobytes()
    output_bytes = bytearray(source_bytes)
    histogram: dict[str, int] = {}
    removed_count = 0
    max_removed = 0
    for pixel_index, keep in enumerate(retained):
        source_offset = pixel_index * 4
        alpha_value = source_bytes[source_offset + 3]
        if keep or alpha_value == 0:
            continue
        removed_count += 1
        max_removed = max(max_removed, alpha_value)
        histogram[str(alpha_value)] = histogram.get(str(alpha_value), 0) + 1
        output_bytes[source_offset + 3] = 0
    if max_removed > 10:
        raise ValueError(f"alpha cleanup would remove alpha {max_removed}; manual review is required")

    cleaned = Image.frombytes("RGBA", source.size, bytes(output_bytes))
    bounds = cleaned.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("Cabir source has no retained alpha silhouette")
    return cleaned, {
        "seedThreshold": 16,
        "componentConnectivity": "8-connected",
        "seedComponentCount": len(components),
        "largestSeedArea": components[0]["area"],
        "largestSeedBounds": components[0]["bbox"],
        "dilationRadius": 4,
        "retainedRgba": "original source RGBA bytes are preserved inside the dilated largest component",
        "removedPixels": "outside that region, only alpha is set to zero; RGB bytes are unchanged",
        "removedPixelCount": removed_count,
        "maximumRemovedAlpha": max_removed,
        "removedAlphaHistogram": histogram,
        "cleanedAlphaBounds": list(bounds),
        "sourceMasterModified": False,
    }


def _fit_size(source_size: tuple[int, int], box_size: tuple[int, int]) -> tuple[int, int]:
    width, height = source_size
    scale = min(box_size[0] / width, box_size[1] / height)
    return max(1, round(width * scale)), max(1, round(height * scale))


def _resize_alpha_correct(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    # Resizing premultiplied channels avoids dark fringes from transparent RGB.
    return source.convert("RGBa").resize(size, LANCZOS).convert("RGBA")


def _transparent_canvas(sprite: Image.Image, at: tuple[int, int], size: tuple[int, int]) -> Image.Image:
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.alpha_composite(sprite, at)
    return canvas


def _render_portraits(cleaned: Image.Image, alpha_bounds: tuple[int, int, int, int]) -> tuple[Image.Image, Image.Image, dict]:
    left, top, right, bottom = alpha_bounds
    body = cleaned.crop(alpha_bounds)
    large_fit = _fit_size(body.size, LARGE_CONTENT_BOX)
    large_sprite = _resize_alpha_correct(body, large_fit)
    large_at = ((LARGE_SIZE[0] - large_fit[0]) // 2, (LARGE_SIZE[1] - large_fit[1]) // 2)
    large = _transparent_canvas(large_sprite, large_at, LARGE_SIZE)

    crop_size = SMALL_AVATAR_CROP_SIZE
    small_crop = (right - crop_size, top, right, top + crop_size)
    if small_crop[0] < 0 or small_crop[1] < 0 or small_crop[3] > cleaned.height:
        raise ValueError(f"head/upper-body avatar crop is outside the source: {small_crop}")
    avatar = cleaned.crop(small_crop)
    small_sprite = _resize_alpha_correct(avatar, SMALL_CONTENT_SIZE)
    small = _transparent_canvas(small_sprite, (1, 1), SMALL_SIZE)

    layout = {
        "large": {
            "outputSize": list(LARGE_SIZE),
            "sourceCrop": list(alpha_bounds),
            "fitBox": list(LARGE_CONTENT_BOX),
            "resizedSource": list(large_fit),
            "placement": list(large_at),
            "intent": "full-body standing portrait; entire cleaned silhouette is contained, not cropped",
        },
        "small": {
            "outputSize": list(SMALL_SIZE),
            "sourceCrop": list(small_crop),
            "resizedSource": list(SMALL_CONTENT_SIZE),
            "placement": [1, 1],
            "intent": "intentional right-facing head/upper-body avatar crop; legs and rear silhouette are outside this role by design",
            "background": "transparent RGBA; no background is composited",
        },
    }
    return large, small, layout


def build_outputs(creature_id: str) -> dict[Path, bytes]:
    try:
        spec = CREATURES[creature_id]
    except KeyError as error:
        raise ValueError(f"unknown Cabir portrait identity: {creature_id}") from error
    prompt_raw = (ROOT / spec["prompt"]).read_bytes()
    if sha256_bytes(prompt_raw) != spec["promptSha256"]:
        raise ValueError(f"pinned HoMM3-Art prompt changed: {spec['prompt']}")
    source = _read_pinned_rgba(spec["source"], spec["sourceSha256"], creature_id)
    cleaned, cleanup_receipt = _clean_master(source)
    alpha_bounds = cleaned.getchannel("A").getbbox()
    if alpha_bounds is None:
        raise ValueError(f"no cleaned source silhouette: {spec['source']}")
    large, small, layout = _render_portraits(cleaned, alpha_bounds)
    if large.mode != "RGBA" or large.size != LARGE_SIZE or small.mode != "RGBA" or small.size != SMALL_SIZE:
        raise ValueError("portrait render did not produce the pinned native geometries")

    output_dir = spec["outputDirectory"]
    portrait_images = {
        spec["largeName"]: png_bytes(large),
        spec["smallName"]: png_bytes(small),
    }
    assets = {
        output_dir / spec["largeName"]: portrait_images[spec["largeName"]],
        output_dir / spec["smallName"]: portrait_images[spec["smallName"]],
        output_dir / "previews" / spec["largeName"].replace(".png", "_4x.png"): png_bytes(
            large.resize((LARGE_SIZE[0] * 4, LARGE_SIZE[1] * 4), NEAREST)
        ),
        output_dir / "previews" / spec["smallName"].replace(".png", "_4x.png"): png_bytes(
            small.resize((SMALL_SIZE[0] * 4, SMALL_SIZE[1] * 4), NEAREST)
        ),
    }
    receipt = {
        "status": "provisional transparent authored portrait derivatives; no user visual acceptance implied",
        "identity": creature_id,
        "source": {
            "path": spec["source"].as_posix(),
            "sha256": spec["sourceSha256"],
            "promptPath": spec["prompt"].as_posix(),
            "promptSha256": spec["promptSha256"],
            "sourceSize": list(source.size),
            "sourceMode": source.mode,
            "artwork": "original HoMM3-Art generated project artwork; CC0-1.0 per standing-master provenance",
        },
        "alphaCleanup": cleanup_receipt,
        "backgroundTreatment": "transparent RGBA; the prior baked Academy scenery is not composited",
        "resampling": "Pillow LANCZOS on premultiplied RGBA for subject reduction; nearest-neighbour 4x review previews",
        "composition": layout,
        "purchaserPixels": "none read or copied",
        "outputs": {
            path.as_posix(): {"pngSha256": sha256_bytes(data), "byteLength": len(data)}
            for path, data in assets.items()
        },
        "runtimeCopies": {
            (RUNTIME_DIRECTORY / name).as_posix(): {"pngSha256": sha256_bytes(data), "byteLength": len(data)}
            for name, data in portrait_images.items()
        },
    }
    assets[output_dir / "receipt.json"] = (json.dumps(receipt, indent=2) + "\n").encode("utf-8")
    for name, data in portrait_images.items():
        assets[RUNTIME_DIRECTORY / name] = data
    return assets


def verify_or_write(write: bool, parser: argparse.ArgumentParser) -> None:
    if ROOT.resolve().is_relative_to(CHECKOUT):
        parser.error("authoring requires an explicit private workspace outside the checkout")
    expected: dict[Path, bytes] = {}
    for creature_id, spec in CREATURES.items():
        try:
            expected.update(build_outputs(creature_id))
        except (OSError, ValueError) as error:
            parser.error(str(error))

    output_directories = [ROOT / spec["outputDirectory"] for spec in CREATURES.values()]
    if write:
        for output_directory in output_directories:
            if output_directory.is_symlink() or output_directory.exists():
                parser.error(f"refusing to replace existing portrait-export directory: {output_directory.relative_to(ROOT)}")
        for relative_path, data in expected.items():
            if relative_path.parts[:len(RUNTIME_DIRECTORY.parts)] != RUNTIME_DIRECTORY.parts:
                continue
            target = ROOT / relative_path
            if target.is_symlink() or not target.is_file():
                parser.error(f"expected a regular existing runtime portrait: {relative_path}")
            current_hash = sha256_bytes(target.read_bytes())
            if current_hash not in {PREVIOUS_RUNTIME_SHA256.get(target.name), sha256_bytes(data)}:
                parser.error(f"refusing to replace unrecognized runtime portrait: {relative_path}")
        for output_directory in output_directories:
            output_directory.mkdir(parents=True, exist_ok=False)
        for relative_path, data in expected.items():
            target = ROOT / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        print("Wrote pinned transparent Cabir portrait exports, runtime PNGs and 4x previews")
        return

    for relative_path, data in expected.items():
        actual = ROOT / relative_path
        if not actual.is_file() or actual.is_symlink() or actual.read_bytes() != data:
            parser.error(f"missing or non-reproducible Cabir portrait export: {relative_path}")
    print("PASS: transparent Cabir portrait exports, runtime PNGs and previews match pinned sources")


def main() -> int:
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-root", required=True, type=Path,
                        help="private project-shaped input/output workspace outside the checkout")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="create the new portrait-export directories and replace the four pinned runtime portraits")
    mode.add_argument("--check", action="store_true", help="recompute and verify every pinned output without writing")
    args = parser.parse_args()
    ROOT = args.private_root.resolve()
    verify_or_write(args.write, parser)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
