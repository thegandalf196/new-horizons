"""Creature ability graphics follow authored metadata and typed identities."""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from import_new_horizons_spell_art import parse_jsonc


class CreatureAbilityArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bonuses = parse_jsonc((ROOT / "config/bonuses.json").read_text())
        cls.handler = (ROOT / "lib/CBonusTypeHandler.cpp").read_text()
        cls.graphics = cls.handler[cls.handler.index("ImagePath CBonusTypeHandler::bonusToGraphics"):
                                   cls.handler.index("std::vector<JsonNode> CBonusTypeHandler::loadLegacyData")]

    def test_authored_icons_precede_semantic_fallback(self):
        ordered = ["bonus->customIconPath", "BonusType::SPELL_IMMUNITY", "bt.subtypeIcons.count",
                   "bt.valueIcons.count", "!bt.icon.empty()", "switch(bonus->type)"]
        offsets = [self.graphics.index(token) for token in ordered]
        self.assertEqual(offsets, sorted(offsets))

    def test_only_typed_subtypes_select_spell_or_creature_art(self):
        fallback = self.graphics[self.graphics.index("switch(bonus->type)"):]
        cases = re.findall(r"case BonusType::([A-Z_]+):", fallback)
        self.assertEqual(set(cases), {"SPELLCASTER", "SPELL_AFTER_ATTACK", "SPELL_LIKE_ATTACK",
                                     "ENCHANTER", "HATE", "SKELETON_TRANSFORMER_TARGET"})
        self.assertIn("definition->getIconBook()", fallback)
        self.assertIn("definition->getIconEffect()", fallback)
        self.assertIn('if(spell < SpellID::FIRST_NON_SPELL)', fallback)
        self.assertIn('if(spell <= SpellID::THUNDERBOLT)', fallback)
        self.assertIn('"SPELLS.DEF:0:" + std::to_string(spell.getNum())', fallback)
        self.assertIn('"SPELLINT.DEF:0:" + std::to_string(spell.getNum() + 1)', fallback)
        self.assertIn('"CPRSMALL.DEF:0:"', fallback)
        self.assertIn("creature.toCreature()->getIconIndex()", fallback)

    def test_creature_only_spells_without_native_frames_have_specific_art(self):
        triggered = self.bonuses["SPELL_AFTER_ATTACK"]["graphics"]["subtypeIcons"]
        self.assertEqual(triggered["spell.acidBreath"], "NH_ability_acid_breath.png")
        self.assertEqual(triggered["spell.acidBreathDamage"], triggered["spell.acidBreath"])
        self.assertEqual(triggered["spell.dispelHelpful"], "SPELLS.def:0:35")
        self.assertEqual(self.bonuses["SPELL_LIKE_ATTACK"]["graphics"]["subtypeIcons"]["spell.fireballAbility"],
                         "SPELLS.def:0:21")
        self.assertEqual(self.bonuses["SPELLCASTER"]["graphics"]["subtypeIcons"]["spell.summonDemons"],
                         "NH_ability_raise_demons.png")

    def test_script_icons_do_not_create_a_generic_internal_trigger_card(self):
        trigger = self.bonuses["COMBAT_EVENT_TRIGGER"]
        self.assertTrue(trigger["hidden"])
        self.assertNotIn("icon", trigger["graphics"])
        self.assertEqual(trigger["graphics"]["subtypeIcons"], {
            "script.lifeDrain": "NH_spell_life_drain_44.png",
            "script.deathStare": "NH_ability_death_stare.png",
            "script.fireShield": "NH_ability_fire_shield.png"})

    def test_new_native_families_and_existing_jousting_immunity(self):
        for kind, stem in {"CHARGE_IMMUNITY": "jousting_immunity", "PASS_THROUGH": "pass_through",
                           "LONG_REACH": "long_reach", "ELEMENTAL_SPELL_DAMAGE_RECEIVED": "elemental_affinity",
                           "SPELL_DAMAGE_REDUCTION_BASIS_POINTS": "magical_protection"}.items():
            self.assertEqual(self.bonuses[kind]["graphics"]["icon"], "NH_ability_" + stem + ".png")

    def test_current_assimilation_row_requires_actual_historical_capability(self):
        source = (ROOT / "client/windows/NewHorizonsCreatureAbilityHelp.cpp").read_text()
        self.assertIn("usesCommandSlots && creature.hasValue()", source)
        self.assertNotIn("capturedCommandRules", source)
        stack = (ROOT / "client/windows/StackAbilityCards.cpp").read_text()
        self.assertIn("hero && hero->getArmyCommandRules().has_value()", stack)
        self.assertIn("strategic->getArmy()", stack)
        self.assertNotIn("getHeroCapabilityRules()", stack)
        wiki = (ROOT / "client/windows/wiki/WikiCreatureContent.cpp").read_text()
        self.assertIn('callback && callback->getHeroCapabilityRules()["armyCommand"].isStruct()', wiki)
        preview = (ROOT / "client/UIHelper.cpp").read_text()
        self.assertIn("hero && hero->getArmyCommandRules() && newHorizonsCreatures::hasIntrinsicAssimilation", preview)
        header = (ROOT / "client/windows/NewHorizonsCreatureAbilityHelp.h").read_text()
        self.assertIn('"NH_ability_freezing_touch.png"', header)
        self.assertIn("usesCommandSlots = false", header)

    def test_existing_reuses_are_selected_shipping_resources(self):
        from .nhart_test_resources import verified_index
        index = {name.casefold() for name in verified_index()}
        for kind in ("JOUSTING", "REBIRTH", "MANA_CHANNELING", "MANA_DRAIN", "SPELL_DAMAGE_REDUCTION",
                     "NO_DISTANCE_PENALTY", "SPELL_RESISTANCE_AURA", "CATAPULT", "HEALER",
                     "ENEMY_DEFENCE_REDUCTION", "CHANGES_SPELL_COST_FOR_ALLY"):
            resource = "SPRITES/" + self.bonuses[kind]["graphics"]["icon"]
            self.assertIn(resource.casefold(), index, resource)
