/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastSideTestFixture.h"
#include "../../AI/BattleAI/ForecastSpellPayment.h"
#include "../../AI/BattleAI/ForecastSideTransitions.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsSpellcraft.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/ScopeGuard.h"
namespace
{
using namespace forecast;
using namespace forecastSideTest;
uint64_t open(SpellPaymentScopes & scopes, const SideState * state, SpellEvaluationKind kind,
	BattleSide side, SpellID spell, int32_t cost, bool admitted, std::optional<uint64_t> epoch)
{
	const auto token = scopes.begin(kind, side, spell, epoch);
	try { scopes.admit(token, state, cost, admitted); }
	catch(...) { scopes.finish(token, false); throw; }
	return token;
}

TEST_F(ForecastSideStateTest, NestedAutomaticReflectedAndFreeScopesCannotRefundAnOuterManualCast)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const auto state = side(); SpellPaymentScopes scopes;
	const auto outer = open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 12, true, 7);
	ASSERT_TRUE(scopes.queueRefund(SIDE, {3, 0, 0}));
	const auto automatic = open(scopes, state.get(), SpellEvaluationKind::AUTOMATIC_HERO, SIDE, SpellID::HASTE, 0, true, {});
	EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	const auto reflected = open(scopes, nullptr, SpellEvaluationKind::OTHER, BattleSide::NONE, SpellID::NONE, 0, true, {});
	EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	EXPECT_FALSE(scopes.finish(reflected, true)); EXPECT_FALSE(scopes.finish(automatic, true));
	ASSERT_TRUE(scopes.queueRefund(SIDE, {0, 5, 4}));
	EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	const auto ticket = scopes.finish(outer, true);
	ASSERT_TRUE(ticket); EXPECT_EQ(ticket->exactCost, 12); EXPECT_EQ(ticket->actionEpoch, 7u);
	EXPECT_TRUE(ticket->evaluated); ASSERT_EQ(ticket->refunds.size(), 2u);
	EXPECT_TRUE(scopes.empty()); EXPECT_TRUE(scopes.healthy()); EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	EXPECT_EQ(state->mana->pools.getTotal(), 59); EXPECT_TRUE(state->castCharges.empty());
}
TEST_F(ForecastSideStateTest, ExceptionAndRejectedScopeCleanUpWithoutPaymentOrAnOuterContextLeak)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const auto state = side(); SpellPaymentScopes scopes;
	EXPECT_THROW(( [&]()
	{
		const auto token = open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 12, true, 4);
		const auto cleanup = vstd::makeScopeGuard([&]() { scopes.finish(token, false); });
		throw std::runtime_error("rejected effect body");
	})(), std::runtime_error);
	EXPECT_TRUE(scopes.empty()); EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	EXPECT_THROW(open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 60, true, 4), std::invalid_argument);
	EXPECT_THROW(open(scopes, state.get(), SpellEvaluationKind::AUTOMATIC_HERO, SIDE, SpellID::HASTE, 1, true, {}), std::invalid_argument);
	EXPECT_THROW(open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 1, false, 4), std::invalid_argument);
	EXPECT_TRUE(scopes.empty()); EXPECT_EQ(state->mana->pools.getTotal(), 59);
}

