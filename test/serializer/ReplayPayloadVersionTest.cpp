/*
 * ReplayPayloadVersionTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/SideInBattle.h"
#include <array>
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
#include <type_traits>

namespace
{
using Version = ESerializationVersion;
// Exact pre-provenance container layout. Opaque sample bytes are never decoded.
struct LegacyChapter
{
	uint32_t day = 1;
	std::vector<std::byte> snapshot{std::byte{4}};
	std::vector<std::vector<std::byte>> packs{{std::byte{5}}};
	std::vector<ReplayTurnMark> turns;
	template<class H> void serialize(H & h) { h & day; h & snapshot; h & packs; h & turns; }
};
struct LegacyLog
{
	std::vector<LegacyChapter> chapters{LegacyChapter{}};
	bool entire = true;
	uint32_t kept = 2;
	template<class H> void serialize(H & h) { h & chapters; h & entire; h & kept; }
};
template<class T> std::vector<std::byte> encode(const T & value, Version version = Version::CURRENT)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	wire.oser & value;
	return wire.extractBuffer();
}
ReplayLog decode(const std::vector<std::byte> & bytes, Version version)
{
	CMemorySerializer wire(bytes);
	wire.iser.version = version;
	ReplayLog result;
	wire.iser & result;
	return result;
}
ReplayPayload packet(Version version)
{
	PlayerStartsTurn value;
	value.player = PlayerColor(2);
	CMemorySerializer wire;
	wire.oser.version = version;
	wire.oser & static_cast<const CPackForClient *>(&value);
	return {wire.extractBuffer(), version};
}
ReplayLog knownLog()
{
	ReplayLog result;
	result.configure(true, 2);
	result.beginDay(1, {{std::byte{4}}, Version::NEW_HORIZONS_FLY_DAY_END});
	result.addPack(packet(Version::NEW_HORIZONS_FLY_DAY_END));
	return result;
}
}

TEST(ReplayPayloadVersionTest, LegacyPayloadsStayUnknownAndByteExactAcrossCurrentResave)
{
	const auto old = encode(LegacyLog{}, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
	auto log = decode(old, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
	const auto & chapter = log.getChapters().front();
	EXPECT_EQ(chapter.gamestateSnapshot, LegacyChapter{}.snapshot);
	EXPECT_EQ(chapter.packs, LegacyChapter{}.packs);
	EXPECT_EQ(chapter.snapshotVersion, Version::NONE);
	EXPECT_EQ(chapter.packVersions, (std::vector<Version>{Version::NONE}));
	EXPECT_THROW(ReplayPackSerializer::read(chapter.packPayload(0), nullptr), std::runtime_error);
	const auto rewritten = decode(encode(log), Version::CURRENT);
	EXPECT_EQ(rewritten.getChapters().front().gamestateSnapshot, chapter.gamestateSnapshot);
	EXPECT_EQ(rewritten.getChapters().front().packs, chapter.packs);
	EXPECT_EQ(rewritten.getChapters().front().packVersions, chapter.packVersions);
	EXPECT_EQ(rewritten.getChapters().front().snapshotVersion, Version::NONE);
	EXPECT_EQ(encode(log, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES), old);
}

TEST(ReplayPayloadVersionTest, ActualPacketCodecUsesEachOldIntermediateAndCurrentVersion)
{
	for(const auto version : {Version::NEW_HORIZONS_FLY_DAY_END,
		Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES, Version::CURRENT})
	{
		const auto payload = packet(version);
		auto decoded = ReplayPackSerializer::read(payload, nullptr);
		const auto * start = dynamic_cast<const PlayerStartsTurn *>(decoded.get());
		ASSERT_NE(start, nullptr);
		EXPECT_EQ(start->player, PlayerColor(2));
	}
	SetSpellResponseState response;
	response.battleID = BattleID(0);
	response.side = BattleSide::ATTACKER;
	response.state.armedInRound = 0;
	auto current = ReplayPackSerializer::write(response);
	EXPECT_EQ(current.version, Version::CURRENT);
	// Native feature guards, not a trial decode or permissive parser, reject
	// an explicit incompatible origin. CURRENT fallback would accept this.
	current.version = Version::MINIMAL;
	EXPECT_THROW(ReplayPackSerializer::read(current, nullptr), std::runtime_error);
	current.version = Version::NONE;
	EXPECT_THROW(ReplayPackSerializer::read(current, nullptr), std::runtime_error);
	current.version = static_cast<Version>(static_cast<int32_t>(Version::CURRENT) + 1);
	EXPECT_THROW(ReplayPackSerializer::read(current, nullptr), std::runtime_error);
}

TEST(ReplayPayloadVersionTest, MixedSameChapterCopyMoveAndResavePreserveEveryOrigin)
{
	auto log = knownLog();
	log.addPack(packet(Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES));
	log.addPack(packet(Version::CURRENT));
	const auto expected = encode(log);
	auto copy = log;
	ReplayLog moved = std::move(copy);
	EXPECT_EQ(encode(moved), expected);
	const auto restored = decode(expected, Version::CURRENT);
	const auto & chapter = restored.getChapters().front();
	EXPECT_EQ(chapter.snapshotVersion, Version::NEW_HORIZONS_FLY_DAY_END);
	EXPECT_EQ(chapter.packVersions, (std::vector<Version>{Version::NEW_HORIZONS_FLY_DAY_END,
		Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES, Version::CURRENT}));
	for(size_t i = 0; i < chapter.packs.size(); ++i)
		EXPECT_NE(ReplayPackSerializer::read(chapter.packPayload(i), nullptr), nullptr);
}

TEST(ReplayPayloadVersionTest, KnownProvenanceCannotBeDroppedBeforeChapterLogOrWorldPrefix)
{
	auto log = knownLog();
	for(const int kind : {0, 1, 2})
	{
		CMemorySerializer wire;
		wire.oser.version = Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
		if(kind == 0)
			EXPECT_THROW(wire.oser & log.getChapters().front(), std::runtime_error);
		else if(kind == 1)
			EXPECT_THROW(wire.oser & log, std::runtime_error);
		else
		{
			CGameState world;
			world.replayLog = log;
			EXPECT_THROW(wire.oser & world, std::runtime_error);
		}
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}

TEST(ReplayPayloadVersionTest, InvalidStorageAndVersionRefuseWithoutChangingExistingHistory)
{
	ReplayChapter chapter;
	chapter.packs = {{std::byte{1}}};
	EXPECT_THROW(chapter.validateStorage(), std::runtime_error);
	chapter.packVersions = {static_cast<Version>(static_cast<int32_t>(Version::CURRENT) + 1)};
	EXPECT_THROW(chapter.validateStorage(), std::runtime_error);
	chapter.packs.clear();
	chapter.packVersions.clear();
	chapter.snapshotVersion = Version::CURRENT;
	EXPECT_THROW(chapter.validateStorage(), std::runtime_error);
	auto log = knownLog();
	const auto before = encode(log);
	const ReplayPayload invalid{{std::byte{9}}, static_cast<Version>(static_cast<int32_t>(Version::CURRENT) + 1)};
	EXPECT_THROW(log.addPack(invalid), std::runtime_error);
	EXPECT_EQ(encode(log), before);
}

TEST(ReplayPayloadVersionTest, PreparedMixedBatchReservesBothArraysAndPublishesExactPairsWithoutAllocation)
{
	auto log = knownLog();
	PreparedReplayAppend::Records records{packet(Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES), packet(Version::CURRENT)};
	const auto expected = records;
	const auto * retainedBytes = records.front().bytes.data();
	PreparedReplayAppend plan(log, std::move(records), 1, [] { return ReplayPayload{}; },
		PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
	const auto & chapter = log.getChapters().back();
	ASSERT_GE(chapter.packs.capacity(), chapter.packs.size() + 2);
	ASSERT_GE(chapter.packVersions.capacity(), chapter.packVersions.size() + 2);
	const auto * array = chapter.packs.data();
	const auto * versions = chapter.packVersions.data();
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(chapter.packs.data(), array);
	EXPECT_EQ(chapter.packVersions.data(), versions);
	EXPECT_EQ(chapter.packs[1].data(), retainedBytes);
	EXPECT_EQ(chapter.packPayload(1), expected[0]);
	EXPECT_EQ(chapter.packPayload(2), expected[1]);
	EXPECT_FALSE(plan.commit());
}

TEST(ReplayPayloadVersionTest, InvalidPreparedPayloadRefusesBeforePublishingAnyPair)
{
	auto log = knownLog();
	const auto before = encode(log);
	ReplayPayload invalid{{std::byte{1}}, static_cast<Version>(static_cast<int32_t>(Version::CURRENT) + 1)};
	EXPECT_THROW((PreparedReplayAppend(log, {invalid}, 1, [] { return ReplayPayload{}; },
		PreparedReplayAppend::RecordingMode::UNCONDITIONAL)), std::runtime_error);
	EXPECT_EQ(encode(log), before);
}

TEST(ReplayPayloadVersionTest, LegacyReadClearsKnownMetadataOnReusedStorage)
{
	auto reused = knownLog();
	CMemorySerializer wire(encode(LegacyLog{}, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES));
	wire.iser.version = Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
	wire.iser & reused;
	ASSERT_EQ(reused.getChapters().size(), 1u);
	EXPECT_EQ(reused.getChapters().front().snapshotVersion, Version::NONE);
	EXPECT_EQ(reused.getChapters().front().packVersions, (std::vector<Version>{Version::NONE}));
}

TEST(ReplayPayloadVersionTest, ActualOldAndCurrentConsecutiveSidesUseTheirOriginAndKeepNativeGuards)
{
	std::array<SideInBattle, 2> sides{SideInBattle(nullptr), SideInBattle(nullptr)};
	sides[0].color = PlayerColor(0);
	sides[1].color = PlayerColor(1);
	sides[1].sylvanLuck.chainOfFortune = true;
	sides[1].sylvanLuck.chainTriggeredThisRound = true;
	sides[1].sylvanLuck.positiveLuckUnits = {7};
	sides[1].sylvanLuck.chainSourceUnitId = 7;
	const ReplayPayload historical{encode(sides, Version::NEW_HORIZONS_FLY_DAY_END), Version::NEW_HORIZONS_FLY_DAY_END};
	const auto decodeSides = [](const ReplayPayload & payload)
	{
		replayPayload::requireKnownVersion(payload.version);
		CMemorySerializer wire(payload.bytes);
		wire.iser.version = payload.version;
		std::array<SideInBattle, 2> result{SideInBattle(nullptr), SideInBattle(nullptr)};
		wire.iser & result;
		return result;
	};
	const auto old = decodeSides(historical);
	EXPECT_EQ(old[1].sylvanLuck, sides[1].sylvanLuck);
	EXPECT_EQ(old[0].manaFurnaceRecoveredNormal, 0);
	auto wrong = historical;
	wrong.version = Version::CURRENT;
	EXPECT_THROW(decodeSides(wrong), std::runtime_error); // appended first-side field consumes the next side's prefix
	sides[0].manaFurnaceRecoveredNormal = 7;
	sides[1].manaFurnaceRecoveredNormal = 9;
	const auto current = decodeSides({encode(sides), Version::CURRENT});
	EXPECT_EQ(current[0].manaFurnaceRecoveredNormal, 7);
	EXPECT_EQ(current[1].manaFurnaceRecoveredNormal, 9);
	EXPECT_EQ(current[1].sylvanLuck, sides[1].sylvanLuck);
}

TEST(ReplayPayloadVersionTest, ActualIllegalChainStateStillRejectsUnderItsCorrectVersion)
{
	SylvanLuckState invalid;
	invalid.chainTriggeredThisRound = true;
	invalid.positiveLuckUnits = {7};
	CMemorySerializer wire(encode(invalid));
	wire.iser.version = Version::CURRENT;
	SylvanLuckState decoded;
	EXPECT_THROW(wire.iser & decoded, std::runtime_error);
}
