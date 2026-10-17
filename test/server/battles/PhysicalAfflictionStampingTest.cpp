/*
 * PhysicalAfflictionStampingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
template<typename T> std::vector<std::byte> wire(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

std::vector<Bonus> group(int spell, int64_t order = 0, const std::string & kind = "other")
{
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(spell)));
	effect.turnsRemain = 2;
	Bonus marker(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(spell)));
	JsonNode metadata;
	metadata["kind"].String() = kind;
	metadata["applicationOrder"].Integer() = order;
	marker.parameters = std::make_shared<BonusParameters>(metadata);
	return {effect, marker};
}

int64_t order(const std::vector<Bonus> & bonuses)
{
	for(const auto & bonus : bonuses)
		if(const auto marker = physicalAfflictions::markerMetadata(bonus))
			return marker->applicationOrder;
	throw std::runtime_error("Expected physical-affliction marker");
}

struct PublicationReached {};

struct UnitObservation
{
	std::string state;
	std::vector<const Bonus *> identities;
	std::vector<std::vector<std::byte>> values;
	bool operator==(const UnitObservation &) const = default;
};

// Observe the actual authority→RecordingGameServer boundary, not a socket.
class StampPublication final : public IGameServer
{
	RecordingGameServer & downstream;
public:
	bool stopBeforeApply = false;
	std::vector<SetStackEffect> effects;
	explicit StampPublication(RecordingGameServer & value) : downstream(value) {}
	void setState(EServerState value) override { downstream.setState(value); }
	EServerState getState() const override { return downstream.getState(); }
	bool isPlayerHost(const PlayerColor & value) const override { return downstream.isPlayerHost(value); }
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return downstream.hasPlayerAt(player, connection);
	}
	bool hasBothPlayersAtSameConnection(PlayerColor first, PlayerColor second) const override
	{
		return downstream.hasBothPlayersAtSameConnection(first, second);
	}
	void sendPack(CPackForClient & packet, GameConnectionID connection) override
	{
		downstream.sendPack(packet, connection);
	}
	void applyPack(CPackForClient & packet) override
	{
		if(const auto * effect = dynamic_cast<const SetStackEffect *>(&packet))
			effects.push_back(*effect);
		if(stopBeforeApply)
			throw PublicationReached{};
		downstream.applyPack(packet);
	}
};
}

// BEFORE characterization: packet-prefix mutation is not whole-packet atomicity.
class PhysicalAfflictionStampingTest : public BattleTestFixture
{
protected:
	StampPublication publication{server};
	CStack * first = nullptr;
	CStack * second = nullptr;

	void world()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		// Rebind before battle construction, as the maintained authority fixture does.
		gameHandler = std::make_shared<CGameHandler>(publication, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		const auto stacks = battle()->battleGetAllStacks(false);
		ASSERT_GE(stacks.size(), 2u);
		first = battle()->getStack(stacks.front()->unitId(), false);
		second = battle()->getStack(stacks.back()->unitId(), false);
		ASSERT_NE(first, second);
		if(first->unitId() > second->unitId())
			std::swap(first, second);
		publication.effects.clear();
	}

	auto randomState()
	{
		return std::make_pair(wire(*gameHandler->randomizer),
			wire(dynamic_cast<CRandomGenerator &>(*getRNG())));
	}

	UnitObservation localBonuses(const CStack & unit)
	{
		UnitObservation result;
		result.state = unit.acquireState()->save().toString();
		for(const auto & bonus : unit.getExportedBonusList())
		{
			// These fresh token-stack fixtures need no opaque runtime graph oracle.
			if(!bonus || bonus->limiter || bonus->updater || bonus->propagationUpdater || bonus->propagator)
				throw std::runtime_error("Unsupported local stamping fixture bonus graph");
			if(bonus->parameters)
				(void)bonus->parameters->toCustom<JsonNode>(); // Reject nested/non-JSON payloads.
			result.identities.push_back(bonus.get());
			result.values.push_back(wire(std::make_pair(*bonus, bonus->bonusOwner)));
		}
		return result;
	}
};

TEST_F(PhysicalAfflictionStampingTest, UnmarkedAndRemovalOnlyMissingBattleReachBoundaryUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const BattleID absentBattle(41);
	ASSERT_EQ(gameState()->getBattle(absentBattle), nullptr);
	publication.stopBeforeApply = true;
	const auto random = randomState();
	for(bool removalOnly : {false, true})
	{
		SetStackEffect packet;
		packet.battleID = absentBattle;
		if(removalOnly)
			packet.toRemove.emplace_back(99, group(71));
		else
			packet.toAdd.emplace_back(99, std::vector<Bonus>{group(71).front()});
		const auto before = wire(packet);
		EXPECT_THROW(gameHandler->sendAndApply(packet), PublicationReached);
		ASSERT_EQ(publication.effects.size(), removalOnly ? 2u : 1u);
		EXPECT_EQ(wire(publication.effects.back()), before);
		EXPECT_EQ(wire(packet), before);
	}
	EXPECT_EQ(randomState(), random);
	// Deliberately never apply the invalid-battle packet to the receiver.
}

TEST_F(PhysicalAfflictionStampingTest, MarkedMissingBattleRejectsBeforePublication)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const BattleID absentBattle(41);
	ASSERT_EQ(gameState()->getBattle(absentBattle), nullptr);
	SetStackEffect packet;
	packet.battleID = absentBattle;
	packet.toAdd.emplace_back(99, group(71));
	const auto before = wire(packet);
	const auto random = randomState();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
	EXPECT_TRUE(publication.effects.empty());
	EXPECT_EQ(wire(packet), before);
	EXPECT_EQ(randomState(), random);
}

TEST_F(PhysicalAfflictionStampingTest, MarkedMissingUnitRejectsBeforePublication)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toUpdate.emplace_back(battle()->battleNextUnitId(), group(71));
	const auto before = wire(packet);
	const auto random = randomState();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
	EXPECT_TRUE(publication.effects.empty());
	EXPECT_EQ(wire(packet), before);
	EXPECT_EQ(randomState(), random);
}

TEST_F(PhysicalAfflictionStampingTest, RemovalThenUpdateBeforeAddAreStampedBeforePublication)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect seed;
	seed.battleID = BattleID(0);
	seed.toAdd.emplace_back(first->unitId(), group(71, 4));
	seed.toAdd.emplace_back(first->unitId(), group(72, 9));
	ASSERT_NO_THROW(gameHandler->sendAndApply(seed));
	publication.effects.clear();
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toRemove.emplace_back(first->unitId(), seed.toAdd.front().second);
	packet.toUpdate.emplace_back(first->unitId(), group(71));
	packet.toAdd.emplace_back(first->unitId(), group(73));
	packet.toAdd.emplace_back(first->unitId(), group(74, 27));
	const auto random = randomState();
	ASSERT_NO_THROW(gameHandler->sendAndApply(packet));
	ASSERT_EQ(publication.effects.size(), 1u);
	EXPECT_EQ(order(packet.toUpdate.front().second), 10);
	EXPECT_EQ(order(packet.toAdd.front().second), 11);
	EXPECT_EQ(order(packet.toAdd.back().second), 27);
	EXPECT_EQ(order(packet.toRemove.front().second), 4);
	EXPECT_EQ(wire(publication.effects.front()), wire(packet));
	EXPECT_EQ(randomState(), random);
}

TEST_F(PhysicalAfflictionStampingTest, LaterMissingUnitLeavesEarlierPacketStampButPublishesNothing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	// Reverse packet order deliberately: marked units are visited by sorted ID.
	packet.toAdd.emplace_back(battle()->battleNextUnitId(), group(72));
	packet.toAdd.emplace_back(first->unitId(), group(71));
	const auto before = localBonuses(*first);
	const auto random = randomState();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
	EXPECT_TRUE(publication.effects.empty());
	EXPECT_EQ(order(packet.toAdd.back().second), 1);
	EXPECT_EQ(order(packet.toAdd.front().second), 0);
	EXPECT_EQ(localBonuses(*first), before);
	EXPECT_EQ(randomState(), random);
}

TEST_F(PhysicalAfflictionStampingTest, LaterInvalidMarkerKeepsEarlierStampAndBothLiveUnitsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(first->unitId(), group(71));
	auto invalid = group(72);
	invalid.back().parameters.reset();
	packet.toAdd.emplace_back(second->unitId(), invalid);
	const auto firstBefore = localBonuses(*first);
	const auto secondBefore = localBonuses(*second);
	const auto random = randomState();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::invalid_argument);
	EXPECT_TRUE(publication.effects.empty());
	EXPECT_EQ(order(packet.toAdd.front().second), 1);
	EXPECT_EQ(localBonuses(*first), firstBefore);
	EXPECT_EQ(localBonuses(*second), secondBefore);
	EXPECT_EQ(randomState(), random);
}
