#!/usr/bin/env python3
"""Export the reviewed Academy landscape cleanup into its bounded native ROI.

The source master is resized to the original 800x374 canvas. Only the
reviewed lower-left rectangle is copied onto the preserved purchaser-derived
baseline; all pixels outside that rectangle remain unchanged. The script also
creates native comparisons and a registered VillageHall-stage composite. It
does not install or modify the live runtime landscape.
"""

from __future__ import annotations

import argparse
from hashlib import sha256 as sha256_bytes
from io import BytesIO
import json
from pathlib import Path
import sys

from PIL import Image, ImageChops, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT
REVISION_ROOT = ROOT / "assets/new-horizons/academy/background-revisions/v2"
MASTER_PATH = REVISION_ROOT / "landscape-master.png"
PROMPT_PATH = REVISION_ROOT / "landscape.prompt.txt"
BASELINE_PATH = ROOT / "assets/new-horizons/academy/native/town/landscape.png"
HALL_V2_PATH = ROOT / "assets/new-horizons/academy/hall-revisions/v2/exports/village-hall-native.png"

EXPORT_PATH = REVISION_ROOT / "exports/landscape-native.png"
MANIFEST_PATH = REVISION_ROOT / "manifest.json"
NATIVE_COMPARISON_PATH = REVISION_ROOT / "comparisons/landscape-native-before-after.png"
ROI_COMPARISON_PATH = REVISION_ROOT / "comparisons/landscape-roi-nearest-4x-before-after.png"
SCENE_COMPARISON_PATH = REVISION_ROOT / "comparisons/village-hall-stage-mechanical-composite-before-after.png"

NATIVE_SIZE = (800, 374)
MASTER_SIZE = (1836, 857)
ROI = (0, 254, 177, 335)  # left, top, right, bottom (right/bottom exclusive)
ROI_DETAIL_CROP = (0, 242, 205, 350)
UPSCALE = 4

EXPECTED_MASTER_SHA256 = "8df9083e6f09e035ab09733bcb6f0c54361919eff1bdbbdc98f77c7cf191eafd"
EXPECTED_PROMPT_SHA256 = "4fadd928d1c329d3d052c4f2e4535ddfed42b1dbf5478e547e1c1af28daa4b50"
EXPECTED_BASELINE_SHA256 = "73387f56e84327e1bd56edbd92c60052390a91ade79671d362f297f7d7cea7b3"
EXPECTED_HALL_V2_SHA256 = "117e8ae670dda32e7cee0c76d78d4a12acb3a22bd3467b6b6d2cfd74173763cd"


def configure_private_root(private_root: Path) -> None:
    global ROOT
    resolved = private_root.resolve()
    if resolved.is_relative_to(CHECKOUT) or CHECKOUT.is_relative_to(resolved):
        raise ValueError("authoring requires a private project root outside and not overlapping the checkout")
    previous = ROOT
    for name, value in list(globals().items()):
        if name not in ("ROOT", "CHECKOUT") and isinstance(value, Path) and value.is_absolute() and value.is_relative_to(previous):
            globals()[name] = resolved / value.relative_to(previous)
    ROOT = resolved


def require_private_outputs() -> None:
    if ROOT.is_relative_to(CHECKOUT) or CHECKOUT.is_relative_to(ROOT):
        raise ValueError("authoring requires an explicit private project root outside the checkout")
    for path in (EXPORT_PATH, MANIFEST_PATH, NATIVE_COMPARISON_PATH, ROI_COMPARISON_PATH, SCENE_COMPARISON_PATH):
        if not path.resolve().is_relative_to(ROOT):
            raise ValueError(f"authoring output escapes the private project root: {path}")


def file_sha256(path: Path) -> str:
    digest = sha256_bytes(path.read_bytes()).hexdigest()
    return digest


def png_bytes(image: Image.Image) -> bytes:
    buffer = BytesIO()
    image.save(buffer, format="PNG", optimize=False, compress_level=9)
    return buffer.getvalue()


def open_rgb(path: Path) -> Image.Image:
    with Image.open(path) as opened:
        return opened.convert("RGB")


def open_rgba(path: Path) -> Image.Image:
    with Image.open(path) as opened:
        return opened.convert("RGBA")


def require_pinned_file(path: Path, expected_sha256: str, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"Missing {label}: {path}")
    actual = file_sha256(path)
    if actual != expected_sha256:
        raise ValueError(f"Pinned {label} changed: expected {expected_sha256}, got {actual}")


