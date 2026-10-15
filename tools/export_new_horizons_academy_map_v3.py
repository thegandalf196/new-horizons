#!/usr/bin/env python3
"""Mechanically register the flagless Academy map-body v3 masters.

The exporter reuses the reviewed v2 master/source registration boxes exactly.
It writes only native-size PNG exports and private before/after comparisons;
it does not recolor, repaint, or touch the live module images.

Use the verified Python interpreter with --private-root PATH, optionally --check.
PATH must contain the complete private v2 baseline and v3 authoring inputs;
neither source images nor prompts are reconstructed from the shipping pack.
"""

from __future__ import annotations

import argparse
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

import import_new_horizons_academy_assets as academy_importer


ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT
PRIVATE_ROOT = None
SOURCE_ROOT = academy_importer.SOURCE_ROOT
REVISION_ROOT = ROOT / SOURCE_ROOT / "map-revisions/v3"
BASELINE_ROOT = ROOT / SOURCE_ROOT / "map-revisions/v2"
REVISION = "v3"

PROMPTS = {
    "village": "masters/village.prompt.txt",
    "fort": "masters/fort.prompt.txt",
    "capitol": "masters/capitol.prompt.txt",
}

REGISTRATIONS = {
    name: {
        "sourceMaster": record["sourceMaster"],
        "master": f"masters/{name}.png",
        "prompt": PROMPTS[name],
        "resource": record["resource"],
        "export": record["export"],
        "runtime": record["runtime"],
        "masterSize": record["masterSize"],
        "masterSolidBox": record["masterSolidBox"],
        "sourceSolidBox": record["sourceSolidBox"],
        "dimensions": record["dimensions"],
        "resampling": record["resampling"],
    }
    for name, record in academy_importer.MAP_REVISION_SLOTS.items()
}


def external_root(path: Path) -> Path:
    requested = path.expanduser().absolute()
    if any(part.is_symlink() for part in (requested, *requested.parents)):
        raise ValueError("private authoring paths must not traverse symlinks")
    resolved = requested.resolve()
    if resolved.is_relative_to(CHECKOUT) or CHECKOUT.is_relative_to(resolved):
        raise ValueError("private root must be outside and not overlap the checkout")
    return resolved


def configure_private_root(path: Path) -> None:
    global ROOT, PRIVATE_ROOT, REVISION_ROOT, BASELINE_ROOT
    ROOT = PRIVATE_ROOT = external_root(path)
    REVISION_ROOT = ROOT / SOURCE_ROOT / "map-revisions/v3"
    BASELINE_ROOT = ROOT / SOURCE_ROOT / "map-revisions/v2"


def private_output_path(path: Path) -> Path:
    if PRIVATE_ROOT is None or ROOT != PRIVATE_ROOT:
        raise ValueError("authoring requires an explicit private root")
    if external_root(PRIVATE_ROOT) != PRIVATE_ROOT:
        raise ValueError("configured private root changed")
    requested = path.absolute()
    if any(part.is_symlink() for part in (requested, *requested.parents)):
        raise ValueError("private output paths must not traverse symlinks")
    if not requested.is_relative_to(PRIVATE_ROOT) or not requested.resolve().is_relative_to(PRIVATE_ROOT):
        raise ValueError("authoring output escapes the private root")
    return requested


def sha256_hex(payload: bytes) -> str:
    import hashlib

    return hashlib.sha256(payload).hexdigest()


def png_bytes(image: Image.Image) -> bytes:
    output = BytesIO()
    image.save(output, format="PNG", optimize=False)
    return output.getvalue()


def compact_json(value: object) -> bytes:
    return (json.dumps(value, indent="\t", ensure_ascii=False) + "\n").encode("utf-8")


