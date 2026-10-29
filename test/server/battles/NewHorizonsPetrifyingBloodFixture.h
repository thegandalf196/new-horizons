/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsPetrifyingBlood.h"
#include "../../../lib/entities/hero/NewHorizonsPetrifyingBlood.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
class NewHorizonsPetrifyingBloodBattleTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool bron = true;
	CStack * basilisk = nullptr;
	CStack * enemy = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(captured)
			rules["petrifyingBloodSpecialty"]["version"].Integer() = 1;
		else
			rules.Struct().erase("petrifyingBloodSpecialty");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(bron ? "core:bron" : "core:coronius")));
	}
	void prepare(bool greater = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		basilisk = addStack(BattleSide::ATTACKER,
			CreatureID(greater ? CreatureID::decode("core:greaterBasilisk") : CreatureID::decode("core:basilisk")), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 10);
		addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:peasant")), BattleHex(3, 2), 1);
		addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:peasant")), BattleHex(12, 8), 1);
		basilisk->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
		forceMaximumDamage(enemy);
		for(auto * unit : {basilisk, enemy})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
		battle()->activeStack = enemy->unitId();
	}
	void addChance(int amount)
	{
		basilisk->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_AFTER_ATTACK, BonusSource::OTHER, amount, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::STONE_GAZE))));
	}
	bool petrified(const battle::Unit & unit) const
	{
		return unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::STONE_GAZE)))
			.And(Selector::type()(BonusType::NOT_ACTIVE)));
	}
	void nextRound()
	{
		BattleNextRound next;
		next.battleID = battle()->getBattleID();
		gameHandler->sendAndApply(next);
		battle()->activeStack = enemy->unitId();
	}
};
