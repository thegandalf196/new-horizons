/*
 * NewHorizonsNaturesWrathFixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/effects/Effect.h"

class NewHorizonsNaturesWrathFixture : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * first = nullptr;
	CStack * friendly = nullptr;
	CStack * third = nullptr;
	CStack * reserve = nullptr;
	static SpellID spell() { return SpellID(SpellID::decode(std::string(newHorizonsNaturesWrath::SPELL_KEY))); }

	void prepare(bool worldroot = false, int friendlyCount = 1000)
	{
		startGame();
		ASSERT_TRUE(spell().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(spell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill nature(SecondarySkill::decode("new-horizons:natureMagic"));
		ASSERT_TRUE(nature.hasValue());
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(worldroot)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:natureMagic", "new-horizons:natureMagic.herbalist"});
			attackerSideHero->applyPerkSelection({"new-horizons:natureMagic", "new-horizons:natureMagic.geomancer"});
			attackerSideHero->applyPerkSelection({"new-horizons:natureMagic", "new-horizons:natureMagic.worldroot"});
			ASSERT_TRUE(newHorizonsNaturesWrath::hasWorldroot(attackerSideHero));
		}
		setTestSpellPointTotal(attackerSideHero, 1000);
		startTerrainBattle(TerrainId::GRASS);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 5), 1000);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), friendlyCount);
		third = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 1000);
		reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(14, 8), friendlyCount);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(third, nullptr);
		ASSERT_NE(reserve, nullptr);
		beginCombat();
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
		const auto mechanics = spell().toSpell()->battleMechanics(&event);
		ASSERT_TRUE(newHorizonsNaturesWrath::enabled(*mechanics)) << "Requires shipped active spell admission";
		ASSERT_TRUE(mechanics->canBeCastAt({spells::Destination(first)}));
	}

	bool cast(const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void damage(CStack * unit, int64_t amount)
	{
		auto state = unit->acquireState();
		state->damage(amount);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		update.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(update);
	}

	void lock(CStack * unit)
	{
		Bonus bonus(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE, BonusSource::SPELL_EFFECT,
			100, BonusSourceID(SpellID(SpellID::decode("new-horizons:spellLock"))));
		bonus.turnsRemain = 2;
		unit->addNewBonus(std::make_shared<Bonus>(bonus));
	}
};