TEST_F(ForecastSideStateTest, ResidualDamageStaysWithItsAcceptedManualScope)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const auto state = side();
	SpellPaymentScopes scopes;
	const auto canceled = open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 12, true, 1);
	ASSERT_TRUE(scopes.queueResidualLife(SIDE, 123));
	EXPECT_FALSE(scopes.queueResidualLife(BattleSide::DEFENDER, 10));
	EXPECT_FALSE(scopes.finish(canceled, false));
	const auto accepted = open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 12, true, 2);
	ASSERT_TRUE(scopes.queueResidualLife(SIDE, 50));
	const auto ticket = scopes.finish(accepted, true);
	ASSERT_TRUE(ticket);
	EXPECT_EQ(ticket->residualLifeDamage, 50);
	EXPECT_EQ(state->mana->pools.getTotal(), 59);
	EXPECT_FALSE(scopes.queueResidualLife(SIDE, 1));
}
TEST_F(ForecastSideStateTest, ScopedRefundsSettleAtomicallyAgainstOneExactOriginalCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto state = side(); const auto original = state;
	auto consumed = state->ledger->allowances;
	const auto selection = consumed.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, state->round);
	ASSERT_TRUE(selection);
	const auto action = consumed.consumeAllowance(selection->grantId, HeroActionAllowanceState::ActionKind::SPELL, state->round);
	ASSERT_TRUE(action);
	const std::array refund{battle::spellPoints::Payment{3, 5, 4}};
	const auto settled = SideTransitions::acceptedAction(state, *action, consumed, false, 0, {},
		CastCost{SpellID::HASTE, 12, false}, nullptr, refund);
	ASSERT_TRUE(settled); ASSERT_TRUE(SideTransitions::commit(state, *settled));
	EXPECT_EQ(state->mana, original->mana); ASSERT_EQ(state->castCharges.size(), 1u);
	EXPECT_EQ(state->castCharges.front().refunded, refund.front());
	EXPECT_FALSE(SideTransitions::refundCast(state, state->castCharges.front().token, {1, 0, 0}));
	const std::array wrongPool{battle::spellPoints::Payment{4, 0, 0}};
	EXPECT_FALSE(SideTransitions::acceptedAction(original, *action, consumed, false, 0, {},
		CastCost{SpellID::HASTE, 12, false}, nullptr, wrongPool));
	EXPECT_EQ(original->mana->pools.getTotal(), 59); EXPECT_TRUE(original->castCharges.empty());
}
TEST_F(ForecastSideStateTest, RealOverchargedArrowUsesConfiguredMechanicsCostThroughObservationAndPaysOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:airMagic")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto * victim = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 3), 500);
	ASSERT_NE(victim, nullptr);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), state);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	candidate.spellOvercharge = 1; candidate.dest.emplace_back(victim);
	const spells::Target aim{spells::Destination(state->battleGetUnitByID(victim->unitId()))};
	spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, candidate.spell); cast.setOvercharge(1);
	const auto mechanics = cast.mechanicsForTarget(aim);
	const auto exactCost = mechanics->getTargetAwareManaCost(aim);
	ASSERT_GT(exactCost, state->battleGetSpellCost(candidate.spell, attackerSideHero));
	const auto prior = state->getProjectedManaAvailable(SIDE); ASSERT_TRUE(prior);
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	uint32_t targetId = victim->unitId();
	const auto projected = battleEvaluatorProjection::projectSpell(*state, *live, attackerSideHero, SIDE, candidate, admission, targetId, {});
	ASSERT_TRUE(projected.accepted); ASSERT_TRUE(projected.arrow);
	ASSERT_EQ(state->forecastSide(SIDE)->castCharges.size(), 1u);
	EXPECT_EQ(state->forecastSide(SIDE)->castCharges.back().paid.total(), exactCost);
	EXPECT_EQ(state->getProjectedManaAvailable(SIDE), *prior - exactCost);
	EXPECT_EQ(sibling->getProjectedManaAvailable(SIDE), prior);
	EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, SpellID::MAGIC_ARROW, targetId, *admission.spellAllowance));
	EXPECT_EQ(state->getProjectedManaAvailable(SIDE), *prior - exactCost);
}
TEST_F(ForecastSideStateTest, CanceledNativeEvaluationClearsPreflightAndClosedScopeCannotRefundLatestCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(SpellID::BLESS));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::BLESS).toSpell()));
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::BLESS).toSpell();
	candidate.dest.emplace_back(state->battleActiveUnit());
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	const auto cost = candidateSpellPaymentCost(*state, SIDE, candidate); ASSERT_TRUE(cost);
	auto * server = state->getServerCallback();
	const auto token = server->beginSpellEvaluation(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::BLESS);
	server->admitSpellEvaluation(token, *cost, true);
	// A child keeps past resource values, never an active parent refund scope.
	auto child = std::make_shared<HypotheticBattle>(environment.get(), state);
	SetMana childRefund; childRefund.hid = attackerSideHero->id;
	childRefund.setOperation(SetMana::Operation::REFUND_HERO_LUCK, 1);
	EXPECT_THROW(child->getServerCallback()->apply(childRefund), std::invalid_argument);
	server->endSpellEvaluation(token, false);
	EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, SpellID::BLESS, candidate.dest.front().unitValue->unitId(), *admission.spellAllowance));
	const auto before = state->forecastSide(SIDE);
	SetMana refund; refund.hid = attackerSideHero->id; refund.setOperation(SetMana::Operation::REFUND_HERO_LUCK, 1);
	EXPECT_THROW(server->apply(refund), std::invalid_argument);
	EXPECT_EQ(state->forecastSide(SIDE), before);
}
TEST_F(ForecastSideStateTest, StormSubsetPreflightUsesAlreadyConfiguredNativeTargetCost)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const SpellID storm(SpellID::decode("new-horizons:stormOfDaggers"));
	attackerSideHero->addSpellToSpellbook(storm);
	const auto * first = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 3), 100);
	const auto * second = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 7), 100);
	ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = storm.toSpell();
	candidate.spellStormOfDaggers = true; candidate.dest = {spells::Destination(first), spells::Destination(second)};
	spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
	const auto native = cast.mechanicsForTarget(candidate.dest);
	ASSERT_TRUE(native->setStormOfDaggersTargetCount(2)); ASSERT_TRUE(native->canBeCastAt(candidate.dest));
	const auto exact = native->getTargetAwareManaCost(candidate.dest);
	EXPECT_EQ(candidateSpellPaymentCost(*state, SIDE, candidate), exact);
	const auto before = state->forecastSide(SIDE);
	const auto admitted = heroActionProjection::admitCandidate(*state, SIDE, candidate);
	ASSERT_TRUE(admitted.admitted);
	// This synthetic accepted route uses the pre-effect native cost witness.
	ASSERT_TRUE(state->projectAcceptedHeroSpell(SIDE, storm, first->unitId(), *admitted.spellAllowance));
	ASSERT_EQ(state->forecastSide(SIDE)->castCharges.size(), 1u);
	EXPECT_EQ(state->forecastSide(SIDE)->castCharges.front().paid.total(), exact);
	EXPECT_EQ(state->getProjectedManaAvailable(SIDE), before->mana->pools.getTotal() - exact);
}

