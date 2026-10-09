/*
 * BattleEffectExchangeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/modding/CModHandler.h"
#ifdef ENABLE_BATTLE_AI
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif

namespace
{
#ifdef ENABLE_BATTLE_AI
class ExchangeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ExchangeEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

Bonus spellEffect(int value, int turns)
{
	Bonus bonus(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		value, BonusSourceID(SpellID(SpellID::HASTE)));
	bonus.turnsRemain = turns;
	bonus.spellCasterOwner = PlayerColor(0);
	bonus.statusTags = {BonusStatusTag::DEBUFF};
	bonus.statusIdentity = "exchange-test";
	return bonus;
}
}

class BattleEffectExchangeTest : public HeroCommandFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void prepare()
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
		beginCombat();
		first->addNewBonus(std::make_shared<Bonus>(spellEffect(2, 3)));
		second->addNewBonus(std::make_shared<Bonus>(spellEffect(4, 1)));
	}
	battle::BattleEffectExchange plan(const IBattleState & state) const
	{
		battle::BattleEffectExchange result;
		result.endpoints[0].id = first->unitId();
		result.endpoints[1].id = second->unitId();
		for(auto & endpoint : result.endpoints)
			endpoint.replacement = endpoint.expected = state.captureBattleEffects(endpoint.id);
		std::swap(result.endpoints[0].replacement.effects, result.endpoints[1].replacement.effects);
		std::swap(result.endpoints[0].replacement.sidecars, result.endpoints[1].replacement.sidecars);
		return result;
	}
	void apply(const battle::BattleEffectExchange & exchange)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		packet.exchange = exchange;
		gameHandler->sendAndApply(packet);
	}
};

TEST_F(BattleEffectExchangeTest, ExactDuplicatesAndDurationsBypassOrdinaryRefresh)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(spellEffect(2, 2)));
	const auto exchange = plan(*battle());
	ASSERT_NO_THROW(apply(exchange));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(battle()->captureBattleEffects(first->unitId()),
		exchange.endpoints[0].replacement));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(battle()->captureBattleEffects(second->unitId()),
		exchange.endpoints[1].replacement));
	ASSERT_EQ(battle()->captureBattleEffects(second->unitId()).effects.size(), 2u);
	EXPECT_EQ(battle()->captureBattleEffects(second->unitId()).effects[0].turnsRemain, 3);
	EXPECT_EQ(battle()->captureBattleEffects(second->unitId()).effects[1].turnsRemain, 2);
}

TEST_F(BattleEffectExchangeTest, StaleSecondEndpointCannotChangeFirst)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto exchange = plan(*battle());
	exchange.endpoints[1].expected.effects[0].val++;
	const auto beforeFirst = battle()->captureBattleEffects(first->unitId());
	const auto beforeSecond = battle()->captureBattleEffects(second->unitId());
	EXPECT_THROW(battle()->exchangeBattleEffects(exchange), std::runtime_error);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(beforeFirst, battle()->captureBattleEffects(first->unitId())));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(beforeSecond, battle()->captureBattleEffects(second->unitId())));
}

TEST_F(BattleEffectExchangeTest, InvalidSecondReplacementCannotChangeEitherEndpoint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto exchange = plan(*battle());
	exchange.endpoints[1].replacement.sidecars.guardianSpiritHitPoints = -1;
	EXPECT_THROW(battle()->exchangeBattleEffects(exchange), std::runtime_error);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(exchange.endpoints[0].expected,
		battle()->captureBattleEffects(first->unitId())));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(exchange.endpoints[1].expected,
		battle()->captureBattleEffects(second->unitId())));
}

TEST_F(BattleEffectExchangeTest, RecipientHealthChangeMakesPlanStale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto exchange = plan(*battle());
	int64_t damage = 1;
	second->damage(damage);
	const auto damaged = second->save()["state"]["health"];
	ASSERT_TRUE(damaged.isStruct());
	EXPECT_THROW(battle()->exchangeBattleEffects(exchange), std::runtime_error);
	EXPECT_EQ(second->save()["state"]["health"], damaged);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(exchange.endpoints[0].expected,
		battle()->captureBattleEffects(first->unitId())));
}

TEST_F(BattleEffectExchangeTest, SidecarPoolsMoveWithoutRefreshAndConfusionHistoryStaysLocal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto guardian = spellEffect(200, 2);
	guardian.type = BonusType::GUARDIAN_SPIRIT;
	guardian.sid = BonusSourceID(SpellID(SpellID::decode("new-horizons:guardianSpirit")));
	first->addNewBonus(std::make_shared<Bonus>(guardian));
	auto regeneration = spellEffect(20, 3);
	regeneration.type = BonusType::HP_REGENERATION;
	regeneration.sid = BonusSourceID(SpellID(SpellID::decode("new-horizons:regeneration")));
	first->addNewBonus(std::make_shared<Bonus>(regeneration));
	first->regenerationRateMillionths = 200'000;
	first->regenerationPendingMicroHealth = 1'500'000;
	first->guardianSpiritHitPoints = 17;
	first->guardianSpiritRoundsRemaining = 2;
	// A carried capacity-regeneration fraction requires a recipient-local
	// capacity basis and ledger on both sides, even when no capacity is changed.
	first->preserveCreatureHealthOnCapacityIncrease();
	second->preserveCreatureHealthOnCapacityIncrease();
	// Canonicalize full-health cohorts before capturing exact JSON. Atomic
	// projection performs this same normalization without changing recipient HP.
	first->normalizeCapacityHealth();
	second->normalizeCapacityHealth();
	const auto firstHealth = first->save()["state"]["health"];
	const auto secondHealth = second->save()["state"]["health"];
	ASSERT_TRUE(firstHealth.isStruct());
	ASSERT_TRUE(secondHealth.isStruct());
	first->capacityRegenerationRemainderTenths = 7;
	first->confusionState.applyPending(PlayerColor(0), true);
	first->confusionState.previousResolved = battle::ConfusionBehavior::ATTACK;
	second->confusionState.previousResolved = battle::ConfusionBehavior::WANDER;
	const auto marker = newHorizonsConfusionControl::pendingMarker(
		SpellID(SpellID::decode("new-horizons:confusion")), PlayerColor(0), true);
	first->addNewBonus(std::make_shared<Bonus>(marker));
	const auto exchange = plan(*battle());
	ASSERT_NO_THROW(apply(exchange));
	EXPECT_EQ(second->guardianSpiritHitPoints, 17);
	EXPECT_EQ(second->guardianSpiritRoundsRemaining, 2);
	EXPECT_EQ(second->regenerationPendingMicroHealth, 1'500'000);
	EXPECT_EQ(second->capacityRegenerationRemainderTenths, 7);
	EXPECT_EQ(first->save()["state"]["health"], firstHealth);
	EXPECT_EQ(second->save()["state"]["health"], secondHealth);
	EXPECT_EQ(first->getAvailableHealth(), 100);
	EXPECT_EQ(second->getAvailableHealth(), 100);
	EXPECT_TRUE(second->confusionState.pendingConfounder);
	EXPECT_EQ(second->confusionState.previousResolved, battle::ConfusionBehavior::WANDER);
	EXPECT_EQ(first->confusionState.previousResolved, battle::ConfusionBehavior::ATTACK);
	EXPECT_FALSE(first->confusionState.pending);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(BattleEffectExchangeTest, LiveAndNestedDetachedExchangeMatchWithoutMutatingParent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ExchangeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	parent->getForUpdate(first->unitId());
	parent->getForUpdate(second->unitId());
	HypotheticBattle child(&environment, parent);
	const auto exchange = plan(child);
	ASSERT_NO_THROW(child.exchangeBattleEffects(exchange));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(parent->captureBattleEffects(first->unitId()),
		exchange.endpoints[0].expected));
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(battle()->captureBattleEffects(first->unitId()),
		exchange.endpoints[0].expected));
	ASSERT_NO_THROW(apply(exchange));
	for(const auto & endpoint : exchange.endpoints)
		EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(child.captureBattleEffects(endpoint.id),
			battle()->captureBattleEffects(endpoint.id)));
}
#endif

TEST_F(BattleEffectExchangeTest, OlderWriterRejectsExchangeAndNonTransferableBeforePacketPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.exchange = plan(*battle());
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_SAFE_BATTLE_FORMS;
	EXPECT_THROW(packet.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	packet.exchange.reset();
	auto effect = spellEffect(2, 1);
	effect.statusTags.push_back(BonusStatusTag::NON_TRANSFERABLE);
	packet.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{effect});
	EXPECT_THROW(packet.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}

TEST_F(BattleEffectExchangeTest, OrdinaryPacketRemainsOlderCompatibleAndExchangeIsExclusive)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{spellEffect(1, 2)});
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_SAFE_BATTLE_FORMS;
	EXPECT_NO_THROW(packet.serialize(writer.oser));
	EXPECT_FALSE(writer.extractBuffer().empty());
	packet.exchange = plan(*battle());
	EXPECT_THROW(packet.validateExchange(), std::runtime_error);
	packet.toAdd.clear();
	packet.exchange->endpoints[1].id = first->unitId();
	EXPECT_THROW(packet.validateExchange(), std::runtime_error);
}

TEST_F(BattleEffectExchangeTest, SecondEndpointStagingFailureLeavesBothLogicalStatesUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto exchange = plan(*battle());
	auto capacity = spellEffect(10, 2);
	capacity.type = BonusType::STACK_HEALTH;
	capacity.limiter = std::make_shared<CCreatureTypeLimiter>(*second->unitType(), false);
	exchange.endpoints[1].replacement.effects.push_back(capacity);
	EXPECT_THROW(battle()->exchangeBattleEffects(exchange), std::runtime_error);
	for(const auto & endpoint : exchange.endpoints)
		EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(endpoint.expected,
			battle()->captureBattleEffects(endpoint.id)));
}

TEST_F(BattleEffectExchangeTest, CapacityTransferPreservesRecipientBodyAndCasualtyLedgersWithoutHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t damage = 20;
	first->damage(damage);
	first->preserveCreatureHealthOnCapacityIncrease();
	auto capacity = spellEffect(10, 3);
	capacity.type = BonusType::STACK_HEALTH;
	capacity.sid = BonusSourceID(SpellID(SpellID::decode("new-horizons:hydrasVitality")));
	first->addNewBonus(std::make_shared<Bonus>(capacity));
	first->normalizeCapacityHealth();
	ASSERT_EQ(first->getAvailableHealth(), 80);
	ASSERT_EQ(second->getAvailableHealth(), 100);
	const auto exchange = plan(*battle());
	ASSERT_NO_THROW(apply(exchange));
	EXPECT_EQ(first->getAvailableHealth(), 80);
	EXPECT_EQ(first->getCount(), 8);
	EXPECT_EQ(second->getAvailableHealth(), 100);
	EXPECT_EQ(second->getCount(), 10);
	EXPECT_EQ(second->getMaxHealth(), 20);
	EXPECT_EQ(second->getCapacityHealthReferenceMax(), 10);
	EXPECT_EQ(first->health.getUnusableRemains(), 0);
	EXPECT_EQ(second->health.getUnusableRemains(), 0);
}

TEST_F(BattleEffectExchangeTest, CurrentPacketRoundTripPreservesExactDynamicOwnersAndMetadata)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto exchange = plan(*battle());
	exchange.endpoints[0].replacement.effects[0].bonusOwner = PlayerColor(1);
	exchange.endpoints[0].replacement.effects[0].statusTags.push_back(BonusStatusTag::NON_TRANSFERABLE);
	SetStackEffect original;
	original.battleID = BattleID(0);
	original.exchange = exchange;
	CMemorySerializer memory;
	ASSERT_NO_THROW(original.serialize(memory.oser));
	SetStackEffect decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	ASSERT_TRUE(decoded.exchange.has_value());
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(decoded.exchange->endpoints[0].replacement,
		exchange.endpoints[0].replacement));
	EXPECT_EQ(decoded.exchange->endpoints[0].replacement.effects[0].bonusOwner, PlayerColor(1));
}

TEST_F(BattleEffectExchangeTest, ConfusionReplacementRequiresCoherenceAndCannotTransferLegacyMarkerlessPending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = plan(*battle());
	const auto marker = newHorizonsConfusionControl::pendingMarker(
		SpellID(SpellID::decode("new-horizons:confusion")), PlayerColor(0), true);
	for(int malformed = 0; malformed < 5; ++malformed)
	{
		SCOPED_TRACE(malformed);
		auto exchange = original;
		auto & replacement = exchange.endpoints[1].replacement;
		replacement.sidecars.confusionPending = true;
		replacement.sidecars.confusionCaster = PlayerColor(0);
		replacement.sidecars.confusionConfounder = true;
		if(malformed != 0)
			replacement.effects.push_back(marker);
		if(malformed == 1)
			replacement.sidecars.confusionCaster = PlayerColor(1);
		else if(malformed == 2)
			replacement.sidecars.confusionConfounder = false;
		else if(malformed == 3)
		{
			replacement.sidecars.confusionPending = false;
			replacement.sidecars.confusionCaster = PlayerColor::CANNOT_DETERMINE;
			replacement.sidecars.confusionConfounder = false;
		}
		else if(malformed == 4)
			replacement.effects.push_back(marker);
		EXPECT_THROW(battle()->exchangeBattleEffects(exchange), std::runtime_error);
		for(const auto & endpoint : original.endpoints)
			EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(endpoint.expected,
				battle()->captureBattleEffects(endpoint.id)));
	}

	// Supported older snapshots may contain pending state without the later
	// marker representation. It remains executable in place, never transferable.
	first->confusionState.applyPending(PlayerColor(0), true);
	auto legacy = plan(*battle());
	EXPECT_THROW(battle()->exchangeBattleEffects(legacy), std::runtime_error);
	for(auto & endpoint : legacy.endpoints)
		endpoint.replacement.sidecars = endpoint.expected.sidecars;
	ASSERT_NO_THROW(battle()->exchangeBattleEffects(legacy));
	EXPECT_TRUE(first->confusionState.pending);
	EXPECT_EQ(first->confusionState.pendingCaster, PlayerColor(0));
	EXPECT_TRUE(first->confusionState.pendingConfounder);
	EXPECT_FALSE(second->confusionState.pending);
}
