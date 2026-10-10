/*
 * NewHorizonsCombatScalarCaptureTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto luckOption = EGameSettings::COMBAT_NEW_HORIZONS_FINAL_LUCK;
constexpr auto moraleOption = EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT;

JsonNode scalarConfig(bool luck, int morale)
{
	JsonNode result;
	result["combat"]["newHorizonsFinalLuck"].Bool() = luck;
	result["combat"]["moraleExtraDamagePercent"].Integer() = morale;
	return result;
}

ESerializationVersion previousScalarFormat()
{
	return static_cast<ESerializationVersion>(std::min(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_FINAL_LUCK),
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_MORALE_EXTRA_DAMAGE)) - 1);
}

void expectScalars(const IGameSettings & settings, bool luck, int morale)
{
	EXPECT_EQ(settings.getBoolean(luckOption), luck);
	EXPECT_EQ(settings.getInteger(moraleOption), morale);
}

template<typename Value>
void expectOldWriterRejectsBeforePrefix(const Value & value)
{
	CMemorySerializer memory;
	memory.oser.version = previousScalarFormat();
	EXPECT_THROW(memory.oser & value, std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}
}

TEST(NewHorizonsCombatScalarCapture, BinaryAbsenceIsLegacyAcrossOldAndCurrentReadersAndResave)
{
	for(const auto version : {previousScalarFormat(), ESerializationVersion::CURRENT})
	{
		SCOPED_TRACE(static_cast<int>(version));
		CMemorySerializer bytes;
		bytes.oser.version = version;
		bytes.iser.version = version;
		JsonNode absent;
		bytes.oser & absent;
		GameSettings restored;
		restored.loadBase(scalarConfig(true, 75));
		ASSERT_NO_THROW(bytes.iser & restored);
		expectScalars(restored, false, 100);
		CMemorySerializer resave;
		resave.oser & restored;
		GameSettings reread;
		reread.loadBase(scalarConfig(true, 75));
		ASSERT_NO_THROW(resave.iser & reread);
		expectScalars(reread, false, 100);
	}
}

TEST(NewHorizonsCombatScalarCapture, ReusedReaderCannotRetainPreviouslyCapturedModernValues)
{
	GameSettings restored;
	restored.loadBase(scalarConfig(false, 100));
	restored.loadOverrides(scalarConfig(true, 75));
	CMemorySerializer bytes;
	JsonNode absent;
	bytes.oser & absent;
	ASSERT_NO_THROW(bytes.iser & restored);
	expectScalars(restored, false, 100);
}

TEST(NewHorizonsCombatScalarCapture, ExplicitCurrentValuesSurviveChangedInstalledDefaults)
{
	GameSettings source;
	source.loadBase(scalarConfig(false, 100));
	source.loadOverrides(scalarConfig(true, 75));
	CMemorySerializer bytes;
	ASSERT_NO_THROW(bytes.oser & source);
	GameSettings restored;
	restored.loadBase(scalarConfig(false, 100));
	ASSERT_NO_THROW(bytes.iser & restored);
	expectScalars(restored, true, 75);
}

TEST(NewHorizonsCombatScalarCapture, GenericFreshMapOverrideLoadingStillInheritsInstalledDefaults)
{
	GameSettings settings;
	settings.loadBase(scalarConfig(true, 75));
	settings.loadOverrides(JsonNode());
	expectScalars(settings, true, 75);
}

TEST(NewHorizonsCombatScalarCapture, MeaningfulOldWritersRejectButLegacyEquivalentDefaultsRoundTrip)
{
	for(const auto & input : {scalarConfig(true, 100), scalarConfig(false, 75)})
	{
		GameSettings settings;
		settings.loadBase(scalarConfig(false, 100));
		settings.loadOverrides(input);
		expectOldWriterRejectsBeforePrefix(settings);
	}
	GameSettings ordinary;
	ordinary.loadBase(scalarConfig(true, 75));
	ordinary.loadOverrides(scalarConfig(false, 100));
	CMemorySerializer bytes;
	bytes.oser.version = previousScalarFormat();
	bytes.iser.version = previousScalarFormat();
	ASSERT_NO_THROW(bytes.oser & ordinary);
	GameSettings restored;
	restored.loadBase(scalarConfig(true, 75));
	ASSERT_NO_THROW(bytes.iser & restored);
	expectScalars(restored, false, 100);
}

TEST(NewHorizonsCombatScalarCapture, MalformedSavedFieldsRejectBeforeChangingExistingValues)
{
	std::vector<JsonNode> invalid;
	for(const auto value : {0, 101})
		invalid.push_back(scalarConfig(false, value));
	auto floating = scalarConfig(false, 100);
	floating["combat"]["moraleExtraDamagePercent"].Float() = 75.5;
	invalid.push_back(floating);
	auto nullMorale = scalarConfig(false, 100);
	nullMorale["combat"]["moraleExtraDamagePercent"] = JsonNode();
	invalid.push_back(nullMorale);
	auto nullLuck = scalarConfig(false, 100);
	nullLuck["combat"]["newHorizonsFinalLuck"] = JsonNode();
	invalid.push_back(nullLuck);
	auto numericLuck = scalarConfig(false, 100);
	numericLuck["combat"]["newHorizonsFinalLuck"].Integer() = 1;
	invalid.push_back(numericLuck);
	for(const auto & input : invalid)
	{
		GameSettings restored;
		restored.loadBase(scalarConfig(true, 75));
		restored.loadOverrides(scalarConfig(true, 75));
		EXPECT_THROW(restored.loadOverrides(input), std::runtime_error);
		expectScalars(restored, true, 75);
		CMemorySerializer bytes;
		bytes.oser & input;
		EXPECT_THROW(bytes.iser & restored, std::runtime_error);
		expectScalars(restored, true, 75);
	}
}

TEST(NewHorizonsCombatScalarCapture, OldReadersRejectForgedMeaningfulRowsBeforeMutation)
{
	for(const auto & input : {scalarConfig(true, 100), scalarConfig(false, 75)})
	{
		CMemorySerializer bytes;
		bytes.oser.version = previousScalarFormat();
		bytes.iser.version = previousScalarFormat();
		bytes.oser & input;
		GameSettings restored;
		restored.loadBase(scalarConfig(false, 100));
		EXPECT_THROW(bytes.iser & restored, std::runtime_error);
		expectScalars(restored, false, 100);
	}
}

class NewHorizonsCombatScalarWorldTest : public TinyMapGameTest
{
protected:
	bool authoredLegacy = false;
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(authoredLegacy)
			loaded->overrideGameSettings(scalarConfig(false, 100));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Combat scalar capture")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
	}
	void expectFutureBattleScalars(bool luck, int morale)
	{
		const auto * attacker = findHeroByOwner(PlayerColor(0));
		const auto * defender = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attacker, nullptr);
		ASSERT_NE(defender, nullptr);
		const int3 tile(4, 4, 0);
		const auto arena = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", std::string("core:grass_hills"));
		ASSERT_TRUE(arena.has_value());
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attacker, defender);
		BattleSideArray<const CGHeroInstance *> heroes = {attacker, defender};
		BattleSideArray<const CArmedInstance *> armies = {attacker, defender};
		const auto battle = BattleInfo::setupBattle(gameState().get(), tile, map()->getTile(tile).terrainType,
			BattleField(*arena), armies, heroes, layout, nullptr);
		EXPECT_EQ(battle->getLuckRollRules().finalDirectPhysicalMultiplier, luck);
		EXPECT_EQ(battle->getMoraleExtraDamagePercent(), morale);
	}
};

TEST_F(NewHorizonsCombatScalarWorldTest, FreshShippedWorldCapturesBothScalarsAndAllOwningPrefixesRejectOldWrites)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectScalars(gameState()->getSettings(), true, 75);
	ASSERT_NO_FATAL_FAILURE(expectFutureBattleScalars(true, 75));
	const auto & settings = dynamic_cast<const GameSettings &>(map()->getSettings());
	CMemorySerializer rawBytes;
	ASSERT_NO_THROW(rawBytes.oser & settings);
	JsonNode raw;
	ASSERT_NO_THROW(rawBytes.iser & raw);
	EXPECT_EQ(raw["combat"], scalarConfig(true, 75)["combat"]);
	expectOldWriterRejectsBeforePrefix(settings);
	expectOldWriterRejectsBeforePrefix(*map());
	expectOldWriterRejectsBeforePrefix(*gameState());
	LobbyStartGame start;
	start.initializedGameState = gameState();
	expectOldWriterRejectsBeforePrefix(start);
	// Test each independent guard, so one enabled scalar cannot mask the other.
	map()->overrideGameSettings(scalarConfig(true, 100));
	expectOldWriterRejectsBeforePrefix(*gameState());
	map()->overrideGameSettings(scalarConfig(false, 75));
	expectOldWriterRejectsBeforePrefix(*gameState());
}

TEST_F(NewHorizonsCombatScalarWorldTest, FreshAuthoredLegacyValuesAreCapturedWithoutReplacingThem)
{
	authoredLegacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectScalars(gameState()->getSettings(), false, 100);
	ASSERT_NO_FATAL_FAILURE(expectFutureBattleScalars(false, 100));
	const auto & settings = dynamic_cast<const GameSettings &>(map()->getSettings());
	CMemorySerializer bytes;
	bytes.oser.version = previousScalarFormat();
	ASSERT_NO_THROW(bytes.oser & settings);
	EXPECT_FALSE(bytes.extractBuffer().empty());
	EXPECT_NO_THROW(gameState()->validateCombatScalarSerialization(false, false));
}