TEST_F(ForecastSideStateTest, UnaffordableExactNativeCostRejectsBeforeBeginOrSideMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const auto available = attackerSideHero->getManaAvailable();
	ASSERT_GT(available, 0);
	SetMana empty(attackerSideHero->id, SetMana::Operation::SPEND, available);
	gameHandler->sendAndApply(empty);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto before = state->forecastSide(SIDE);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::HASTE).toSpell();
	candidate.dest.emplace_back(state->battleActiveUnit());
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate);
	EXPECT_FALSE(admission.admitted); ASSERT_TRUE(admission.spellAllowance);
	EXPECT_EQ(state->forecastSide(SIDE), before);
	// The rejected payment preflight did not latch an action or run cleanup.
	EXPECT_TRUE(state->beginProjectedHeroAction(SIDE, *admission.spellAllowance));
	EXPECT_TRUE(state->forecastSide(SIDE)->castCharges.empty());
}

TEST_F(ForecastSideStateTest, RejectedCostSpellOrAdmissionCannotReuseSyntheticOrWrongEpochTicket)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(SpellID::BLESS));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::BLESS).toSpell()));
	for(int failure = 0; failure < 3; ++failure)
	{
		auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
		PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::BLESS).toSpell();
		candidate.dest.emplace_back(state->battleActiveUnit());
		const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
		const auto cost = candidateSpellPaymentCost(*state, SIDE, candidate); ASSERT_TRUE(cost);
		const auto before = state->forecastSide(SIDE); auto * server = state->getServerCallback();
		if(failure == 2)
			EXPECT_THROW(server->beginSpellEvaluation(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::SLOW), std::invalid_argument);
		else
		{
			const auto token = server->beginSpellEvaluation(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::BLESS);
			EXPECT_THROW(server->admitSpellEvaluation(token, *cost + (failure == 0 ? 1 : 0), failure != 1), std::invalid_argument);
			server->endSpellEvaluation(token, false);
		}
		auto wrongEpoch = *admission.spellAllowance; ++wrongEpoch.action.epoch;
		EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, SpellID::BLESS, candidate.dest.front().unitValue->unitId(), wrongEpoch));
		EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, SpellID::BLESS, candidate.dest.front().unitValue->unitId(), *admission.spellAllowance));
		EXPECT_FALSE(state->stageProjectedSpellPayment(SIDE, SpellID::BLESS, *cost));
		EXPECT_EQ(state->forecastSide(SIDE), before);
	}
}
TEST_F(ForecastSideStateTest, StormInvalidTargetEarlyReturnCancelsPreflightBeforeAnyNativeBenefit)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	const SpellID storm(SpellID::decode("new-horizons:stormOfDaggers")); attackerSideHero->addSpellToSpellbook(storm);
	const auto * victim = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 3), 100);
	ASSERT_NE(victim, nullptr);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = storm.toSpell(); candidate.dest.emplace_back(victim);
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	const auto before = state->forecastSide(SIDE);
	spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
	const auto mechanics = cast.mechanicsForTarget(candidate.dest);
	mechanics->castEval(state->getServerCallback(), {}); // Native target-count rejection precedes admission.
	EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, storm, victim->unitId(), *admission.spellAllowance));
	EXPECT_FALSE(state->stageProjectedSpellPayment(SIDE, storm, 0));
	EXPECT_EQ(state->forecastSide(SIDE), before);
	EXPECT_EQ(state->getWisdomCombatState(SIDE).priorCasts(storm), 0u);
	EXPECT_FALSE(state->hasCompletedHeroSpellCast(SIDE));
}
TEST_F(ForecastSideStateTest, CaughtExceptionAfterNativeAdmissionCannotFallBackToSyntheticAcceptance)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(SpellID::BLESS));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::BLESS).toSpell()));
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::BLESS).toSpell();
	candidate.dest.emplace_back(state->battleActiveUnit());
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	const auto cost = candidateSpellPaymentCost(*state, SIDE, candidate); ASSERT_TRUE(cost);
	const auto before = state->forecastSide(SIDE); auto * server = state->getServerCallback();
	EXPECT_THROW(([&]()
	{
		const auto token = server->beginSpellEvaluation(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::BLESS);
		const auto cleanup = vstd::makeScopeGuard([&]() { server->endSpellEvaluation(token, false); });
		server->admitSpellEvaluation(token, *cost, true);
		throw std::runtime_error("caught native evaluation failure");
	})(), std::runtime_error);
	EXPECT_FALSE(state->projectAcceptedHeroSpell(SIDE, SpellID::BLESS, candidate.dest.front().unitValue->unitId(), *admission.spellAllowance));
	EXPECT_FALSE(state->stageProjectedSpellPayment(SIDE, SpellID::BLESS, *cost));
	EXPECT_EQ(state->forecastSide(SIDE), before);
}
TEST_F(ForecastSideStateTest, ActualConcentrationDelegationPaysAndCompletesExactlyOneOriginalCast)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(SpellID::BLESS));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::BLESS).toSpell()));
	const std::string skill(newHorizonsMagic::SPELLCRAFT_SKILL);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsSpellcraft::CONCENTRATION)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(newHorizonsSpellcraft::CONCENTRATION)));
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::BLESS).toSpell();
	candidate.dest.emplace_back(state->battleActiveUnit());
	const auto exact = candidateSpellPaymentCost(*state, SIDE, candidate); ASSERT_TRUE(exact);
	const auto before = state->getProjectedManaAvailable(SIDE); ASSERT_TRUE(before);
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	uint32_t id = candidate.dest.front().unitValue->unitId();
	const auto projection = battleEvaluatorProjection::projectSpell(*state, *live, attackerSideHero, SIDE, candidate, admission, id, {});
	ASSERT_TRUE(projection.accepted);
	ASSERT_EQ(state->forecastSide(SIDE)->castCharges.size(), 1u);
	EXPECT_EQ(state->getProjectedManaAvailable(SIDE), *before - *exact);
	EXPECT_EQ(state->getWisdomCombatState(SIDE).priorCasts(SpellID::BLESS), 1u);
	EXPECT_EQ(state->forecastSide(SIDE)->ledger->completedCastCount, 1);
}
TEST_F(ForecastSideStateTest, AcceptedZeroDamageKeepsExistingNativeCompletionClassificationAndExactPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide()); attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	const auto * victim = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 3), 100);
	ASSERT_NE(victim, nullptr);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	state->addUnitBonus(victim->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGICAL_DAMAGE_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID())});
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	candidate.dest.emplace_back(state->battleGetUnitByID(victim->unitId()));
	const auto exact = candidateSpellPaymentCost(*state, SIDE, candidate); ASSERT_TRUE(exact);
	const auto hp = state->battleGetUnitByID(victim->unitId())->getAvailableHealth();
	const auto before = state->getProjectedManaAvailable(SIDE); ASSERT_TRUE(before);
	const auto admission = heroActionProjection::admitCandidate(*state, SIDE, candidate); ASSERT_TRUE(admission.admitted);
	uint32_t id = victim->unitId();
	const auto projected = battleEvaluatorProjection::projectSpell(*state, *live, attackerSideHero, SIDE, candidate, admission, id, {});
	ASSERT_TRUE(projected.accepted);
	EXPECT_EQ(state->battleGetUnitByID(id)->getAvailableHealth(), hp);
	EXPECT_EQ(state->getProjectedManaAvailable(SIDE), *before - *exact);
	EXPECT_TRUE(state->hasCompletedHeroSpellCast(SIDE));
	EXPECT_EQ(state->getWisdomCombatState(SIDE).priorCasts(SpellID::MAGIC_ARROW), 1u);
}

