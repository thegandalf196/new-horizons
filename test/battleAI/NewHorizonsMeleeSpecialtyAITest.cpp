/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMeleeSpecialtyFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
namespace
{
class MeleeSpecialtyEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MeleeSpecialtyEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
template<typename Random>
auto meleeRandomBytes(Random & random)
{
	CMemorySerializer memory; memory.oser & random; return memory.extractBuffer();
}
int64_t primaryDamage(const FortuneStrikeProjection & strike, uint32_t victim)
{
	int64_t result = 0;
	for(const auto & [id, damage] : strike.resolvedHits) if(id == victim) result += damage;
	return result;
}
class NewHorizonsMeleeSpecialtyAITest : public NewHorizonsMeleeSpecialtyFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	std::unique_ptr<MeleeSpecialtyEnvironment> env;
	void prepare(const char * hero, bool upgraded = false)
	{
		NewHorizonsMeleeSpecialtyFixture::prepare(hero, GetParam(), upgraded);
		env = std::make_unique<MeleeSpecialtyEnvironment>(gameState());
	}
	std::shared_ptr<HypotheticBattle> simulation(PlayerColor player = PlayerColor(0))
	{
		return std::make_shared<HypotheticBattle>(env.get(), std::make_shared<CPlayerBattleCallback>(battle(), player));
	}
	AttackPossibility forecast(const std::shared_ptr<HypotheticBattle> & model, DamageCache & cache)
	{
		return AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(actor->unitId()),
			model->battleGetUnitByID(victim->unitId()), 0, false), actor->getPosition(), cache, model);
	}
};
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, CrusaderPreviewPublishesGuardAfterAllStrikesAndLeavesCounterUnprotected)
{
	prepare("core:sorsha", true); beginCombat();
	auto model = simulation(); DamageCache cache;
	const auto liveBefore = gameState()->saveToMemory();
	const auto serverRandom = meleeRandomBytes(*gameHandler->randomizer);
	auto * worldRandom = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(worldRandom, nullptr);
	const auto worldBytes = meleeRandomBytes(*worldRandom);
	const auto aiRandom = meleeRandomBytes(CRandomGenerator::getDefault());
	const auto preview = forecast(model, cache);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(actor->unitId()) : preview.attackerState.get();
	ASSERT_NE(final, nullptr);
	EXPECT_EQ(final->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)), GetParam() == 0);
	const auto opening = std::ranges::find_if(preview.fortuneStrikes, [&](const auto & hit)
		{ return hit.attackerId == actor->unitId() && !hit.retaliation && hit.attackIndex == 0; });
	const auto response = std::ranges::find_if(preview.fortuneStrikes, [&](const auto & hit)
		{ return hit.attackerId == victim->unitId() && hit.retaliation; });
	ASSERT_NE(opening, preview.fortuneStrikes.end()); ASSERT_NE(response, preview.fortuneStrikes.end());
	const auto ownStrikes = std::ranges::count_if(preview.fortuneStrikes, [&](const auto & hit)
		{ return hit.attackerId == actor->unitId() && !hit.retaliation && !hit.shooting; });
	EXPECT_EQ(ownStrikes, 2);
	// Reconstruct only the first hit on an independent baseline, before the guard exists.
	auto baseline = simulation();
	auto counterSource = baseline->getForUpdate(victim->unitId());
	auto counterTarget = baseline->getForUpdate(actor->unitId());
	int64_t firstHit = primaryDamage(*opening, victim->unitId()); counterSource->damage(firstHit);
	BattleAttackInfo counter(counterSource.get(), counterTarget.get(), 0, false); counter.retaliation = true;
	const auto unguarded = baseline->calculateDmgRange(counter).damage;
	ASSERT_EQ(unguarded.min, unguarded.max);
	EXPECT_EQ(primaryDamage(*response, actor->unitId()), unguarded.min);
	EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
	EXPECT_EQ(meleeRandomBytes(*gameHandler->randomizer), serverRandom);
	EXPECT_EQ(meleeRandomBytes(*worldRandom), worldBytes);
	EXPECT_EQ(meleeRandomBytes(CRandomGenerator::getDefault()), aiRandom);
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, SelectedSorshaSequenceReplaysOneGuardAndBypassesPrimedDamageCache)
{
	prepare("core:sorsha"); beginCombat();
	auto model = simulation(); DamageCache cache;
	cache.cacheDamage(model->battleGetUnitByID(victim->unitId()), model->battleGetUnitByID(actor->unitId()), model);
	const auto preview = forecast(model, cache);
	auto selected = simulation(); BattleExchangeVariant exchange;
	exchange.trackAttack(preview, selected, cache);
	const auto source = selected->getForUpdate(victim->unitId());
	const auto target = selected->getForUpdate(actor->unitId());
	EXPECT_EQ(target->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection))->size(), GetParam() == 0 ? 1u : 0u);
	DamageCacheProbeTiming timing;
	const BattleAttackInfo incoming(source.get(), target.get(), 0, false);
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), selected, &timing), selected->battleExpectedLuckDamage(incoming));
	if(GetParam() == 0) EXPECT_GT(timing.dynamicBypasses, 0u);
	EXPECT_FALSE(actor->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)));
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, StrakerPreviewAndSelectedReplayUseMovementOnlyDebuff)
{
	prepare("core:straker"); beginCombat();
	const auto movement = victim->getMovementRange(); const auto initiative = victim->getInitiative();
	auto model = simulation(); DamageCache cache;
	const auto preview = forecast(model, cache);
	const auto affected = std::ranges::find_if(preview.affectedUnits, [&](const auto & unit)
		{ return unit && unit->unitId() == victim->unitId(); });
	ASSERT_NE(affected, preview.affectedUnits.end());
	const auto * projected = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : affected->get();
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->getMovementRange(), movement - (GetParam() == 0 ? 1u : 0u));
	EXPECT_EQ(projected->getInitiative(), initiative);
	auto selected = simulation(); BattleExchangeVariant exchange; exchange.trackAttack(preview, selected, cache);
	const auto * replayed = selected->battleGetUnitByID(victim->unitId()); ASSERT_NE(replayed, nullptr);
	EXPECT_EQ(replayed->getMovementRange(), projected->getMovementRange());
	EXPECT_EQ(replayed->getInitiative(), initiative);
	EXPECT_EQ(victim->getMovementRange(), movement);
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, StrakerEndActionExpiryCannotLeakFromMaterializedParentIntoChild)
{
	prepare("core:straker");
	auto parent = simulation();
	AttackPreviewHitContext::applyOrdinaryStrike(*parent,
		BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()), parent->battleGetUnitByID(victim->unitId()), 0, false), 1);
	const auto original = parent->getForUpdate(victim->unitId());
	auto child = std::make_shared<HypotheticBattle>(env.get(), parent);
	const auto copied = child->getForUpdate(victim->unitId());
	original->removeUnitBonus(CSelector(Bonus::UntilActivationEnds));
	EXPECT_FALSE(original->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)));
	EXPECT_EQ(copied->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)), GetParam() == 0);
	EXPECT_EQ(copied->getInitiative(), victim->getInitiative());
	EXPECT_FALSE(victim->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)));
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, NativeAndProjectedSorshaLookupAgreeForKnownAndHiddenHeroPolicies)
{
	prepare("core:sorsha");
	CPlayerBattleCallback known(battle(), PlayerColor(0)), hidden(battle(), PlayerColor(1));
	auto projectedKnown = simulation(PlayerColor(0)), projectedHidden = simulation(PlayerColor(1));
	EXPECT_EQ(newHorizonsShieldAndSword::eligible(*battle(), *actor), GetParam() == 0);
	EXPECT_EQ(newHorizonsShieldAndSword::eligible(known, *actor), GetParam() == 0);
	EXPECT_FALSE(newHorizonsShieldAndSword::eligible(hidden, *actor));
	EXPECT_EQ(newHorizonsShieldAndSword::eligible(*projectedKnown, *projectedKnown->battleGetUnitByID(actor->unitId())), GetParam() == 0);
	EXPECT_FALSE(newHorizonsShieldAndSword::eligible(*projectedHidden, *projectedHidden->battleGetUnitByID(actor->unitId())));
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, NativeAndProjectedStrakerLookupAgreeForKnownAndHiddenHeroPolicies)
{
	prepare("core:straker");
	CPlayerBattleCallback known(battle(), PlayerColor(0)), hidden(battle(), PlayerColor(1));
	auto projectedKnown = simulation(PlayerColor(0)), projectedHidden = simulation(PlayerColor(1));
	const BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsOrdinaryStrike::plan(*battle(), hit, 1).add.empty(), GetParam() != 0);
	EXPECT_EQ(newHorizonsOrdinaryStrike::plan(known, hit, 1).add.empty(), GetParam() != 0);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(hidden, hit, 1).add.empty());
	EXPECT_EQ(newHorizonsOrdinaryStrike::plan(*projectedKnown, hit, 1).add.empty(), GetParam() != 0);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*projectedHidden, hit, 1).add.empty());
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, ObservableGuardCacheHistorySurvivesExpiryWithoutKnowingEnemyHeroPolicy)
{
	prepare("core:sorsha");
	auto hidden = simulation(PlayerColor(1));
	ASSERT_EQ(hidden->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	const auto source = hidden->getForUpdate(victim->unitId());
	const auto target = hidden->getForUpdate(actor->unitId());
	EXPECT_FALSE(newHorizonsShieldAndSword::eligible(*hidden, *target));
	DamageCache cache;
	cache.cacheDamage(source.get(), target.get(), hidden);
	const BattleAttackInfo incoming(source.get(), target.get(), 0, false);
	(void)cache.getDamage(source.get(), target.get(), hidden);
	const auto before = cache.captureSnapshot();
	// This is an observed server-produced status, not permission to read hidden captured rules.
	const auto published = newHorizonsShieldAndSword::completion(*battle(), *actor, true);
	target->addUnitBonus(published.add);
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), hidden), hidden->battleExpectedLuckDamage(incoming));
	EXPECT_EQ(cache.matchesSnapshot(before), GetParam() != 0);
	const auto remembered = cache.captureSnapshot();
	hidden->nextTurn(actor->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)));
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), hidden), hidden->battleExpectedLuckDamage(incoming));
	EXPECT_TRUE(cache.matchesSnapshot(remembered));
	DamageCache cold;
	EXPECT_EQ(cold.getDamage(source.get(), target.get(), hidden), hidden->battleExpectedLuckDamage(incoming));
}
TEST_P(NewHorizonsMeleeSpecialtyAITest, ExistingEvasiveShroudRetainsItsRememberedTargetExpiryBehavior)
{
	prepare("core:sorsha");
	auto model = simulation(PlayerColor(1));
	const auto source = model->getForUpdate(victim->unitId());
	const auto target = model->getForUpdate(actor->unitId());
	DamageCache cache;
	cache.cacheDamage(source.get(), target.get(), model);
	const BattleAttackInfo incoming(source.get(), target.get(), 0, false);
	(void)cache.getDamage(source.get(), target.get(), model);
	const auto before = cache.captureSnapshot();
	target->addUnitBonus({newHorizonsShroud::evasiveShroudProtection()});
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), model), model->battleExpectedLuckDamage(incoming));
	EXPECT_FALSE(cache.matchesSnapshot(before));
	const auto remembered = cache.captureSnapshot();
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection)));
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), model), model->battleExpectedLuckDamage(incoming));
	EXPECT_TRUE(cache.matchesSnapshot(remembered));
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsMeleeSpecialtyAITest, ::testing::Values(0, 1, 2));
