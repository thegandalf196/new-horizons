#!/usr/bin/env python3
"""Export the Withering Touch icon into the four active-perk button states."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageEnhance


STEM = "NH_withering_touch"
STATES = ("normal", "pressed", "disabled", "highlighted")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    source_root = Path(__file__).resolve().parent
    repository = source_root.parents[3]
    image_root = repository / "Mods/new-horizons/Images"
    normal_path = source_root / "exports/withering-touch/withering-touch-44.png"
    normal = Image.open(normal_path).convert("RGBA")
    if normal.size != (44, 44):
        raise ValueError(f"Expected 44x44 normal art, received {normal.size}.")

    states = {
        "normal": normal,
        "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
        "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
        "highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
    }
    outputs: dict[str, str] = {}
    frames = []
    roles = {
        "normal": "Default active-perk button state; unchanged 44x44 export.",
        "pressed": "Pressed button state; brightness 0.82.",
        "disabled": "Disabled button state; saturation 0.15 then brightness 0.65.",
        "highlighted": "Highlighted button state; brightness 1.13.",
    }

    for frame, state in enumerate(STATES):
        path = image_root / f"{STEM}_{state}.png"
        if path.exists():
            raise FileExistsError(f"Refusing to replace existing runtime art: {path}")
        states[state].save(path, format="PNG")
        outputs[path.name] = sha256(path)
        frames.append({"group": 0, "frame": frame, "file": path.name})

    descriptor = image_root / f"{STEM}.json"
    if descriptor.exists():
        raise FileExistsError(f"Refusing to replace existing binding descriptor: {descriptor}")
    descriptor.write_text(json.dumps({"images": frames}, indent=2) + "\n", encoding="utf-8")
    outputs[descriptor.name] = sha256(descriptor)

    manifest = {
        "schema_version": 1,
        "status": "provisional; not final user-approved artwork",
        "asset_id": "new-horizons:shadowMagic.witheringTouch",
        "stem": STEM,
        "source_normal": "exports/withering-touch/withering-touch-44.png",
        "state_roles": roles,
        "descriptor": f"Mods/new-horizons/Images/{descriptor.name}",
        "files": outputs,
    }
    (source_root / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
