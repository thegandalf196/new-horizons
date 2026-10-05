#!/usr/bin/env python3
"""Guard Academy assets that must be composed from external Heroes III art."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/render/AssetGenerator.h").read_text(encoding="utf-8")
IMAGES = ROOT / "Mods/new-horizons/Images"


ICON_ROUTES = {
    "NH_academy_fort_large_built.png": (
        '"NH_academy_fort_large_normal.png", AnimationPath::builtin("ITPT"), 4, 5'
    ),
    "NH_academy_fort_small_built.png": (
        '"NH_academy_fort_small_normal.png", AnimationPath::builtin("ITPA"), 6, 7'
    ),
    "NH_academy_village_large_built.png": (
        '"NH_academy_village_large_normal.png", AnimationPath::builtin("ITPT"), 22, 23'
    ),
    "NH_academy_village_small_built.png": (
        '"NH_academy_village_small_normal.png", AnimationPath::builtin("ITPA"), 24, 25'
    ),
}

for generated, frame_pair in ICON_ROUTES.items():
    assert f'ImagePath::builtin("{generated}")' in GENERATOR, generated
    assert frame_pair in GENERATOR, frame_pair

assert "normalFrameCanvas.getPixel(pixel)" in GENERATOR
assert "builtFrameCanvas.getPixel(pixel)" in GENERATOR
assert "canvas.drawPoint(pixel, built)" in GENERATOR
assert "createAcademyTownIconBuiltToday" in HEADER
assert 'authoredPath.addPrefix("SPRITES/")' in GENERATOR
assert 'authoredPath.addPrefix("DATA/")' in GENERATOR
assert "if(!hasAuthoredImage || !resources->existsResource(originalDef))" in GENERATOR

MAP_ROUTES = {
    "NH_ACADEMY_VILLAGE_BODY": "AVCTOWR0",
    "NH_ACADEMY_FORT_BODY": "AVCTOWX0",
    "NH_ACADEMY_CAPITOL_BODY": "AVCTOWZ0",
}
for body, original_def in MAP_ROUTES.items():
    assert f'addAcademyMapLayers("{body}", AnimationPath::builtin("{original_def}"))' in GENERATOR
    assert not (IMAGES / f"{body}-SHADOW.png").exists(), f"committed original shadow: {body}"
    assert not (IMAGES / f"{body}-OVERLAY.png").exists(), f"committed original overlay: {body}"

assert "ONLY_SHADOW_HIDE_FLAG_COLOR" in GENERATOR
assert "ONLY_FLAG_COLOR" in GENERATOR
assert 'image + "-SHADOW.png"' in GENERATOR
assert 'image + "-OVERLAY.png"' in GENERATOR
assert 'originalAnimation.addPrefix("SPRITES/")' in GENERATOR
assert "MAP_IMAGE_SIZE = 192" in GENERATOR
assert "createAcademyMapLayer" in HEADER
assert 'addDialogBackground("newHorizonsAdventureGuildBackground.png", Point(640, 440))' in GENERATOR

for generated in ICON_ROUTES:
    assert not (IMAGES / generated).exists(), f"committed original badge image: {generated}"

print("Academy built-town icons, map layers, and guild background routes are registered.")
