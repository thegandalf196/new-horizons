#!/usr/bin/env python3
"""Export offline CPRSMALL comparison derivatives of selected Magi portraits.

No painting, palette changes, baked-backdrop scaling, live binding edits or
source mutation. Both contain variants retain the complete visible foreground.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw

CHECKOUT = Path(__file__).resolve().parents[1]
FORMS = ("magi", "archmagi")
PREFIX = Path("Mods/new-horizons/Images/magi-vcmi-complete/icons")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def portrait_small(foreground: Image.Image, resampling=Image.Resampling.NEAREST) -> Image.Image:
    """Remove transparent padding only, contain the whole subject inside 28x28."""
    if foreground.size != (58, 64):
        raise ValueError("approved foreground must be 58x64")
    foreground = foreground.convert("RGBA")
    bounds = foreground.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("approved foreground is empty")
    subject = foreground.crop(bounds)
    scale = min(28 / subject.width, 28 / subject.height)
    size = (max(1, round(subject.width * scale)), max(1, round(subject.height * scale)))
    fitted = subject.resize(size, resampling)
    result = Image.new("RGBA", (32, 32))
    # Paste without a mask: preserve fitted alpha rather than multiplying it.
    result.paste(fitted, ((32 - size[0]) // 2, (32 - size[1]) // 2))
    return result



def authoring_output(path: Path) -> Path:
    """Keep local comparison exports outside Git and reject linked destinations."""
    requested = path.expanduser().absolute()
    resolved = requested.resolve()
    if (requested.is_relative_to(CHECKOUT) or resolved.is_relative_to(CHECKOUT)
            or CHECKOUT.is_relative_to(resolved)):
        raise ValueError("authoring output must be outside and not overlap the checkout")
    if any(parent.is_symlink() for parent in (requested, *requested.parents)):
        raise ValueError("authoring output must not traverse symlinks")
    if any((parent / ".git").exists() for parent in (resolved, *resolved.parents)):
        raise ValueError("authoring output must be outside Git working trees")
    return resolved


def export(snapshot: Path, output: Path, review: Path | None = None) -> dict:
    snapshot, output = snapshot.expanduser().resolve(), authoring_output(output)
    review = authoring_output(review) if review else None
    if output.exists() or (review and review.exists()):
        raise ValueError("choose unused output and review directories")
    if output.is_relative_to(snapshot) or (review and review.resolve().is_relative_to(snapshot)):
        raise ValueError("output must not modify the snapshot")
    if review and (review.is_relative_to(output) or output.is_relative_to(review)):
        raise ValueError("review and export directories must be separate")
    inputs = [snapshot / PREFIX / f"{form}-{suffix}.png" for form in FORMS
              for suffix in ("portrait-58x64", "portrait-foreground-58x64", "small-32")]
    hashes = {str(path): digest(path) for path in inputs}
    loaded = {}
    records = {}
    for form in FORMS:
        large = Image.open(snapshot / PREFIX / f"{form}-portrait-58x64.png").convert("RGBA")
        foreground = Image.open(snapshot / PREFIX / f"{form}-portrait-foreground-58x64.png").convert("RGBA")
        current = Image.open(snapshot / PREFIX / f"{form}-small-32.png").convert("RGBA")
        if large.size != (58, 64) or current.size != (32, 32):
            raise ValueError("unexpected delivered portrait dimensions")
        loaded[form] = {"large": large, "current": current}
        for variant, filter_ in (("nearest", Image.Resampling.NEAREST), ("lanczos", Image.Resampling.LANCZOS)):
            image = portrait_small(foreground, filter_)
            loaded[form][variant] = image
            relative = Path(f"overlay-{variant}") / PREFIX / f"{form}-small-32.png"
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            image.save(destination)
            records[str(relative)] = {
                "sha256": digest(destination), "size": list(image.size),
                "alphaBounds": image.getchannel("A").getbbox(),
                "solidAlphaBounds": image.getchannel("A").point(lambda n: 255 if n >= 128 else 0).getbbox(),
                "sourceAlphaBounds": foreground.getchannel("A").getbbox(),
                "visibleSourcePixelsDiscardedBeforeResize": 0,
                "source": str(snapshot / PREFIX / f"{form}-portrait-foreground-58x64.png"),
                "resourceBindingChanged": False,
            }
    if hashes != {str(path): digest(path) for path in inputs}:
        raise AssertionError("source changed during export")
    sheet = Image.new("RGB", (336, 168), (42, 38, 32))
    draw = ImageDraw.Draw(sheet)
    for row, form in enumerate(FORMS):
        for column, (role, label) in enumerate((("large", "58x64 supplied"), ("current", "32 current"),
                                               ("nearest", "32 portrait NN"), ("lanczos", "32 portrait LZ"))):
            x, y = column * 84 + 3, row * 84 + 16
            draw.text((x, y - 13), label, fill=(235, 225, 200))
            image = loaded[form][role]
            sheet.paste(image, (x, y), image)
    sheet.save(output / "comparison-native.png")
    sheet.resize((1344, 672), Image.Resampling.NEAREST).save(output / "comparison-4x.png")
    manifest = {
        "method": "complete approved portrait foreground; transparent-padding crop; isotropic contain28x28; 2px safe inset",
        "selection": "unapproved nearest and lanczos comparison alternatives; no live installation",
        "inputs": hashes, "inputsUnchanged": True, "largePortraitsUnchanged": True,
        "bakedBackdropResized": False, "paintedPixels": False,
        "acceptance": "native exports only; all four gameplay consumers require isolated rendered verification",
        "outputs": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if review:
        # Review versions are never overwritten. No purchaser-derived PNG enters Git.
        shutil.copytree(output, review)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True, help="Read-only staged Mage portrait input root")
    parser.add_argument("--output", type=Path, required=True, help="Fresh external authoring output; never installed")
    parser.add_argument("--review", type=Path, help="Optional fresh external review copy")
    args = parser.parse_args()
    manifest = export(args.snapshot, args.output, args.review)
    print(json.dumps({"outputs": len(manifest["outputs"]), "inputsUnchanged": True,
                      "output": str(args.output), "review": str(args.review)}))


if __name__ == "__main__":
    main()
