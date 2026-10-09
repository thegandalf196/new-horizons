/*
 * NewHorizonsPandemoniumFixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/spells/NewHorizonsPandemonium.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../lib/battle/NewHorizonsOffense.h"

class NewHorizonsPandemoniumFixture : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * enemy = nullptr;
	CStack * friendly = nullptr;
	CStack * clean = nullptr;
	static SpellID spell() { return SpellID(SpellID::decode(std::string(newHorizonsPandemonium::SPELL_KEY))); }

	void prepare(bool master = false, int friendlyCount = 1000)
	{
		startGame();
		ASSERT_TRUE(spell().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(spell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
		ASSERT_TRUE(chaos.hasValue());
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(master)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.pandemoniumMaster"});
			ASSERT_TRUE(newHorizonsPandemonium::hasMaster(attackerSideHero));
		}
		setTestSpellPointTotal(attackerSideHero, 1000);
		startTerrainBattle(TerrainId::GRASS);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), friendlyCount);
		clean = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 8), friendlyCount);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(clean, nullptr);
		beginCombat();
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
		const auto mechanics = spell().toSpell()->battleMechanics(&event);
		ASSERT_TRUE(newHorizonsPandemonium::enabled(*mechanics)) << "Requires shipped active admission";
	}

	static std::shared_ptr<Bonus> status(const std::string & identity, BonusType type = BonusType::STACKS_SPEED,
		SpellID source = SpellID::SLOW)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
			-1, BonusSourceID(source));
		bonus->turnsRemain = 3;
		bonus->statusTags = {BonusStatusTag::DEBUFF};
		bonus->statusIdentity = identity;
		return bonus;
	}

	bool cast()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell();
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void poison(CStack * unit, int64_t damage = 7, int turns = 3)
	{
		auto state = unit->acquireState();
		state->physicalPoisonBaseDamage = damage;
		state->physicalPoisonActivationsRemaining = turns;
		state->physicalPoisonSourceStackId = -1;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		update.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(update);
	}
};
