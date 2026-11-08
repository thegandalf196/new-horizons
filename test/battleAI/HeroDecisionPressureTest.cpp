/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/CRandomGenerator.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/CanonicalOrderValue.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/ScopeGuard.h"
#include "../../AI/BattleAI/ArrowContinuationReuse.h"
#include <limits>
#include <stdexcept>

TEST(HeroDecisionPressureRankingTest, OnlyTwoNearTiedPositiveCandidatesRefineInOriginalOrder)
{
	const std::vector<heroDecisionPressure::Ranked> ranked{{4, 100}, {2, 100}, {7, 99}};
	EXPECT_EQ(heroDecisionPressure::ambiguousRefinements(ranked), (std::vector<size_t>{2, 4}));
}
TEST(HeroDecisionPressureRankingTest, ClearWinnerAndUselessCandidatesDoNotTriggerFullSearch)
{
	EXPECT_TRUE(heroDecisionPressure::ambiguousRefinements({{0, 100}, {1, 79}}).empty());
	EXPECT_TRUE(heroDecisionPressure::ambiguousRefinements({{0, 0}, {1, -1},
		{2, std::numeric_limits<float>::quiet_NaN()}}).empty());
}
class HeroDecisionPressureTest : public ProjectedTargetHitFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		ProjectedTargetHitFixture::mapLoaded(loaded);
	}
	void preparePolicy()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		activate(braced->unitId());
		resetChild();
	}
	std::shared_ptr<HypotheticBattle> accepted(HeroCommand command)
	{
		auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
		PossibleSpellcast candidate;
		candidate.command = command;
		const auto admission = heroActionProjection::admitCandidate(*state, BattleSide::ATTACKER, candidate);
		if(!admission.admitted || !admission.orderEffects || !admission.orderAllowance
			|| !state->projectAcceptedHeroOrder(BattleSide::ATTACKER,
				*admission.orderEffects, *admission.orderAllowance))
			return {};
		return state;
	}
};
TEST_F(HeroDecisionPressureTest, FeintReusesExistingRevisedScorerAndKeepsNativeStateAndAllRng)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	const auto before = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto ambient = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	auto baseline = heroDecisionPressure::Baseline::create(environment.get(), live, BattleSide::ATTACKER);
	ASSERT_TRUE(baseline);
	auto projected = accepted(HeroCommand::FEINT);
	ASSERT_TRUE(projected);
	const auto value = baseline->orderUtility(projected, HeroCommand::FEINT);
	ASSERT_TRUE(value);
	EXPECT_EQ(*value, heroOrderValue::revisedArmyUtility(live, projected,
		BattleSide::ATTACKER, HeroCommand::FEINT));
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), ambient);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
}
TEST_F(HeroDecisionPressureTest, UnknownMovementAndHistoricalCommandsRemainExplicitFallback)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	auto baseline = heroDecisionPressure::Baseline::create(environment.get(), live, BattleSide::ATTACKER);
	ASSERT_TRUE(baseline);
	auto charge = accepted(HeroCommand::CHARGE);
	ASSERT_TRUE(charge);
	EXPECT_FALSE(baseline->orderUtility(charge, HeroCommand::CHARGE));
	EXPECT_FALSE(baseline->orderUtility(charge, HeroCommand::BRACE));
	EXPECT_FALSE(baseline->orderUtility(charge, HeroCommand::PROTECT));
	EXPECT_FALSE(baseline->orderUtility(charge, HeroCommand::FLANK));
}

TEST_F(HeroDecisionPressureTest, FeintCannotValueRetaliationImmuneInitiatingAttacks)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	braced->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	auto baseline = heroDecisionPressure::Baseline::create(environment.get(), live, BattleSide::ATTACKER);
	ASSERT_TRUE(baseline);
	auto projected = accepted(HeroCommand::FEINT);
	ASSERT_TRUE(projected);
	const auto value = baseline->orderUtility(projected, HeroCommand::FEINT);
	ASSERT_TRUE(value);
	EXPECT_EQ(*value, 0.0f);
}

