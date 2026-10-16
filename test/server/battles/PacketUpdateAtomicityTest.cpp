/*
 * PacketUpdateAtomicityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <cstddef>
#include <array>
#include <set>
#include <sstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using Bytes = std::vector<std::byte>;

std::string jsonDiagnostic(const JsonNode & actual, const JsonNode & expected)
{
	if(actual == expected)
		return {};
	std::ostringstream out;
	out << "\nactual=" << actual.toCompactString() << "\nexpected=" << expected.toCompactString();
	const auto & left = actual["state"].Struct();
	const auto & right = expected["state"].Struct();
	std::set<std::string> keys;
	for(const auto & [key, value] : left)
		keys.insert(key);
	for(const auto & [key, value] : right)
		keys.insert(key);
	const auto describe = [](JsonMap::const_iterator found, const JsonMap & object)
	{
		return found == object.end() ? std::string("missing")
			: "type=" + std::to_string(static_cast<int>(found->second.getType()))
				+ " value=" + found->second.toCompactString();
	};
	for(const auto & key : keys)
	{
		const auto first = left.find(key), second = right.find(key);
		if(first != left.end() && second != right.end() && first->second == second->second)
			continue;
		out << "\nstate[" << JsonNode(key).toCompactString() << "] actual " << describe(first, left)
			<< " expected " << describe(second, right);
	}
	return out.str() + "\nJsonType: null=0 bool=1 float=2 string=3 vector=4 struct=5 integer=6\n";
}

template<typename Value>
Bytes serialized(Value & value)
{
	CMemorySerializer writer;
	writer.oser & value;
	return writer.extractBuffer();
}

Bytes bonusBytes(const Bonus & bonus)
{
	auto copy = bonus;
	return serialized(copy);
}

// Counts the fixture's authority-to-server boundary, NOT transport deliveries.
// All actual recording/application still belongs to the maintained server.
class ObservedRecordingServer final : public IGameServer
{
	RecordingGameServer & downstream;
public:
	CStack * observedFirst = nullptr;
	std::vector<const CPackForClient *> attempts;
	std::vector<bool> firstDefendingBeforeApply;

	explicit ObservedRecordingServer(RecordingGameServer & server) : downstream(server) {}
	void setState(EServerState state) override { downstream.setState(state); }
	EServerState getState() const override { return downstream.getState(); }
	bool isPlayerHost(const PlayerColor & player) const override { return downstream.isPlayerHost(player); }
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
		attempts.push_back(&packet);
		firstDefendingBeforeApply.push_back(observedFirst && observedFirst->defended());
		downstream.sendPack(packet, connection);
	}
	void applyPack(CPackForClient & packet) override
	{
		attempts.push_back(&packet);
		firstDefendingBeforeApply.push_back(observedFirst && observedFirst->defended());
		downstream.applyPack(packet);
	}
};

struct BonusObservation
{
	std::vector<std::shared_ptr<Bonus>> local;
	std::vector<Bytes> localValues;
	TConstBonusListPtr retainedSpeedQuery;
	std::vector<const Bonus *> queryPointers;
	std::vector<Bytes> queryValues;
};

BonusObservation observeBonuses(const CStack & unit)
{
	BonusObservation result;
	for(const auto & bonus : unit.getExportedBonusList())
	{
		result.local.push_back(bonus);
		result.localValues.push_back(bonusBytes(*bonus));
	}
	result.retainedSpeedQuery = unit.getBonuses(Selector::type()(BonusType::STACKS_SPEED));
	for(const auto & bonus : *result.retainedSpeedQuery)
	{
		result.queryPointers.push_back(bonus.get());
		result.queryValues.push_back(bonusBytes(*bonus));
	}
	return result;
}

void expectBonusesUnchanged(const CStack & unit, const BonusObservation & before)
{
	const auto current = observeBonuses(unit);
	EXPECT_EQ(current.local, before.local);
	EXPECT_EQ(current.localValues, before.localValues);
	EXPECT_EQ(current.queryPointers, before.queryPointers);
	EXPECT_EQ(current.queryValues, before.queryValues);
	std::vector<const Bonus *> retainedPointers;
	std::vector<Bytes> retainedValues;
	for(const auto & bonus : *before.retainedSpeedQuery)
	{
		retainedPointers.push_back(bonus.get());
		retainedValues.push_back(bonusBytes(*bonus));
	}
	EXPECT_EQ(retainedPointers, before.queryPointers);
	EXPECT_EQ(retainedValues, before.queryValues);
}
}

// BEFORE characterization only. A later atomic rejection contract needs its own
// reviewed assertions; do not silently flip these or the existing detached test.
class PacketUpdateAtomicityTest : public HeroCommandFixture
{
protected:
	ObservedRecordingServer observed{server};
	CStack * first = nullptr;
	CStack * second = nullptr;

	struct Before
	{
		std::vector<const battle::Unit *> roster;
		uint32_t firstId;
		uint32_t secondId;
		JsonNode firstState;
		JsonNode secondState;
		BonusObservation firstBonuses;
		BonusObservation secondBonuses;
		Bytes authorityRandom;
		Bytes callbackRandom;
	};

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void prepare()
	{
		startGame();
		// Rebind BEFORE battle construction; every later packet uses this owner.
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		gameHandler->randomizer->setSeed(seed);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
		beginCombat();
		for(auto * unit : {first, second})
		{
			ASSERT_FALSE(unit->isTimeStopped());
			ASSERT_FALSE(unit->hasBattleForm());
			ASSERT_FALSE(unit->defended());
			ASSERT_EQ(unit->guardianSpiritHitPoints, 0);
			ASSERT_EQ(unit->guardianSpiritRoundsRemaining, 0);
			auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
				BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
			marker->turnsRemain = 3;
			unit->addNewBonus(marker);
		}
		observed.observedFirst = first;
		observed.attempts.clear();
		observed.firstDefendingBeforeApply.clear();
	}

	std::vector<const battle::Unit *> roster() const
	{
		std::vector<const battle::Unit *> result;
		for(const auto * unit : battle()->battleGetAllUnits(true))
			result.push_back(unit);
		return result;
	}

	Bytes callbackRandomBytes()
	{
		// TinyMapGameTest::getRNG returns its actual CRandomGenerator member.
		auto * generator = static_cast<CRandomGenerator *>(TinyMapGameTest::getRNG());
		return serialized(*generator);
	}

	Before capture()
	{
		return {roster(), first->unitId(), second->unitId(), first->save(), second->save(), observeBonuses(*first), observeBonuses(*second),
			serialized(*gameHandler->randomizer), callbackRandomBytes()};
	}

	BattleUnitsChanged twoUpdates(const Before & before)
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.emplace_back(first->unitId(), UnitChanges::EOperation::UPDATE);
		packet.changedStacks.back().data = before.firstState;
		packet.changedStacks.back().data["state"]["defending"].Bool() = true;
		packet.changedStacks.emplace_back(second->unitId(), UnitChanges::EOperation::UPDATE);
		packet.changedStacks.back().data = before.secondState;
		packet.changedStacks.back().data["state"]["defending"].Bool() = true;
		return packet;
	}

	void expectCurrentEnvelopeWrites(BattleUnitsChanged & packet)
	{
		ASSERT_EQ(packet.changedStacks.size(), 2u);
		const std::array<JsonNode, 2> beforeWriter{packet.changedStacks[0].data, packet.changedStacks[1].data};
		for(const auto & requested : beforeWriter)
		{
			ASSERT_TRUE(requested["state"].isStruct());
			ASSERT_EQ(requested["state"].Struct().count("activationMovementBonus"), 0u);
			ASSERT_EQ(requested["state"].Struct().count("veteranPhysicalDamageSinceActivation"), 0u);
		}
		CMemorySerializer writer;
		writer.oser.version = ESerializationVersion::CURRENT;
		ASSERT_NO_THROW(packet.serialize(writer.oser));
		const auto firstWire = writer.extractBuffer();
		EXPECT_FALSE(firstWire.empty());
		std::array<JsonNode, 2> expectedAfterWriter = beforeWriter;
		for(size_t index = 0; index < expectedAfterWriter.size(); ++index)
		{
			// Exact existing producer mutation, NOT canonicalization of arbitrary keys.
			expectedAfterWriter[index]["state"].Struct().emplace("activationMovementBonus", JsonNode());
			expectedAfterWriter[index]["state"].Struct().emplace("veteranPhysicalDamageSinceActivation", JsonNode());
			EXPECT_EQ(packet.changedStacks[index].data, expectedAfterWriter[index])
				<< jsonDiagnostic(packet.changedStacks[index].data, expectedAfterWriter[index]);
		}
		CMemorySerializer repeatedWriter;
		repeatedWriter.oser.version = ESerializationVersion::CURRENT;
		ASSERT_NO_THROW(packet.serialize(repeatedWriter.oser));
		EXPECT_EQ(repeatedWriter.extractBuffer(), firstWire);
		for(size_t index = 0; index < expectedAfterWriter.size(); ++index)
			EXPECT_EQ(packet.changedStacks[index].data, expectedAfterWriter[index])
				<< jsonDiagnostic(packet.changedStacks[index].data, expectedAfterWriter[index]);
	}

	void expectRejected(BattleUnitsChanged & packet, const std::string & text)
	{
		try
		{
			gameHandler->sendAndApply(packet);
			FAIL() << "Expected ordinary unit update rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_EQ(error.what(), text);
		}
	}

	void expectCommonBeforeEvidence(const Before & before, const BattleUnitsChanged & packet)
	{
		EXPECT_EQ(roster(), before.roster);
		EXPECT_EQ(first->unitId(), before.firstId);
		EXPECT_EQ(second->unitId(), before.secondId);
		EXPECT_EQ(battle()->getStack(before.firstId, false), first);
		EXPECT_EQ(battle()->getStack(before.secondId, false), second);
		expectBonusesUnchanged(*first, before.firstBonuses);
		expectBonusesUnchanged(*second, before.secondBonuses);
		EXPECT_EQ(serialized(*gameHandler->randomizer), before.authorityRandom);
		EXPECT_EQ(callbackRandomBytes(), before.callbackRandom);
		ASSERT_EQ(observed.attempts.size(), 1u);
		EXPECT_EQ(observed.attempts.front(), &packet);
		ASSERT_EQ(observed.firstDefendingBeforeApply.size(), 1u);
		EXPECT_FALSE(observed.firstDefendingBeforeApply.front());
	}
};

TEST_F(PacketUpdateAtomicityTest, MissingSecondIdRetainsAppliedFirstAndRecordingAttempt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = capture();
	auto packet = twoUpdates(before);
	const auto missing = std::numeric_limits<uint32_t>::max();
	ASSERT_EQ(battle()->getStack(missing, false), nullptr);
	packet.changedStacks[1].id = missing;
	const auto requestedFirst = packet.changedStacks[0].data;
	ASSERT_NO_FATAL_FAILURE(expectCurrentEnvelopeWrites(packet));
	expectRejected(packet, "Invalid unit id in BattleInfo update");
	EXPECT_EQ(first->save(), requestedFirst) << jsonDiagnostic(first->save(), requestedFirst);
	EXPECT_EQ(second->save(), before.secondState);
	expectCommonBeforeEvidence(before, packet);
}

TEST_F(PacketUpdateAtomicityTest, KnownSecondPreloadFailureHasRecordingPrefixButWriterRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = capture();
	auto packet = twoUpdates(before);
	packet.changedStacks[1].data["state"]["frozenAppliedRound"].Integer() = -2;
	const auto firstBeforeWriter = packet.changedStacks[0].data;
	const auto secondBeforeWriter = packet.changedStacks[1].data;
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(packet.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	EXPECT_EQ(packet.changedStacks[0].data, firstBeforeWriter);
	EXPECT_EQ(packet.changedStacks[1].data, secondBeforeWriter);
	// Maintained RecordingGameServer does NOT serialize. Do not claim wire admission.
	expectRejected(packet, "Invalid Frozen application round");
	EXPECT_EQ(first->save(), packet.changedStacks[0].data);
	EXPECT_EQ(second->save(), before.secondState);
	expectCommonBeforeEvidence(before, packet);
}

TEST_F(PacketUpdateAtomicityTest, NegativeGuardianSecondFailsAfterResetAndRetainsAppliedPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = capture();
	auto packet = twoUpdates(before);
	packet.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	const auto requestedFirst = packet.changedStacks[0].data;
	ASSERT_NO_FATAL_FAILURE(expectCurrentEnvelopeWrites(packet));
	expectRejected(packet, "Invalid Guardian Spirit state");
	EXPECT_EQ(first->save(), requestedFirst) << jsonDiagnostic(first->save(), requestedFirst);
	// Do not rely on serializing a rejected invalid live state as the only oracle.
	EXPECT_TRUE(second->defended());
	EXPECT_EQ(second->guardianSpiritHitPoints, -1);
	EXPECT_EQ(second->guardianSpiritRoundsRemaining, 0);
	EXPECT_EQ(second->getPosition(), BattleHex::INVALID);
	EXPECT_NE(second->getPosition().toInt(), before.secondState["state"]["position"].Integer());
	EXPECT_THROW(second->save(), std::runtime_error);
	expectCommonBeforeEvidence(before, packet);
}

TEST_F(PacketUpdateAtomicityTest, ValidTwoUpdatesAreAppliedWithoutBonusOrRandomizerChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = capture();
	auto packet = twoUpdates(before);
	const auto requestedFirst = packet.changedStacks[0].data;
	const auto requestedSecond = packet.changedStacks[1].data;
	ASSERT_NO_FATAL_FAILURE(expectCurrentEnvelopeWrites(packet));
	ASSERT_NO_THROW(gameHandler->sendAndApply(packet));
	EXPECT_EQ(first->save(), requestedFirst) << jsonDiagnostic(first->save(), requestedFirst);
	EXPECT_EQ(second->save(), requestedSecond) << jsonDiagnostic(second->save(), requestedSecond);
	expectCommonBeforeEvidence(before, packet);
}
