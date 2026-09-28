#!/usr/bin/env python3
"""Keep the UI/art perk register aligned with the playable perk registry."""

import csv
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PERK_ID = re.compile(r"\((new-horizons:[^)]+)\)$")
ICON_BINDING = re.compile(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}')


class NewHorizonsUiPerkInventoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        registry = json.loads((ROOT / "config/newHorizonsPerks.json").read_text(encoding="utf-8"))
        cls.status = {
            perk["id"]: perk["effect"]["status"]
            for skill in registry["skills"].values()
            for perk in skill["perks"]
        }
        with (ROOT / "docs/NH_UI_ASSET_INVENTORY.csv").open(newline="", encoding="utf-8") as inventory:
            cls.rows = list(csv.DictReader(inventory))
        icons = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")
        cls.icons = dict(ICON_BINDING.findall(icons))

    def test_every_perk_row_matches_current_activation(self):
        perk_rows = {}
        for row in self.rows:
            if row["Area"] not in {"Active perk", "Planned perk"}:
                continue
            match = PERK_ID.search(row["Item / ID"])
            self.assertIsNotNone(match, row["Item / ID"])
            perk_id = match.group(1)
            self.assertNotIn(perk_id, perk_rows)
            perk_rows[perk_id] = row

        self.assertEqual(set(perk_rows), set(self.status))
        for perk_id, row in perk_rows.items():
            with self.subTest(perk=perk_id):
                expected_area = "Active perk" if self.status[perk_id] == "active" else "Planned perk"
                self.assertEqual(row["Area"], expected_area)
                if "effect.status=" in row["Live binding / source"]:
                    self.assertIn(f"effect.status={self.status[perk_id]}", row["Live binding / source"])

    def test_active_neutral_fallbacks_are_not_mistaken_for_finished_art(self):
        for row in self.rows:
            if row["Area"] != "Active perk":
                continue
            match = PERK_ID.search(row["Item / ID"])
            self.assertIsNotNone(match, row["Item / ID"])
            perk_id = match.group(1)
            if self.icons.get(perk_id, "NH_perk_neutral") != "NH_perk_neutral":
                continue
            with self.subTest(perk=perk_id):
                self.assertEqual(row["Art"], "Not done")
                self.assertIn("NH_perk_neutral", row["Live binding / source"])


if __name__ == "__main__":
    unittest.main()
