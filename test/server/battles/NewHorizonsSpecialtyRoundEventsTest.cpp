/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyEventFixture.h"
#include "../../../server/battles/BattleRoundHealing.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
class RecoveryVisualRecorder : public RecordingGameServer
{
	const CStack & target;
public:
	std::vector<char> packetOrder;
	std::vector<int64_t> healthAtAnimation;
	explicit RecoveryVisualRecorder(const CStack & target) : target(target) {}
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<const BattleUnitsChanged *>(&pack))
			packetOrder.push_back('u');
		if(dynamic_cast<const BattleAnimationPlayed *>(&pack))
		{
			packetOrder.push_back('a');
			healthAtAnimation.push_back(target.getAvailableHealth());
		}
		RecordingGameServer::applyPack(pack);
	}
};

void expectRecoveryVisual(const RecoveryVisualRecorder & server, const CStack & target)
{
	EXPECT_EQ(server.packetOrder, (std::vector<char>{'u', 'a'}));
	EXPECT_EQ(server.healthAtAnimation, (std::vector<int64_t>{target.getAvailableHealth()}));
	ASSERT_EQ(server.battleAnimations.size(), 1u);
	const auto & visual = server.battleAnimations.front();
	EXPECT_EQ(visual.animation, AnimationPath::builtin("SP12_.DEF"));
	EXPECT_EQ(visual.sound, AudioPath::builtin("REGENER"));
	EXPECT_FLOAT_EQ(visual.transparency, 0.5f);
	EXPECT_FALSE(visual.deferred);
	ASSERT_EQ(visual.targets.size(), 1u);
	EXPECT_EQ(visual.targets.front().unitID, static_cast<int32_t>(target.unitId()));
	EXPECT_EQ(visual.targets.front().tile, target.getPosition());
	EXPECT_TRUE(server.casts.empty());
	EXPECT_TRUE(server.startedActions.empty());
	EXPECT_TRUE(server.orderStateUpdates.empty());
}
}