TEST_F(ForecastSideStateTest, UnadmittedOrRejectedAutomaticFrameMasksOuterRefundBeforeCostValidation)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide()); const auto state = side(); SpellPaymentScopes scopes;
	const auto outer = open(scopes, state.get(), SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 12, true, 7);
	const auto automatic = scopes.begin(SpellEvaluationKind::AUTOMATIC_HERO, SIDE, SpellID::HASTE, {});
	EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	EXPECT_THROW(scopes.admit(automatic, state.get(), 1, true), std::invalid_argument);
	EXPECT_FALSE(scopes.queueRefund(SIDE, {1, 0, 0}));
	EXPECT_FALSE(scopes.finish(automatic, false));
	EXPECT_TRUE(scopes.queueRefund(SIDE, {1, 0, 0}));
	const auto ticket = scopes.finish(outer, true); ASSERT_TRUE(ticket); ASSERT_EQ(ticket->refunds.size(), 1u);
}
TEST_F(ForecastSideStateTest, SameSpellAndEpochDoNotAuthorizeDelegationWithoutNativePermit)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide()); const auto state = side(); SpellPaymentScopes scopes;
	const auto outer = scopes.begin(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 7);
	EXPECT_THROW(scopes.begin(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 7), std::invalid_argument);
	scopes.allowDelegation(outer);
	const auto inner = scopes.begin(SpellEvaluationKind::MANUAL_HERO, SIDE, SpellID::HASTE, 7);
	scopes.admit(inner, state.get(), 12, true);
	const auto ticket = scopes.finish(inner, true); ASSERT_TRUE(ticket);
	EXPECT_TRUE(scopes.completedDelegation()); EXPECT_EQ(ticket->exactCost, 12);
	EXPECT_FALSE(scopes.finish(outer, true)); EXPECT_TRUE(scopes.empty());
}

}