def make_export() -> tuple[Image.Image, Image.Image]:
    require_pinned_file(MASTER_PATH, EXPECTED_MASTER_SHA256, "landscape master")
    require_pinned_file(PROMPT_PATH, EXPECTED_PROMPT_SHA256, "landscape prompt")
    require_pinned_file(BASELINE_PATH, EXPECTED_BASELINE_SHA256, "preserved native landscape")
    require_pinned_file(HALL_V2_PATH, EXPECTED_HALL_V2_SHA256, "reviewed VillageHall v2 export")

    with Image.open(MASTER_PATH) as master_file:
        if master_file.size != MASTER_SIZE or master_file.mode != "RGB":
            raise ValueError(
                f"Unexpected landscape master: {master_file.size} {master_file.mode}; "
                f"expected {MASTER_SIZE} RGB"
            )
        resized_master = master_file.resize(NATIVE_SIZE, Image.Resampling.LANCZOS)

    baseline = open_rgb(BASELINE_PATH)
    if baseline.size != NATIVE_SIZE:
        raise ValueError(f"Unexpected preserved landscape size: {baseline.size}, expected {NATIVE_SIZE}")

    exported = baseline.copy()
    exported.paste(resized_master.crop(ROI), ROI[:2])
    if exported.mode != "RGB" or exported.size != NATIVE_SIZE:
        raise ValueError("Native landscape export changed its required RGB canvas")

    return baseline, exported


