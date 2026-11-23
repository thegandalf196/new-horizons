/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "ForecastSideTestFixture.h"
#include "../../AI/BattleAI/ForecastSideState.h"
#include "../../AI/BattleAI/ForecastSideTransitions.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../mock/mock_battle_IBattleState.h"
namespace
{
using namespace forecast;
using namespace forecastSideTest;

TEST_F(ForecastSideStateTest, VisibleCaptureKeepsExactAdmittedOwnersAndNativeLedgers)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide(true));
	const auto captured = side();
	ASSERT_TRUE(captured->hasVisibleHero());
	EXPECT_EQ(captured->hero, attackerSideHero->id);
	EXPECT_EQ(captured->controller, PlayerColor(0));
	EXPECT_EQ(captured->magicOwner, battle()->getCapturedMagicRules());
	EXPECT_EQ(captured->perkOwner, attackerSideHero->getPerkState().capturedOwner());
	ASSERT_TRUE(captured->ledger);
	EXPECT_EQ(captured->ledger->allowances, battle()->getHeroActionAllowances(SIDE));
	EXPECT_EQ(captured->ledger->wisdom.completedCasts, battle()->getWisdomCombatState(SIDE).completedCasts);
	EXPECT_EQ(captured->ledger->response, battle()->getSpellResponseState(SIDE));
	EXPECT_EQ(captured->ledger->overwhelming, battle()->getOverwhelmingFormulaState(SIDE));
	EXPECT_EQ(captured->attunement, battle()->getElementalAttunementProfile(SIDE));
	EXPECT_EQ(captured->attunementMask, battle()->getElementalAttunementMask(SIDE));
	expectNativeMana(*captured);
	const auto hidden = SideState::capture(*live, BattleSide::DEFENDER);
	ASSERT_TRUE(hidden);
	EXPECT_EQ(hidden->knowledge, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_FALSE(hidden->mana); EXPECT_FALSE(hidden->hero); EXPECT_FALSE(hidden->ledger); EXPECT_FALSE(hidden->perkOwner);
	auto unknown = std::make_shared<const SideState>(*hidden);
	EXPECT_FALSE(SideTransitions::payCast(unknown, {SpellID::HASTE, 0, false}));
	EXPECT_FALSE(SideTransitions::recordWisdom(unknown, SpellID::HASTE, hidden->round));
}

TEST_F(ForecastSideStateTest, PaymentAndReverseRefundMatchAppliedNativePacketsWithTemporaryBuffer)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto parent = side(), childSide = parent, sibling = parent;
	ASSERT_EQ(parent->mana->pools.getBuffer(), 9);
	ASSERT_EQ(parent->mana->temporaryBufferRemaining, 4);
	const auto paid = SideTransitions::payCast(childSide, {SpellID::HASTE, 12, false});
	ASSERT_TRUE(paid); ASSERT_TRUE(paid->chargedCast());
	ASSERT_TRUE(SideTransitions::commit(childSide, *paid));
	const auto charge = childSide->castCharges.back();
	EXPECT_EQ(charge.paid, (battle::spellPoints::Payment{3, 5, 4}));
	SetMana spend(attackerSideHero->id, SetMana::Operation::SPEND, 12);
	gameHandler->sendAndApply(spend);
	expectNativeMana(*childSide);
	const auto nativeRefund = newHorizonsHeroes::SpellPointState::refundForSpent(
		charge.paid.normal, charge.paid.permanentBuffer, charge.paid.temporaryBuffer, 50);
	const battle::spellPoints::Payment refund{nativeRefund.normal, nativeRefund.permanentBuffer, nativeRefund.temporaryBuffer};
	const auto restored = SideTransitions::refundCast(childSide, charge.token, refund);
	ASSERT_TRUE(restored); ASSERT_TRUE(SideTransitions::commit(childSide, *restored));
	SetMana packet;
	packet.hid = attackerSideHero->id;
	packet.setOperation(SetMana::Operation::REFUND_HERO_LUCK, refund.normal, refund.permanentBuffer, refund.temporaryBuffer);
	gameHandler->sendAndApply(packet);
	expectNativeMana(*childSide);
	EXPECT_EQ(parent->mana->pools.getNormal(), 50); EXPECT_EQ(parent->mana->pools.getBuffer(), 9);
	EXPECT_EQ(sibling.get(), parent.get());
	EXPECT_FALSE(SideTransitions::refundCast(childSide, charge.token, refund));
	EXPECT_FALSE(SideTransitions::commit(childSide, *paid));
}

