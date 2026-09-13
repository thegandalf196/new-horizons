/*
 * HypotheticTimedSpellTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

namespace
{
class TimedEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TimedEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class HypotheticTimedSpellTest : public HeroCommandFixture
{
protected:
	CStack * unit = nullptr;
	std::shared_ptr<TimedEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareTimedBattle(bool opening = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		if(!opening)
			ASSERT_NO_FATAL_FAILURE(beginCombat());
		unit = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 2);
		environment = std::make_shared<TimedEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}

	Bonus haste(int turns = 1)
	{
		Bonus bonus;
		bonus.type = BonusType::STACKS_SPEED;
		bonus.val = 3;
		bonus.source = BonusSource::SPELL_EFFECT;
		bonus.sid = BonusSourceID(SpellID(SpellID::HASTE));
		bonus.duration = BonusDuration::N_TURNS;
		bonus.turnsRemain = turns;
		return bonus;
	}
};

TEST_F(HypotheticTimedSpellTest, OriginalSpellExpiresInModelWithoutChangingLiveDurationOrPermanentBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepareTimedBattle());
	const auto speed = unit->getMovementRange();
	auto timed = std::make_shared<Bonus>(haste());
	unit->addNewBonus(timed);
	Bonus permanent = haste();
	permanent.duration = BonusDuration::ONE_BATTLE;
	permanent.source = BonusSource::HERO_COMMAND;
	permanent.val = 2;
	unit->addNewBonus(std::make_shared<Bonus>(permanent));
	HypotheticBattle model(environment.get(), callback);
	ASSERT_EQ(model.battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 5);
	const auto liveRound = battle()->battleGetRound();
	model.nextRound();
	EXPECT_EQ(model.battleGetRound(), liveRound + 1);
	EXPECT_EQ(model.battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 2);
	EXPECT_EQ(unit->getMovementRange(), speed + 5);
	EXPECT_EQ(timed->turnsRemain, 1);
	EXPECT_EQ(battle()->battleGetRound(), liveRound);
}

TEST_F(HypotheticTimedSpellTest, NestedAddedAndUpdatedSpellsExpireWithoutResurrectingOnRepeatedQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepareTimedBattle());
	const auto speed = unit->getMovementRange();
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	parent->addUnitBonus(unit->unitId(), {haste()});
	Bonus slow = haste();
	slow.sid = BonusSourceID(SpellID(SpellID::SLOW));
	slow.val = -1;
	parent->updateUnitBonus(unit->unitId(), {slow});
	ASSERT_EQ(parent->battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 2);
	HypotheticBattle child(environment.get(), parent);
	child.nextRound();
	for(int query = 0; query < 3; ++query)
	{
		const auto * projected = child.battleGetUnitByID(unit->unitId());
		EXPECT_TRUE(projected->getAllBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT))->empty());
		EXPECT_EQ(projected->getMovementRange(), speed);
	}
	EXPECT_EQ(parent->battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 2);
	EXPECT_EQ(unit->getMovementRange(), speed);

	HypotheticBattle dispelled(environment.get(), parent);
	dispelled.getForUpdate(unit->unitId())->removeUnitBonus(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	for(int query = 0; query < 3; ++query)
	{
		const auto * projected = dispelled.battleGetUnitByID(unit->unitId());
		EXPECT_TRUE(projected->getAllBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT))->empty());
		EXPECT_EQ(projected->getMovementRange(), speed);
	}
	dispelled.nextRound();
	EXPECT_EQ(dispelled.battleGetUnitByID(unit->unitId())->getMovementRange(), speed);
	EXPECT_EQ(parent->battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 2);
}

TEST_F(HypotheticTimedSpellTest, OpeningRoundDoesNotShortenOneTurnSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepareTimedBattle(true));
	ASSERT_EQ(battle()->battleGetRound(), 0);
	const auto speed = unit->getMovementRange();
	unit->addNewBonus(std::make_shared<Bonus>(haste()));
	HypotheticBattle model(environment.get(), callback);
	model.nextRound();
	EXPECT_EQ(model.battleGetRound(), 1);
	EXPECT_EQ(model.battleGetUnitByID(unit->unitId())->getMovementRange(), speed + 3);
	model.nextRound();
	EXPECT_EQ(model.battleGetRound(), 2);
	EXPECT_EQ(model.battleGetUnitByID(unit->unitId())->getMovementRange(), speed);
	EXPECT_EQ(unit->getMovementRange(), speed + 3);
}

TEST_F(HypotheticTimedSpellTest, RemovedRecipientStillAgesTimedSpellAndCommandBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepareTimedBattle());
	unit->addNewBonus(std::make_shared<Bonus>(haste()));
	HypotheticBattle model(environment.get(), callback);
	Bonus command = haste();
	command.source = BonusSource::HERO_COMMAND;
	model.addUnitBonus(unit->unitId(), {command});
	model.removeUnit(unit->unitId());
	ASSERT_TRUE(model.battleGetUnitByID(unit->unitId())->isGhost());
	model.nextRound();
	EXPECT_TRUE(model.battleGetUnitByID(unit->unitId())->getAllBonuses(Bonus::NTurns)->empty());
	EXPECT_FALSE(unit->getAllBonuses(Bonus::NTurns)->empty());
	EXPECT_TRUE(unit->alive());
}
