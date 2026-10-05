#!/usr/bin/env python3
"""Guard Academy runtime-composed icons and clean authored fallbacks."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/render/AssetGenerator.h").read_text(encoding="utf-8")
IMAGES = ROOT / "Mods/new-horizons/Images"
SDL2 = (ROOT / "clientsdl2/render/RenderHandler.cpp").read_text(encoding="utf-8")
SDL3 = (ROOT / "clientsdl3/render/RenderHandler.cpp").read_text(encoding="utf-8")


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
    normal = generated.replace("_built.png", "_normal.png")
    assert (IMAGES / generated).read_bytes() == (IMAGES / normal).read_bytes(), generated

assert "normalFrameCanvas.getPixel(pixel)" in GENERATOR
assert "builtFrameCanvas.getPixel(pixel)" in GENERATOR
assert "canvas.drawPoint(pixel, built)" in GENERATOR
assert "locator.originalDefFrame = true;" in GENERATOR
assert "createAcademyTownIconBuiltToday" in HEADER
assert 'authoredPath.addPrefix("SPRITES/")' in GENERATOR
assert 'authoredPath.addPrefix("DATA/")' in GENERATOR
assert "if(!hasAuthoredImage || !resources->existsResource(originalDef))" in GENERATOR
assert "bool preferGeneratedImage(const ImagePath & image) const;" in HEADER
preference = GENERATOR.split("bool AssetGenerator::preferGeneratedImage", 1)[1].split("std::map<ImagePath", 1)[0]
for generated in ICON_ROUTES:
    assert f'ImagePath::builtin("{generated}")' in preference, generated

for backend in (SDL2, SDL3):
    base_load = backend.split("RenderHandler::loadImageFromFileUncached", 1)[1].split("RenderHandler::storeCachedImage", 1)[0]
    scaled_load = backend.split("RenderHandler::loadScaledImage", 1)[1].split("RenderHandler::loadImage(", 1)[0]
    assert "assetGenerator->preferGeneratedImage(imagePath)" in base_load
    assert base_load.index("assetGenerator->generateImage(imagePath)") < base_load.index("existsResource(imagePathSprites)")
    assert "locator.scalingFactor == 1 && assetGenerator->preferGeneratedImage(imagePath)" in scaled_load
    assert "!preferGeneratedImage" in scaled_load
    assert "if(locator.originalDefFrame)\n\t\treturn nullptr;" in scaled_load

for backend_path in (
    ROOT / "clientsdl2/render/ScalableImage.cpp",
    ROOT / "clientsdl3/render/ScalableImage.cpp",
):
    backend = backend_path.read_text(encoding="utf-8")
    load_or_generate = backend.split("ScalableImageShared::loadOrGenerateImage", 1)[1].split("ScalableImageShared::", 1)[0]
    assert "loadingLocator.originalDefFrame = locator.originalDefFrame;" in load_or_generate

assert "bool originalDefFrame = false;" in (ROOT / "client/render/ImageLocator.h").read_text(encoding="utf-8")
assert "if(originalDefFrame != other.originalDefFrame)" in (ROOT / "client/render/ImageLocator.cpp").read_text(encoding="utf-8")

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

print("Academy built-town icons prefer runtime composition and retain clean authored fallbacks.")
