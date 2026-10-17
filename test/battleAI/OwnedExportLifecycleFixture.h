/*
 * OwnedExportLifecycleFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "OwnedExportMeleeFixture.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

class OwnedExportLifecycleTest : public OwnedExportMeleeTest
{
protected:
	struct FrozenUnit { uint32_t id; JsonNode saved; int attack; unsigned speed; };
	struct Frame
	{
		int round;
		std::vector<FrozenUnit> units;
		BattleSideArray<SylvanLuckState> fortune;
	};
	Frame freezeFrame()
	{
		Frame value{known->getRound(), {}, {}};
		for(const auto * unit : known->getUnitsIf([](const battle::Unit *) { return true; }))
			value.units.push_back({unit->unitId(), unit->acquireState()->save(), unit->getAttack(false), unit->getMovementRange(0)});
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			value.fortune.at(side) = known->getSylvanLuckState(side);
		return value;
	}
	void retireFrame()
	{
		known.reset();
		seed.reset();
	}
	void compareFrame(const Frame & value)
	{
		EXPECT_EQ(battle()->getRound(), value.round);
		for(const auto & unit : value.units)
		{
			const auto * actual = battle()->battleGetUnitByID(unit.id);
			ASSERT_NE(actual, nullptr);
			EXPECT_EQ(actual->acquireState()->save(), unit.saved);
			EXPECT_EQ(actual->getAttack(false), unit.attack);
			EXPECT_EQ(actual->getMovementRange(0), unit.speed);
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			EXPECT_EQ(projectedHitTest::serialized(battle()->getSylvanLuckState(side)), projectedHitTest::serialized(value.fortune.at(side)));
	}
	void roundPacket()
	{
		BattleNextRound event;
		event.battleID = BattleID(0);
		gameHandler->sendAndApply(event);
	}
	void turnPacket(BattleUnitTurnReason reason)
	{
		BattleSetActiveStack event;
		event.battleID = BattleID(0);
		event.stack = incoming->unitId();
		event.reason = reason;
		gameHandler->sendAndApply(event);
	}
	void checkTurn(BattleUnitTurnReason reason)
	{
		incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
			BonusType::STACKS_SPEED, BonusSource::OTHER, -2, BonusSourceID()));
		incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN,
			BonusType::PRIMARY_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
		captureOwned();
		startOwned();
		ASSERT_EQ(known->getMoraleExtraDamagePercent(), battle()->getMoraleExtraDamagePercent());
		known->nextTurn(incoming->unitId(), reason);
		if(reason == BattleUnitTurnReason::MORALE)
		{
			EXPECT_TRUE(known->getForUpdate(incoming->unitId())->hadMorale);
			EXPECT_EQ(known->getForUpdate(incoming->unitId())->moraleExtraActivation,
				battle()->getMoraleExtraDamagePercent() != 100);
		}
		const auto frozen = freezeFrame();
		retireFrame(); // No query of borrowed original-world providers after dispatch.
		turnPacket(reason);
		compareFrame(frozen);
	}
};