def register_master(master_path: Path, registration: dict) -> Image.Image:
    destination = registration["sourceSolidBox"]
    crop = registration["masterSolidBox"]
    crop_box = (
        crop["left"],
        crop["top"],
        crop["left"] + crop["width"],
        crop["top"] + crop["height"],
    )

    with Image.open(master_path) as opened:
        master = opened.convert("RGBA")
    if list(master.size) != registration["masterSize"]:
        raise ValueError(f"Unexpected v3 master size for {master_path}: {master.size}")
    if crop_box[2] > master.width or crop_box[3] > master.height:
        raise ValueError(f"Registered masterSolidBox exceeds {master_path}: {crop_box} vs {master.size}")

    fitted = master.crop(crop_box).resize(
        (destination["width"], destination["height"]),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGBA", tuple(registration["dimensions"]), (0, 0, 0, 0))
    canvas.alpha_composite(fitted, (destination["left"], destination["top"]))
    return canvas


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
    differences = difference.get_flattened_data()
    channel_sum = sum(sum(pixel) for pixel in differences)
    changed_pixels = sum(1 for pixel in differences if any(pixel))
    return png_bytes(comparison), {
        "comparison": "left=exact registered v2 export; right=mechanical flagless v3 export",
        "dimensions": [192, 192],
        "changedPixels": changed_pixels,
        "pixelCount": old.width * old.height,
        "meanAbsoluteChannelDifference": channel_sum / (old.width * old.height * 4),
        "runtimeAcceptance": False,
    }


def build_revision() -> tuple[dict, dict[str, bytes], dict[str, tuple[bytes, bytes]]]:
    v2 = academy_importer.load_map_revision(
        ROOT,
        academy_importer.APPROVED_MAP_REVISION_MANIFEST_SHA256,
    )
    source_registration_path = academy_importer._academy_source_file(ROOT, "integration/academy-assets.json")
    manifest = {
        "schemaVersion": 1,
        "revision": REVISION,
        "sourceRegistration": {
            "path": "integration/academy-assets.json",
            "sha256": sha256_hex(source_registration_path.read_bytes()),
        },
        "bodies": {},
    }
    exports: dict[str, bytes] = {}
    comparisons: dict[str, tuple[bytes, bytes]] = {}

    for name, registration in REGISTRATIONS.items():
        baseline_record = v2["manifest"]["bodies"][name]
        master_path = REVISION_ROOT / registration["master"]
        prompt_path = REVISION_ROOT / registration["prompt"]
        candidate = register_master(master_path, registration)
        export = png_bytes(candidate)
        baseline_path = BASELINE_ROOT / baseline_record["export"]
        with Image.open(baseline_path) as opened:
            baseline = opened.convert("RGBA")
        comparison, metrics = comparison_bytes(baseline, candidate)
        metrics["registration"] = {
            "name": name,
            "master": registration["master"],
            "masterSize": registration["masterSize"],
            "baselineExport": str(baseline_path.relative_to(ROOT)),
            "masterSolidBox": registration["masterSolidBox"],
            "sourceSolidBox": registration["sourceSolidBox"],
            "canvasSize": registration["dimensions"],
            "resampling": registration["resampling"],
            "runtimeAcceptance": False,
        }
        manifest["bodies"][name] = {
            **registration,
            "sourceMasterSha256": baseline_record["sourceMasterSha256"],
            "masterSha256": sha256_hex(master_path.read_bytes()),
            "promptSha256": sha256_hex(prompt_path.read_bytes()),
            "sha256": sha256_hex(export),
        }
        exports[registration["runtime"]] = export
        comparisons[name] = (comparison, compact_json(metrics))

    return manifest, exports, comparisons


def write_or_check(path: Path, payload: bytes, check_only: bool) -> None:
    path = private_output_path(path)
    if check_only:
        if not path.is_file() or path.read_bytes() != payload:
            raise RuntimeError(f"Generated Academy map v3 file is missing or stale: {path.relative_to(ROOT)}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        private_output_path(path)
        path.write_bytes(payload)


def process(check_only: bool) -> None:
    outputs = [REVISION_ROOT / "manifest.json"]
    for name, registration in REGISTRATIONS.items():
        outputs.append(REVISION_ROOT / registration["export"])
        stem = f"{name}-map-body-v3-before-after"
        outputs.extend(REVISION_ROOT / "comparisons" / f"{stem}.{suffix}" for suffix in ("png", "json"))
    for path in outputs:
        private_output_path(path)
    manifest, exports, comparisons = build_revision()
    write_or_check(REVISION_ROOT / "manifest.json", compact_json(manifest), check_only)
    for name, registration in REGISTRATIONS.items():
        export_path = REVISION_ROOT / registration["export"]
        write_or_check(export_path, exports[registration["runtime"]], check_only)
        comparison, metrics = comparisons[name]
        stem = f"{name}-map-body-v3-before-after"
        write_or_check(REVISION_ROOT / "comparisons" / f"{stem}.png", comparison, check_only)
        write_or_check(REVISION_ROOT / "comparisons" / f"{stem}.json", metrics, check_only)
        print(f"{name}: {export_path.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify manifest, exports, and comparisons without writing")
    parser.add_argument("--private-root", required=True, type=Path,
                        help="complete private authoring workspace outside the checkout")
    args = parser.parse_args()
    try:
        configure_private_root(args.private_root)
        process(args.check)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f"Academy map v3 export failed: {error}\n")
    print("PASS: Academy map v3 exports preserve v2 registration and pin all sources")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
