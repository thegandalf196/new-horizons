/*
 * NewHorizonsCanonicalSpellClausesSerializationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMapInfo.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
struct Clause
{
	const char * spell;
	const char * field;
};
constexpr std::array<Clause, 5> CLAUSES{{
	{"core:implosion", "implosion"},
	{"core:teleport", "ignoreInterveningBarriers"},
	{"core:dispel", "temporaryMagicalEffectsOnly"},
	{"core:curse", "schoolRankDurations"},
	{"core:fireWall", "burnGroundedFlyers"}
}};

ESerializationVersion previousVersion()
{
	return static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_IMPLOSION) - 1);
}

JsonNode absentRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto & clause : CLAUSES)
		rules["spells"][clause.spell].Struct().erase(clause.field);
	return rules;
}

JsonNode validValue(size_t index)
{
	const auto & clause = CLAUSES.at(index);
	if(index == 0)
		return JsonNode(JsonPath::builtin("config/newHorizonsMagic"))["spells"][clause.spell][clause.field];
	if(index == 3)
	{
		JsonNode table;
		for(int duration : {3, 4, 4, 5})
			table.Vector().emplace_back(duration);
		return table;
	}
	return JsonNode(true);
}

JsonNode rawOverrides(const JsonNode & rules)
{
	JsonNode overrides;
	overrides["magic"]["newHorizons"] = rules;
	return overrides;
}

template<typename T>
void rejectsBeforePrefix(const T & value)
{
	CMemorySerializer bytes;
	bytes.oser.version = previousVersion();
	EXPECT_THROW(bytes.oser & value, std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
}

class NewHorizonsCanonicalSpellClausesSerializationTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<size_t>
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, capturedRules);
	}
	void prepare()
	{
		capturedRules = absentRules();
		const auto & clause = CLAUSES.at(GetParam());
		capturedRules["spells"][clause.spell][clause.field] = validValue(GetParam());
		ASSERT_NO_THROW(newHorizonsMagic::validateCanonicalSpellClausesSerialization(capturedRules, true));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({8, 8, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({28, 28, 0}, HeroTypeID(1), PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroAt({8, 8, 0});
		opponent = findHeroAt({28, 28, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(opponent, nullptr);
		ASSERT_EQ(hero->getMagicRules(), capturedRules);
	}
	std::unique_ptr<BattleInfo> initializedBattle()
	{
		const BattleSideArray<const CGHeroInstance *> heroes = {hero, opponent};
		const BattleSideArray<const CArmedInstance *> armies = {hero, opponent};
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), hero, opponent);
		const BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", std::string("core:sand_shore")));
		const auto tile = hero->visitablePos();
		return BattleInfo::setupBattle(gameState().get(), tile, gameState()->getTile(tile)->getTerrainID(),
			battlefield, armies, heroes, layout, nullptr);
	}
	JsonNode capturedRules;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * opponent = nullptr;
};

TEST_P(NewHorizonsCanonicalSpellClausesSerializationTest, EveryKeyPresenceRejectsOldRawWriterAndReader)
{
	const auto & clause = CLAUSES.at(GetParam());
	JsonNode empty;
	empty.Struct();
	for(const auto & value : {JsonNode(true), JsonNode(false), JsonNode(), empty})
	{
		SCOPED_TRACE(static_cast<int>(value.getType()));
		auto rules = absentRules();
		rules["spells"][clause.spell][clause.field] = value;
		GameSettings settings;
		settings.loadOverrides(rawOverrides(rules));
		rejectsBeforePrefix(settings);
		// Write raw JSON, not GameSettings, to exercise the old reader guard.
		CMemorySerializer raw;
		raw.oser & rawOverrides(rules);
		raw.iser.version = previousVersion();
		GameSettings restored;
		EXPECT_THROW(raw.iser & restored, std::runtime_error);
	}
}

TEST_P(NewHorizonsCanonicalSpellClausesSerializationTest, CurrentValidRoundTripAndMalformedRawRead)
{
	const auto & clause = CLAUSES.at(GetParam());
	auto rules = absentRules();
	rules["spells"][clause.spell][clause.field] = validValue(GetParam());
	GameSettings settings;
	settings.loadOverrides(rawOverrides(rules));
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & settings);
	GameSettings restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_TRUE(restored.getMagicOverride().has_value());
	EXPECT_EQ(*restored.getMagicOverride(), rules);
	for(const auto & value : {JsonNode(), JsonNode(7), JsonNode("invalid")})
	{
		SCOPED_TRACE(static_cast<int>(value.getType()));
		rules["spells"][clause.spell][clause.field] = value;
		CMemorySerializer raw;
		raw.oser & rawOverrides(rules);
		GameSettings malformed;
		EXPECT_THROW(raw.iser & malformed, std::runtime_error);
		GameSettings malformedWriter;
		malformedWriter.loadOverrides(rawOverrides(rules));
		CMemorySerializer invalid;
		EXPECT_THROW(invalid.oser & malformedWriter, std::runtime_error);
		EXPECT_TRUE(invalid.extractBuffer().empty());
	}
}

TEST_P(NewHorizonsCanonicalSpellClausesSerializationTest, CapturedOwnersAndAuthoritativeEnvelopesRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	rejectsBeforePrefix(*hero);
	rejectsBeforePrefix(*map());
	rejectsBeforePrefix(*gameState());
	auto battle = initializedBattle();
	ASSERT_NE(battle, nullptr);
	ASSERT_EQ(battle->getMagicRules(), capturedRules);
	rejectsBeforePrefix(*battle);
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = initializedBattle();
	rejectsBeforePrefix(start);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & start);
	current.iser.cb = gameState().get();
	BattleStart restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_NE(restored.info, nullptr);
	EXPECT_EQ(restored.info->getMagicRules(), capturedRules);
	EXPECT_EQ(restored.battleID, start.battleID);
	// Actual object and off-map pool ownership, without a raw map override.
	CMap delegated(gameState().get());
	ASSERT_FALSE(dynamic_cast<const GameSettings &>(delegated.getSettings()).getMagicOverride().has_value());
	auto sharedHero = std::dynamic_pointer_cast<CGHeroInstance>(map()->objects.at(hero->id.getNum()));
	ASSERT_NE(sharedHero, nullptr);
	delegated.objects.push_back(sharedHero);
	rejectsBeforePrefix(delegated);
	delegated.objects.clear();
	delegated.addToHeroPool(sharedHero);
	rejectsBeforePrefix(delegated);
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	rejectsBeforePrefix(lobby);
	// Follow the maintained actual-world replay harness, including callback
	// rebinding during decode; no standalone fabricated world is introduced.
	CMemorySerializer worldBytes;
	ASSERT_NO_THROW(worldBytes.oser & *gameState());
	CGameState restoredWorld;
	worldBytes.iser.cb = &restoredWorld;
	worldBytes.iser.loadingGamestate = true;
	ASSERT_NO_THROW(worldBytes.iser & restoredWorld);
	EXPECT_EQ(restoredWorld.getMagicRules(), capturedRules);
	const auto * restoredHero = restoredWorld.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getMagicRules(), capturedRules);
}

INSTANTIATE_TEST_SUITE_P(FiveCapturedClauses, NewHorizonsCanonicalSpellClausesSerializationTest,
	::testing::Range<size_t>(0, CLAUSES.size()));

TEST(NewHorizonsCanonicalSpellClausesCompatibilityTest, AbsentClausesAndEmptyBattleEnvelopeRemainOldWritable)
{
	const auto rules = absentRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateCanonicalSpellClausesSerialization(rules, false));
	GameSettings settings;
	settings.loadOverrides(rawOverrides(rules));
	CMemorySerializer historical;
	historical.oser.version = previousVersion();
	historical.iser.version = previousVersion();
	ASSERT_NO_THROW(historical.oser & settings);
	GameSettings restored;
	ASSERT_NO_THROW(historical.iser & restored);
	ASSERT_TRUE(restored.getMagicOverride().has_value());
	EXPECT_EQ(*restored.getMagicOverride(), rules);
	BattleStart empty;
	empty.battleID = BattleID(0);
	CMemorySerializer bytes;
	bytes.oser.version = previousVersion();
	bytes.iser.version = previousVersion();
	ASSERT_NO_THROW(bytes.oser & empty);
	BattleStart restoredEmpty;
	ASSERT_NO_THROW(bytes.iser & restoredEmpty);
	EXPECT_EQ(restoredEmpty.battleID, empty.battleID);
	EXPECT_EQ(restoredEmpty.info, nullptr);
}
}
