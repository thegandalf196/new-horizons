#!/usr/bin/env python3
"""Validate the canonical New Horizons Skill/perk registry shape only.

This is deliberately a data-contract test.  Active effects are expected to
have matching runtime coverage; unimplemented registry entries remain planned.
"""
import hashlib
import json
from pathlib import Path
import unittest
from xml.etree import ElementTree
from zipfile import ZipFile

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
ACTIVE_PERKS = {
    "new-horizons:warcasting.martialChanneling",
    "new-horizons:warcasting.arcaneChanneling",
    "new-horizons:warcasting.tacticalWeaving",
    "new-horizons:warcasting.battleMeditation",
    "new-horizons:demonicGating.swiftGate",
    "new-horizons:demonicGating.wideGate",
    "new-horizons:demonicGating.hellfireArrival",
    "new-horizons:demonicGating.reinforcedGate",
    "new-horizons:demonicGating.mobileGate",
    "new-horizons:demonicGating.infernalBeacon",
    "new-horizons:demonicGating.chainGate",
    "new-horizons:demonicGating.reserveDiscipline",
    "new-horizons:demonicGating.endlessLegion",
    "new-horizons:offense.shockAssault",
    "new-horizons:offense.executioner",
    "new-horizons:offense.armorPiercer",
    "new-horizons:offense.breakthrough",
    "new-horizons:discipline.inspirationalLeader",
    "new-horizons:sorceryMagic.overcharger",
    "new-horizons:sorceryMagic.matterShaper",
    "new-horizons:sorceryMagic.selectiveDispel",
    "new-horizons:sorceryMagic.temporalField",
    "new-horizons:sorceryMagic.temporalist",
    "new-horizons:sorceryMagic.teleporter",
    "new-horizons:sorceryMagic.countermage",
    "new-horizons:sorceryMagic.illusionist",
    "new-horizons:sorceryMagic.chronomancer",
    "new-horizons:sylvanLuck.elvenPrecision",
    "new-horizons:sylvanLuck.forestSFavor",
    "new-horizons:sylvanLuck.serendipity",
    "new-horizons:sylvanLuck.luckyRecovery",
    "new-horizons:sylvanLuck.sharedFortune",
    "new-horizons:sylvanLuck.natureSProvidence",
    "new-horizons:sylvanLuck.fortunateAim",
    "new-horizons:sylvanLuck.wildChance",
    "new-horizons:sylvanLuck.perfectMoment",
    "new-horizons:sylvanLuck.cascadingFortune",
    "new-horizons:necromancy.boneCollector",
    "new-horizons:necromancy.darkConversion",
    "new-horizons:necromancy.blackHarvest",
    "new-horizons:bloodrage.warDrums",
    "new-horizons:metamagic.spellSequencing",
    "new-horizons:metamagic.arcaneEconomy",
    "new-horizons:metamagic.focusedPairing",
    "new-horizons:metamagic.countersequence",
    "new-horizons:metamagic.echoedDuration",
    "new-horizons:metamagic.splitFocus",
    "new-horizons:metamagic.formulaReserve",
    "new-horizons:metamagic.spellEcho",
    "new-horizons:metamagic.grandMetamagic",
    "new-horizons:metamagic.perfectSequence",
    "new-horizons:battlecraft.entrench",
    "new-horizons:recruitment.volunteerNetwork",
    "new-horizons:recruitment.eliteDraft",
    "new-horizons:recruitment.championSCall",
    "new-horizons:recruitment.masterRecruiter",
    "new-horizons:havocMagic.stormcaller",
    "new-horizons:havocMagic.conductor",
    "new-horizons:havocMagic.annihilator",
}
ACTIVE_RANK_SKILLS = {
    "new-horizons:warcasting",
    "new-horizons:demonicGating",
    "new-horizons:offense",
    "new-horizons:armorer",
    "new-horizons:archery",
    "new-horizons:battlecraft",
    "new-horizons:command",
    "new-horizons:lightMagic",
    "new-horizons:shadowMagic",
    "new-horizons:natureMagic",
    "new-horizons:havocMagic",
    "new-horizons:sorceryMagic",
    "new-horizons:chaosMagic",
    "new-horizons:warMachines",
    "new-horizons:discipline",
    "new-horizons:recruitment",
    "new-horizons:logistics",
    "new-horizons:estates",
    "new-horizons:learning",
    "new-horizons:luck",
    "new-horizons:wisdom",
    "new-horizons:sylvanLuck",
    "new-horizons:necromancy",
    "new-horizons:bloodrage",
    "new-horizons:metamagic",
    "new-horizons:bulwarkOfTheMire",
    "new-horizons:shroudOfMalassa",
}
EXPECTED_SKILLS = (
    "new-horizons:offense",
    "new-horizons:armorer",
    "new-horizons:archery",
    "new-horizons:battlecraft",
    "new-horizons:warMachines",
    "new-horizons:discipline",
    "new-horizons:recruitment",
    "new-horizons:command",
    "new-horizons:lightMagic",
    "new-horizons:shadowMagic",
    "new-horizons:natureMagic",
    "new-horizons:havocMagic",
    "new-horizons:sorceryMagic",
    "new-horizons:chaosMagic",
    "new-horizons:spellcraft",
    "new-horizons:wisdom",
    "new-horizons:warcasting",
    "new-horizons:logistics",
    "new-horizons:diplomacy",
    "new-horizons:estates",
    "new-horizons:learning",
    "new-horizons:luck",
    "new-horizons:divineMandate",
    "new-horizons:sylvanLuck",
    "new-horizons:metamagic",
    "new-horizons:shroudOfMalassa",
    "new-horizons:demonicGating",
    "new-horizons:necromancy",
    "new-horizons:bloodrage",
    "new-horizons:bulwarkOfTheMire",
    "new-horizons:elementalRebirth",
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def source_perk_tables(path):
    """Return the authored perk rows from the source document's perk tables."""
    namespace = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
    with ZipFile(path) as archive:
        document = ElementTree.fromstring(archive.read("word/document.xml"))

    tables = []
    for table in document.findall(".//w:body/w:tbl", namespace):
        rows = []
        for row in table.findall("./w:tr", namespace):
            rows.append([
                "".join(text.text or "" for text in cell.findall(".//w:t", namespace)).strip()
                for cell in row.findall("./w:tc", namespace)
            ])
        if rows and rows[0] == ["Perk", "Requires", "Effect"]:
            tables.append(rows[1:])
    return tables


def source_description_for_current_rules(description):
    """Apply the deterministic-growth wording migration to frozen source prose.

    The supplied design document predates the fixed class-vector rule. Keep its
    hash and table layout as provenance checks while allowing the live registry
    to remove the retired primary-growth chance promise.
    """
    return description.replace(
        "Wisdom's chance to grant +1 Knowledge at level-up increases by 10 percentage points.",
        "Wisdom's Mana discount remains effective when other percentage-based Mana modifiers are active.",
    ).replace(
        "Lightning Bolt and Chain Lightning receive +15% to their Spell Power-derived damage components.",
        "Lightning Bolt, Chain Lightning, and Master Chain Lightning receive +15% to their Spell Power-derived damage components.",
    )


def source_perk_row_for_current_rules(row):
    """Apply the user-authorized replacement of the retired Metamagic perk.

    The canonical document predates the playable replacement, so preserve its
    hash/provenance while comparing the live registry against the current
    rules contract.
    """
    if row[0] == "Spell Buffer":
        return [
            "Spell Echo",
            row[1],
            "If the additional Spell repeats the first Spell in the Metamagic sequence, it gains +25% to its Spell Power-derived component.",
        ]
    return row


class NewHorizonsPerkDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load("config/newHorizonsPerks.json")
        cls.schema = load("config/schemas/newHorizonsPerks.json")

    def test_registry_validates_against_schema(self):
        Draft4Validator(self.schema).validate(self.rules)

    def test_source_identity_and_selection_limits(self):
        self.assertEqual(self.rules["schemaVersion"], 1)
        self.assertEqual(self.rules["rulesetVersion"], 1)
        self.assertEqual(self.rules["sourceDocument"], "docs/design-sources/New Horizons.docx")
        source = ROOT / self.rules["sourceDocument"]
        self.assertTrue(source.is_file())
        self.assertEqual(
            hashlib.sha256(source.read_bytes()).hexdigest(), self.rules["sourceSha256"]
        )
        self.assertEqual(self.rules["maxSkillChoices"], 2)
        self.assertEqual(self.rules["maxPerkChoices"], 2)
        self.assertEqual(self.rules["maxPerksPerSkill"], 3)

    def test_canonical_31_skill_roster(self):
        self.assertEqual(tuple(self.rules["skills"]), EXPECTED_SKILLS)
        self.assertEqual(len(self.rules["skills"]), 31)

    def test_perk_definitions_match_source_document(self):
        source_tables = source_perk_tables(ROOT / self.rules["sourceDocument"])
        self.assertEqual(len(source_tables), len(self.rules["skills"]))
        for (skill_id, skill), source_rows in zip(self.rules["skills"].items(), source_tables):
            with self.subTest(skill=skill_id):
                self.assertEqual(len(source_rows), len(skill["perks"]))
                self.assertEqual(
                    [
                        (perk["name"], perk["requires"].title(), perk["description"])
                        for perk in skill["perks"]
                    ],
                    [
                        (
                            current_row[0],
                            current_row[1],
                            source_description_for_current_rules(current_row[2]),
                        )
                        for row in source_rows
                        for current_row in [source_perk_row_for_current_rules(row)]
                    ],
                )

    def test_each_skill_has_ranked_effects_and_4_4_2_perks(self):
        all_perk_ids = []
        for skill_id, skill in self.rules["skills"].items():
            with self.subTest(skill=skill_id):
                self.assertEqual(skill["id"], skill_id)
                self.assertEqual(set(skill["ranks"]), set(RANKS))
                for rank in RANKS:
                    effect = skill["ranks"][rank]["effect"]
                    expected_status = "active" if skill_id in ACTIVE_RANK_SKILLS else "planned"
                    self.assertEqual(effect["status"], expected_status)
                    self.assertTrue(effect["description"])
                    self.assertEqual(skill["ranks"][rank]["description"], effect["description"])

                perks = skill["perks"]
                self.assertEqual(len(perks), 10)
                self.assertEqual(
                    {rank: sum(perk["requires"] == rank for perk in perks) for rank in RANKS},
                    {"basic": 4, "advanced": 4, "expert": 2},
                )
                for perk in perks:
                    self.assertTrue(perk["id"].startswith(skill_id + "."))
                    self.assertEqual(perk["description"], perk["effect"]["description"])
                    expected_status = "active" if perk["id"] in ACTIVE_PERKS else "planned"
                    self.assertEqual(perk["effect"]["status"], expected_status)
                    self.assertTrue(perk["name"])
                    self.assertTrue(perk["description"])
                    all_perk_ids.append(perk["id"])

        self.assertEqual(len(all_perk_ids), 310)
        self.assertEqual(len(set(all_perk_ids)), 310)

    def test_game_settings_schema_exposes_registry_without_activating_it(self):
        settings = load("config/schemas/gameSettings.json")
        heroes = settings["properties"]["heroes"]["properties"]
        self.assertEqual(heroes["newHorizonsPerks"], {"$ref": "newHorizonsPerks.json"})

    def test_default_module_carries_canonical_registry(self):
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["version"], "0.11.0")
        self.assertEqual(module["settings"]["heroes"]["newHorizonsPerks"], self.rules)


if __name__ == "__main__":
    unittest.main()
