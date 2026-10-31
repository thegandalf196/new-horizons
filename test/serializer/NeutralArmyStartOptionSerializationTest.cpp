/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/StartInfo.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
#include <limits>

namespace
{
using Version = ESerializationVersion;
constexpr std::array<Version, 3> oldEpochs{{static_cast<Version>(1144), static_cast<Version>(1145), static_cast<Version>(1146)}};
template<class T> std::vector<std::byte> encode(T & value, Version epoch = Version::CURRENT)
{
	CMemorySerializer wire;
	wire.oser.version = epoch;
	value.serialize(wire.oser);
	return wire.extractBuffer();
}
template<class T> void oldPrefixes(T & value)
{
	for(const auto epoch : oldEpochs)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		try { value.serialize(wire.oser); FAIL() << "Expected unsupported neutral start option"; }
		catch(const std::runtime_error & error)
		{
			EXPECT_NE(std::string(error.what()).find("Neutral army start option"), std::string::npos);
		}
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
template<class T> void currentRoundtrip(T & source, T & target)
{
	CMemorySerializer wire(encode(source));
	ASSERT_NO_THROW(target.serialize(wire.iser));
}
std::vector<std::byte> directExtraWire(const ExtraOptionsInfo & flags, std::optional<int32_t> value, bool withOption)
{
	// Narrow malformed-wire fixture. A valid four-field control below must
	// match the ordinary codec before using an invalid trailing integer.
	CMemorySerializer wire;
	wire.oser & flags.cheatsAllowed;
	wire.oser & flags.unlimitedReplay;
	wire.oser & flags.recordGame;
	if(withOption) wire.oser & value;
	return wire.extractBuffer();
}
class NeutralArmyStartOptionSerializationTest : public ::testing::TestWithParam<int> {};
}
TEST_P(NeutralArmyStartOptionSerializationTest, EveryExplicitValueRejectsOldOwnerPrefixesAndCurrentKeepsRequest)
{
	StartInfo start;
	start.mode = EStartMode::NEW_GAME;
	start.extraOptionsInfo.neutralArmySizePercent = GetParam();
	oldPrefixes(start.extraOptionsInfo);
	oldPrefixes(start);
	ExtraOptionsInfo extraDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(start.extraOptionsInfo, extraDecoded));
	EXPECT_EQ(extraDecoded, start.extraOptionsInfo);
	StartInfo startDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(start, startDecoded));
	EXPECT_EQ(startDecoded.extraOptionsInfo, start.extraOptionsInfo);
	LobbySetExtraOptions set;
	set.extraOptionsInfo = start.extraOptionsInfo;
	oldPrefixes(set);
	LobbySetExtraOptions setDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(set, setDecoded));
	EXPECT_EQ(setDecoded.extraOptionsInfo, start.extraOptionsInfo);
	CMapInfo savedMap;
	savedMap.scenarioOptionsOfSave = std::make_unique<StartInfo>(start);
	oldPrefixes(savedMap);
	CMapInfo mapDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(savedMap, mapDecoded));
	ASSERT_NE(mapDecoded.scenarioOptionsOfSave, nullptr);
	EXPECT_EQ(mapDecoded.scenarioOptionsOfSave->extraOptionsInfo, start.extraOptionsInfo);
	LobbyState state;
	state.si = std::make_shared<StartInfo>(start);
	oldPrefixes(state);
	LobbyState stateDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(state, stateDecoded));
	EXPECT_EQ(stateDecoded.si->extraOptionsInfo, start.extraOptionsInfo);
	// Isolate the second owned StartInfo route, inside saved-map metadata.
	state.si->extraOptionsInfo.neutralArmySizePercent.reset();
	state.mi = std::make_shared<CMapInfo>();
	state.mi->scenarioOptionsOfSave = std::make_unique<StartInfo>(start);
	oldPrefixes(state);
	LobbyUpdateState update;
	update.state = state;
	oldPrefixes(update);
	LobbyUpdateState updateDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(update, updateDecoded));
	ASSERT_NE(updateDecoded.state.mi, nullptr);
	ASSERT_NE(updateDecoded.state.mi->scenarioOptionsOfSave, nullptr);
	EXPECT_EQ(updateDecoded.state.mi->scenarioOptionsOfSave->extraOptionsInfo, start.extraOptionsInfo);
	LobbyStartGame begin;
	begin.initializedStartInfo = std::make_shared<StartInfo>(start);
	oldPrefixes(begin);
	LobbyStartGame beginDecoded;
	ASSERT_NO_FATAL_FAILURE(currentRoundtrip(begin, beginDecoded));
	ASSERT_NE(beginDecoded.initializedStartInfo, nullptr);
	EXPECT_EQ(beginDecoded.initializedStartInfo->extraOptionsInfo, start.extraOptionsInfo);
}
INSTANTIATE_TEST_SUITE_P(SelectedPercentages, NeutralArmyStartOptionSerializationTest, ::testing::Values(1, 50, 75, 100));
TEST(NeutralArmyStartOptionSerialization, AbsentOptionPreservesAllOldExtraBytesAndReusedReadClearsRequest)
{
	static_assert(Version::NEW_HORIZONS_NEUTRAL_ARMY_START_OPTION == static_cast<Version>(1147));
	static_assert(Version::CURRENT >= Version::NEW_HORIZONS_NEUTRAL_ARMY_START_OPTION);
	ExtraOptionsInfo flags;
	flags.cheatsAllowed = false;
	flags.unlimitedReplay = true;
	flags.recordGame = true;
	for(const auto epoch : oldEpochs)
	{
		const auto bytes = encode(flags, epoch);
		EXPECT_EQ(bytes, directExtraWire(flags, std::nullopt, false));
		CMemorySerializer wire(bytes);
		wire.iser.version = epoch;
		ExtraOptionsInfo reused;
		reused.neutralArmySizePercent = 1;
		ASSERT_NO_THROW(reused.serialize(wire.iser));
		EXPECT_EQ(reused, flags);
		StartInfo source;
		source.extraOptionsInfo = flags;
		CMemorySerializer startWire(encode(source, epoch));
		startWire.iser.version = epoch;
		StartInfo target;
		target.extraOptionsInfo.neutralArmySizePercent = 100;
		ASSERT_NO_THROW(target.serialize(startWire.iser));
		EXPECT_EQ(target.extraOptionsInfo, flags);
	}
}
TEST(NeutralArmyStartOptionSerialization, InvalidEncodeRejectsBeforeFlagsAndInvalidReadRejects)
{
	for(const int32_t value : {std::numeric_limits<int32_t>::min(), -1, 0, 101, std::numeric_limits<int32_t>::max()})
	{
		ExtraOptionsInfo bad;
		bad.neutralArmySizePercent = value;
		for(const auto epoch : {oldEpochs.front(), oldEpochs[1], oldEpochs.back(), Version::CURRENT})
		{
			CMemorySerializer wire;
			wire.oser.version = epoch;
			EXPECT_THROW(bad.serialize(wire.oser), std::runtime_error);
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		ExtraOptionsInfo valid;
		valid.neutralArmySizePercent = 75;
		ASSERT_EQ(encode(valid), directExtraWire(valid, 75, true));
		CMemorySerializer malformed(directExtraWire(valid, value, true));
		ExtraOptionsInfo discarded;
		EXPECT_THROW(discarded.serialize(malformed.iser), std::runtime_error);
	}
}
