#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Preserve the commissioned subject and require runtime classic button chrome."""
import hashlib
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
generator = (ROOT / "client/render/AssetGenerator.cpp").read_text()
button = generator.split("AssetGenerator::AnimationLayoutMap AssetGenerator::createNewHorizonsOrdersButton()", 1)[1].split(
    "AssetGenerator::AnimationLayoutMap AssetGenerator::createGSPButtonClear()", 1)[0]
assert 'SPRITES/NH_orders_gauntlet_framed' in generator
for token in ('"normal", "pressed", "disabled", "highlighted"',
              'constexpr int width = 48', 'constexpr int height = 36', 'constexpr int border = 3',
              'createDialogBackground(Point(width, height))', 'AnimationPath::builtin("ICM005")',
              'classic->getImage(state)', 'gauntlet->scaleTo(Point(40, 30)',
              'canvas.draw(gauntlet, Point(4, 3))',
              'Rect(0, 0, width, border)', 'Rect(0, height - border, width, border)',
              'Rect(0, border, border, height - 2 * border)',
              'Rect(width - border, border, border, height - 2 * border)'):
    assert token in button, token
assert button.index('canvas.draw(gauntlet') < button.index('canvas.draw(frame'), "subject may overwrite chrome"
assert 40 * 36 == 30 * 48, "gauntlet aspect ratio changed"
assert 4 >= 3 and 4 + 40 <= 48 - 3 and 3 + 30 <= 36 - 3
window = (ROOT / "client/battle/BattleWindow.cpp").read_text()
assert 'Point(595, 560), AnimationPath::builtin("NH_orders_gauntlet_framed")' in window
assert 595 + 48 <= 646, "Orders overlaps Cast slot"
hashes = {
    "normal": "29701781c0dfd49293165b5b6bfd1fb9d50634c03f771d010799e169e7c8b5a5",
    "pressed": "0d3131908ae346e3226f96dcfa3589f6b7ea5b1eced95b933563bd6f147fb076",
    "disabled": "bbc6e68e48f3c2e03573cc86e404a5fe15355f2e4a95c428b79350a0d81122ce",
    "highlighted": "648a96a7ec8a2a42bb914a9e3a79b602dd84ed4795cea34eacc3b1bbeac89e3c",
}
for state, digest in hashes.items():
    path = ROOT / "Mods/new-horizons/Images" / f"NH_orders_gauntlet_{state}.png"
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, "commissioned art changed"
    with Image.open(path) as image:
        assert image.size == (48, 36) and image.mode == "RGBA"
        assert image.getchannel("A").getextrema()[0] <= 1, "subject alpha was lost"
print("PASS: four preserved gauntlet states, classic runtime chrome, bounded battle-bar geometry")
