/*
 * NewHorizonsRebirthChainFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "HeroCommandFixture.h"
#include "../../../lib/CStack.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/modding/CModHandler.h"

class NewHorizonsRebirthChainFixture : public HeroCommandFixture
{
protected:
	static constexpr auto SKILL = "new-horizons:elementalRebirth";
	static constexpr auto CHAIN = "new-horizons:elementalRebirth.rebirthChain";
	CStack * source = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool registered = false;
		for(const auto & perk : perks["skills"][SKILL]["perks"].Vector())
			if(perk["id"].String() == CHAIN)
			{
				ASSERT_EQ(perk["effect"]["status"].String(), "active")
					<< "Chain must be active in the shipped registry";
				registered = true;
			}
		ASSERT_TRUE(registered) << "Chain must exist in the shipped registry";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	static CreatureID creature(const char * id)
	{
		return CreatureID(CreatureID::decode(id));
	}

	void prepare(bool selectChain = true)
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill skill(SecondarySkill::decode(SKILL));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, "new-horizons:elementalRebirth.primalBurst"});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selectChain)
			defenderSideHero->applyPerkSelection({SKILL, CHAIN});
		ASSERT_EQ(defenderSideHero->hasActivePerk(SKILL, CHAIN), selectChain);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:peasant"), 101));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature("core:peasant"), 101));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				enemy = battle()->getStack(unit->unitId(), false);
		}
		ASSERT_NE(source, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	void injure(CStack * target, int64_t damage)
	{
		BattleStackAttacked hit;
		hit.attackerID = enemy->unitId();
		hit.stackAttacked = target->unitId();
		hit.damageAmount = damage;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), target->acquireState());
		StacksInjured pack;
		pack.battleID = BattleID(0);
		pack.stacks.push_back(std::move(hit));
		gameHandler->sendAndApply(pack);
	}

	CStack * firstReborn()
	{
		injure(source, source->getAvailableHealth());
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->alive() && unit->getRebirthOriginalAggregateHP() > 0)
				return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}

	BattleUnitsChanged candidate(CStack * first)
	{
		const auto pool = newHorizonsElementalRebirth::legalCandidatePool(
			battle()->getCreatureCategoryRules(), battle()->getAccessibility(), first->getPosition(), first->unitSide());
		if(pool.empty())
			throw std::runtime_error("No Chain candidate in fixture");
		const auto type = pool.front();
		const auto maxHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
			battle()->getSideArmy(first->unitSide()), type, first->unitOwner(), first->unitSide());
		auto output = newHorizonsElementalRebirth::makeSpawnDescriptor(battle()->battleNextUnitId(),
			type, first->unitSide(), first->getPosition(), first->getRebirthOriginalAggregateHP() / 4, maxHP, true);
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
			first->unitSide(), first->unitId(), output->unit.id};
		add.changedStacks.emplace_back(output->unit.id, UnitChanges::EOperation::ADD);
		output->unit.save(add.changedStacks.back().data);
		return add;
	}
};
