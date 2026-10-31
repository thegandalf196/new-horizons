/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/InfernoStrikeDebuffFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../AI/BattleAI/OriginalMeleeSequence.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/GameLibrary.h"
#include <vcmi/Environment.h>
namespace
{
class InfernoEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit InfernoEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class InfernoStrikeDebuffAITest : public InfernoStrikeDebuffFixture, public ::testing::WithParamInterface<bool> {};
}
TEST_P(InfernoStrikeDebuffAITest, ScalarPreviewAndSelectedReplayUseTheSharedPrimaryPlan)
{
	prepare(GetParam()); beginCombat();
	InfernoEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, authority);
	const auto before = victim->getAvailableHealth();
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(rng, nullptr);
	const auto random = infernoRandomBytes(*rng), rolls = infernoRandomBytes(*gameHandler->randomizer);
	DamageCache cache;
	const BattleAttackInfo hit(actor, victim, 0, GetParam());
	const auto preview = AttackPossibility::evaluate(hit, actor->getPosition(), cache, branch);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto * projected = preview.effectPreview->battleGetUnitByID(victim->unitId());
	ASSERT_NE(projected, nullptr); ASSERT_EQ(effects(*projected)->size(), 1u);
	EXPECT_EQ(effects(*projected)->front()->val, GetParam() ? 20 : -3);
	EXPECT_TRUE(effects(*preview.effectPreview->battleGetUnitByID(collateral->unitId()))->empty());
	auto replay = std::make_shared<HypotheticBattle>(&env, authority);
	BattleExchangeVariant exchange; exchange.trackAttack(preview, replay, cache);
	ASSERT_EQ(effects(*replay->battleGetUnitByID(victim->unitId()))->size(), 1u);
	EXPECT_EQ(effects(*replay->battleGetUnitByID(victim->unitId()))->front()->toJsonNode(), effects(*projected)->front()->toJsonNode());
	EXPECT_EQ(victim->getAvailableHealth(), before); EXPECT_TRUE(effects(*victim)->empty());
	EXPECT_EQ(infernoRandomBytes(*rng), random); EXPECT_EQ(infernoRandomBytes(*gameHandler->randomizer), rolls);
}
TEST_P(InfernoStrikeDebuffAITest, HiddenHeroCannotCreateRiderButVisibleExistingStatusStillChangesDamageCache)
{
	prepare(GetParam());
	InfernoEnvironment env(gameState());
	auto hidden = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto branch = std::make_shared<HypotheticBattle>(&env, hidden);
	const auto source = branch->getForUpdate(actor->unitId());
	const auto target = branch->getForUpdate(victim->unitId());
	const BattleAttackInfo originating(source.get(), target.get(), 0, GetParam());
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*branch, originating, 100).add.empty());
	const auto nativePlan = newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, GetParam()), 100);
	ASSERT_EQ(nativePlan.add.size(), 1u);
	target->addUnitBonus(nativePlan.add); // Published visible input; no hidden policy import.
	const auto * damageActor = GetParam() ? target.get() : source.get();
	const auto * damageTarget = GetParam() ? source.get() : target.get();
	DamageCache cache; cache.cacheDamage(damageActor, damageTarget, branch);
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cache.getDamage(damageActor, damageTarget, branch, &timing),
		branch->battleExpectedLuckDamage(BattleAttackInfo(damageActor, damageTarget, 0, false)));
	EXPECT_EQ(timing.dynamicBypasses, 1u);
	target->removeUnitBonus(Bonus::UntilActivationEnds);
	DamageCache child(&cache); DamageCacheProbeTiming expired;
	EXPECT_EQ(child.getDamage(damageActor, damageTarget, branch, &expired),
		branch->battleExpectedLuckDamage(BattleAttackInfo(damageActor, damageTarget, 0, false)));
	EXPECT_EQ(expired.dynamicBypasses, 1u);
}
TEST_P(InfernoStrikeDebuffAITest, DescendantOwnsStatusAcrossLiveAndParentExpiry)
{
	prepare(GetParam());
	publish(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, GetParam()), 100));
	InfernoEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, authority);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	SetStackEffect expire; expire.battleID = BattleID(0);
	std::vector<Bonus> removed; for(const auto & bonus : *effects(*victim)) removed.push_back(*bonus);
	expire.toRemove.emplace_back(victim->unitId(), removed); gameHandler->sendAndApply(expire);
	EXPECT_TRUE(effects(*victim)->empty());
	EXPECT_EQ(effects(*child->battleGetUnitByID(victim->unitId()))->size(), 1u);
	parent->getForUpdate(victim->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
	EXPECT_EQ(effects(*child->battleGetUnitByID(victim->unitId()))->size(), 1u);
	child->getForUpdate(victim->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
	EXPECT_TRUE(effects(*child->battleGetUnitByID(victim->unitId()))->empty());
}
TEST_P(InfernoStrikeDebuffAITest, EffectiveFormAndOwnerAreReevaluatedWithoutBorrowingActionControl)
{
	prepare(GetParam());
	const BattleAttackInfo hit(actor, victim, 0, GetParam());
	actor->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), hit, 100).add.empty());
	actor->endBattleForm(); EXPECT_EQ(newHorizonsOrdinaryStrike::plan(*battle(), hit, 100).add.size(), 1u);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), hit, 100).add.empty());
}
TEST_P(InfernoStrikeDebuffAITest, PreviewChangesOnlyTheRequestedBranchAndPreservesSiblingStatus)
{
	prepare(GetParam());
	InfernoEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, authority);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	const auto source = child->getForUpdate(actor->unitId());
	const auto target = child->getForUpdate(victim->unitId());
	AttackPreviewHitContext::applyOrdinaryStrike(*child, BattleAttackInfo(source.get(), target.get(), 0, GetParam()), 100);
	EXPECT_EQ(effects(*target)->size(), 1u);
	EXPECT_TRUE(effects(*sibling->battleGetUnitByID(victim->unitId()))->empty());
	EXPECT_TRUE(effects(*parent->battleGetUnitByID(victim->unitId()))->empty());
	EXPECT_TRUE(effects(*victim)->empty());
}
INSTANTIATE_TEST_SUITE_P(CalhAndNymus, InfernoStrikeDebuffAITest, ::testing::Values(true, false));
class NymusOriginalMeleeProjectionTest : public InfernoStrikeDebuffFixture {};
TEST_F(NymusOriginalMeleeProjectionTest, OwnedOriginalSequenceCarriesDefenseDebuffIntoSecondBlow)
{
	prepare(false); beginCombat();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	InfernoEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	const auto origin = InitialExportBattleSubject::capture(authority, *battle());
	const heroOrderValue::BraceMeleeAction action{actor->unitId(), victim->unitId(), actor->getPosition(), victim->getPosition()};
	const auto result = heroOrderValue::projectRecipientMeleeSequence(&env, origin, action);
	ASSERT_TRUE(result.meleeSequenceComplete()) << static_cast<int>(result.stop);
	ASSERT_EQ(result.incomingBlows, 2);
	ASSERT_EQ(result.hits.size(), 2u);
	EXPECT_GT(result.hits[1].requestedDamage, result.hits[0].requestedDamage);
	EXPECT_EQ(effects(*result.branch->battleGetUnitByID(victim->unitId()))->size(), 1u);
	EXPECT_TRUE(effects(*victim)->empty());
}
