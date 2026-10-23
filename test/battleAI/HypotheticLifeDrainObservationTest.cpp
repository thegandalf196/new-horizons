/*
 * HypotheticLifeDrainObservationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/InitialExportWorld.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
namespace
{
class ObservationEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ObservationEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return state.get(); }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
};
class HypotheticLifeDrainObservationTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<ObservationEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;
	std::unique_ptr<HypotheticBattle> model;
	uint32_t id = 0;
	JsonNode original;
	void prepare(bool bloodrage = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		if(bloodrage)
		{
			// Conditional invariant fixture, not an ordinary authored hero profile.
			battle()->getSide(BattleSide::ATTACKER).bloodrageRank = 1;
			battle()->getSide(BattleSide::ATTACKER).bloodrageCapPercent = 100;
		}
		id = battle()->battleActiveUnit()->unitId();
		environment = std::make_unique<ObservationEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		model = std::make_unique<HypotheticBattle>(environment.get(), callback);
		original = model->battleGetUnitByID(id)->acquireState()->save();
	}
};
}
TEST_F(HypotheticLifeDrainObservationTest, ActualUnchangedLoadAndZeroUpdatesCertifyWithoutMutatingSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->updateUnit(id, original, 0);
	EXPECT_TRUE(model->finishLifeDrainFriendlyObservation());
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	EXPECT_TRUE(model->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), original);
}
TEST_F(HypotheticLifeDrainObservationTest, ZeroDeltaWithChangedActualTypedStateRejectsButStillAppliesLoad)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto changed = model->battleGetUnitByID(id)->acquireState();
	int64_t damage = 1;
	changed->damage(damage);
	const auto injured = changed->save();
	ASSERT_NE(injured, original);
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->updateUnit(id, injured, 0);
	EXPECT_EQ(model->battleGetUnitByID(id)->acquireState()->save(), injured);
	model->updateUnit(id, original, 0);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(model->battleGetUnitByID(id)->acquireState()->save(), original);
}
TEST_F(HypotheticLifeDrainObservationTest, NonzeroDeltaAndMutableAccessInvalidateEvenWithEqualState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->updateUnit(id, original, 1);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->getForUpdate(id);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
}
TEST_F(HypotheticLifeDrainObservationTest, ExplicitBonusOrMovementMutationNeverCertifies)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->addUnitBonus(id, {});
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->moveUnit(id, model->battleGetUnitByID(id)->getPosition());
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
}
TEST_F(HypotheticLifeDrainObservationTest, FormAndFormViewChangesCannotHideBehindEqualJson)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto held = model->getForUpdate(id);
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	const auto revision = held->getBattleFormViewRevision();
	// Public base access still dispatches to the actual projected-unit override.
	static_cast<battle::CUnitState &>(*held).onBattleFormChanged();
	EXPECT_NE(held->getBattleFormViewRevision(), revision);
	EXPECT_EQ(held->save(), original);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	held->beginBattleForm(held->unitType()->getId(), 1);
	EXPECT_FALSE(model->beginLifeDrainFriendlyObservation(id));
}
TEST_F(HypotheticLifeDrainObservationTest, LatentDestroyedMembershipEraseRejectsEqualAliveLoad)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto dead = model->battleGetUnitByID(id)->acquireState();
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage);
	ASSERT_FALSE(dead->alive());
	model->updateUnit(id, dead->save(), -damage);
	// Recreate a conditional stale bookkeeping state: alive load without the
	// owner transition. Its later erase must not be mistaken for a no-op.
	const auto held = model->getForUpdate(id);
	held->load(original);
	ASSERT_TRUE(held->alive());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->updateUnit(id, original, 0);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(held->save(), original);
}
TEST_F(HypotheticLifeDrainObservationTest, InvalidNestedCancelledAndFailedLoadScopesFailClosed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(model->beginLifeDrainFriendlyObservation(std::numeric_limits<uint32_t>::max()));
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	EXPECT_FALSE(model->beginLifeDrainFriendlyObservation(id));
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->cancelLifeDrainFriendlyObservation();
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	auto malformed = original;
	malformed["state"]["frozenAppliedRound"].Integer() = -2;
	EXPECT_THROW(model->updateUnit(id, malformed, 0), std::runtime_error);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
}

TEST_F(HypotheticLifeDrainObservationTest, CapacityTrackingChangeRejectsEvenWithEqualAggregateHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto changed = model->battleGetUnitByID(id)->acquireState();
	const auto health = changed->getAvailableHealth();
	changed->preserveCreatureHealthOnCapacityIncrease();
	ASSERT_EQ(changed->getAvailableHealth(), health);
	const auto tracked = changed->save();
	ASSERT_NE(tracked, original);
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->updateUnit(id, tracked, 0);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(model->battleGetUnitByID(id)->getAvailableHealth(), health);
}
TEST_F(HypotheticLifeDrainObservationTest, OwnedReadOnlyRosterMaterializationDoesNotInvalidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto authorized = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	ASSERT_EQ(authorized->getBattle(), battle());
	ASSERT_EQ(authorized->battleGetMySide(), BattleSide::ALL_KNOWING);
	const auto exported = InitialExportBattleSubject::capture(authorized, *battle());
	model = std::make_unique<HypotheticBattle>(environment.get(), exported);
	ASSERT_TRUE(model->initialExportSubject());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	EXPECT_FALSE(model->getUnitsIf([](const battle::Unit *) { return true; }).empty());
	EXPECT_TRUE(model->finishLifeDrainFriendlyObservation());
}

TEST_F(HypotheticLifeDrainObservationTest, FailedExchangeAttemptAndRemovalInvalidateWithoutTouchingSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle::BattleEffectExchange invalid;
	invalid.endpoints[0].id = invalid.endpoints[1].id = id;
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	EXPECT_THROW(model->exchangeBattleEffects(invalid), std::runtime_error);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(model->beginLifeDrainFriendlyObservation(id));
	model->removeUnit(id);
	EXPECT_FALSE(model->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), original);
}