namespace
{
class HeroSourceForeignLimiter final : public ILimiter
{
public:
	mutable unsigned calls = 0;
	EDecision limit(const BonusLimitationContext &) const override
	{
		++calls;
		throw std::runtime_error("foreign Hero source limiter query");
	}
};
}
TEST_F(HeroDecisionPressureTest, ActorBatchSourceRejectsForeignModifierWithoutCallingItsLimiter)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	auto actor = child->getForUpdate(braced->unitId());
	const auto target = child->battleGetUnitByID(incoming->unitId());
	{
		heroActionProjection::CanonicalSourceAdmission known(*actor, *child);
		ASSERT_TRUE(known.admits(*target));
	}
	auto limiter = std::make_shared<HeroSourceForeignLimiter>();
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::OTHER, 1, BonusSourceID());
	bonus.limiter = limiter;
	braced->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto authority = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto ambient = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	heroActionProjection::CanonicalSourceAdmission unknown(*actor, *child);
	EXPECT_FALSE(unknown.admits(*target));
	EXPECT_EQ(limiter->calls, 0u);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), ambient);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
}
TEST_F(HeroDecisionPressureTest, ActorBatchSourceRechecksReboundReceiverBeforeUsingPriorAdmission)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	const auto actor = child->getForUpdate(braced->unitId());
	const auto target = child->battleGetUnitByID(incoming->unitId());
	heroActionProjection::CanonicalSourceAdmission admission(*actor, *child);
	ASSERT_TRUE(admission.admits(*target));
	auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto replacement = std::make_shared<HypotheticBattle>(environment.get(), view);
	static_cast<BattleProxy &>(*child) = static_cast<const BattleProxy &>(*replacement);
	EXPECT_FALSE(child->hasCanonicalDamageQuerySources());
	EXPECT_FALSE(admission.admits(*target));
}

TEST_F(HeroDecisionPressureTest, ExactNativeNeutralArmySourceIsAdmittedForArrowAndDescendant)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	CGCreature neutral(gameState().get());
	incoming->attachToSource(neutral);
	const auto detach = vstd::makeScopeGuard([&] { incoming->detachFromSource(neutral); });
	const auto actor = child->getForUpdate(braced->unitId());
	const auto target = child->getForUpdate(incoming->unitId());
	heroActionProjection::CanonicalSourceAdmission admission(*actor, *child);
	EXPECT_TRUE(admission.admits(*target));
	EXPECT_TRUE(arrowContinuation::sourcesSupported(*child, target->unitId()));
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	EXPECT_TRUE(arrowContinuation::sourcesSupported(*descendant, target->unitId()));
}

namespace
{
class ForeignNeutralSource final : public CGCreature
{
public:
	using CGCreature::CGCreature;
	mutable unsigned queries = 0;
	TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override
	{
		++queries;
		throw std::runtime_error("Foreign neutral bonus query must not run during admission");
	}
};
}
TEST_F(HeroDecisionPressureTest, DerivedNeutralGetterIsRejectedWithoutDispatch)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	ForeignNeutralSource neutral(gameState().get());
	incoming->attachToSource(neutral);
	const auto detach = vstd::makeScopeGuard([&] { incoming->detachFromSource(neutral); });
	EXPECT_FALSE(arrowContinuation::sourcesSupported(*child, incoming->unitId()));
	EXPECT_EQ(neutral.queries, 0u);
}
TEST_F(HeroDecisionPressureTest, ExactNeutralWithForeignWorldCallbackRemainsRejected)
{
	ASSERT_NO_FATAL_FAILURE(preparePolicy());
	CGCreature neutral(nullptr);
	incoming->attachToSource(neutral);
	const auto detach = vstd::makeScopeGuard([&] { incoming->detachFromSource(neutral); });
	EXPECT_FALSE(arrowContinuation::sourcesSupported(*child, incoming->unitId()));
}