class NewHorizonsSpecialtyRoundEventsTest : public NewHorizonsSpecialtyEventFixture {};
TEST_F(NewHorizonsSpecialtyRoundEventsTest, RisingTideAddsExactlyOneWithoutHealingWoundsOrRepeating)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	int64_t damage = 5; state->health.damage(damage);
	const auto before = state->health.getCreatureHealthAvailable();
	const auto wounds = state->getSurvivingMissingHealth();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), 6);
	EXPECT_EQ(state->health.getResurrected(), 1);
	EXPECT_EQ(state->health.getCreatureHealthAvailable(), before + state->getMaxHealth());
	EXPECT_EQ(state->getSurvivingMissingHealth(), wounds);
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	state->health.takeResurrected();
	EXPECT_EQ(state->getCount(), 5);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, RisingTidePreservesWoundedCapacityCohorts)
{
	prepare("core:kalt", "core:iceElemental");
	auto state = actor->acquireState();
	state->health.preserveCapacityHealth();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(), BonusValueType::PERCENT_TO_ALL));
	state->normalizeCapacityHealth();
	const auto missing = state->getSurvivingMissingHealth();
	ASSERT_GT(missing, 0);
	const auto before = state->health.getCreatureHealthAvailable();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), 6);
	EXPECT_EQ(state->health.getCreatureHealthAvailable(), before + state->getMaxHealth());
	EXPECT_EQ(state->getSurvivingMissingHealth(), missing);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, CreatedTemporaryMembersDieBeforePermanentCasualtiesAndNeverBecomePermanent)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	const auto health = state->getMaxHealth();
	int64_t damage = 2 * health;
	state->health.damage(damage, false, false, battle::DamageProvenance::SPELL);
	ASSERT_EQ(state->getCount(), 3);
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	ASSERT_EQ(state->getCount(), 4);
	damage = health;
	state->health.damage(damage, false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(state->getCount(), 3);
	EXPECT_EQ(state->health.getResurrected(), 0);
	EXPECT_EQ(state->health.getCasualtyCount(battle::DamageProvenance::SPELL), 2);
	int64_t restoration = 100 * health;
	state->heal(restoration, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	state->health.takeResurrected();
	EXPECT_EQ(state->getCount(), 5);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, SummonedWaterGrowsOnOwnedStateWithoutChangingTheSource)
{
	prepare("core:kalt", "core:waterElemental");
	auto summoned = actor->acquireState();
	summoned->summoned = true;
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *summoned, 1));
	EXPECT_EQ(summoned->getCount(), 6);
	EXPECT_EQ(actor->getCount(), 5);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, WaterTransformationPausesGrowthAndReversionCannotBackpay)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	state->beginBattleForm(CreatureID(CreatureID::decode("core:pikeman")), 2);
	const auto before = state->getCount();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), before);
	state->endBattleForm();
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 2));
	EXPECT_EQ(state->getCount(), 6);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, NativeWaterBorrowingIceBodyPausesUntilNativeRestoration)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	state->beginBattleForm(CreatureID(CreatureID::decode("core:iceElemental")), 2);
	const auto before = state->getCount();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), before);
	state->endBattleForm();
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 2));
	EXPECT_EQ(state->getCount(), 6);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, ForeignNativeFamilyCannotGainRisingTideByTakingWaterForm)
{
	prepare("core:kalt", "core:goblin");
	auto state = actor->acquireState();
	state->beginBattleForm(CreatureID(CreatureID::decode("core:waterElemental")), 2);
	const auto before = state->getCount();
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), before);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, UndyingHealsSurvivorsThenCasualtiesWithinOneHundredAndInitialCount)
{
	prepare("core:straker", "core:walkingDead", 20);
	auto state = actor->acquireState();
	int64_t damage = 8 * state->getMaxHealth() + state->getMaxHealth() / 2;
	state->health.damage(damage);
	const auto before = state->health.getCreatureHealthAvailable();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->health.getCreatureHealthAvailable() - before, std::min<int64_t>(100, damage));
	EXPECT_LE(state->getCount(), 20);
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 2));
	EXPECT_LE(state->getCount(), 20);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, UndyingDoesNotRestoreDestroyedStacksOrUnusableRemains)
{
	prepare("core:straker", "core:zombieLord", 20);
	auto state = actor->acquireState();
	int64_t damage = 5 * state->getMaxHealth();
	state->health.damage(damage, true);
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), 15);
	damage = state->getAvailableHealth(); state->health.damage(damage);
	ASSERT_FALSE(state->alive());
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 2));
	EXPECT_FALSE(state->alive());
	EXPECT_EQ(state->getCount(), 0);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, RoundReceiptCopiesRoundtripsAndRejectsMalformedInputBeforeMutation)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	int64_t damage = 7;
	state->health.damage(damage);
	state->afterAttack(false, true);
	const auto saved = state->save();
	UnitChanges sent(actor->unitId(), UnitChanges::EOperation::UPDATE);
	sent.data = saved;
	CMemorySerializer wire;
	wire.oser & sent;
	EXPECT_EQ(sent.data, saved); // Preflight must not insert omitted default fields.
	UnitChanges received;
	wire.iser & received;
	EXPECT_EQ(received.id, actor->unitId());
	EXPECT_EQ(received.data, saved);
	auto restored = actor->acquireState();
	restored->load(received.data);
	EXPECT_EQ(restored->save(), saved);
	EXPECT_EQ(restored->health.getResurrected(), state->health.getResurrected());
	EXPECT_EQ(restored->getCount(), state->getCount());
	EXPECT_EQ(restored->ordinarySpecialtyLastRound, 1);
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *restored, 1));
	const auto before = restored->save();
	auto malformed = saved;
	malformed["state"]["ordinarySpecialtyLastRound"].Float() = 1.5;
	EXPECT_THROW(restored->load(malformed), std::runtime_error);
	EXPECT_EQ(restored->save(), before);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, NativeRoundPublicationAddsOneAndPublishesTheSpentReceipt)
{
	prepare("core:kalt", "core:waterElemental");
	BattleNextRound next; next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_EQ(actor->getCount(), 6);
	EXPECT_EQ(actor->ordinarySpecialtyLastRound, battle()->getRound());
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_EQ(actor->getCount(), 6);
}

TEST_F(NewHorizonsSpecialtyRoundEventsTest, RisingTideIsCountGrowthAndIgnoresHealingReceivedReduction)
{
	prepare("core:kalt", "core:waterElemental");
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS, BonusSource::OTHER, 10000, BonusSourceID(),
		BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	auto state = actor->acquireState();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), 6);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, UndyingUsesOrdinaryReceivedHealingReduction)
{
	prepare("core:straker", "core:walkingDead", 20);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS, BonusSource::OTHER, 5000, BonusSourceID(),
		BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	auto state = actor->acquireState();
	int64_t damage = 8 * state->getMaxHealth(); state->health.damage(damage);
	const auto before = state->health.getCreatureHealthAvailable();
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->health.getCreatureHealthAvailable() - before, 50);
}
TEST_F(NewHorizonsSpecialtyRoundEventsTest, HeldRoundIsConsumedAndCannotBeReplayedAfterStasisEnds)
{
	prepare("core:kalt", "core:waterElemental");
	auto stop = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	actor->addNewBonus(stop);
	auto state = actor->acquireState();
	ASSERT_TRUE(state->isTimeStopped());
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	EXPECT_EQ(state->getCount(), 5);
	actor->removeBonus(stop);
	EXPECT_FALSE(state->isTimeStopped());
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 1));
	ASSERT_TRUE(newHorizonsSpecialtyEvents::applyRound(*battle(), *state, 2));
	EXPECT_EQ(state->getCount(), 6);
}

