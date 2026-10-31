/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsCoordinatedMarkFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/entities/building/TownFortifications.h"
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
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"
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
	prepare("core:lorelei", true);
	addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(2, 3), 1);
	beginCombat();
	int64_t wound = actor->getAvailableHealth() - 1; actor->damage(wound);
	addReturnMoat(actor->getPosition());
	auto model = simulation(), sibling = simulation(); DamageCache cache;
	const auto sourceBefore = actor->save(), targetBefore = victim->save();
	auto * world = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(world, nullptr);
	const auto worldBytes = markRandomBytes(*world), serverBytes = markRandomBytes(*gameHandler->randomizer);
	const auto preview = forecast(model, cache, false);
	const auto * final = preview.effectPreview ? preview.effectPreview->battleGetUnitByID(victim->unitId()) : model->battleGetUnitByID(victim->unitId());
	EXPECT_EQ(marks::hasMark(*final), GetParam() == 0);
	if(GetParam() == 0)
	{
		ASSERT_TRUE(preview.completedReturnDestination); EXPECT_EQ(*preview.completedReturnDestination, actor->getPosition());
		EXPECT_FALSE(preview.attackerState->alive()); EXPECT_GT(preview.attackerDamageReduce, 0);
		auto replay = simulation(); BattleExchangeVariant selected; selected.trackAttack(preview, replay, cache);
		EXPECT_FALSE(replay->battleGetUnitByID(actor->unitId())->alive());
		EXPECT_EQ(replay->battleGetUnitByID(actor->unitId())->getPosition(), actor->getPosition());
		EXPECT_TRUE(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())));
	}
	else EXPECT_FALSE(preview.completedReturnDestination);
	EXPECT_EQ(actor->save(), sourceBefore); EXPECT_EQ(victim->save(), targetBefore);
	EXPECT_EQ(model->battleGetUnitByID(actor->unitId())->getAvailableHealth(), 1);
	EXPECT_EQ(sibling->battleGetUnitByID(actor->unitId())->getAvailableHealth(), 1);
	EXPECT_EQ(markRandomBytes(*world), worldBytes); EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), serverBytes);
	EXPECT_TRUE(actor->alive()); EXPECT_FALSE(marks::hasMark(*victim));
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
TEST_P(NewHorizonsCoordinatedMarkAITest, NonlethalLandingMatchesNativeAndSelectedReplayDoesNotChargeItTwice)
{
	prepare("core:lorelei", true);
	addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(2, 3), 1);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	int64_t wound = actor->getAvailableHealth() - 100; actor->damage(wound);
	const auto origin = actor->getPosition(); addReturnMoat(origin);
	auto parent = simulation(); DamageCache cache;
	const auto preview = forecast(parent, cache, false);
	if(GetParam() == 0)
	{
		ASSERT_TRUE(preview.completedReturnDestination);
		ASSERT_TRUE(preview.attackerState->alive());
		EXPECT_LT(preview.attackerState->getAvailableHealth(), 100);
		auto withoutMoat = simulation(); withoutMoat->removeObstacle(731); DamageCache controlCache;
		const auto control = forecast(withoutMoat, controlCache, false);
		EXPECT_GT(preview.attackerDamageReduce, control.attackerDamageReduce);
		EXPECT_FLOAT_EQ(preview.defenderDamageReduce, control.defenderDamageReduce);
		auto replay = simulation(); BattleExchangeVariant selected; selected.trackAttack(preview, replay, cache);
		const auto projectedHealth = preview.attackerState->getAvailableHealth();
		EXPECT_EQ(replay->battleGetUnitByID(actor->unitId())->getAvailableHealth(), projectedHealth);
		EXPECT_EQ(replay->battleGetUnitByID(actor->unitId())->getPosition(), origin);
		EXPECT_TRUE(marks::hasMark(*replay->battleGetUnitByID(victim->unitId())));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(actor, victim, BattleHex(4, 5), true)));
		EXPECT_EQ(actor->getAvailableHealth(), projectedHealth); EXPECT_EQ(actor->getPosition(), origin);
		EXPECT_TRUE(marks::hasMark(*victim));
	}
	else
	{
		EXPECT_FALSE(preview.completedReturnDestination);
		EXPECT_FALSE(marks::hasMark(*victim));
	}
}
TEST_P(NewHorizonsCoordinatedMarkAITest, SharedLandingHazardUsesTheWideRearFootprintAndMatchesNative)
{
	prepare("core:lorelei", true);
	auto * wide = addStack(BattleSide::ATTACKER, creatureByName("core:behemoth"), BattleHex(5, 2), 10);
	ASSERT_TRUE(wide->doubleWide()); addReturnMoat(wide->occupiedHex());
	auto branch = simulation(); auto projected = branch->getForUpdate(wide->unitId());
	const auto before = wide->getAvailableHealth();
	branch->evaluateLandingObstacleTriggersForUnit(*branch->getServerCallback(), *projected);
	const auto after = projected->getAvailableHealth(); EXPECT_LT(after, before);
	EXPECT_EQ(wide->getAvailableHealth(), before);
	battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *wide);
	EXPECT_EQ(wide->getAvailableHealth(), after);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, LandingEvaluationDoesNotImportAnUnseenHostileObstacle)
{
	prepare("core:lorelei", true);
	auto * wide = addStack(BattleSide::ATTACKER, creatureByName("core:behemoth"), BattleHex(5, 2), 10);
	addReturnMoat(wide->getPosition());
	auto obstacle = std::dynamic_pointer_cast<SpellCreatedObstacle>(battle()->obstacles.back()); ASSERT_NE(obstacle, nullptr);
	obstacle->obstacleType = CObstacleInstance::SPELL_CREATED;
	obstacle->hidden = true; obstacle->revealed = false; obstacle->nativeVisible = false;
	auto branch = simulation(); auto projected = branch->getForUpdate(wide->unitId());
	for(const auto & visible : branch->battleGetAllObstacles()) EXPECT_NE(visible->uniqueID, obstacle->uniqueID);
	const auto before = wide->getAvailableHealth();
	branch->evaluateLandingObstacleTriggersForUnit(*branch->getServerCallback(), *projected);
	EXPECT_EQ(projected->getAvailableHealth(), before); EXPECT_EQ(wide->getAvailableHealth(), before);
	EXPECT_FALSE(obstacle->revealed);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, FortifiedDefenderReturnOpensItsIntactGateBeforeLandingAndMatchesNative)
{
	NewHorizonsCoordinatedMarkFixture::prepare("core:lorelei", GetParam(), true, true);
	env = std::make_unique<CoordinatedMarkEnvironment>(gameState());
	const BattleHex origin(BattleHex::GATE_INNER), approach(8, 5);
	auto * walker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 4), 10);
	actor->setPosition(origin); victim->setPosition(BattleHex(7, 5)); beginCombat();
	ASSERT_EQ(actor->unitSide(), BattleSide::DEFENDER); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_GT(battle()->battleGetFortifications().wallsHealth, 0); ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	auto parent = simulation(PlayerColor(1)); DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()),
		parent->battleGetUnitByID(victim->unitId()), 0, false), approach, cache, parent);
	if(GetParam() == 0)
	{
		ASSERT_TRUE(preview.completedReturnDestination); EXPECT_EQ(*preview.completedReturnDestination, origin);
		EXPECT_EQ(preview.effectPreview->battleGetGateState(), EGateState::OPENED);
		auto selectedWorld = simulation(PlayerColor(1)); BattleExchangeVariant selected; selected.trackAttack(preview, selectedWorld, cache);
		EXPECT_EQ(selectedWorld->battleGetGateState(), EGateState::OPENED);
		EXPECT_EQ(selectedWorld->battleGetUnitByID(actor->unitId())->getPosition(), origin);
		auto evaluationEnv = std::make_shared<CoordinatedMarkEnvironment>(gameState());
		PotentialTargets candidates(parent->battleGetUnitByID(actor->unitId()), cache, parent);
		BattleExchangeEvaluator primed(parent, evaluationEnv, 1, 2); primed.updateReachabilityMap(parent);
		const auto closed = primed.getOneTurnReachableUnits(0, origin);
		const auto includesWalker = [&](const battle::Units & units)
		{ return std::ranges::any_of(units, [&](const battle::Unit * unit) { return unit->unitId() == walker->unitId(); }); };
		EXPECT_FALSE(includesWalker(closed));
		const auto rebound = primed.getExchangeUnits(preview, 0, candidates, parent);
		EXPECT_TRUE(rebound.enemyUnitsReachingAttacker.contains(walker->unitId()));
		BattleExchangeEvaluator fresh(preview.effectPreview, evaluationEnv, 1, 2); fresh.updateReachabilityMap(preview.effectPreview);
		const auto freshMembership = fresh.getExchangeUnits(preview, 0, candidates, parent);
		EXPECT_EQ(rebound.enemyUnitsReachingAttacker, freshMembership.enemyUnitsReachingAttacker);
		EXPECT_EQ(primed.getOneTurnReachableUnits(0, origin), closed);
		EXPECT_FALSE(parent->getReachability(parent->battleGetUnitByID(walker->unitId())).isReachable(BattleHex(BattleHex::GATE_OUTER)));
		HypotheticBattle nested(env.get(), preview.effectPreview);
		EXPECT_EQ(nested.battleGetGateState(), EGateState::OPENED);
	}
	else EXPECT_FALSE(preview.completedReturnDestination);
	EXPECT_EQ(parent->battleGetGateState(), EGateState::CLOSED); EXPECT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(actor, victim, approach, true)));
	EXPECT_EQ(actor->getPosition(), origin); EXPECT_EQ(battle()->battleGetGateState(), EGateState::OPENED);
	EXPECT_TRUE(battle()->getReachability(walker).isReachable(BattleHex(BattleHex::GATE_OUTER)));
	EXPECT_EQ(marks::hasMark(*victim), GetParam() == 0);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, BlockedFortifiedGateIsNotOpenedByTheReturnForecast)
{
	NewHorizonsCoordinatedMarkFixture::prepare("core:lorelei", GetParam(), true, true);
	env = std::make_unique<CoordinatedMarkEnvironment>(gameState());
	const BattleHex origin(BattleHex::GATE_BRIDGE), approach(8, 5);
	actor->setPosition(origin); victim->setPosition(BattleHex(7, 5)); beginCombat();
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::BLOCKED);
	auto parent = simulation(PlayerColor(1)); DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()),
		parent->battleGetUnitByID(victim->unitId()), 0, false), approach, cache, parent);
	if(GetParam() == 0 && preview.completedReturnDestination)
		EXPECT_EQ(preview.effectPreview->battleGetGateState(), EGateState::BLOCKED);
	EXPECT_EQ(parent->battleGetGateState(), EGateState::BLOCKED);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(actor, victim, approach, true)));
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::BLOCKED);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, HypnotizedHarpyChargesItsOwnLandingToTheActingController)
{
	NewHorizonsCoordinatedMarkFixture::prepare("core:lorelei", GetParam(), true, true);
	env = std::make_unique<CoordinatedMarkEnvironment>(gameState());
	actor->setPosition(BattleHex(14, 2));
	actor = addStack(BattleSide::ATTACKER, creatureByName("core:harpyHag"), BattleHex(3, 5), 20);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	int64_t wound = actor->getAvailableHealth() - 100; actor->damage(wound);
	addReturnMoat(actor->getPosition());
	auto moat = std::dynamic_pointer_cast<SpellCreatedObstacle>(battle()->obstacles.back()); ASSERT_NE(moat, nullptr);
	moat->casterSide = BattleSide::ATTACKER; // Hostile to the current controller, despite original army affiliation.
	auto withMoat = simulation(PlayerColor(1)), withoutMoat = simulation(PlayerColor(1));
	ASSERT_EQ(withMoat->battleGetActionController(withMoat->battleGetUnitByID(actor->unitId())), PlayerColor(1));
	ASSERT_EQ(actor->unitSide(), BattleSide::ATTACKER);
	withoutMoat->removeObstacle(731); DamageCache wetCache, dryCache;
	const auto wet = forecast(withMoat, wetCache, false), dry = forecast(withoutMoat, dryCache, false);
	if(GetParam() == 0)
	{
		ASSERT_TRUE(wet.completedReturnDestination);
		EXPECT_LT(wet.attackerState->getAvailableHealth(), dry.attackerState->getAvailableHealth());
		EXPECT_GT(wet.attackerDamageReduce, dry.attackerDamageReduce);
		EXPECT_FLOAT_EQ(wet.defenderDamageReduce, dry.defenderDamageReduce);
	}
	else EXPECT_FALSE(wet.completedReturnDestination);
	EXPECT_EQ(actor->getAvailableHealth(), 100); EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkAITest, VisibleEnemyObstaclePermissionRequiresTheActualDescriptorAndTrigger)
{
	prepare("core:lorelei", true);
	auto * wide = addStack(BattleSide::ATTACKER, creatureByName("core:behemoth"), BattleHex(5, 2), 10);
	addReturnMoat(wide->occupiedHex());
	auto branch = simulation(), sibling = simulation();
	auto projected = branch->getForUpdate(wide->unitId());
	const auto source = std::dynamic_pointer_cast<const SpellCreatedObstacle>(branch->battleGetObstacleByID(731));
	ASSERT_NE(source, nullptr); const auto trigger = source->getTrigger(); ASSERT_TRUE(trigger.hasValue());
	ASSERT_EQ(branch->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(branch->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	spells::ObstacleCasterProxy actual(branch->sideToPlayer(BattleSide::DEFENDER), nullptr, *source);
	const auto nativeBefore = wide->save(), projectedBefore = projected->save(), siblingBefore = sibling->battleGetUnitByID(wide->unitId())->acquireState()->save();
	const auto serverRandom = markRandomBytes(*gameHandler->randomizer);
	EXPECT_EQ(branch->battleCanCastSpell(&actual, spells::Mode::PASSIVE), ESpellCastProblem::INVALID);
	EXPECT_EQ(branch->battleCanCastSpell(&actual, spells::Mode::PASSIVE, trigger), ESpellCastProblem::OK);
	spells::BattleCast cast(branch.get(), &actual, spells::Mode::PASSIVE, trigger.toSpell());
	spells::detail::ProblemImpl problem;
	EXPECT_TRUE(cast.mechanicsForTarget({spells::Destination(projected.get())})->canBeCastAt({spells::Destination(projected.get())}, problem));
	const auto rejected = [&](const spells::Caster & caster, SpellID requested, spells::Mode mode)
	{
		EXPECT_EQ(branch->battleCanCastSpell(&caster, mode, requested), ESpellCastProblem::INVALID);
	};
	SpellCreatedObstacle copy = *source;
	spells::ObstacleCasterProxy copied(actual.getCasterOwner(), nullptr, copy);
	rejected(copied, trigger, spells::Mode::PASSIVE);
	spells::ObstacleCasterProxy wrongOwner(branch->sideToPlayer(BattleSide::ATTACKER), nullptr, *source);
	rejected(wrongOwner, trigger, spells::Mode::PASSIVE);
	spells::ObstacleCasterProxy hiddenHero(actual.getCasterOwner(), defenderSideHero, *source);
	rejected(hiddenHero, trigger, spells::Mode::PASSIVE);
	spells::SilentCaster foreign(actual.getCasterOwner(), nullptr);
	rejected(foreign, trigger, spells::Mode::PASSIVE);
	class DerivedCaster final : public spells::ObstacleCasterProxy { public: using ObstacleCasterProxy::ObstacleCasterProxy; };
	DerivedCaster derived(actual.getCasterOwner(), nullptr, *source);
	rejected(derived, trigger, spells::Mode::PASSIVE);
	rejected(actual, SpellID(SpellID::decode("core:castleMoat")), spells::Mode::PASSIVE);
	rejected(actual, trigger, spells::Mode::HERO);
	rejected(actual, trigger, spells::Mode::CREATURE_ACTIVE);
	spells::ObstacleCasterProxy nativeCaster(battle()->sideToPlayer(BattleSide::DEFENDER), defenderSideHero,
		*std::dynamic_pointer_cast<const SpellCreatedObstacle>(battle()->battleGetObstacleByID(731)));
	EXPECT_EQ(battle()->battleCanCastSpell(&nativeCaster, spells::Mode::PASSIVE), ESpellCastProblem::OK);
	EXPECT_EQ(battle()->battleCanCastSpell(&nativeCaster, spells::Mode::PASSIVE, trigger), ESpellCastProblem::OK);
	EXPECT_EQ(wide->save(), nativeBefore); EXPECT_EQ(projected->save(), projectedBefore);
	EXPECT_EQ(sibling->battleGetUnitByID(wide->unitId())->acquireState()->save(), siblingBefore);
	EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), serverRandom);
	EXPECT_EQ(branch->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, HiddenEnemyObstacleCannotBorrowEnvironmentalPermission)
{
	prepare("core:lorelei", true); addReturnMoat(actor->getPosition());
	auto source = std::dynamic_pointer_cast<SpellCreatedObstacle>(battle()->obstacles.back()); ASSERT_NE(source, nullptr);
	source->obstacleType = CObstacleInstance::SPELL_CREATED; source->hidden = true; source->nativeVisible = false;
	auto branch = simulation(); auto projected = branch->getForUpdate(actor->unitId());
	const auto before = projected->save(); const auto nativeBefore = actor->save();
	const auto random = markRandomBytes(*gameHandler->randomizer);
	EXPECT_TRUE(branch->battleGetAllObstacles().empty());
	spells::ObstacleCasterProxy caster(branch->sideToPlayer(BattleSide::DEFENDER), nullptr, *source);
	EXPECT_EQ(branch->battleCanCastSpell(&caster, spells::Mode::PASSIVE, source->getTrigger()), ESpellCastProblem::INVALID);
	branch->evaluateLandingObstacleTriggersForUnit(*branch->getServerCallback(), *projected);
	EXPECT_EQ(projected->save(), before); EXPECT_EQ(actor->save(), nativeBefore);
	EXPECT_FALSE(source->revealed); EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), random);
}
TEST_P(NewHorizonsCoordinatedMarkAITest, NeutralObstacleUsesNoHeroAndRetainsCanonicalLandingDamage)
{
	prepare("core:lorelei", true);
	auto * wide = addStack(BattleSide::ATTACKER, creatureByName("core:behemoth"), BattleHex(5, 2), 10);
	addReturnMoat(wide->occupiedHex());
	auto nativeSource = std::dynamic_pointer_cast<SpellCreatedObstacle>(battle()->obstacles.back()); ASSERT_NE(nativeSource, nullptr);
	nativeSource->casterSide = BattleSide::NONE;
	auto branch = simulation(); auto projected = branch->getForUpdate(wide->unitId());
	const auto source = std::dynamic_pointer_cast<const SpellCreatedObstacle>(branch->battleGetObstacleByID(731)); ASSERT_NE(source, nullptr);
	spells::ObstacleCasterProxy caster(branch->sideToPlayer(BattleSide::DEFENDER), nullptr, *source);
	EXPECT_EQ(branch->battleCanCastSpell(&caster, spells::Mode::PASSIVE, source->getTrigger()), ESpellCastProblem::OK);
	spells::ObstacleCasterProxy injectedHero(branch->sideToPlayer(BattleSide::ATTACKER), attackerSideHero, *source);
	EXPECT_EQ(branch->battleCanCastSpell(&injectedHero, spells::Mode::PASSIVE, source->getTrigger()), ESpellCastProblem::INVALID);
	const auto before = wide->getAvailableHealth();
	branch->evaluateLandingObstacleTriggersForUnit(*branch->getServerCallback(), *projected);
	EXPECT_EQ(projected->getAvailableHealth(), before - 70); EXPECT_EQ(wide->getAvailableHealth(), before);
	battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *wide);
	EXPECT_EQ(wide->getAvailableHealth(), projected->getAvailableHealth());
}
TEST_P(NewHorizonsCoordinatedMarkAITest, WrappedUnitAndGenericCasterCannotBorrowObstaclePermission)
{
	prepare("core:lorelei", true); addReturnMoat(actor->getPosition());
	const auto nativeSource = std::dynamic_pointer_cast<SpellCreatedObstacle>(battle()->obstacles.back()); ASSERT_NE(nativeSource, nullptr);
	auto neutral = std::make_shared<SpellCreatedObstacle>(*nativeSource);
	neutral->uniqueID = 732; neutral->casterSide = BattleSide::NONE; battle()->obstacles.push_back(neutral);
	auto branch = simulation();
	const auto source = std::dynamic_pointer_cast<const SpellCreatedObstacle>(branch->battleGetObstacleByID(731));
	const auto neutralSource = std::dynamic_pointer_cast<const SpellCreatedObstacle>(branch->battleGetObstacleByID(732));
	ASSERT_NE(source, nullptr); ASSERT_NE(neutralSource, nullptr);
	const auto owner = branch->sideToPlayer(BattleSide::DEFENDER); const auto trigger = source->getTrigger();
	const auto * unit = branch->battleGetUnitByID(victim->unitId()); ASSERT_NE(unit, nullptr);
	ASSERT_EQ(unit->getCasterOwner(), owner); ASSERT_EQ(unit->getHeroCaster(), nullptr);
	spells::SilentCaster generic(owner, nullptr);
	const auto before = actor->save(); const auto random = markRandomBytes(*gameHandler->randomizer);
	for(const auto * descriptor : {source.get(), neutralSource.get()})
	{
		spells::ObstacleCasterProxy valid(owner, nullptr, *descriptor);
		EXPECT_EQ(branch->battleCanCastSpell(&valid, spells::Mode::PASSIVE, trigger), ESpellCastProblem::OK);
		spells::ObstacleCasterProxy wrappedUnit(owner, unit, *descriptor);
		spells::ObstacleCasterProxy wrappedGeneric(owner, &generic, *descriptor);
		EXPECT_EQ(wrappedUnit.getHeroCaster(), nullptr); EXPECT_EQ(wrappedGeneric.getHeroCaster(), nullptr);
		EXPECT_EQ(branch->battleCanCastSpell(&wrappedUnit, spells::Mode::PASSIVE, trigger), ESpellCastProblem::INVALID);
		EXPECT_EQ(branch->battleCanCastSpell(&wrappedGeneric, spells::Mode::PASSIVE, trigger), ESpellCastProblem::INVALID);
	}
	auto known = simulation(PlayerColor(1));
	const auto knownSource = std::dynamic_pointer_cast<const SpellCreatedObstacle>(known->battleGetObstacleByID(731));
	ASSERT_NE(knownSource, nullptr);
	const auto * visibleHero = known->battleGetFightingHero(BattleSide::DEFENDER); ASSERT_NE(visibleHero, nullptr);
	spells::ObstacleCasterProxy realHero(owner, visibleHero, *knownSource);
	EXPECT_EQ(known->battleCanCastSpell(&realHero, spells::Mode::PASSIVE, trigger), ESpellCastProblem::OK);
	spells::SilentCaster heroWrapper(owner, visibleHero);
	spells::ObstacleCasterProxy nestedHero(owner, &heroWrapper, *knownSource);
	EXPECT_EQ(nestedHero.getHeroCaster(), visibleHero);
	EXPECT_EQ(known->battleCanCastSpell(&nestedHero, spells::Mode::PASSIVE, trigger), ESpellCastProblem::INVALID);
	EXPECT_EQ(actor->save(), before); EXPECT_EQ(markRandomBytes(*gameHandler->randomizer), random);
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsCoordinatedMarkAITest, ::testing::Values(0, 1, 2));
