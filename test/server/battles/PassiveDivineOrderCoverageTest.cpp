/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/PassiveDivineMandate.h"
#include "../../../lib/battle/HeroOrderEffects.h"
#include "../../../lib/battle/HeroCommandRoyalStandard.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CStack.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>

namespace
{
class DivineCoverageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DivineCoverageEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class PassiveDivineOrderCoverageTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * other = nullptr;
	void prepare()
	{
		useRevisedOrders = true;
		prepareCommands();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 10);
		other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(73), 10);
	}
	HeroOrderState order(HeroCommand command)
	{
		HeroOrderState result;
		result.command = command;
		result.issuedRound = battle()->getRound();
		result.passiveDivineMandate = newHorizonsPassiveDivineMandate::Policy{
			2, static_cast<uint16_t>(newHorizonsPassiveDivineMandate::Perk::RIGHTEOUS_DISCIPLINE)};
		return result;
	}
	HeroOrderState vanguard(CStack * target, int duration, bool withRiders = true)
	{
		auto state = order(HeroCommand::VANGUARD);
		if(!withRiders)
			state.passiveDivineMandate->perks = 0;
		state.revision.emplace();
		state.revision->recipientUnitIds = {target->unitId()};
		state.revision->vanguardStatBonus = 1;
		state.revision->vanguardInitiativePercent = 5;
		state.revision->expiresAfterRound = state.issuedRound + duration - 1;
		state.revision->potencyFinalized = true;
		install(state);
		heroCommands::applyIssuedOrderGrants(*battle(),
			heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, state));
		return state;
	}
	void install(const HeroOrderState & value)
	{
		value.validateShape();
		battle()->setHeroOrderStates(BattleSide::ATTACKER, {value});
	}
};

TEST_F(PassiveDivineOrderCoverageTest, ConsumedChargeRemovesDerivedRetaliationWithoutOrdinaryCacheLatch)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	auto state = order(HeroCommand::CHARGE);
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	state.consumedUnitIds = {friendly->unitId()};
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
	EXPECT_EQ(other->counterAttacks.total(), baseline + 1);
}

TEST_F(PassiveDivineOrderCoverageTest, BrokenHoldCannotRearmDerivedRetaliationAtOriginalAnchor)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	auto state = order(HeroCommand::HOLD_THE_LINE);
	state.anchors = {{friendly->unitId(), friendly->getPosition().toInt()}};
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	state.holdBrokenUnitIds = {friendly->unitId()};
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
	EXPECT_EQ(newHorizonsPassiveDivineMandate::activeOrderRiders(*battle(), friendly).extraRetaliations, 0);
}

TEST_F(PassiveDivineOrderCoverageTest, SecondWindCoversOnlyItsActiveRecipient)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	auto state = order(HeroCommand::SECOND_WIND);
	state.primaryTargetUnitId = friendly->unitId();
	state.secondWindActive = true;
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	EXPECT_EQ(other->counterAttacks.total(), baseline);
	state.secondWindActive = false;
	install(state);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
}

TEST_F(PassiveDivineOrderCoverageTest, OrdinaryExpiryRemovesAllDerivedRetaliations)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	install(order(HeroCommand::RIPOSTE));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	advanceRound();
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
}

TEST_F(PassiveDivineOrderCoverageTest, HypotheticFallbackUsesCurrentBranchCoverageAndPreservesNativeParent)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	auto state = order(HeroCommand::CHARGE);
	install(state);
	DivineCoverageEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto fallback = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projected = fallback->getForUpdate(friendly->unitId());
	EXPECT_EQ(projected->counterAttacks.total(), baseline + 1);
	state.consumedUnitIds = {friendly->unitId()};
	fallback->setHeroOrderState(BattleSide::ATTACKER, state);
	EXPECT_EQ(projected->counterAttacks.total(), baseline);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
}

TEST_F(PassiveDivineOrderCoverageTest, PausedVanguardRetainsCapturedRiderAfterSideSnapshotExpires)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	vanguard(friendly, 1);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	auto stasis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(stasis);
	advanceRound();
	ASSERT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD));
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(friendly));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	friendly->removeBonus(stasis);
	advanceRound();
	EXPECT_FALSE(heroCommands::activeVanguardBonuses(friendly));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
}

TEST_F(PassiveDivineOrderCoverageTest, PausedVanguardKeepsOriginalRiderAcrossReplacementElsewhere)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	vanguard(friendly, 3);
	auto stasis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(stasis);
	auto refresh = vanguard(other, 1, false);
	refresh.passiveDivineMandate->perks = static_cast<uint16_t>(newHorizonsPassiveDivineMandate::Perk::SACRED_COMMAND);
	install(refresh);
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, refresh));
	EXPECT_EQ(newHorizonsPassiveDivineMandate::activeOrderRiders(*battle(), friendly).morale, 0);
	EXPECT_EQ(newHorizonsPassiveDivineMandate::activeOrderRiders(*battle(), other).morale, 1);
	EXPECT_FALSE(heroCommands::hasVanguardRoyalStandardEntitlement(other, BattleSide::ATTACKER));
	advanceRound();
	ASSERT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	EXPECT_EQ(other->counterAttacks.total(), baseline);
}

TEST_F(PassiveDivineOrderCoverageTest, RecastWithoutRiderRemovesEarlierNativeMarker)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	vanguard(friendly, 3);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	vanguard(friendly, 1, false);
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(friendly));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
}

TEST_F(PassiveDivineOrderCoverageTest, VanguardEntitlementUsesCapturedEffectiveOwningSide)
{
	prepare();
	const auto baseline = friendly->counterAttacks.total();
	vanguard(friendly, 3);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
	auto hypnosis = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(hypnosis);
	ASSERT_EQ(battle()->battleGetOwner(friendly), PlayerColor(1));
	EXPECT_EQ(friendly->counterAttacks.total(), baseline);
	friendly->removeBonus(hypnosis);
	EXPECT_EQ(friendly->counterAttacks.total(), baseline + 1);
}
