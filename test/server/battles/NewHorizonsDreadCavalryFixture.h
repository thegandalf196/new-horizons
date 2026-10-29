/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsDreadCavalry.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
class NewHorizonsDreadCavalryFixture : public BattleTestFixture
{
protected:
	bool captured = true;
	bool allowRetaliation = false;
	bool defenderCrisis = false;
	int opposingCount = 1000;
	int primaryCount = 1;
	CStack * source = nullptr;
	CStack * victim = nullptr;
	CStack * other = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, combat["combat"]["heroCommands"]);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(captured)
		{
			rules["dreadCavalrySpecialty"]["version"].Integer() = 1;
			rules["dreadCavalrySpecialty"]["followupDamagePercent"].Integer() = 50;
		}
		else
			rules.Struct().erase("dreadCavalrySpecialty");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tamika")));
	}
	void prepare()
	{
		startGame();
		if(defenderCrisis)
		{
			defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsCrisisCommand::SKILL)),
				MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, "new-horizons:command.aggressiveCommander"});
			defenderSideHero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK});
			ASSERT_TRUE(newHorizonsCrisisCommand::eligible(defenderSideHero));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, creatureByName("core:blackKnight"), BattleHex(5, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), primaryCount);
		other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(9, 5), opposingCount);
		for(auto * unit : {source, victim, other})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		forceMaximumDamage(source);
		if(!allowRetaliation)
			blockRetaliation(source);
		beginCombat();
		battle()->activeStack = source->unitId();
	}
	bool killFirst()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(source, victim, BattleHex(6, 5), false));
	}
};