def make_comparison(before: Image.Image, after: Image.Image, scale: int = 1) -> Image.Image:
    if before.size != after.size:
        raise ValueError(f"Comparison images differ in size: {before.size} vs {after.size}")
    if scale != 1:
        size = (before.width * scale, before.height * scale)
        before = before.resize(size, Image.Resampling.NEAREST)
        after = after.resize(size, Image.Resampling.NEAREST)

    gutter = 8 * scale
    canvas = Image.new("RGB", (before.width * 2 + gutter, before.height), (32, 32, 32))
    canvas.paste(before, (0, 0))
    canvas.paste(after, (before.width + gutter, 0))
    draw = ImageDraw.Draw(canvas)
    draw.line((before.width + gutter // 2, 0, before.width + gutter // 2, before.height - 1),
              fill=(240, 240, 240), width=max(1, scale))
    return canvas


def make_roi_comparison(baseline: Image.Image, exported: Image.Image) -> Image.Image:
    before = baseline.crop(ROI_DETAIL_CROP)
    after = exported.crop(ROI_DETAIL_CROP)
    return make_comparison(before, after, UPSCALE)


def effective_village_hall_layers() -> list[tuple[int, int, str, int, int, Path]]:
    """Reuse the already-reviewed town-layout/config merge for stage preview."""
    sys.path.insert(0, str(CHECKOUT / "tools"))
    import export_new_horizons_academy_hall_v2 as hall_exporter
    hall_exporter.configure_private_root(ROOT)
    return hall_exporter.effective_village_hall_layers()


def make_registered_scene(background: Image.Image, hall: Image.Image) -> tuple[Image.Image, list[dict]]:
    if background.size != NATIVE_SIZE:
        raise ValueError(f"Registered scene background must be {NATIVE_SIZE}, got {background.size}")
    if hall.size != (177, 75):
        raise ValueError(f"Reviewed VillageHall overlay must be 177x75, got {hall.size}")

    layers = effective_village_hall_layers()
    hall_rows = [row for row in layers if row[2] == "villageHall"]
    if len(hall_rows) != 1:
        raise ValueError(f"Expected one VillageHall-stage layer, found {len(hall_rows)}")
    z, sequence, _, x, y, _ = hall_rows[0]
    if (x, y, z) != (0, 259, 2):
        raise ValueError(f"VillageHall placement changed: expected x0/y259/z2, got x{x}/y{y}/z{z}")
    later_halls = {"townHall", "cityHall", "capitol"}
    if any(row[2] in later_halls for row in layers):
        raise ValueError("Registered scene unexpectedly includes a later Town Hall stage")

    output = background.convert("RGBA")
    scene_inputs = []
    for layer_z, layer_sequence, structure_id, layer_x, layer_y, layer_path in sorted(layers):
        if structure_id == "villageHall":
            layer = hall
            source_path = HALL_V2_PATH
        else:
            with Image.open(layer_path) as opened:
                layer = opened.convert("RGBA")
            source_path = layer_path
        if source_path != HALL_V2_PATH:
            scene_inputs.append({
                "structure": structure_id,
                "path": str(source_path.relative_to(ROOT)),
                "sha256": file_sha256(source_path),
                "position": [layer_x, layer_y],
                "z": layer_z,
                "sequence": layer_sequence,
            })
        output.alpha_composite(layer.convert("RGBA"), (layer_x, layer_y))
    return output.convert("RGB"), scene_inputs


def input_manifest(scene_inputs: list[dict], images: dict[str, bytes]) -> dict:
    def source(path: Path, digest: str | None = None) -> dict:
        return {
            "path": str(path.relative_to(ROOT)),
            "sha256": digest or file_sha256(path),
        }

    with Image.open(MASTER_PATH) as master:
        master_size = list(master.size)
        master_mode = master.mode
    with Image.open(BASELINE_PATH) as baseline:
        baseline_size = list(baseline.size)
        baseline_mode = baseline.mode

    comparisons = {
        "native": NATIVE_COMPARISON_PATH,
        "roiNearest4x": ROI_COMPARISON_PATH,
        "registeredVillageHallStage": SCENE_COMPARISON_PATH,
    }
    return {
        "revision": "v2",
        "status": "Provisional; native composite review only",
        "sources": {
            "master": {**source(MASTER_PATH, EXPECTED_MASTER_SHA256), "dimensions": master_size, "mode": master_mode},
            "prompt": source(PROMPT_PATH, EXPECTED_PROMPT_SHA256),
            "preservedNativeLandscape": {
                **source(BASELINE_PATH, EXPECTED_BASELINE_SHA256),
                "dimensions": baseline_size,
                "mode": baseline_mode,
            },
            "villageHallV2Overlay": {
                **source(HALL_V2_PATH, EXPECTED_HALL_V2_SHA256),
                "placement": {"x": 0, "y": 259, "z": 2},
            },
            "registeredSceneOtherLayers": scene_inputs,
        },
        "export": {
            "path": str(EXPORT_PATH.relative_to(ROOT)),
            "sha256": sha256_bytes(images["export"]).hexdigest(),
            "dimensions": list(NATIVE_SIZE),
            "mode": "RGB",
            "resize": {"dimensions": list(NATIVE_SIZE), "resampling": "Pillow LANCZOS"},
            "composition": "paste resized-master pixels into the reviewed ROI on preserved baseline; no alpha blend",
            "roi": list(ROI),
            "outsideRoi": "pixel-identical to preserved native landscape",
        },
        "comparisons": {
            key: {
                "path": str(path.relative_to(ROOT)),
                "sha256": sha256_bytes(images[key]).hexdigest(),
            }
            for key, path in comparisons.items()
        },
        "registeredSceneNotice": (
            "Mechanical 800x374 composite from the preserved background, registered "
            "town layers and the same v2 VillageHall sprite; not a game screenshot. "
            "Later townHall/cityHall/capitol stages are excluded."
        ),
        "runtimeInstallation": False,
        "userVisualAcceptance": False,
    }


def build_artifacts() -> dict[Path, bytes]:
    baseline, exported = make_export()
    native_comparison = make_comparison(baseline, exported)
    roi_comparison = make_roi_comparison(baseline, exported)

    hall = open_rgba(HALL_V2_PATH)
    native_scene, scene_inputs = make_registered_scene(baseline, hall)
    proposed_scene, proposed_inputs = make_registered_scene(exported, hall)
    if scene_inputs != proposed_inputs:
        raise ValueError("Registered scene layers differ between baseline and proposed backgrounds")
    scene_comparison = make_comparison(native_scene, proposed_scene)

    image_payloads = {
        "export": png_bytes(exported),
        "native": png_bytes(native_comparison),
        "roiNearest4x": png_bytes(roi_comparison),
        "registeredVillageHallStage": png_bytes(scene_comparison),
    }
    manifest = input_manifest(scene_inputs, image_payloads)
    manifest_bytes = (json.dumps(manifest, indent="\t", ensure_ascii=False) + "\n").encode("utf-8")
    return {
        EXPORT_PATH: image_payloads["export"],
        NATIVE_COMPARISON_PATH: image_payloads["native"],
        ROI_COMPARISON_PATH: image_payloads["roiNearest4x"],
        SCENE_COMPARISON_PATH: image_payloads["registeredVillageHallStage"],
        MANIFEST_PATH: manifest_bytes,
    }


def process(check_only: bool) -> None:
    require_private_outputs()
    artifacts = build_artifacts()
    for path, expected in artifacts.items():
        if check_only:
            if not path.is_file() or path.read_bytes() != expected:
                raise RuntimeError(f"Missing or stale background v2 artifact: {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(expected)
        print(f"{'OK' if check_only else 'Wrote'} {path.relative_to(ROOT)} sha256={sha256_bytes(expected).hexdigest()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-root", required=True, type=Path,
                        help="private project-shaped input/output workspace outside the checkout")
    parser.add_argument("--check", action="store_true", help="verify exports/manifest without rewriting")
    args = parser.parse_args()
    try:
        configure_private_root(args.private_root)
        process(args.check)
    except (FileNotFoundError, OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f"Academy background v2 export failed: {error}\n")
    print("PASS: Academy background v2 export is pinned and preserves pixels outside its reviewed ROI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