TEST_F(ForecastSideStateTest, AcceptedActionAndPaymentPublishTogetherAndRejectedCostHasNoPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto original = side(), branch = original;
	auto native = original->ledger->allowances;
	const auto selected = native.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, original->round);
	ASSERT_TRUE(selected);
	const auto action = native.consumeAllowance(selected->grantId, HeroActionAllowanceState::ActionKind::SPELL, original->round);
	ASSERT_TRUE(action);
	const auto rejected = SideTransitions::acceptedAction(branch, *action, native, false, 0, {},
		CastCost{SpellID::HASTE, branch->mana->pools.getTotal() + 1, false});
	EXPECT_FALSE(rejected); EXPECT_EQ(branch.get(), original.get());
	const auto accepted = SideTransitions::acceptedAction(branch, *action, native, false, 0, {}, CastCost{SpellID::HASTE, 12, false});
	ASSERT_TRUE(accepted); ASSERT_TRUE(SideTransitions::commit(branch, *accepted));
	EXPECT_EQ(branch->ledger->allowances, native);
	EXPECT_EQ(branch->mana->pools.getTotal(), original->mana->pools.getTotal() - 12);
	EXPECT_EQ(branch->castCharges.size(), 1u);
	EXPECT_FALSE(SideTransitions::acceptedAction(branch, *action, native, false, 0, {}, CastCost{SpellID::HASTE, 12, false}));
	EXPECT_EQ(original->ledger->allowances.remainingCounts(original->round).heroActions, 1u);
}

TEST_F(ForecastSideStateTest, WisdomAutomaticCompletionAndRoundUseNativeValueTransitionsWithoutActionOrManaCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto root = side(), branch = root;
	auto wisdom = root->ledger->wisdom;
	wisdom.recordCompletedCast(SpellID::HASTE, root->round);
	const auto completed = SideTransitions::recordWisdom(branch, SpellID::HASTE, root->round);
	ASSERT_TRUE(completed); ASSERT_TRUE(SideTransitions::commit(branch, *completed));
	EXPECT_EQ(branch->ledger->wisdom.completedCasts, wisdom.completedCasts);
	const auto automatic = SideTransitions::recordCompletion(branch, 1, nullptr, SpellID::HASTE);
	ASSERT_TRUE(automatic); ASSERT_TRUE(SideTransitions::commit(branch, *automatic));
	ASSERT_EQ(branch->castCharges.size(), 1u);
	EXPECT_TRUE(branch->castCharges.front().automatic); EXPECT_EQ(branch->castCharges.front().paid.total(), 0);
	EXPECT_EQ(branch->mana, root->mana); EXPECT_EQ(branch->ledger->allowances, root->ledger->allowances);
	EXPECT_FALSE(SideTransitions::payCast(branch, {SpellID::HASTE, 1, true}));
	SpellResponseState response; response.armAt(root->round);
	const auto armed = SideTransitions::setResponse(branch, response);
	ASSERT_TRUE(armed); ASSERT_TRUE(SideTransitions::commit(branch, *armed));
	auto native = branch->ledger->allowances; native.resetForRound(root->round + 2);
	const auto round = SideTransitions::advanceRound(branch, root->round + 2);
	ASSERT_TRUE(round); ASSERT_TRUE(SideTransitions::commit(branch, *round));
	EXPECT_EQ(branch->ledger->allowances, native); EXPECT_FALSE(branch->ledger->response.hasState());
	EXPECT_EQ(branch->ledger->wisdom.completedCasts, wisdom.completedCasts);
	EXPECT_EQ(branch->ledger->completedCastCount, 0);
	EXPECT_FALSE(root->ledger->completedHeroCast);
}

