/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/entities/hero/NewHorizonsAerialSupremacy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class NewHorizonsAerialSupremacyBattleTest : public BattleTestFixture
{
protected:
	bool captured = true;
	CStack * griffin = nullptr;
	CStack * shooter = nullptr;
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured) rules["additionalSpecialties"].Struct().erase("aerialSupremacy");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(1))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:edric")));
	}
	void prepare(bool royal = false)
	{
		startGame(); startBattle();
		BattleUnitsChanged remove;
		remove.battleID = battle()->getBattleID();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1);
		griffin = addStack(BattleSide::DEFENDER, creatureByName(royal ? "core:royalGriffin" : "core:griffin"), BattleHex(10, 5), 100);
		for(auto * unit : {shooter, griffin})
		{
			forceMaximumDamage(unit);
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		}
		beginCombat();
	}
	bool shoot()
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0),
			BattleAction::makeShotAttack(shooter, griffin));
	}
};
