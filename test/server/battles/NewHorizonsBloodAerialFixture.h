/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "NewHorizonsPetrifyingBloodFixture.h"
#include "../../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleAction.h"
class NewHorizonsBloodAerialFixture : public NewHorizonsPetrifyingBloodBattleTest
{
protected:
	CStack * shooter = nullptr;
	CStack * griffin = nullptr;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsPetrifyingBloodBattleTest::mapLoaded(map);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(1))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:edric")));
	}
	void prepareBoundary(bool extraShot)
	{
		startGame(); startBattle();
		BattleUnitsChanged remove;
		remove.battleID = battle()->getBattleID();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 100);
		griffin = addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(10, 5), 100);
		basilisk = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:basilisk")), BattleHex(6, 4), 100);
		enemy = griffin;
		addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:peasant")), BattleHex(12, 8), 1);
		for(auto * unit : {shooter, griffin, basilisk})
		{
			forceMaximumDamage(unit);
			for(auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
					BonusSource::OTHER, 1, BonusSourceID()));
		}
		griffin->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
		if(extraShot)
			shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
		addChance(80); // Ordinary native20 plus fixture80, never production100.
		beginCombat();
		const auto shot = battle()->calculateDmgRange(BattleAttackInfo(shooter, griffin, 0, true)).damage.max;
		ASSERT_GT(shot, 0);
		ASSERT_GT(griffin->getAvailableHealth(), shot + 1);
		int64_t wound = griffin->getAvailableHealth() - shot - 1;
		griffin->damage(wound);
		const auto plan = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
		ASSERT_TRUE(plan);
		const auto contact = battle()->getAerialAttackedBattleUnits(*plan).first;
		ASSERT_TRUE(std::ranges::any_of(contact, [&](const auto * unit) { return unit->unitId() == basilisk->unitId(); }));
	}
	bool shootBoundary()
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0),
			BattleAction::makeShotAttack(shooter, griffin));
	}
};
