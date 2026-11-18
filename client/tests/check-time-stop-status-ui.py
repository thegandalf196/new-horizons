#!/usr/bin/env python3
"""Guard retired Sorcery removal and surviving battle-status presentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
HOVER_PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
TEMPORARY = (ROOT / "client/battle/StackTemporaryStatusPresenter.cpp").read_text(encoding="utf-8")
PRIORITY = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
STACK_WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")
SPELLS = (ROOT / "Mods/new-horizons/Content/config/spells/newHorizons.json").read_text(encoding="utf-8")


class RetiredSorceryStatusUiSourceTest(unittest.TestCase):
    def test_retired_statuses_are_absent(self):
        for source in (STATUS, HOVER_PANEL, STACK_WINDOW, PRIORITY):
            for retired in ("TIME_STOP", "SPELL_LOCK", "isTimeStop", "isSpellLock", "arcaneBreach"):
                self.assertNotIn(retired, source)

    def test_ordinary_duration_and_overflow_remain(self):
        for source in (HOVER_PANEL, STACK_WINDOW):
            self.assertIn("std::to_string(duration)", source)
        self.assertIn("spellDurationRemaining", STACK_WINDOW)
        self.assertIn("displayPlan.ellipsisUsesSlot", HOVER_PANEL)
        self.assertIn("result.ellipsisUsesSlot ? 2 : 3", PRIORITY)
        self.assertIn("printed >= 8", STACK_WINDOW)

    def test_focus_displays_authoritative_charges(self):
        for source in (HOVER_PANEL, STACK_WINDOW):
            self.assertIn("newHorizonsFocusMagic::charges", source)
            self.assertIn("Energy shots remaining", source)
        self.assertIn("StackStatusIconKind::FOCUS_MAGIC: return 7", PRIORITY)

    def test_surviving_control_statuses_keep_priority(self):
        for kind in ("FROZEN", "CONFUSION", "ENTANGLE", "DOOM", "PHYSICAL_POISON"):
            self.assertIn("StackStatusIconKind::" + kind, PRIORITY)
        self.assertIn("stackStatusDisplayPlan(statusKinds, totalEffectCount)", TEMPORARY)
        self.assertLess(STACK_WINDOW.index("std::stable_partition"), STACK_WINDOW.index("for(SpellID effect : spells)"))

    def test_focus_presentation_uses_attack_receipt_including_final_charge(self):
        dispatch = (ROOT / "client/CPlayerInterface.cpp").read_text()
        animation = (ROOT / "client/battle/BattleRangedAnimation.cpp").read_text()
        projectile = (ROOT / "client/battle/BattleProjectileController.cpp").read_text()
        orb = (ROOT / "client/battle/BattleFocusMagicProjectile.cpp").read_text()
        for field in ("focusMagicRanged", "focusMagicConverted"):
            self.assertIn("info." + field + " = ba->" + field, dispatch)
        self.assertIn('AudioPath::builtin("ZELTSHOT.wav")', animation)
        self.assertIn("if(focusMagicConverted)", animation)
        self.assertNotIn("newHorizonsFocusMagic::active", animation + projectile)
        self.assertIn("if(focusMagicRanged)", projectile)
        focus = projectile.split("if(focusMagicRanged)", 1)[1].split("else if", 1)[0]
        self.assertIn("createFocusMagicProjectile(from, dest)", focus)
        for token in ('AnimationPath::builtin("NH_focus_magic_orb")',
                      'std::make_shared<ProjectileMissile>()',
                      'orb->visibility.protectFirstRender()',
                      'AnimationControls::getProjectileSpeed()'):
            self.assertIn(token, orb)
        self.assertNotIn("ProjectileRay", focus)
        self.assertIn('rayProjectile->rayConfig = shooterInfo.animation.projectileRay', projectile)

    def test_recall_preview_uses_shared_actual_landing_footprint(self):
        preview = (ROOT / "client/battle/BattleSpellManaPreview.cpp").read_text()
        field = (ROOT / "client/battle/BattleFieldController.cpp").read_text()
        self.assertIn("newHorizonsSorcerySpells::forcedRecallDestination(*battle, *target)", preview)
        self.assertIn("target->getHexes(*destination)", preview)
        self.assertIn("getForcedRecallTargetHexes(spell, hoveredHex)", field)

    def test_force_lance_is_scene_owned_and_status_driven(self):
        effects = (ROOT / "client/battle/BattleEffectsController.h").read_text()
        presentation = (ROOT / "client/battle/ForceLancePresentation.cpp").read_text()
        spell = (ROOT / "client/battle/BattleSpellPresentation.cpp").read_text()
        self.assertIn("ForceLancePresentation forceLance;", effects)
        for token in ("BonusType::BIND_EFFECT", "BonusSource::SPELL_EFFECT", "BonusDuration::N_TURNS",
                      "bonus->turnsRemain > 0", "bonus->stacking == newHorizonsSorcerySpells::FORCE_LANCE",
                      "bonus->sid.toString() == newHorizonsSorcerySpells::FORCE_LANCE"):
            self.assertIn(token, presentation)
        self.assertIn("if(!stack.alive() || stack.isGhost())", presentation)
        self.assertIn("return foot - Point(110, 138);", presentation)
        self.assertIn("RELEASE_MILLISECONDS = 250", presentation)
        self.assertIn("release.phase.elapsedForAdvance(elapsed)", presentation)
        self.assertIn("ForceLancePresentation::strike(*this, *target)", spell)
        refresh = presentation.split("void ForceLancePresentation::refresh", 1)[1]
        for forbidden in ("new EffectAnimation", "addBonus", "removeBonus", "battleMake", "static std::map"):
            self.assertNotIn(forbidden, refresh)


if __name__ == "__main__":
    unittest.main()
