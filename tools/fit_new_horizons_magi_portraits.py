#!/usr/bin/env python3
"""Export private, mechanical Magi portrait candidates from approved artwork.

No painting, color substitution, runtime binding edits or input mutation occurs.
The standing alternative contains the complete approved HOLDING silhouette;
it is comparison evidence, not a replacement selected for gameplay.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


FORMS = ("magi", "archmagi")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def contain(image: Image.Image, size: tuple[int, int], inset: int) -> Image.Image:
    """Fit the complete canvas without cropping; retain native raster edges."""
    available = (size[0] - 2 * inset, size[1] - 2 * inset)
    if min(available) <= 0:
        raise ValueError("inset leaves no room for the subject")
    scale = min(available[0] / image.width, available[1] / image.height)
    fitted_size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    fitted = image.convert("RGBA").resize(fitted_size, Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", size)
    canvas.alpha_composite(fitted, ((size[0] - fitted.width) // 2, (size[1] - fitted.height) // 2))
    return canvas


def export_candidates(source: Path, backdrop_path: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError(f"output already exists: {output}")
    input_paths = [backdrop_path]
    for form in FORMS:
        input_paths.extend(source / "icons" / f"{form}-{name}.png" for name in
                           ("portrait-58x64", "portrait-foreground-58x64", "small-32"))
        input_paths.append(source / "battle" / form / "idle" / "000.png")
    before = {str(path.resolve()): sha256(path) for path in input_paths}
    backdrop = Image.open(backdrop_path).convert("RGBA")
    if backdrop.size != (58, 64):
        raise ValueError("Academy backdrop must be exactly 58x64")

    candidates: dict[str, Image.Image] = {}
    records = {}
    for form in FORMS:
        original_large = Image.open(source / "icons" / f"{form}-portrait-58x64.png").convert("RGBA")
        foreground = Image.open(source / "icons" / f"{form}-portrait-foreground-58x64.png").convert("RGBA")
        original_small = Image.open(source / "icons" / f"{form}-small-32.png").convert("RGBA")
        if original_large.size != (58, 64) or foreground.size != (58, 64) or original_small.size != (32, 32):
            raise ValueError(f"unexpected approved icon size: {form}")
        standing = Image.open(source / "battle" / form / "idle" / "000.png").convert("RGBA")
        standing_bounds = standing.getchannel("A").getbbox()
        if standing_bounds is None:
            raise ValueError(f"empty HOLDING source: {form}")
        # Transparent battle-canvas padding is removed; every visible pixel,
        # including authored shadows and complete head/staff, remains covered.
        standing_subject = standing.crop(standing_bounds)
        authored_large_fg = contain(foreground, (58, 64), 3)
        standing_large_fg = contain(standing_subject, (58, 64), 3)
        variants = {
            "approved-large": original_large,
            "approved-small": original_small,
            "authored-large-foreground": authored_large_fg,
            "authored-large": Image.alpha_composite(backdrop, authored_large_fg),
            "authored-small": contain(foreground, (32, 32), 2),
            "original-small-inset": contain(original_small, (32, 32), 2),
            "standing-large-foreground": standing_large_fg,
            "standing-large": Image.alpha_composite(backdrop, standing_large_fg),
            "standing-small": contain(standing_subject, (32, 32), 2),
        }
        for variant, image in variants.items():
            name = f"{form}-{variant}.png"
            candidates[name] = image
            records[name] = {"size": list(image.size), "alphaBounds": image.getchannel("A").getbbox()}
        for prefix, fg in (("authored", authored_large_fg), ("standing", standing_large_fg)):
            composite = variants[f"{prefix}-large"]
            unchanged = all(composite.getpixel((x, y)) == backdrop.getpixel((x, y))
                            for y in range(64) for x in range(58) if fg.getpixel((x, y))[3] == 0)
            if not unchanged:
                raise AssertionError("backdrop changed outside subject")
            records[f"{form}-{prefix}-large.png"]["backdropOutsideForegroundUnchanged"] = True
        records[f"{form}-standing-large.png"]["sourceAlphaBounds"] = standing_bounds
        records[f"{form}-standing-large.png"]["sourceVisiblePixelsDiscardedBeforeFit"] = 0

    after = {str(path.resolve()): sha256(path) for path in input_paths}
    if after != before:
        raise AssertionError("approved inputs changed during export")
    output.mkdir(parents=True)
    for name, image in candidates.items():
        image.save(output / name)
        records[name]["sha256"] = sha256(output / name)

    # Native comparison is intentionally unscaled; the second sheet is a
    # nearest-neighbour inspection aid, never runtime/UI acceptance evidence.
    sheet = Image.new("RGB", (590, 170), (42, 42, 42))
    draw = ImageDraw.Draw(sheet)
    roles = (("approved-large", "large supplied"), ("authored-large", "large inset"),
             ("standing-large", "standing contain"), ("approved-small", "small supplied"),
             ("authored-small", "small foreground"), ("original-small-inset", "small inset"),
             ("standing-small", "standing small"))
    for row, form in enumerate(FORMS):
        for column, (variant, label) in enumerate(roles):
            image = candidates[f"{form}-{variant}.png"]
            x, y = column * 84 + 3, row * 84 + 16
            draw.text((x, y - 13), label, fill=(235, 225, 200))
            sheet.paste(image, (x, y), image)
    sheet.save(output / "comparison-native.png")
    sheet.resize((2360, 680), Image.Resampling.NEAREST).save(output / "comparison-4x.png")
    manifest = {
        "method": "mechanical nearest-neighbour contain; no painted pixels or binding changes",
        "acceptance": "private candidate comparison only; four UI roles remain unverified",
        "inputs": before,
        "inputsUnchanged": True,
        "smallInset": 2,
        "largeInset": 3,
        "outputs": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def stage_selected(candidates: Path, output: Path) -> dict:
    """Stage selected close portraits under their existing resource filenames."""
    candidates = candidates.resolve()
    output = output.resolve()
    if output.is_relative_to(candidates) or candidates.is_relative_to(output):
        raise ValueError("overlay must be separate from candidate inputs")
    manifest_path = candidates / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    input_hashes = manifest["inputs"]
    for path, expected in input_hashes.items():
        if Path(path).resolve().is_relative_to(output):
            raise ValueError("overlay must not contain original inputs")
        if sha256(Path(path)) != expected:
            raise ValueError(f"approved input changed: {path}")
    backdrop_paths = [Path(path) for path in input_hashes
                      if Path(path).name == "NH_academy_creature_portrait_backdrop.png"]
    if len(backdrop_paths) != 1:
        raise ValueError("candidate manifest requires one original Academy backdrop")
    backdrop = Image.open(backdrop_paths[0]).convert("RGBA")
    payloads = {}
    records = {}
    selected_inputs = {str(manifest_path): sha256(manifest_path)}
    prefix = Path("Mods/new-horizons/Images/magi-vcmi-complete/icons")
    for form in FORMS:
        variants = (("authored-large", "portrait-58x64"),
                    ("authored-large-foreground", "portrait-foreground-58x64"),
                    ("original-small-inset", "small-32"))
        loaded = {}
        for variant, destination in variants:
            name = f"{form}-{variant}.png"
            path = candidates / name
            if sha256(path) != manifest["outputs"][name]["sha256"]:
                raise ValueError(f"candidate changed: {name}")
            loaded[variant] = Image.open(path).convert("RGBA")
            selected_inputs[str(path)] = sha256(path)
            relative = prefix / f"{form}-{destination}.png"
            payloads[relative] = path.read_bytes()
            records[str(relative)] = {"source": str(path), "sha256": sha256(path),
                                      "size": list(loaded[variant].size),
                                      "alphaBounds": loaded[variant].getchannel("A").getbbox()}
        # Recompute the approved mechanical operations to reject a painted or
        # misleading candidate even if its local manifest hash was updated.
        originals = {}
        for suffix in ("portrait-foreground-58x64", "small-32"):
            paths = [Path(path) for path in input_hashes if Path(path).name == f"{form}-{suffix}.png"]
            if len(paths) != 1:
                raise ValueError(f"candidate manifest requires one original {form}-{suffix}")
            originals[suffix] = Image.open(paths[0]).convert("RGBA")
        expected_fg = contain(originals["portrait-foreground-58x64"], (58, 64), 3)
        expected_small = contain(originals["small-32"], (32, 32), 2)
        expected_large = Image.alpha_composite(backdrop, expected_fg)
        for variant, expected in (("authored-large", expected_large),
                                  ("authored-large-foreground", expected_fg),
                                  ("original-small-inset", expected_small)):
            actual = loaded[variant]
            if actual.size != expected.size or actual.tobytes() != expected.tobytes():
                raise ValueError(f"candidate is not the selected mechanical composition: {form}-{variant}")
    receipt = {
        "status": "Selected crop-safe candidate; all four reported UI roles remain unverified",
        "selection": "authored-large inset58x64 and original-small inset32; no standing replacement",
        "resourceBindingsChanged": False, "originalInputsUnchanged": True,
        "backdropPreserved": True, "inputs": input_hashes,
        "candidateInputs": selected_inputs, "outputs": records,
    }
    payloads[Path("manifest.json")] = (json.dumps(receipt, indent=2) + "\n").encode()
    if output.exists():
        existing = {path.relative_to(output) for path in output.rglob("*") if path.is_file()}
        if existing != set(payloads) or any((output / path).read_bytes() != data for path, data in payloads.items()):
            raise ValueError("existing overlay differs; choose a new directory")
        return receipt
    output.mkdir(parents=True)
    for relative, data in payloads.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-images", type=Path)
    parser.add_argument("--backdrop", type=Path)
    parser.add_argument("--stage-from", type=Path, help="stage selected variants from an existing candidate directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.stage_from:
        if args.source_images or args.backdrop:
            parser.error("--stage-from cannot be combined with export inputs")
        result = stage_selected(args.stage_from, args.output)
        print(json.dumps({"output": str(args.output), "candidateCount": len(result["outputs"]),
                          "originalInputsUnchanged": result["originalInputsUnchanged"]}))
        return
    if not args.source_images or not args.backdrop:
        parser.error("export requires --source-images and --backdrop")
    result = export_candidates(args.source_images, args.backdrop, args.output)
    print(json.dumps({"output": str(args.output), "inputsUnchanged": result["inputsUnchanged"],
                      "candidateCount": len(result["outputs"])}))


if __name__ == "__main__":
    main()
