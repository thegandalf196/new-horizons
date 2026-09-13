/*
 * DamageReductionControlTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

namespace
{
class DamageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DamageEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class DamageReductionControlTest : public HeroCommandFixture {};

TEST_F(DamageReductionControlTest, ImplicitDamageMeasurementUsesAnOpponentOfTheCurrentController)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * own = addStack(BattleSide::ATTACKER, creatureByName("core:stoneGolem"), BattleHex(3, 5), 100);
	auto * subject = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(8, 5), 100);
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != own && unit != subject && unit != other)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto environment = std::make_shared<DamageEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto measure = [&](const battle::Unit * opponent)
	{
		return AttackPossibility::calculateDamageReduce(opponent, subject, 5, cache, model);
	};
	const auto againstOwn = measure(own);
	const auto againstOther = measure(other);
	ASSERT_NE(againstOwn, againstOther); // Distinct defenses make the wrong peer observable.
	EXPECT_FLOAT_EQ(measure(nullptr), againstOther);
	auto control = std::make_shared<Bonus>();
	control->type = BonusType::HYPNOTIZED;
	control->duration = BonusDuration::ONE_BATTLE;
	subject->addNewBonus(control);
	ASSERT_EQ(model->battleGetOwner(subject), PlayerColor(1));
	EXPECT_FLOAT_EQ(measure(nullptr), againstOwn);
	subject->removeBonus(control);
	ASSERT_EQ(model->battleGetOwner(subject), PlayerColor(0));
	EXPECT_FLOAT_EQ(measure(nullptr), againstOther);
	EXPECT_EQ(subject->getAvailableHealth(), subject->getTotalHealth());
}
