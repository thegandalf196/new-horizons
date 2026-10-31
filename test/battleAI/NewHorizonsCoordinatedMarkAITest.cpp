/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsCoordinatedMarkFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleAction.h"
#include "../../server/battles/BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
namespace marks = newHorizonsCoordinatedMarks;
class CoordinatedMarkEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CoordinatedMarkEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
template<typename Random> auto markRandomBytes(Random & random)
{
	CMemorySerializer memory; memory.oser & random; return memory.extractBuffer();
}
// Explicit future world codec for the captured policy; live battles are not world fields.
std::vector<std::byte> markWorldBytes(CGameState & state)
{
	NativeBonusGraphAccess graph;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	memory.oser & state;
	return memory.extractBuffer();
}
class NewHorizonsCoordinatedMarkAITest : public NewHorizonsCoordinatedMarkFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	std::unique_ptr<CoordinatedMarkEnvironment> env;
	void prepare(const char * hero, bool upgraded = false)
	{
		NewHorizonsCoordinatedMarkFixture::prepare(hero, GetParam(), upgraded);
		env = std::make_unique<CoordinatedMarkEnvironment>(gameState());
	}
	std::shared_ptr<HypotheticBattle> simulation(PlayerColor player = PlayerColor(0))
	{
		return std::make_shared<HypotheticBattle>(env.get(), std::make_shared<CPlayerBattleCallback>(battle(), player));
	}
	AttackPossibility forecast(const std::shared_ptr<HypotheticBattle> & model, DamageCache & cache, bool shooting)
	{
		return AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(actor->unitId()),
			model->battleGetUnitByID(victim->unitId()), 0, shooting), shooting ? actor->getPosition() : BattleHex(4, 5), cache, model);
	}
};
}
TEST_P(NewHorizonsCoordinatedMarkAITest, MonkShotForecastAndSelectedReplayKeepTheMarkWithoutMutatingNativeOrRng)
{
	prepare("core:ingham"); beginCombat(); auto model = simulation(); DamageCache cache;
	CMemorySerializer current;
	ASSERT_EQ(current.oser.version, ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	if(GetParam() != 1)
	{
		NativeBonusGraphAccess graph;
		EXPECT_THROW(current.oser & *gameState(), std::runtime_error);
		EXPECT_TRUE(current.extractBuffer().empty());
	}
	const auto live = markWorldBytes(*gameState());
	auto * world = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(world, nullptr);
	const auto worldBytes = markRandomBytes(*world), serverBytes = markRandomBytes(*gameHandler->randomizer);
	const auto aiBytes = markRandomBytes(CRandomGenerator::getDefault());
	const auto preview = forecast(model, cache, true);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : model->battleGetUnitByID(victim->unitId());
	EXPECT_EQ(marks::hasMark(*final), GetParam() == 0);
	auto replay = simulation(); BattleExchangeVariant applied; applied.trackAttack(preview, replay, cache);
	EXPECT_EQ(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	EXPECT_FALSE(marks::hasMark(*victim)); EXPECT_EQ(markWorldBytes(*gameState()), live);
	EXPECT_EQ(markRandomBytes(*world), worldBytes); EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), serverBytes);
	EXPECT_EQ(markRandomBytes(CRandomGenerator::getDefault()), aiBytes);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, NextAlliedMeleeConsumesInBranchAndCacheRetainsCorrectPostConsumptionDamage)
{
	prepare("core:ingham"); auto * friendUnit = addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(9, 5), 10);
	auto model = simulation(); DamageCache cache;
	const auto * source = model->battleGetUnitByID(actor->unitId());
	const auto * target = model->battleGetUnitByID(victim->unitId());
	cache.getDamage(model->battleGetUnitByID(friendUnit->unitId()), target, model);
	AttackPreviewHitContext::applyOrdinaryStrike(*model, BattleAttackInfo(source, target, 0, true), 1);
	const BattleAttackInfo melee(model->battleGetUnitByID(friendUnit->unitId()), model->battleGetUnitByID(victim->unitId()), 0, false);
	EXPECT_EQ(cache.getDamage(melee.attacker, melee.defender, model), model->battleExpectedLuckDamage(melee));
	const auto preview = AttackPossibility::evaluate(melee, friendUnit->getPosition(), cache, model);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : model->battleGetUnitByID(victim->unitId());
	EXPECT_FALSE(marks::hasMark(*final));
	if(preview.effectPreview)
		EXPECT_EQ(cache.getDamage(preview.effectPreview->battleGetUnitByID(friendUnit->unitId()), final, preview.effectPreview),
			preview.effectPreview->battleExpectedLuckDamage(BattleAttackInfo(preview.effectPreview->battleGetUnitByID(friendUnit->unitId()), final, 0, false)));
	EXPECT_EQ(marks::hasMark(*model->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkAITest, OpenGroundHarpyReturnForecastCreatesOnlyOtherAttackerProtectionMark)
{
	prepare("core:lorelei", true); beginCombat(); auto model = simulation(); DamageCache cache;
	const auto preview = forecast(model, cache, false);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : model->battleGetUnitByID(victim->unitId());
	EXPECT_EQ(marks::hasMark(*final), GetParam() == 0);
	if(GetParam() == 0)
	{
		ASSERT_NE(preview.effectPreview, nullptr);
		EXPECT_EQ(preview.attackerState->getPosition(), actor->getPosition());
		EXPECT_EQ(marks::physicalProtectionIgnorePercent(*preview.effectPreview,
			BattleAttackInfo(preview.attackerState.get(), final, 0, false)), 0);
	}
	auto replay = simulation(); BattleExchangeVariant selected; selected.trackAttack(preview, replay, cache);
	EXPECT_EQ(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	EXPECT_EQ(preview.completedReturnMarkTarget.has_value(), GetParam() == 0);
	if(GetParam() == 0) EXPECT_EQ(*preview.completedReturnMarkTarget, victim->unitId());
	EXPECT_FALSE(marks::hasMark(*victim)); EXPECT_EQ(actor->getPosition(), BattleHex(3, 5));
}
TEST_P(NewHorizonsCoordinatedMarkAITest, MarkSnapshotSurvivesParentExpiryAndHiddenSourcePolicyIsNotNeededForPublishedEntitlement)
{
	prepare("core:ingham"); ASSERT_EQ(battle()->getRound(), 0);
	beginCombat(); ASSERT_EQ(battle()->getRound(), 1);
	auto model = simulation(); ASSERT_EQ(model->getRound(), 1);
	AttackPreviewHitContext::applyOrdinaryStrike(*model, BattleAttackInfo(model->battleGetUnitByID(actor->unitId()), model->battleGetUnitByID(victim->unitId()), 0, true), 1);
	if(GetParam() == 0)
	{
		const auto published = model->battleGetUnitByID(victim->unitId())->getUnstackedBonuses(CSelector(marks::isMark));
		ASSERT_EQ(published->size(), 1u);
		EXPECT_EQ(published->front()->duration, BonusDuration::N_TURNS);
		EXPECT_EQ(published->front()->turnsRemain, 1);
	}
	auto child = std::make_shared<HypotheticBattle>(env.get(), model);
	child->getForUpdate(victim->unitId()); model->nextRound();
	ASSERT_EQ(model->getRound(), 2); ASSERT_EQ(child->getRound(), 1);
	EXPECT_FALSE(marks::hasMark(*model->battleGetUnitByID(victim->unitId())));
	EXPECT_EQ(marks::hasMark(*child->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	const auto * childActor = child->battleGetUnitByID(actor->unitId());
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*child, BattleAttackInfo(childActor, child->battleGetUnitByID(victim->unitId()), 0, false)), GetParam() == 0 ? 25 : 0);
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	EXPECT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_FALSE(marks::consecratingShot(hidden, BattleAttackInfo(actor, victim, 0, true)));
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
		newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, true), 1));
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(hidden, BattleAttackInfo(actor, victim, 0, false)), GetParam() == 0 ? 25 : 0);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, FlyingHomeCommitMarkEntitlementDoesNotDependOnSurvivingItsLanding)
{
	prepare("core:lorelei", true); beginCombat();
	int64_t wound = actor->getAvailableHealth() - 1; actor->damage(wound);
	addReturnMoat(actor->getPosition());
	auto model = simulation(); DamageCache cache;
	const auto preview = forecast(model, cache, false);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : model->battleGetUnitByID(victim->unitId());
	EXPECT_EQ(marks::hasMark(*final), GetParam() == 0);
	EXPECT_TRUE(actor->alive()); EXPECT_FALSE(marks::hasMark(*victim));
	// This asserts entitlement at the flying home commit only. The existing
	// return forecast does not claim exact post-landing health/obstacle state.
}
TEST_P(NewHorizonsCoordinatedMarkAITest, MarkOnlyOnMeleeInitiatorIsOwnedAndConsumedByTheOrdinaryRetaliation)
{
	prepare("core:ingham"); actor->setPosition(BattleHex(5, 5));
	for(auto * unit : {actor, victim})
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 1, BonusSourceID(), subtype, bound));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), victim); ASSERT_TRUE(actor->alive());
	auto model = simulation(PlayerColor(1)); DamageCache cache;
	const BattleAttackInfo attack(model->battleGetUnitByID(victim->unitId()), model->battleGetUnitByID(actor->unitId()), 4, false);
	EXPECT_EQ(newHorizonsOrdinaryStrike::needsProjection(*model, attack), GetParam() == 0);
	const auto live = markWorldBytes(*gameState());
	auto * world = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(world, nullptr);
	const auto worldBytes = markRandomBytes(*world), serverBytes = markRandomBytes(*gameHandler->randomizer);
	const auto aiBytes = markRandomBytes(CRandomGenerator::getDefault());
	const auto preview = AttackPossibility::evaluate(attack, BattleHex(6, 5), cache, model);
	if(GetParam() == 0)
	{
		ASSERT_NE(preview.effectPreview, nullptr);
		EXPECT_TRUE(std::ranges::any_of(preview.fortuneStrikes, [](const auto & strike) { return strike.retaliation; }));
		EXPECT_FALSE(marks::hasMark(*preview.effectPreview->battleGetUnitByID(victim->unitId())));
	}
	auto replay = simulation(PlayerColor(1)); BattleExchangeVariant selected; selected.trackAttack(preview, replay, cache);
	EXPECT_FALSE(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())));
	EXPECT_EQ(marks::hasMark(*victim), GetParam() == 0); EXPECT_EQ(markWorldBytes(*gameState()), live);
	EXPECT_EQ(markRandomBytes(*world), worldBytes); EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), serverBytes);
	EXPECT_EQ(markRandomBytes(CRandomGenerator::getDefault()), aiBytes);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, FutureActorReturnUsesOneFullActionAndKeepsTargetProbeIsolated)
{
	prepare("core:lorelei", true); beginCombat(); auto model = simulation(); DamageCache cache;
	auto source = model->getForUpdate(actor->unitId()), target = model->getForUpdate(victim->unitId());
	const auto sourceBefore = source->save(), targetBefore = target->save();
	const auto aiBytes = markRandomBytes(CRandomGenerator::getDefault());
	BattleExchangeVariant probe;
	probe.trackAttack(source, target, false, true, cache, model, true);
	EXPECT_EQ(source->save(), sourceBefore); EXPECT_EQ(target->save(), targetBefore);
	EXPECT_FALSE(marks::hasMark(*target)); EXPECT_EQ(markRandomBytes(CRandomGenerator::getDefault()), aiBytes);
	BattleExchangeAttackResult receipt; BattleExchangeVariant applied;
	applied.trackAttack(source, target, false, true, cache, model, false, true, nullptr, nullptr, nullptr, &receipt);
	EXPECT_EQ(receipt.completedAction, GetParam() == 0);
	EXPECT_EQ(marks::hasMark(*model->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	EXPECT_EQ(model->battleGetUnitByID(actor->unitId())->getPosition(), actor->getPosition());
	EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkAITest, OldOwnReturnMarkWithoutNewReturnDoesNotFabricateACompletionReceipt)
{
	prepare("core:lorelei", true);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), marks::completedReturn(*battle(), *actor, *victim, true));
	actor->setPosition(BattleHex(4, 5)); beginCombat(); auto model = simulation(); DamageCache cache;
	const auto preview = forecast(model, cache, false);
	EXPECT_FALSE(preview.completedReturnMarkTarget.has_value());
	auto replay = simulation(); BattleExchangeVariant selected; selected.trackAttack(preview, replay, cache);
	EXPECT_EQ(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())), GetParam() == 0);
	EXPECT_EQ(replay->battleGetUnitByID(actor->unitId())->getPosition(), actor->getPosition());
}
TEST_P(NewHorizonsCoordinatedMarkAITest, DefaultSnapshotPreservesNonemptyReplayOnCapturedPolicyRejectionAndSuccess)
{
	prepare("core:ingham");
	auto & log = gameState()->replayLog;
	log.configure(false, 2);
	log.beginDay(gameState()->day, ReplayPayload{{std::byte{7}}, ESerializationVersion::CURRENT});
	ASSERT_EQ(log.getChapters().size(), 1u);
	ASSERT_EQ(log.getChapters().front().gamestateSnapshot, (std::vector<std::byte>{std::byte{7}}));
	ASSERT_EQ(log.getChapters().front().snapshotVersion, ESerializationVersion::CURRENT);
	const auto before = markRandomBytes(log);
	ASSERT_FALSE(before.empty());
	ASSERT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	if(GetParam() == 1)
	{
		EXPECT_NO_THROW(gameState()->saveToMemory());
	}
	else
	{
		EXPECT_THROW(gameState()->saveToMemory(), std::runtime_error);
	}
	EXPECT_EQ(markRandomBytes(log), before);
	ASSERT_EQ(log.getChapters().size(), 1u);
	EXPECT_EQ(log.getChapters().front().gamestateSnapshot, (std::vector<std::byte>{std::byte{7}}));
	EXPECT_EQ(log.getChapters().front().snapshotVersion, ESerializationVersion::CURRENT);
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsCoordinatedMarkAITest, ::testing::Values(0, 1, 2));
