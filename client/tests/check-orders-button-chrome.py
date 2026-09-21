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
              'classic->getImage(state)',
              'canvas.draw(gauntlet, Point(0, 0))',
              'Rect(0, 0, width, border)', 'Rect(0, height - border, width, border)',
              'Rect(0, border, border, height - 2 * border)',
              'Rect(width - border, border, border, height - 2 * border)'):
    assert token in button, token
assert button.index('canvas.draw(gauntlet') < button.index('canvas.draw(frame'), "subject may overwrite chrome"
window = (ROOT / "client/battle/BattleWindow.cpp").read_text()
assert 'Point(595, 560), AnimationPath::builtin("NH_orders_gauntlet_framed")' in window
assert 595 + 48 <= 646, "Orders overlaps Cast slot"
hashes = {
    "normal": "c10f810822c36ef37c703816362613ac1f0708ee6351f1571c3031b9aab44292",
    "pressed": "4b95a39f31da441b9137c389951323bd236bd4c2957b761b6e7b6b414136c1b5",
    "disabled": "de8bafd6d499406a306e37f2a91d46b45bad3fc4b6a7bdd6bee32d9148259da6",
    "highlighted": "00499102f5d757964d6d130fcc24a669f3b9c18e4450d8779dc41ec14c671d58",
}
for state, digest in hashes.items():
    path = ROOT / "Mods/new-horizons/Images" / f"NH_orders_gauntlet_{state}.png"
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, "commissioned art changed"
    with Image.open(path) as image:
        assert image.size == (48, 36) and image.mode == "RGBA"
        assert image.getchannel("A").getextrema()[0] <= 1, "subject alpha was lost"
print("PASS: four preserved gauntlet states, classic runtime chrome, bounded battle-bar geometry")