TEST_F(NewHorizonsSpecialtyRoundEventsTest, UndyingPublishesHydraVisualOnlyAfterActualRestoration)
{
	prepare("core:straker", "core:walkingDead", 20);
	auto state = actor->acquireState();
	int64_t damage = 5 * state->getMaxHealth() + 3;
	state->health.damage(damage);
	publish(*state);
	const auto before = actor->getAvailableHealth();
	const auto countBefore = actor->getCount();
	RecoveryVisualRecorder recorder(*actor);
	recorder.gameState = gameState();
	CGameHandler handler(recorder, gameState());
	BattleNextRound next; next.battleID = battle()->getBattleID();
	handler.sendAndApply(next);
	battleRoundHealing::afterRound(handler, *battle());
	EXPECT_EQ(actor->getAvailableHealth(), before + std::min<int64_t>(100, damage));
	EXPECT_GT(actor->getCount(), countBefore);
	EXPECT_LE(actor->getCount(), 20);
	expectRecoveryVisual(recorder, *actor);
	battleRoundHealing::afterRound(handler, *battle());
	expectRecoveryVisual(recorder, *actor); // Re-reading the spent round adds no visual or recovery.
}

TEST_F(NewHorizonsSpecialtyRoundEventsTest, RisingTidePublishesTentVisualWithoutHealingOrCasting)
{
	prepare("core:kalt", "core:waterElemental");
	auto state = actor->acquireState();
	int64_t damage = 5;
	state->health.damage(damage);
	publish(*state);
	const auto before = actor->getAvailableHealth();
	const auto wounds = actor->getSurvivingMissingHealth();
	RecoveryVisualRecorder recorder(*actor);
	recorder.gameState = gameState();
	CGameHandler handler(recorder, gameState());
	BattleNextRound next; next.battleID = battle()->getBattleID();
	handler.sendAndApply(next);
	battleRoundHealing::afterRound(handler, *battle());
	EXPECT_EQ(actor->getCount(), 6);
	EXPECT_EQ(actor->getAvailableHealth(), before + actor->getMaxHealth());
	EXPECT_EQ(actor->getSurvivingMissingHealth(), wounds);
	EXPECT_EQ(actor->health.getResurrected(), 1);
	expectRecoveryVisual(recorder, *actor);
	battleRoundHealing::afterRound(handler, *battle());
	expectRecoveryVisual(recorder, *actor);
}

TEST_F(NewHorizonsSpecialtyRoundEventsTest, FullAndFullyBlockedUndyingDoNotPublishFalseRecoveryVisuals)
{
	prepare("core:straker", "core:zombieLord", 20);
	BattleNextRound next; next.battleID = battle()->getBattleID();
	gameHandler->sendAndApply(next);
	const auto fullHealth = actor->getAvailableHealth();
	server.battleAnimations.clear();
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_EQ(actor->getAvailableHealth(), fullHealth);
	EXPECT_TRUE(server.battleAnimations.empty());

	auto state = actor->acquireState();
	int64_t damage = 150;
	state->health.damage(damage);
	publish(*state);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS, BonusSource::OTHER, 10000,
		BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	gameHandler->sendAndApply(next);
	const auto woundedHealth = actor->getAvailableHealth();
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_EQ(actor->getAvailableHealth(), woundedHealth);
	EXPECT_TRUE(server.battleAnimations.empty());
}

TEST_F(NewHorizonsSpecialtyRoundEventsTest, StoppedAndDestroyedWaterStacksDoNotPublishFalseGrowthVisuals)
{
	prepare("core:kalt", "core:iceElemental");
	auto stop = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	actor->addNewBonus(stop);
	BattleNextRound next; next.battleID = battle()->getBattleID();
	gameHandler->sendAndApply(next);
	const auto before = actor->getAvailableHealth();
	server.battleAnimations.clear();
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_EQ(actor->getCount(), 5);
	EXPECT_EQ(actor->getAvailableHealth(), before);
	EXPECT_TRUE(server.battleAnimations.empty());

	actor->removeBonus(stop);
	auto state = actor->acquireState();
	int64_t damage = state->getAvailableHealth();
	state->health.damage(damage);
	publish(*state);
	ASSERT_FALSE(actor->alive());
	gameHandler->sendAndApply(next);
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_FALSE(actor->alive());
	EXPECT_TRUE(server.battleAnimations.empty());
}
