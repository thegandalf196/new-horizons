#!/usr/bin/env python3
"""Audit symbol-only New Horizons spell icon exports and live bindings."""

import hashlib
import json
from pathlib import Path
import re
import unittest

if __package__:
    from .nhart_test_resources import ArtPath
else:
    from nhart_test_resources import ArtPath


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "assets/new-horizons/art-source/spell-symbols-v2/manifest.json"
SPELL_CONFIG = ROOT / "Mods/new-horizons/Content/config/spells/newHorizons.json"
IMAGE_DIR = ArtPath()
STRING = r'"(?:\\.|[^"\\])*"'
ROLE_SIZES = {"44": (44, 44), "32": (32, 32), "30": (30, 30)}


def parse_jsonc(text):
    """Parse the spell config's comments/trailing commas without touching strings."""
    text = re.sub(
        STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
        lambda match: match[0] if match[0].startswith('"') else "",
        text,
    )
    text = re.sub(
        STRING + r'|,(?=\s*[}\]])',
        lambda match: match[0] if match[0].startswith('"') else "",
        text,
    )
    return json.loads(text)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class NewHorizonsSpellSymbolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cls.spells = parse_jsonc(SPELL_CONFIG.read_text(encoding="utf-8"))
        cls.families = cls.manifest["families"]

    def test_heavenly_gale_provenance_keeps_export_identity_without_session_records(self):
        base = ROOT / 'assets/new-horizons/art-source/heavenly-gale-v1'
        recipe = json.loads((base / 'generation.json').read_text())
        historical = json.loads((base / 'heavenly-gale-manifest.json').read_text())
        self.assertEqual(recipe['status'], 'provisional')
        self.assertEqual(recipe['final_master']['sha256'], historical['source']['sha256'])
        self.assertEqual(recipe['final_master']['dimensions_pixels'], historical['source']['dimensions'])
        self.assertEqual(recipe['final_master']['color_mode'], 'RGB')
        self.assertEqual([row['operation'] for row in recipe['generation_history']],
                         ['generate', 'edit', 'edit', 'edit'])
        private_fields = {'imagegen_output_id', 'generated_image_reference', 'input_generated_image_reference'}
        for row in recipe['generation_history']:
            self.assertFalse(private_fields.intersection(row), 'Public provenance must omit session identifiers')
            self.assertTrue(row['prompt_file'].endswith('.txt'))
            self.assertFalse((base / row['prompt_file']).exists(), 'Raw requests belong in the external archive')
        originals = {tuple(row['dimensions']): row['sha256'] for row in historical['outputs'] if row['kind'] == 'icon'}
        self.assertEqual(set(originals), {(44, 44), (32, 32), (30, 30)})
        self.assertEqual({tuple(row['dimensions_pixels']): row['sha256'] for row in recipe['export']['runtime_exports']}, originals)
        family = next(row for row in self.families if row['spellKey'] == 'heavenlyGale')
        target, fragment = family['symbol']['prompt'].split('#')
        self.assertEqual(fragment, 'exact-edit-prompt')
        contract = (ROOT / target).read_text()
        for fact in ('## Exact edit prompt', 'GPL-2.0-or-later', 'RGBA', 'external authoring', 'LANCZOS'):
            self.assertTrue(fact in contract, 'Public symbol contract must retain required provenance and native facts')
        self.assertFalse(any(label in contract for label in ('Use case:', 'Primary request:', 'Constraints:')),
                         'Public contract must not embed the raw edit request')

    def test_manifest_covers_inventory_and_preserves_existing_bindings(self):
        self.assertEqual(self.manifest["schemaVersion"], 1)
        self.assertEqual(len(self.families), 26)
        keys = [family["spellKey"] for family in self.families]
        self.assertEqual(len(keys), len(set(keys)))

        counts = {
            "families": len(self.families),
            "converted": sum(family["status"] == "converted" for family in self.families),
            "pending": sum(family["status"] == "pending" for family in self.families),
        }
        self.assertEqual(counts, self.manifest["counts"])
        self.assertEqual(counts["converted"] + counts["pending"], 26)

        for family in self.families:
            with self.subTest(spell=family["spellKey"]):
                self.assertIn(family["status"], {"converted", "pending"})
                self.assertIn(family["spellKey"], self.spells)
                self.assertEqual(
                    self.spells[family["spellKey"]]["graphics"],
                    family["liveBindings"],
                    "inventory must describe the existing image-role bindings, not rewrite them",
                )
                if family["status"] == "converted":
                    for role, binding in family["liveBindings"].items():
                        # A few legacy spell-trait aliases intentionally use a DEF
                        # reference rather than a project PNG. Every PNG binding
                        # must name the matching converted export at that size.
                        if ".def:" in binding.lower():
                            continue
                        size_match = re.search(r"_(44|32|30)\.png$", binding)
                        self.assertIsNotNone(size_match, f"unrecognized {role} image binding: {binding}")
                        size = size_match.group(1)
                        export = family["symbol"]["exports"][size]
                        self.assertEqual(Path(export["live"]).name, binding)
                        self.assertEqual(Path(export["live"]), Path("Mods/new-horizons/Images") / binding)
                self.assertTrue(family["reference"]["master"].endswith(".png"))
                for reference in family["reference"]["exports"].values():
                    self.assertTrue(reference.endswith(".png"), reference)
                self.assertEqual(
                    family["status"] == "converted", "symbol" in family,
                    "pending rows stay explicit until their approved cutout is integrated",
                )

    def test_converted_exports_are_transparent_native_symbols_and_exact_live_copies(self):
        converted = [family for family in self.families if family["status"] == "converted"]
        self.assertGreater(len(converted), 0)

        for family in converted:
            symbol = family["symbol"]
            with self.subTest(spell=family["spellKey"], role="master"):
                self.assertTrue(symbol["master"].endswith(".png"))
                self.assertEqual(len(symbol["masterSha256"]), 64)
                self.assertTrue((ROOT / symbol["prompt"].split("#", 1)[0]).is_file())

            self.assertEqual(set(symbol["exports"]), set(ROLE_SIZES))
            for size, export in symbol["exports"].items():
                live = IMAGE_DIR / Path(export["live"]).name
                with self.subTest(spell=family["spellKey"], size=size):
                    self.assertTrue(live.is_file())
                    self.assertEqual(sha256(live), export["liveSha256"])
                    self.assertEqual(export["sourceSha256"], export["liveSha256"])
                    self.assertEqual(len(export["referenceSha256"]), 64)

                    with live.open_image() as live_image:
                        self.assertEqual(live_image.format, "PNG")
                        self.assertEqual(live_image.size, ROLE_SIZES[size])
                        self.assertEqual(live_image.mode, "RGBA")

                        alpha = live_image.getchannel("A")
                        self.assertEqual(alpha.getextrema(), (0, 255))
                        self.assertIsNotNone(alpha.getbbox(), "symbol image must not be empty")
                        alpha_values = {value for _, value in alpha.getcolors(maxcolors=256)}
                        self.assertTrue(
                            any(0 < value < 255 for value in alpha_values),
                            "cutout should retain antialiased partial alpha",
                        )
                        for corner in ((0, 0), (live_image.width - 1, 0),
                                       (0, live_image.height - 1),
                                       (live_image.width - 1, live_image.height - 1)):
                            self.assertLessEqual(
                                alpha.getpixel(corner), 24,
                                f"painted background reaches icon corner {corner}",
                            )


if __name__ == "__main__":
    unittest.main()
