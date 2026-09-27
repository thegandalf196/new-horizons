#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate reviewed biography decisions and their selective runtime overlay."""

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_new_horizons_content import ROOT


FACTIONS = (
    "castle", "rampart", "tower", "inferno", "necropolis", "dungeon",
    "stronghold", "fortress", "conflux",
)


def load_module_generator():
    path = ROOT / "tools/update-new-horizons-module.py"
    spec = importlib.util.spec_from_file_location("new_horizons_module_generator", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NewHorizonsHeroBiographyTest(unittest.TestCase):
    def test_candidates_cover_exact_standard_faction_roster(self):
        candidates = {}
        indices = []
        for faction in FACTIONS:
            heroes = json.loads((ROOT / "config/heroes" / f"{faction}.json").read_text())
            biographies = json.loads((ROOT / "config/newHorizonsHeroBiographies" /
                                       f"{faction}.json").read_text())
            self.assertEqual(set(biographies), set(heroes))
            self.assertEqual(len(biographies), 16)
            indices.extend(hero["index"] for hero in heroes.values())
            candidates.update({f"{faction}:{hero}": text for hero, text in biographies.items()})
        self.assertEqual(sorted(indices), list(range(144)))
        self.assertEqual(len(candidates), 144)

    def test_candidates_are_bounded_and_not_formula_duplicates(self):
        biographies = {}
        for faction in FACTIONS:
            data = json.loads((ROOT / "config/newHorizonsHeroBiographies" /
                               f"{faction}.json").read_text())
            biographies.update({f"{faction}:{hero}": text for hero, text in data.items()
                                if text is not None})
        ngrams = {}
        openings = {}
        for hero, biography in biographies.items():
            words = re.findall(r"[a-z0-9']+", biography.casefold())
            self.assertGreaterEqual(len(words), 35, hero)
            self.assertLessEqual(len(words), 75, hero)
            self.assertNotIn("has spent years", biography.casefold(), hero)
            self.assertNotIn("is famous for", biography.casefold(), hero)
            self.assertNotIn("has always", biography.casefold(), hero)
            openings.setdefault(tuple(words[:3]), []).append(hero)
            for index in range(len(words) - 5):
                ngrams.setdefault(tuple(words[index:index + 6]), []).append(hero)
        self.assertFalse({opening: heroes for opening, heroes in openings.items() if len(heroes) > 1})
        self.assertFalse({ngram: heroes for ngram, heroes in ngrams.items() if len(set(heroes)) > 1})

    def test_only_editorially_accepted_biographies_are_runtime_bound(self):
        expected = {}
        for faction in FACTIONS:
            data = json.loads((ROOT / "config/newHorizonsHeroBiographies" /
                               f"{faction}.json").read_text())
            for hero, biography in data.items():
                if biography is not None:
                    expected[f"core:{hero}"] = {"texts": {"biography": biography}}

        patch_path = ROOT / "Mods/new-horizons/Content/config/heroes/biographies.json"
        patch = json.loads(patch_path.read_text())
        self.assertEqual(patch, expected)
        self.assertEqual(len(expected), 52)
        for hero, definition in patch.items():
            self.assertEqual(set(definition), {"texts"}, hero)
            self.assertEqual(set(definition["texts"]), {"biography"}, hero)

        module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())
        self.assertIn("config/heroes/biographies.json", module["heroes"])
        subprocess.run([sys.executable, str(ROOT / "tools/update-new-horizons-module.py"),
                        "--check"], check=True, capture_output=True)

    def test_custom_map_biography_still_precedes_type_biography(self):
        source = (ROOT / "lib/mapObjects/CGHeroInstance.cpp").read_text()
        function = source.split("std::string CGHeroInstance::getBiographyTextID() const", 1)[1]
        function = function.split("CGHeroInstance::ArtPlacementMap", 1)[0]
        self.assertLess(function.index("if (!biographyCustomTextId.empty())"),
                        function.index("if (getHeroTypeID().hasValue())"))

    def test_generator_rejects_wrong_rosters_and_invalid_decision_types(self):
        generator = load_module_generator()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            heroes = root / "config/heroes"
            decisions = root / "config/newHorizonsHeroBiographies"
            heroes.mkdir(parents=True)
            decisions.mkdir(parents=True)
            for faction in FACTIONS:
                hero = f"{faction}Hero"
                (heroes / f"{faction}.json").write_text(json.dumps({hero: {}}))
                (decisions / f"{faction}.json").write_text(json.dumps({hero: None}))

            castle = decisions / "castle.json"
            castle.write_text(json.dumps({"wrongHero": None}))
            with self.assertRaisesRegex(ValueError, "do not match"):
                generator.biography_patch(root)

            castle.write_text(json.dumps({"castleHero": 7}))
            with self.assertRaisesRegex(ValueError, "non-empty text or null"):
                generator.biography_patch(root)


if __name__ == "__main__":
    unittest.main()
