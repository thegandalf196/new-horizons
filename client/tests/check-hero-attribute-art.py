#!/usr/bin/env python3
"""Focused source and resource guard for New Horizons hero-attribute art."""

from pathlib import Path
import json
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "Mods/new-horizons/Images"
HERO = (ROOT / "client/windows/CHeroWindow.cpp").read_text(encoding="utf-8")
GROWTH = (ROOT / "client/windows/HeroGrowthWindow.cpp").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def verify_image(stem: str, size: int) -> None:
    descriptor_path = IMAGES / f"{stem}.json"
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
    assert descriptor["images"] == [{"group": 0, "frame": 0, "file": f"{stem}.png"}], stem
    with Image.open(IMAGES / f"{stem}.png") as image:
        assert image.size == (size, size), (stem, image.size)


def main() -> None:
    require(HERO, '"NH_hero_leadership_24"', "legacy hero Leadership binding")
    require(HERO, "Rect(410, 404, 24, 24)", "legacy Leadership display dimensions")
    require(HERO, 'fieldName == "Leadership" ? "NH_hero_leadership_44"', "hero-pane Leadership binding")
    require(HERO, 'fieldName == "Movement" ? "NH_hero_movement_44"', "hero-pane Movement binding")
    require(HERO, 'Rect(152, 88, 140, 44)', "Leadership tooltip area dimensions")
    require(HERO, 'Rect(152, 132, 140, 44)', "Movement tooltip area dimensions")
    require(HERO, '"Movement points remaining / current limit: "', "Movement tooltip readout")
    require(GROWTH, '"NH_hero_movement_painted_32"', "hero-development Movement binding")
    if '"NH_hero_movement_32"' in HERO or '"NH_hero_movement_32"' in GROWTH:
        raise AssertionError("hero attribute surfaces must use the replacement Movement art")
    if '"NH_capability_leadership"' in HERO or '"NH_capability_leadership_32"' in HERO:
        raise AssertionError("hero Leadership art must remain separate from creature Leadership Cost art")

    for stem, size in {
        "NH_hero_leadership_24": 24,
        "NH_hero_leadership_44": 44,
        "NH_hero_movement_44": 44,
        "NH_hero_movement_painted_32": 32,
    }.items():
        verify_image(stem, size)

    print("Hero attribute art bindings and resource dimensions passed")


if __name__ == "__main__":
    main()
