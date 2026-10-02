#!/usr/bin/env python3
"""Validate the canonical New Horizons Skill/perk registry shape only.

This is deliberately a data-contract test.  Active effects are expected to
have matching runtime coverage; unimplemented registry entries remain planned.
"""
import hashlib
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
ACTIVE_PERKS = {
    "new-horizons:havocMagic.mineLayer",
    "new-horizons:natureMagic.geomancer",
    "new-horizons:lightMagic.sanctuaryKeeper",
    "new-horizons:natureMagic.venomancer",
    "new-horizons:havocMagic.pyromancer",
    "new-horizons:havocMagic.cryomancer",
    "new-horizons:havocMagic.controlledBlast",
    "new-horizons:warMachines.quartermaster",
    "new-horizons:warMachines.masterGunner",
    "new-horizons:warMachines.surgeon",
    "new-horizons:warMachines.piercingBolts",
    "new-horizons:warMachines.fortificationEngineer",
    "new-horizons:spellcraft.arcaneFocus",
    "new-horizons:spellcraft.grandFormula",
    "new-horizons:estates.taxCollector",
    "new-horizons:estates.estateNetwork",
    "new-horizons:estates.financier",
    "new-horizons:estates.landSurveyor",
    "new-horizons:learning.mentor",
    "new-horizons:learning.quickStudy",
    "new-horizons:chaosMagic.paradoxShield",
    "new-horizons:wisdom.meditation",
    "new-horizons:wisdom.manaConservation",
    "new-horizons:wisdom.deepKnowledge",
    "new-horizons:wisdom.preparedCaster",
    "new-horizons:wisdom.archmage",
    "new-horizons:wisdom.mysticism",
    "new-horizons:command.combinedArms",
    "new-horizons:battlecraft.reserve",
    "new-horizons:discipline.standardBearer",
    "new-horizons:discipline.holdFast",
    "new-horizons:discipline.fearless",
    "new-horizons:command.aggressiveCommander",
    "new-horizons:command.defensiveCommander",
    "new-horizons:command.veteranCommander",
    "new-horizons:chaosMagic.blinkmaster",
    "new-horizons:chaosMagic.misfortuneWeaver",
    "new-horizons:shroudOfMalassa.backstab",
    "new-horizons:spellcraft.empowerSpell",
    "new-horizons:spellcraft.spellPenetration",
    "new-horizons:natureMagic.herbalist",
    "new-horizons:bulwarkOfTheMire.mireborn",
    "new-horizons:bulwarkOfTheMire.thickHide",
    "new-horizons:bulwarkOfTheMire.bogAmbush",
    "new-horizons:bulwarkOfTheMire.toxicSpines",
    "new-horizons:bulwarkOfTheMire.swampRenewal",
    "new-horizons:bulwarkOfTheMire.mireGrip",
    "new-horizons:bulwarkOfTheMire.sharedCover",
    "new-horizons:bulwarkOfTheMire.immovable",
    "new-horizons:bulwarkOfTheMire.vengefulMire",
    "new-horizons:logistics.pathfinding",
    "new-horizons:logistics.navigation",
    "new-horizons:logistics.scouting",
    "new-horizons:logistics.roadmaster",
    "new-horizons:logistics.wayfarer",
    "new-horizons:wisdom.intelligence",
    "new-horizons:wisdom.arcaneReservoir",
    "new-horizons:lightMagic.benediction",
    "new-horizons:lightMagic.healer",
    "new-horizons:lightMagic.guardian",
    "new-horizons:lightMagic.aegis",
    "new-horizons:lightMagic.litany",
    "new-horizons:lightMagic.purifier",
    "new-horizons:lightMagic.retributionist",
    "new-horizons:lightMagic.crusader",
    "new-horizons:natureMagic.rootcaller",
    "new-horizons:natureMagic.beastcaller",
    "new-horizons:natureMagic.verdantWarden",
    "new-horizons:natureMagic.verdantCommunion",
    "new-horizons:shadowMagic.malediction",
    "new-horizons:shadowMagic.bloodDrinker",
    "new-horizons:shadowMagic.painweaver",
    "new-horizons:shadowMagic.witheringTouch",
    "new-horizons:shadowMagic.soulBinder",
    "new-horizons:shadowMagic.darkGift",
    "new-horizons:shadowMagic.nightFeeder",
    "new-horizons:shadowMagic.reanimator",
    "new-horizons:shadowMagic.grandMalediction",
    "new-horizons:warcasting.martialChanneling",
    "new-horizons:warcasting.arcaneChanneling",
    "new-horizons:warcasting.spellward",
    "new-horizons:warcasting.tacticalWeaving",
    "new-horizons:warcasting.battleMeditation",
    "new-horizons:archery.targetCaller",
    "new-horizons:archery.skirmisher",
    "new-horizons:archery.pointBlankShot",
    "new-horizons:archery.counterfire",
    "new-horizons:archery.armorPiercingShot",
    "new-horizons:archery.suppression",
    "new-horizons:archery.highArc",
    "new-horizons:archery.crossfire",
    "new-horizons:archery.deadeye",
    "new-horizons:archery.rainOfArrows",
    "new-horizons:demonicGating.swiftGate",
    "new-horizons:demonicGating.wideGate",
    "new-horizons:demonicGating.hellfireArrival",
    "new-horizons:demonicGating.reinforcedGate",
    "new-horizons:demonicGating.mobileGate",
    "new-horizons:demonicGating.infernalBeacon",
    "new-horizons:demonicGating.chainGate",
    "new-horizons:demonicGating.reserveDiscipline",
    "new-horizons:demonicGating.masterGate",
    "new-horizons:demonicGating.endlessLegion",
    "new-horizons:offense.shockAssault",
    "new-horizons:offense.encirclement",
    "new-horizons:offense.pursuit",
    "new-horizons:offense.executioner",
    "new-horizons:offense.armorPiercer",
    "new-horizons:offense.breakthrough",
    "new-horizons:offense.cleave",
    "new-horizons:offense.vengeance",
    "new-horizons:offense.relentlessAssault",
    "new-horizons:offense.noQuarter",
    "new-horizons:discipline.inspirationalLeader",
    "new-horizons:discipline.rally",
    "new-horizons:discipline.unbreakable",
    "new-horizons:armorer.shieldMaster",
    "new-horizons:armorer.ironDiscipline",
    "new-horizons:armorer.countercharge",
    "new-horizons:armorer.pavise",
    "new-horizons:armorer.formationFighting",
    "new-horizons:armorer.veteran",
    "new-horizons:luck.fortuneSFavor",
    "new-horizons:luck.luckyAim",
    "new-horizons:luck.secondChance",
    "new-horizons:luck.gambler",
    "new-horizons:luck.chainOfFortune",
    "new-horizons:luck.twistOfFate",
    "new-horizons:sorceryMagic.overcharger",
    "new-horizons:sorceryMagic.matterShaper",
    "new-horizons:sorceryMagic.selectiveDispel",
    "new-horizons:sorceryMagic.temporalField",
    "new-horizons:sorceryMagic.temporalist",
    "new-horizons:sorceryMagic.teleporter",
    "new-horizons:sorceryMagic.countermage",
    "new-horizons:sorceryMagic.illusionist",
    "new-horizons:sorceryMagic.chronomancer",
    "new-horizons:sorceryMagic.spellbinder",
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
    "new-horizons:metamagic.arcaneAcquisition",
    "new-horizons:metamagic.echoedDuration",
    "new-horizons:metamagic.splitFocus",
    "new-horizons:metamagic.formulaReserve",
    "new-horizons:metamagic.spellBuffer",
    "new-horizons:metamagic.grandMetamagic",
    "new-horizons:metamagic.perfectSequence",
    "new-horizons:battlecraft.entrench",
    "new-horizons:recruitment.volunteerNetwork",
    "new-horizons:recruitment.externalRecruiter",
    "new-horizons:recruitment.broadMuster",
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
    """Return the 31 authored three-column perk tables from canonical Markdown."""
    tables = []
    current = []

    def finish_table():
        if not current or current[0] != ["Perk", "Requires", "Effect"]:
            return
        if len(current) < 2 or current[1] != ["---", "---", "---"]:
            raise AssertionError("Canonical perk table lacks a Markdown separator")
        if any(len(row) != 3 for row in current[2:]):
            raise AssertionError("Canonical perk table has a malformed row")
        tables.append(current[2:].copy())

    for line in [*path.read_text(encoding="utf-8").splitlines(), ""]:
        if not (line.startswith("|") and line.endswith("|")):
            finish_table()
            current = []
            continue
        cells = []
        for cell in line[1:-1].split("|"):
            cell = re.sub(r"<br\s*/?>", " ", cell, flags=re.IGNORECASE)
            cell = re.sub(r"\*\*", "", cell)
            cells.append(" ".join(cell.split()))
        current.append(cells)
    return tables


def source_perk_row_for_current_rules(row):
    """Return a canonical perk row; retained as the comparison seam."""
    return row


def source_markdown_tables(path):
    """Parse all pipe tables so conversion regressions are not perk-only."""
    tables = []
    current = []
    for line in [*path.read_text(encoding="utf-8").splitlines(), ""]:
        if line.startswith("|") and line.endswith("|"):
            current.append([" ".join(cell.split()) for cell in line[1:-1].split("|")])
            continue
        if current:
            widths = {len(row) for row in current}
            if len(widths) != 1:
                raise AssertionError(f"Malformed canonical Markdown table: {current[0]}")
            tables.append(current)
            current = []
    return tables


class NewHorizonsPerkDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load("config/newHorizonsPerks.json")
        cls.schema = load("config/schemas/newHorizonsPerks.json")

    def test_registry_validates_against_schema(self):
        Draft4Validator(self.schema).validate(self.rules)

    def test_source_identity_and_selection_limits(self):
        self.assertEqual(self.rules["schemaVersion"], 1)
        self.assertEqual(self.rules["rulesetVersion"], 2)
        self.assertEqual(self.rules["sourceDocument"], "docs/design-sources/New Horizons.md")
        source = ROOT / self.rules["sourceDocument"]
        self.assertTrue(source.is_file())
        self.assertEqual(
            hashlib.sha256(source.read_bytes()).hexdigest(), self.rules["sourceSha256"]
        )
        self.assertEqual(self.rules["maxSkillChoices"], 2)
        self.assertEqual(self.rules["maxPerkChoices"], 2)
        self.assertEqual(self.rules["maxPerksPerSkill"], 3)

    def test_converted_non_perk_tables_keep_key_rows(self):
        source = ROOT / self.rules["sourceDocument"]
        tables = source_markdown_tables(source)
        self.assertGreaterEqual(len(tables), 125)
        rows = {tuple(row) for table in tables for row in table}
        expected = {
            ("Fortress", "I–V", "Adds Levels IV and V"),
            ("Conflux", "I–V", "No maximum-level change"),
            ("Sacrifice", "Damage bonus"),
            ("Targets", "Total", "Per target"),
            ("Debuffs", "Damage"),
            ("Growth", "Level 1", "Level 10", "Level 20", "Level 30"),
            ("1", "195", "195"),
            ("5", "312", "62"),
        }
        self.assertTrue(expected <= rows, expected - rows)
        text = source.read_text(encoding="utf-8")
        self.assertIn("|Crusade!|", text)
        self.assertIn("|Pandemonium|", text)
        self.assertIn("|Shield of Chaos|", text)
        self.assertIn(
            "Luck and Morale are Secondary Attributes with a fixed legal range from −10 to +10.",
            text,
        )

    def test_canonical_31_skill_roster(self):
        self.assertEqual(tuple(self.rules["skills"]), EXPECTED_SKILLS)
        self.assertEqual(len(self.rules["skills"]), 31)

    def test_cleave_is_active_advanced_offense_perk(self):
        cleave = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.cleave"
        )
        description = (
            "After destroying a stack in melee, automatically strike the adjacent enemy "
            "stack with the highest current aggregate HP for 50% normal damage. Ties use "
            "deterministic hex order. Once per activation; Cleave cannot trigger itself."
        )
        self.assertEqual(cleave["name"], "Cleave")
        self.assertEqual(cleave["requires"], "advanced")
        self.assertEqual(cleave["description"], description)
        self.assertEqual(cleave["effect"]["status"], "active")
        self.assertEqual(cleave["effect"]["description"], description)

    def test_vengeance_is_active_advanced_offense_perk(self):
        vengeance = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.vengeance"
        )
        description = "While Riposte! is active, each affected stack gains one additional retaliation for the round."
        self.assertEqual(vengeance["name"], "Vengeance")
        self.assertEqual(vengeance["requires"], "advanced")
        self.assertEqual(vengeance["description"], description)
        self.assertEqual(vengeance["effect"]["status"], "active")
        self.assertEqual(vengeance["effect"]["description"], description)

    def test_relentless_assault_is_active_expert_offense_perk(self):
        perk = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.relentlessAssault"
        )
        description = (
            "Consecutive activations attacking the same enemy stack gain +10% damage, "
            "stacking to +30%. Attacking another target resets the bonus."
        )
        self.assertEqual(perk["name"], "Relentless Assault")
        self.assertEqual(perk["requires"], "expert")
        self.assertEqual(perk["description"], description)
        self.assertEqual(perk["effect"]["status"], "active")
        self.assertEqual(perk["effect"]["description"], description)

    def test_no_quarter_is_active_expert_offense_perk(self):
        perk = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.noQuarter"
        )
        description = (
            "If a melee attack leaves an enemy below 25% maximum HP, it loses all remaining "
            "retaliations for the round and suffers -2 Morale until the end of its next activation."
        )
        self.assertEqual(perk["name"], "No Quarter")
        self.assertEqual(perk["requires"], "expert")
        self.assertEqual(perk["description"], description)
        self.assertEqual(perk["effect"]["status"], "active")
        self.assertEqual(perk["effect"]["description"], description)

    def test_all_ten_archery_perks_are_active_at_their_canonical_ranks(self):
        skill = self.rules["skills"]["new-horizons:archery"]
        canonical = {
            "new-horizons:archery.targetCaller": "basic",
            "new-horizons:archery.skirmisher": "basic",
            "new-horizons:archery.pointBlankShot": "basic",
            "new-horizons:archery.counterfire": "basic",
            "new-horizons:archery.armorPiercingShot": "advanced",
            "new-horizons:archery.suppression": "advanced",
            "new-horizons:archery.highArc": "advanced",
            "new-horizons:archery.crossfire": "advanced",
            "new-horizons:archery.deadeye": "expert",
            "new-horizons:archery.rainOfArrows": "expert",
        }
        perks = {perk["id"]: perk for perk in skill["perks"]}
        self.assertEqual(set(perks), set(canonical))
        for perk_id, rank in canonical.items():
            with self.subTest(perk=perk_id):
                self.assertEqual(perks[perk_id]["requires"], rank)
                self.assertEqual(perks[perk_id]["effect"]["status"], "active")

    def test_six_implemented_bulwark_perks_are_active_and_deep_bulwark_stays_planned(self):
        skill = self.rules["skills"]["new-horizons:bulwarkOfTheMire"]
        perks = {perk["id"]: perk for perk in skill["perks"]}
        canonical = {
            "new-horizons:bulwarkOfTheMire.toxicSpines": "basic",
            "new-horizons:bulwarkOfTheMire.swampRenewal": "advanced",
            "new-horizons:bulwarkOfTheMire.mireGrip": "advanced",
            "new-horizons:bulwarkOfTheMire.sharedCover": "advanced",
            "new-horizons:bulwarkOfTheMire.immovable": "expert",
            "new-horizons:bulwarkOfTheMire.vengefulMire": "expert",
        }
        self.assertEqual(set(canonical) | {"new-horizons:bulwarkOfTheMire.deepBulwark"}
                         | {
                             "new-horizons:bulwarkOfTheMire.mireborn",
                             "new-horizons:bulwarkOfTheMire.thickHide",
                             "new-horizons:bulwarkOfTheMire.bogAmbush",
                         }, set(perks))
        for perk_id, rank in canonical.items():
            with self.subTest(perk=perk_id):
                self.assertEqual(perks[perk_id]["requires"], rank)
                self.assertEqual(perks[perk_id]["effect"]["status"], "active")
        deep_bulwark = perks["new-horizons:bulwarkOfTheMire.deepBulwark"]
        self.assertEqual(deep_bulwark["requires"], "advanced")
        self.assertEqual(deep_bulwark["effect"]["status"], "planned")

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
                            current_row[2],
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
                    expected_status = "active" if (skill_id in ACTIVE_RANK_SKILLS or
                                                   skill_id == "new-horizons:spellcraft") else "planned"
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

    def test_metamagic_text_describes_round_long_optional_spell_actions(self):
        metamagic = self.rules["skills"]["new-horizons:metamagic"]
        for rank in RANKS:
            with self.subTest(rank=rank):
                description = metamagic["ranks"][rank]["description"]
                self.assertIn("optional Spell Action usable until the round ends", description)
                self.assertIn("Casting with it spends", description)
                self.assertIn("cannot trigger Metamagic again", description)

        grand = next(
            perk for perk in metamagic["perks"]
            if perk["id"] == "new-horizons:metamagic.grandMetamagic"
        )["description"]
        self.assertIn("first additional Spell of the third used Metamagic sequence", grand)
        self.assertIn("Both opportunities expire at the end of the round", grand)
        self.assertIn("neither can trigger Metamagic", grand)

    def test_metamagic_rewards_distinguish_normal_restoration_from_buffer(self):
        perks = {
            perk["id"]: perk for perk in self.rules["skills"]["new-horizons:metamagic"]["perks"]
        }
        self.assertNotIn("new-horizons:metamagic.spellEcho", perks)
        self.assertNotIn("new-horizons:metamagic.countersequence", perks)
        acquisition = perks["new-horizons:metamagic.arcaneAcquisition"]
        self.assertEqual(acquisition["name"], "Arcane Acquisition")
        self.assertEqual(acquisition["effect"]["status"], "active")
        self.assertIn("each living enemy stack", acquisition["description"])
        self.assertIn("Check each target when it is damaged", acquisition["description"])
        buffer = perks["new-horizons:metamagic.spellBuffer"]
        self.assertEqual(buffer["requires"], "advanced")
        self.assertIn("Once per combat", buffer["description"])
        self.assertIn("expires unused at the end of the round", buffer["description"])
        self.assertIn("6 Buffer Spell Points", buffer["description"])
        self.assertIn("does not consume a Metamagic use", buffer["description"])
        reserve = perks["new-horizons:metamagic.formulaReserve"]["description"]
        self.assertIn("After each Metamagic sequence", reserve)
        self.assertIn("at least one additional Spell", reserve)
        self.assertIn("3 Normal Spell Points", reserve)

    def test_spell_point_perks_are_integrated_into_the_canonical_document(self):
        perks = {
            perk["name"]: perk["description"]
            for perk in self.rules["skills"]["new-horizons:metamagic"]["perks"]
        }
        self.assertIn("Normal Spell Points", perks["Formula Reserve"])
        self.assertIn("Buffer Spell Points", perks["Spell Buffer"])
        intelligence = next(
            perk for perk in self.rules["skills"]["new-horizons:wisdom"]["perks"]
            if perk["name"] == "Intelligence"
        )["description"]
        self.assertIn("floor(1.30 × effective Knowledge)", intelligence)
        source_rows = source_perk_tables(ROOT / self.rules["sourceDocument"])
        all_rows = [row for table in source_rows for row in table]
        source_text = {row[0]: row[2] for row in all_rows}
        self.assertIn("3 Normal Spell Points", source_text["Formula Reserve"])
        self.assertIn("6 Buffer Spell Points", source_text["Spell Buffer"])
        self.assertIn("floor(1.30 × effective Knowledge)", source_text["Intelligence"])
        self.assertEqual(source_text["Intelligence"], intelligence)

    def test_game_settings_schema_exposes_registry_without_activating_it(self):
        settings = load("config/schemas/gameSettings.json")
        heroes = settings["properties"]["heroes"]["properties"]
        self.assertEqual(heroes["newHorizonsPerks"], {"$ref": "newHorizonsPerks.json"})

    def test_default_module_carries_canonical_registry(self):
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["version"], load("config/newHorizonsVersion.json")["version"])
        self.assertEqual(module["settings"]["heroes"]["newHorizonsPerks"], self.rules)


if __name__ == "__main__":
    unittest.main()
