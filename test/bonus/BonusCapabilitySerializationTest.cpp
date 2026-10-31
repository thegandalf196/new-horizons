/*
 * BonusCapabilitySerializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto previous = static_cast<ESerializationVersion>(1144);
Bonus ordinary()
{
	return {BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};
}
Bonus marker(int value = 20)
{
	auto result = ordinary();
	result.type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
	result.val = value;
	return result;
}
template<typename T> void expectRejectedBeforePrefix(T & source)
{
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & source);
	EXPECT_FALSE(current.extractBuffer().empty());
	CMemorySerializer historical;
	historical.oser.version = previous;
	EXPECT_THROW(historical.oser & source, std::runtime_error);
	EXPECT_TRUE(historical.extractBuffer().empty());
}
battle::BattleEffectExchange exchange()
{
	battle::BattleEffectExchange result;
	for(size_t i = 0; i < result.endpoints.size(); ++i)
	{
		auto & endpoint = result.endpoints[i];
		endpoint.id = static_cast<uint32_t>(i + 1);
		for(auto * snapshot : {&endpoint.expected, &endpoint.replacement})
		{
			snapshot->recipientHealth.Struct();
			auto bonus = ordinary();
			bonus.source = BonusSource::SPELL_EFFECT;
			snapshot->effects.push_back(bonus);
		}
	}
	return result;
}

TEST(BonusCapabilitySerialization, OrdinaryPayloadRetainsCurrentAnd1144Bytes)
{
	auto source = ordinary();
	CMemorySerializer current, historical;
	historical.oser.version = previous;
	ASSERT_NO_THROW(current.oser & source);
	ASSERT_NO_THROW(historical.oser & source);
	EXPECT_EQ(current.extractBuffer(), historical.extractBuffer());
}

TEST(BonusCapabilitySerialization, DirectNewTypeIncludesZeroValue)
{
	for(const int value : {0, 20})
	{
		auto source = marker(value);
		expectRejectedBeforePrefix(source);
		CMemorySerializer current;
		current.oser & source;
		Bonus restored;
		ASSERT_NO_THROW(current.iser & restored);
		EXPECT_EQ(restored.type, source.type);
		EXPECT_EQ(restored.val, source.val);
		EXPECT_EQ(restored.duration, source.duration);
	}
}

TEST(BonusCapabilitySerialization, DirectReadRejectsNewEnumTagged1144)
{
	auto source = marker(); // No new limiter fields: the old decoder reaches the enum check.
	CMemorySerializer wire;
	wire.oser & source;
	wire.iser.version = previous;
	Bonus restored;
	EXPECT_THROW(wire.iser & restored, std::runtime_error);
}

TEST(BonusCapabilitySerialization, StoredListRejectsBeforeCountAfterOrdinaryEntry)
{
	BonusList source;
	source.push_back(std::make_shared<Bonus>(ordinary()));
	CMemorySerializer historical;
	historical.oser.version = previous;
	ASSERT_NO_THROW(historical.oser & source);
	source.push_back(std::make_shared<Bonus>(marker()));
	expectRejectedBeforePrefix(source);
}

TEST(BonusCapabilitySerialization, NodeRejectsBeforeNodeTypeAndReadDoesNotExport)
{
	CBonusSystemNode source(BonusNodeType::ARMY);
	source.addNewBonus(std::make_shared<Bonus>(ordinary()));
	source.addNewBonus(std::make_shared<Bonus>(marker()));
	expectRejectedBeforePrefix(source);
	CMemorySerializer wire;
	wire.oser & source;
	wire.iser.version = previous;
	wire.iser.loadingGamestate = true;
	CBonusSystemNode restored(BonusNodeType::ARMY);
	EXPECT_THROW(wire.iser & restored, std::runtime_error);
	// The partially decoded node is discarded; no rollback of exported storage is promised.
	EXPECT_EQ(restored.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST(BonusCapabilitySerialization, CurrentNodeRoundtripRetainsStoredBonusesAndExports)
{
	CBonusSystemNode source(BonusNodeType::ARMY);
	source.addNewBonus(std::make_shared<Bonus>(ordinary()));
	source.addNewBonus(std::make_shared<Bonus>(marker()));
	CMemorySerializer wire;
	wire.oser & source;
	wire.iser.loadingGamestate = true;
	CBonusSystemNode restored(BonusNodeType::ARMY);
	ASSERT_NO_THROW(wire.iser & restored);
	ASSERT_EQ(restored.getExportedBonusList().size(), 2u);
	EXPECT_EQ(restored.valOfBonuses(BonusType::STACKS_SPEED), 7);
	EXPECT_EQ(restored.valOfBonuses(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION), 20);
}

TEST(BonusCapabilitySerialization, ParentAndCachedBonusesAreNotOwnedPayload)
{
	CBonusSystemNode parent(BonusNodeType::ARMY), child(BonusNodeType::ARMY);
	parent.addNewBonus(std::make_shared<Bonus>(marker()));
	child.attachTo(parent);
	EXPECT_EQ(child.valOfBonuses(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION), 20);
	CMemorySerializer historical;
	historical.oser.version = previous;
	ASSERT_NO_THROW(historical.oser & child);
	child.detachFrom(parent);
}

TEST(BonusCapabilitySerialization, FilterOnlyBonusUsesSameOuterCapability)
{
	BonusList source;
	auto bonus = std::make_shared<Bonus>(ordinary());
	auto limiter = std::make_shared<UnitAdjacentLimiter>();
	limiter->targetUnit = CreatureID(0);
	limiter->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	bonus->limiter = limiter;
	source.push_back(bonus);
	expectRejectedBeforePrefix(source);
}

TEST(BonusCapabilitySerialization, AllFourExchangeSnapshotsProtectEveryEnvelope)
{
	for(size_t endpoint = 0; endpoint < 2; ++endpoint)
		for(const bool replacement : {false, true})
		{
			auto value = exchange();
			SetStackEffect clean;
			clean.battleID = BattleID(0);
			clean.exchange = value;
			CMemorySerializer old;
			old.oser.version = previous;
			ASSERT_NO_THROW(old.oser & clean);
			auto & selected = value.endpoints[endpoint];
			auto & snapshot = replacement ? selected.replacement : selected.expected;
			auto bonus = marker();
			bonus.source = BonusSource::SPELL_EFFECT;
			snapshot.effects.push_back(bonus);
			ASSERT_NO_THROW(value.validateShape());
			expectRejectedBeforePrefix(snapshot);
			expectRejectedBeforePrefix(selected);
			expectRejectedBeforePrefix(value);
			SetStackEffect packet;
		packet.battleID = BattleID(0);
			packet.exchange = value;
			expectRejectedBeforePrefix(packet);
		}
}

TEST(BonusCapabilitySerialization, CurrentExchangePacketRoundtripRetainsMarker)
{
	SetStackEffect source;
	source.battleID = BattleID(0);
	source.exchange = exchange();
	auto bonus = marker();
	bonus.source = BonusSource::SPELL_EFFECT;
	source.exchange->endpoints[1].replacement.effects.push_back(bonus);
	CMemorySerializer wire;
	wire.oser & source;
	SetStackEffect restored;
	ASSERT_NO_THROW(wire.iser & restored);
	ASSERT_TRUE(restored.exchange);
	const auto & effects = restored.exchange->endpoints[1].replacement.effects;
	ASSERT_EQ(effects.size(), 2u);
	EXPECT_EQ(effects.back().type, bonus.type);
	EXPECT_EQ(effects.back().val, bonus.val);
}

TEST(BonusCapabilitySerialization, OrdinaryPacketChangeListsAllRejectBeforeBattleId)
{
	for(size_t which = 0; which < 3; ++which)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		const std::array lists{&packet.toAdd, &packet.toUpdate, &packet.toRemove};
		lists[which]->push_back({1, {ordinary(), marker()}});
		expectRejectedBeforePrefix(packet);
	}
}
TEST(BonusCapabilitySerialization, CommanderPacketRejectsBeforeHeroId)
{
	SetCommanderProperty packet;
	packet.accumulatedBonus = ordinary();
	CMemorySerializer old;
	old.oser.version = previous;
	ASSERT_NO_THROW(old.oser & packet);
	packet.accumulatedBonus = marker();
	expectRejectedBeforePrefix(packet);
}

}
