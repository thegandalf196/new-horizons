/*
 * BattlePacketReceiptCompatibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using Version = ESerializationVersion;
enum class Receipt { PHOENIX, REBIRTH, PERFECT, SERENDIPITY, LAST_STAND, CHAIN_GATE };
constexpr std::array RECEIPTS{Receipt::PHOENIX, Receipt::REBIRTH, Receipt::PERFECT,
	Receipt::SERENDIPITY, Receipt::LAST_STAND, Receipt::CHAIN_GATE};
constexpr std::array BOUNDARIES{Version::NEW_HORIZONS_PHOENIX_SPARK, Version::NEW_HORIZONS_REBIRTH_CHAIN,
	Version::NEW_HORIZONS_PERFECT_FORTUNE, Version::NEW_HORIZONS_LUCK_SERENDIPITY,
	Version::NEW_HORIZONS_ARMORER_LAST_STAND, Version::NEW_HORIZONS_CHAIN_GATE};
constexpr std::array NAMES{"phoenix", "rebirthRollback", "perfectFortune", "luckSerendipity", "lastStand", "chainGate"};
constexpr std::array UNSUPPORTED{"Cannot discard Phoenix Spark death receipt from an older packet",
	"Cannot discard Rebirth Chain consumption from an older packet", "Cannot discard Perfect Fortune strike state",
	"Cannot discard Luck Serendipity strike history", "Cannot discard Last Stand attack state update",
	"Cannot discard Chain Gate attack state"};

Version previous(Version boundary) { return static_cast<Version>(static_cast<int>(boundary) - 1); }

BattleStackAttacked ordinaryHit()
{
	BattleStackAttacked result;
	result.stackAttacked = 8; result.attackerID = 7;
	result.newState = UnitChanges(8, UnitChanges::EOperation::UPDATE);
	return result;
}

template<typename Function>
void withReceipt(Receipt family, bool populated, Function function)
{
	if(family == Receipt::PHOENIX || family == Receipt::REBIRTH)
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(4);
		if(populated && family == Receipt::PHOENIX)
			packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{BattleSide::DEFENDER, 7};
		if(populated && family == Receipt::REBIRTH)
		{
			packet.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{BattleSide::DEFENDER, 7, 8, true};
			packet.changedStacks.emplace_back(8, UnitChanges::EOperation::REMOVE);
		}
		function(packet);
	}
	else if(family == Receipt::LAST_STAND)
	{
		auto packet = ordinaryHit();
		if(populated)
		{
			packet.armorerLastStandSide = BattleSide::DEFENDER;
			packet.newState.data["state"]["armorerLastStandDefending"] = JsonNode(true);
		}
		function(packet);
	}
	else
	{
		BattleAttack packet;
		packet.battleID = packet.attackerChanges.battleID = BattleID(4);
		packet.bsa.push_back(ordinaryHit());
		if(populated && family == Receipt::PERFECT)
		{
			packet.perfectFortuneSide = BattleSide::ATTACKER;
			packet.perfectFortuneState = PerfectFortuneState{true, true}; packet.flags = BattleAttack::LUCKY;
		}
		if(populated && family == Receipt::SERENDIPITY)
		{
			packet.luckSerendipitySide = BattleSide::ATTACKER;
			packet.luckSerendipityState = LuckSerendipityState{true, 2, false, true, true};
		}
		packet.chainGateTriggered = populated && family == Receipt::CHAIN_GATE;
		function(packet);
	}
}

struct Wire { std::vector<std::byte> bytes; std::string error; };
template<typename Packet>
Wire encode(Packet & packet, Version version, const std::vector<std::byte> & prefix = {})
{
	CMemorySerializer memory(prefix); memory.oser.version = version;
	Wire result;
	try { memory.oser & packet; }
	catch(const std::runtime_error & error) { result.error = error.what(); }
	result.bytes = memory.extractBuffer();
	return result;
}

std::string hex(const std::vector<std::byte> & bytes)
{
	constexpr auto digits = "0123456789abcdef";
	std::string text;
	for(const auto value : bytes)
	{
		const auto byte = std::to_integer<unsigned char>(value);
		text += digits[byte >> 4]; text += digits[byte & 15];
	}
	return text;
}

void record(JsonNode & rows, const std::string & label, const Wire & wire)
{
	JsonNode result;
	result["label"] = JsonNode(label); result["bytesHex"] = JsonNode(hex(wire.bytes));
	result["exception"] = JsonNode(wire.error.empty() ? "none" : "std::runtime_error");
	result["message"] = JsonNode(wire.error); rows.Vector().push_back(result);
}

void publish(const JsonNode & rows)
{
	const auto text = rows.toCompactString(); ASSERT_LE(text.size(), 1024u * 1024u);
	::testing::Test::RecordProperty("receiptRows", text);
}

template<typename Packet>
void sameReceipt(const Packet & source, const Packet & restored)
{
	if constexpr(std::is_same_v<Packet, BattleUnitsChanged>)
	{
		ASSERT_EQ(source.phoenixSparkConsumption.has_value(), restored.phoenixSparkConsumption.has_value());
		ASSERT_EQ(source.rebirthChainConsumption.has_value(), restored.rebirthChainConsumption.has_value());
		if(source.phoenixSparkConsumption)
		{
			EXPECT_EQ(source.phoenixSparkConsumption->side, restored.phoenixSparkConsumption->side);
			EXPECT_EQ(source.phoenixSparkConsumption->sourceUnitId, restored.phoenixSparkConsumption->sourceUnitId);
		}
		if(source.rebirthChainConsumption)
		{
			EXPECT_EQ(source.rebirthChainConsumption->side, restored.rebirthChainConsumption->side);
			EXPECT_EQ(source.rebirthChainConsumption->sourceUnitId, restored.rebirthChainConsumption->sourceUnitId);
			EXPECT_EQ(source.rebirthChainConsumption->spawnUnitId, restored.rebirthChainConsumption->spawnUnitId);
			EXPECT_EQ(source.rebirthChainConsumption->rollback, restored.rebirthChainConsumption->rollback);
		}
		ASSERT_EQ(source.changedStacks.size(), restored.changedStacks.size());
		for(size_t i = 0; i < source.changedStacks.size(); ++i)
		{
			EXPECT_EQ(source.changedStacks[i].id, restored.changedStacks[i].id);
			EXPECT_EQ(source.changedStacks[i].operation, restored.changedStacks[i].operation);
			EXPECT_EQ(source.changedStacks[i].data, restored.changedStacks[i].data);
		}
	}
	else if constexpr(std::is_same_v<Packet, BattleStackAttacked>)
	{
		EXPECT_EQ(source.armorerLastStandSide, restored.armorerLastStandSide);
		EXPECT_EQ(source.armorerLastStandEndsActivation, restored.armorerLastStandEndsActivation);
		EXPECT_EQ(source.newState.data, restored.newState.data);
	}
	else
	{
		EXPECT_EQ(source.perfectFortuneSide, restored.perfectFortuneSide);
		EXPECT_EQ(source.perfectFortuneState, restored.perfectFortuneState);
		EXPECT_EQ(source.luckSerendipitySide, restored.luckSerendipitySide);
		EXPECT_EQ(source.luckSerendipityState, restored.luckSerendipityState);
		EXPECT_EQ(source.chainGateTriggered, restored.chainGateTriggered);
		EXPECT_EQ(source.flags, restored.flags);
	}
}

template<typename Packet>
void roundTrip(Packet & packet, Receipt family, Version version, const std::vector<std::byte> & bytes)
{
	Packet restored;
	withReceipt(family, true, [&](auto & stale)
	{
		if constexpr(std::is_same_v<Packet, std::remove_reference_t<decltype(stale)>>) restored = stale;
	});
	CMemorySerializer reader(bytes); reader.iser.version = version;
	ASSERT_NO_THROW(reader.iser & restored); sameReceipt(packet, restored);
	const auto rewritten = encode(restored, version);
	EXPECT_TRUE(rewritten.error.empty()); EXPECT_EQ(rewritten.bytes, bytes);
}

template<typename Packet>
void reject(Packet & packet, Version version, const char * message, const std::string & label, JsonNode & rows)
{
	const std::vector<std::byte> prefix{std::byte{0x31}, std::byte{0xa7}};
	const auto empty = encode(packet, version); const auto seeded = encode(packet, version, prefix);
	EXPECT_EQ(empty.error, message); EXPECT_EQ(seeded.error, message);
	EXPECT_TRUE(empty.bytes.empty()); EXPECT_EQ(seeded.bytes, prefix);
	record(rows, label, empty); record(rows, label + "/prefilled", seeded);
}
}

TEST(BattlePacketReceiptCompatibilityTest, ReceiptBoundariesAndCurrentPreserveBytesAndReusedDefaults)
{
	JsonNode rows;
	for(const auto family : RECEIPTS)
		for(const bool populated : {false, true})
			for(const auto version : {previous(BOUNDARIES[static_cast<size_t>(family)]), BOUNDARIES[static_cast<size_t>(family)], Version::CURRENT})
				withReceipt(family, populated, [&](auto & packet)
				{
					const auto index = static_cast<size_t>(family);
					const auto label = std::string(NAMES[index]) + "/" + std::to_string(populated) + "/" + std::to_string(static_cast<int>(version));
					SCOPED_TRACE(label);
					if(populated && version < BOUNDARIES[index]) reject(packet, version, UNSUPPORTED[index], label, rows);
					else
					{
						const auto result = encode(packet, version); EXPECT_TRUE(result.error.empty());
						if(result.error.empty()) roundTrip(packet, family, version, result.bytes);
						record(rows, label, result);
					}
				});
	ASSERT_EQ(rows.Vector().size(), 42u); publish(rows);
}

TEST(BattlePacketReceiptCompatibilityTest, OuterShapeAndVersionErrorsKeepPriorityOverMalformedUnitState)
{
	JsonNode rows;
	for(const auto family : RECEIPTS)
		withReceipt(family, true, [&](auto & packet)
		{
			using Packet = std::remove_reference_t<decltype(packet)>;
			const char * expected = "";
			if constexpr(std::is_same_v<Packet, BattleUnitsChanged>)
			{
				if(family == Receipt::PHOENIX)
				{
					packet.phoenixSparkConsumption->side = BattleSide::NONE;
					packet.changedStacks.emplace_back(8, UnitChanges::EOperation::UPDATE);
					expected = "Invalid Phoenix Spark death provenance";
				}
				else { packet.changedStacks.front().id = 99; expected = "Rebirth Chain provenance must accompany exactly its output operation"; }
				packet.changedStacks.front().data["state"]["heroicSpiritRetaliation"] = JsonNode("bad");
			}
			else if constexpr(std::is_same_v<Packet, BattleStackAttacked>)
			{
				packet.armorerLastStandSide = static_cast<BattleSide>(99);
				packet.newState.data["state"]["frozenAppliedRound"] = JsonNode(-2);
				expected = "Invalid Frozen application receipt in unit update";
			}
			else
			{
				packet.bsa.front().newState.data["state"]["heroicSpiritRetaliation"] = JsonNode("bad");
				if(family == Receipt::PERFECT) { packet.perfectFortuneState->enabled = false; expected = "Perfect Fortune used without an enabled perk"; }
				else if(family == Receipt::SERENDIPITY) { packet.luckSerendipityState->enabled = false; expected = "Invalid Luck Serendipity round history"; }
				else expected = "Heroic Spirit receipt must be a boolean";
			}
			reject(packet, Version::CURRENT, expected, NAMES[static_cast<size_t>(family)], rows);
		});
	withReceipt(Receipt::PERFECT, true, [&](auto & packet)
	{
		if constexpr(std::is_same_v<std::remove_reference_t<decltype(packet)>, BattleAttack>)
		{
			packet.bsa.front().newState.data["state"]["heroicSpiritRetaliation"] = JsonNode("bad");
			reject(packet, previous(Version::NEW_HORIZONS_PERFECT_FORTUNE), UNSUPPORTED[2], "perfect-version-first", rows);
		}
	});
	publish(rows);
}

TEST(BattlePacketReceiptCompatibilityTest, LastStandSidesAndActivationEndMatchNestedAttackMarkers)
{
	JsonNode rows;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		for(const bool ended : {false, true})
		{
			auto hit = ordinaryHit(); hit.armorerLastStandSide = side; hit.armorerLastStandEndsActivation = ended;
			hit.newState.data["state"]["armorerLastStandDefending"] = JsonNode(true);
			hit.newState.data["state"]["armorerLastStandEndedActivation"] = JsonNode(ended);
			BattleAttack attack; attack.battleID = attack.attackerChanges.battleID = BattleID(4);
			attack.bsa.push_back(hit); attack.flags = ended ? BattleAttack::LAST_STAND_RETALIATION : 0;
			for(const auto version : {Version::NEW_HORIZONS_ARMORER_LAST_STAND, Version::CURRENT})
			{
				const auto wire = encode(attack, version); EXPECT_TRUE(wire.error.empty());
				CMemorySerializer reader(wire.bytes); reader.iser.version = version;
				BattleAttack restored; ASSERT_NO_THROW(reader.iser & restored); ASSERT_EQ(restored.bsa.size(), 1u);
				EXPECT_EQ(restored.bsa.front().armorerLastStandSide, side);
				EXPECT_EQ(restored.bsa.front().armorerLastStandEndsActivation, ended);
				EXPECT_EQ(restored.lastStandRetaliation(), ended); EXPECT_EQ(encode(restored, version).bytes, wire.bytes);
				record(rows, std::to_string(static_cast<int>(side)) + "/" + std::to_string(ended) + "/" + std::to_string(static_cast<int>(version)), wire);
			}
			attack.flags = ended ? 0 : BattleAttack::LAST_STAND_RETALIATION;
			reject(attack, Version::CURRENT, "Last Stand retaliation marker does not match its activation-ending hit", "marker-mismatch", rows);
		}
	publish(rows);
}

TEST(BattlePacketReceiptCompatibilityTest, MalformedOuterSidesAndLastStandShapesRejectBeforePrefixes)
{
	JsonNode rows;
	for(int variant = 0; variant < 6; ++variant)
	{
		auto hit = ordinaryHit();
		hit.armorerLastStandSide = BattleSide::DEFENDER;
		hit.newState.data["state"]["armorerLastStandDefending"] = JsonNode(true);
		if(variant == 0) hit.armorerLastStandSide = static_cast<BattleSide>(99);
		if(variant == 1) hit.newState.id = 99;
		if(variant == 2) hit.newState.operation = UnitChanges::EOperation::ADD;
		if(variant == 3) hit.newState.data["state"]["armorerLastStandDefending"] = JsonNode(false);
		if(variant >= 4) hit.armorerLastStandEndsActivation = true;
		if(variant == 5) hit.armorerLastStandSide = BattleSide::NONE;
		reject(hit, Version::CURRENT, variant == 0 ? "Invalid Last Stand attack side"
			: "Invalid Last Stand unit state in attack hit", "lastStand-shape/" + std::to_string(variant), rows);
	}
	for(const auto family : {Receipt::PERFECT, Receipt::SERENDIPITY})
		withReceipt(family, true, [&](auto & packet)
		{
			if constexpr(std::is_same_v<std::remove_reference_t<decltype(packet)>, BattleAttack>)
			{
				if(family == Receipt::PERFECT) packet.perfectFortuneSide = BattleSide::NONE;
				else packet.luckSerendipitySide = static_cast<BattleSide>(99);
				reject(packet, Version::CURRENT, family == Receipt::PERFECT ? "Invalid Perfect Fortune attack side"
					: "Invalid Luck Serendipity attack side", NAMES[static_cast<size_t>(family)], rows);
			}
		});
	publish(rows);
}
