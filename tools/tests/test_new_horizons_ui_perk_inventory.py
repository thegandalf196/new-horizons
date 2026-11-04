#!/usr/bin/env python3
"""Keep the UI/art perk register aligned with the playable perk registry."""

import csv
import copy
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PERK_ID = re.compile(r"\((new-horizons:[^)]+)\)$")
ICON_BINDING = re.compile(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}')
REWORKED_SKILLS = ('wisdom', 'warcasting', 'learning', 'estates')


def validate_current_reworked_row(row, perk, icon):
    expected = perk['name'] + ' (' + perk['id'] + ')'
    if row['Item / ID'] != expected or row['Area'] != 'Active perk':
        raise ValueError('Current perk label or activation differs from its definition')
    tokens = row['Live binding / source'].split('; ')
    if icon not in tokens or 'requires=' + perk['requires'] not in tokens:
        raise ValueError('Current perk icon or tier differs from its definition')
    if 'effect.status=active' not in tokens or row['Art'] != 'Provisional':
        raise ValueError('Current reused perk must retain its provisional active record')


class NewHorizonsUiPerkInventoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        registry = json.loads((ROOT / "config/newHorizonsPerks.json").read_text(encoding="utf-8"))
        cls.reworked = [perk for skill in REWORKED_SKILLS
                        for perk in registry['skills']['new-horizons:' + skill]['perks']]
        cls.status = {
            perk["id"]: perk["effect"]["status"]
            for skill in registry["skills"].values()
            for perk in skill["perks"]
        }
        with (ROOT / "docs/NH_UI_ASSET_INVENTORY.csv").open(newline="", encoding="utf-8") as inventory:
            cls.rows = list(csv.DictReader(inventory))
        icons = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")
        cls.icons = dict(ICON_BINDING.findall(icons))
        cls.icon_rows = ICON_BINDING.findall(icons)

    def test_four_reworked_catalogues_have_exact_current_labels_tiers_and_icons(self):
        rows = {row['Item / ID']: row for row in self.rows}
        current = {perk['id'] for perk in self.reworked}
        active = [identity for identity, _ in self.icon_rows
                  if identity.split(':')[1].split('.')[0] in REWORKED_SKILLS]
        self.assertEqual(len(active), 40)
        self.assertEqual(set(active), current)
        for perk in self.reworked:
            row = rows[perk['name'] + ' (' + perk['id'] + ')']
            validate_current_reworked_row(row, perk, self.icons[perk['id']])

    def test_reworked_inventory_rejects_stale_label_tier_icon_activation_and_approval(self):
        for perk in self.reworked:
            item = perk['name'] + ' (' + perk['id'] + ')'
            row = next(row for row in self.rows if row['Item / ID'] == item)
            for field, value in (
                ('Item / ID', 'Retired name (' + perk['id'] + ')'),
                ('Area', 'Planned perk'), ('Art', 'Final'),
                ('Live binding / source', row['Live binding / source'].replace(
                    'requires=' + perk['requires'], 'requires=retired')),
                ('Live binding / source', row['Live binding / source'].replace(
                    self.icons[perk['id']], 'NH_wrong')),
                ('Live binding / source', row['Live binding / source'].replace(
                    'effect.status=active', 'effect.status=planned')),
            ):
                changed = copy.deepcopy(row)
                changed[field] = value
                with self.subTest(perk=perk['id'], field=field), self.assertRaises(ValueError):
                    validate_current_reworked_row(changed, perk, self.icons[perk['id']])

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

    def test_development_pool_uses_descriptor_normal_frame(self):
        source = (ROOT / "client/windows/HeroGrowthWindow.cpp").read_text(encoding="utf-8")
        # Older selected families contain descriptors/state images, not a flat
        # image at the icon stem. This guards the read-only Development pool;
        # it is not a rendered assertion about the level-up offer dialog.
        self.assertRegex(source, re.compile(
            r"std::make_shared<CAnimImage>\(\s*AnimationPath::builtin\(newHorizonsPerkIcon\(perk.id,\s*perk.name,\s*perkState.rules\)\),"
            r"\s*0\s*,"))
        self.assertNotRegex(source, re.compile(
            r"std::make_shared<CPicture>\(ImagePath::builtin\(newHorizonsPerkIcon\("))

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