TEST_F(ForecastSideStateTest, ZeroPerkAenainAttunementKeepsOriginalSideAcrossControlAndForks)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide(true));
	const auto * fire = addStack(SIDE, CreatureID(CreatureID::FIRE_ELEMENTAL), BattleHex(6, 3), 10);
	ASSERT_NE(fire, nullptr);
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto before = parent->forecastSide(SIDE);
	ASSERT_TRUE(before->perkOwner); ASSERT_EQ(before->attunement.rank, 1);
	ASSERT_TRUE(attackerSideHero->getPerkState().getSelected().empty());
	branch->addUnitBonus(fire->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	branch->setElementalAttunementMask(SIDE, newHorizonsElementalAttunement::activatedMask(before->attunementMask, 2));
	const auto * controlled = branch->battleGetUnitByID(fire->unitId());
	EXPECT_EQ(branch->battleGetOwner(controlled), PlayerColor(1));
	EXPECT_TRUE(controlled->elementalAttunementPassives().attuned);
	EXPECT_EQ(branch->forecastSide(SIDE)->controller, PlayerColor(0));
	EXPECT_EQ(branch->forecastSide(SIDE)->perkOwner, before->perkOwner);
	EXPECT_EQ(parent->forecastSide(SIDE).get(), before.get()); EXPECT_EQ(sibling->forecastSide(SIDE).get(), before.get());
	EXPECT_EQ(battle()->getElementalAttunementMask(SIDE), before->attunementMask);
}

TEST_F(ForecastSideStateTest, ExhaustedBudgetAndMalformedResourceStateCannotPublish)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto current = side(); const auto original = current;
	battleSearch::SearchBudget budget({0, 0, 0, 0, 0});
	EXPECT_THROW(SideTransitions::payCast(current, {SpellID::HASTE, 1, false}, &budget), battleSearch::BudgetExhausted);
	EXPECT_EQ(current.get(), original.get());
	auto invalid = current->fork(); invalid.mana->temporaryBufferRemaining = invalid.mana->pools.getBuffer() + 1;
	const auto malformed = std::make_shared<const SideState>(std::move(invalid));
	EXPECT_FALSE(SideTransitions::payCast(malformed, {SpellID::HASTE, 1, false}));
	EXPECT_EQ(current.get(), original.get());
}

