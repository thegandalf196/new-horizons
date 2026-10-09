#!/usr/bin/env python3
"""Export Soul Binder's provisional 44px perk states into runtime Images."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageEnhance


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging-root", type=Path, required=True, help="External authoring mirror; never installs shipping art")
    args = parser.parse_args()
    repository_path = Path(__file__).resolve().parents[4]
    staging = args.staging_root.resolve()
    if staging == repository_path or repository_path in staging.parents:
        parser.error("Authoring staging must be outside the checkout")
    source_root = staging / "assets/new-horizons/art-source/soul-binder-v1"
    repo_root = staging
    image_dir = repo_root / "Mods/new-horizons/Images"
    source = source_root / "exports/soul-binder/soul-binder-44.png"
    stem = "NH_perk_soul_binder"
    state_images = {
        "normal": None,
        "pressed": ("brightness", 0.82),
        "disabled": ("disabled", None),
        "highlighted": ("brightness", 1.13),
    }
    names = [f"{stem}_{state}.png" for state in state_images]
    descriptor_name = f"{stem}.json"
    targets = [image_dir / name for name in names]
    targets.append(image_dir / descriptor_name)
    if any(path.exists() for path in targets):
        existing = next(path for path in targets if path.exists())
        raise FileExistsError(existing)

    with Image.open(source) as opened:
        if opened.size != (44, 44):
            raise ValueError(f"Expected a 44x44 source icon, got {opened.size}")
        normal = opened.convert("RGBA")
    states = {
        "normal": normal,
        "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
        "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
        "highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
    }
    frames = []
    for frame, (state, image) in enumerate(states.items()):
        path = image_dir / f"{stem}_{state}.png"
        image.save(path, format="PNG")
        frames.append({"group": 0, "frame": frame, "file": path.name})

    descriptor_path = image_dir / descriptor_name
    descriptor_path.write_text(json.dumps({"images": frames}, indent=2) + "\n", encoding="utf-8")
    files = {str(Path("Mods/new-horizons/Images") / name): sha256(image_dir / name) for name in names}
    files[str(Path("Mods/new-horizons/Images") / descriptor_name)] = sha256(descriptor_path)
    runtime_manifest = {
        "method": "homm3-art high-resolution master, LANCZOS reduction, brightness/color-only runtime states",
        "status": "provisional; pending in-game rendering and user approval",
        "assets": [stem],
        "files": files,
    }
    (source_root / "runtime-manifest.json").write_text(json.dumps(runtime_manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