TEST(SpellPointBattleTransitions, LegacyStoredBufferAndNativeRefundLimitsRemainDistinct)
{
	battle::spellPoints::State state;
	state.normalCapacity = std::numeric_limits<int32_t>::max(); state.twoPools = false;
	ASSERT_TRUE(state.pools.restoreSnapshot(30, 50, state.normalCapacity));
	const auto paid = battle::spellPoints::spend(state, 10);
	ASSERT_TRUE(paid); EXPECT_EQ(paid->permanentBuffer, 10); EXPECT_EQ(state.pools.getNormal(), 30);
	const auto before = state;
	EXPECT_FALSE(battle::spellPoints::refund(state, *paid)); EXPECT_EQ(state, before);
	EXPECT_THROW(battle::spellPoints::refund(state, {-1, 0, 0}), std::invalid_argument);
	EXPECT_EQ(state, before);
}
class AbsentSideSource : public BattleStateMock
{
public:
	std::shared_ptr<const nativeRules::CapturedRuleDocument> magic;
	mutable unsigned profileReads = 0;
	const newHorizonsElementalAttunement::Profile & getElementalAttunementProfile(BattleSide) const override
	{
		++profileReads;
		static const newHorizonsElementalAttunement::Profile unrelated{.rank = 3};
		return unrelated;
	}
	const JsonNode & getMagicRules() const override { return magic->document(); }
	std::shared_ptr<const nativeRules::CapturedRuleDocument> getCapturedMagicRules() const override { return magic; }
	PlayerColor getSidePlayer(BattleSide side) const override { return PlayerColor(static_cast<int>(side)); }
};
class SideObserver : public CBattleInfoCallback
{
	const IBattleInfo & source;
public:
	explicit SideObserver(const IBattleInfo & source) : source(source) {}
	const IBattleInfo * getBattle() const override { return &source; }
	std::optional<PlayerColor> getPlayerID() const override { return PlayerColor(0); }
};
TEST(ForecastSideState, KnownAbsenceStaysDistinctFromHiddenAndNeverReadsHiddenHero)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	using namespace ::testing;
	NiceMock<AbsentSideSource> source;
	source.magic = nativeRules::CapturedRuleDocument::compile(nativeRules::CapturedRuleKind::WorldMagic,
		JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	ON_CALL(source, getRound()).WillByDefault(Return(1));
	EXPECT_CALL(source, getSideHero(BattleSide::ATTACKER)).Times(AtLeast(1)).WillRepeatedly(Return(nullptr));
	EXPECT_CALL(source, getSideHero(BattleSide::DEFENDER)).Times(0);
	SideObserver observer(source);
	const auto absent = SideState::capture(observer, BattleSide::ATTACKER);
	const auto hidden = SideState::capture(observer, BattleSide::DEFENDER);
	ASSERT_TRUE(absent); ASSERT_TRUE(hidden);
	EXPECT_EQ(absent->knowledge, VisibleHeroPolicy::Knowledge::KNOWN_ABSENT);
	EXPECT_EQ(hidden->knowledge, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_TRUE(absent->attunementKnown);
	EXPECT_EQ(absent->attunement, newHorizonsElementalAttunement::Profile{});
	EXPECT_FALSE(hidden->attunementKnown);
	EXPECT_EQ(source.profileReads, 0u);
	const auto knownAbsent = std::make_shared<const SideState>(*absent);
	EXPECT_TRUE(SideTransitions::setAttunement(knownAbsent, knownAbsent->attunementMask));
	EXPECT_FALSE(SideTransitions::setAttunement(knownAbsent, 1));
	auto legacyMask = std::make_shared<SideState>(*absent);
	legacyMask->attunementMask = 4; // Preserve an already captured value without authorizing a new activation.
	EXPECT_TRUE(SideTransitions::setAttunement(legacyMask, 4));
	EXPECT_FALSE(SideTransitions::setAttunement(legacyMask, 2));
	EXPECT_FALSE(absent->mana); EXPECT_FALSE(hidden->mana); EXPECT_FALSE(absent->ledger); EXPECT_FALSE(hidden->ledger);
	const auto unknown = std::make_shared<const SideState>(*hidden);
	EXPECT_FALSE(SideTransitions::setAttunement(unknown, 1));
}

TEST_F(ForecastSideStateTest, OverwhelmingRegistrationAndActualInjuryClaimUseNativeLedgerOnIsolatedSide)
{
	ASSERT_NO_FATAL_FAILURE(prepareSide());
	auto root = side(), branch = root;
	auto native = root->ledger->overwhelming;
	const auto token = native.registerAcceptedEligibleCast();
	ASSERT_NE(token, 0u);
	EXPECT_FALSE(native.claimActualDamage(token, true, 0, true));
	EXPECT_FALSE(native.claimActualDamage(token, false, 30, true));
	EXPECT_TRUE(native.claimActualDamage(token, true, 30, true));
	const auto update = SideTransitions::setOverwhelming(branch, native);
	ASSERT_TRUE(update); ASSERT_TRUE(SideTransitions::commit(branch, *update));
	EXPECT_EQ(branch->ledger->overwhelming, native);
	EXPECT_EQ(root->ledger->overwhelming.winningCastToken, 0u);
}

}
