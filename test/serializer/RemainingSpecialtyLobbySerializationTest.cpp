/*
 * Part of VCMI / New Horizons. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/StartInfo.h"
#include "../../lib/GameSettings.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"

class RemainingSpecialtyLobbySerializationTest : public HeroCommandFixture
{
protected:
	int families = 0;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		for(const auto key : {"learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
			rules.Struct().erase(key);
		if(families & 1)
		{
			auto & table = rules["learningExchangeSpecialties"];
			table["version"].Integer() = 1;
			auto & row = table["ecumenicalScholar"];
			row["learningPercentPerLevel"].Integer() = 5;
			row["learningMaximumPercent"].Integer() = 100;
			row["additionalSpellsTaught"].Integer() = 1;
		}
		if(families & 2)
		{
			auto & row = rules["swampSeerSpecialty"];
			row["version"].Integer() = 1;
			row["learningPercentPerLevel"].Integer() = 0;
			row["learningMaximumPercent"].Integer() = 0;
			row["studyExperiencePerMagicPrimaryPoint"].Integer() = 0;
		}
		if(families & 4)
		{
			auto & row = rules["reclamationSpecialty"];
			row["version"].Integer() = 1;
			row["conversionPercentPerLevel"].Integer() = 0;
			row["conversionMaximumPercent"].Integer() = 0;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	LobbyStartGame packet()
	{
		LobbyStartGame value;
		value.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
		value.initializedGameState = gameState();
		return value;
	}
	void samePolicies(const CGameState & decoded)
	{
		const auto * hero = decoded.getHero(attackerSideHero->id);
		ASSERT_NE(hero, nullptr);
		for(const auto key : {"learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
		{
			const auto & expected = gameState()->getHeroDevelopmentRules()[key];
			EXPECT_EQ(decoded.getHeroDevelopmentRules()[key], expected);
			EXPECT_EQ(hero->getPrimaryGrowthRules()[key], expected);
		}
	}
	void currentRoundtripAndOldRefusal()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_TRUE(gameState()->currentBattles.empty());
		const auto before = gameState()->saveReplaySnapshot();
		CGameState direct;
		direct.preInit(LIBRARY);
		ASSERT_NO_THROW(direct.loadReplaySnapshot(before));
		samePolicies(direct);
		auto source = packet();
		CMemorySerializer current;
		ASSERT_NO_THROW(source.serialize(current.oser));
		LobbyStartGame decoded;
		ASSERT_NO_THROW(decoded.serialize(current.iser));
		ASSERT_NE(decoded.initializedGameState, nullptr);
		samePolicies(*decoded.initializedGameState);
		EXPECT_EQ(gameState()->saveReplaySnapshot(), before);
		for(const bool lobby : {false, true})
		{
			CMemorySerializer old;
			old.oser.version = ESerializationVersion::NEW_HORIZONS_RECOVERY_SPECIALTIES;
			if(lobby)
			{
				EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
			}
			else
			{
				EXPECT_THROW(old.oser & *gameState(), std::runtime_error);
			}
			EXPECT_TRUE(old.extractBuffer().empty());
		}
	}
};
TEST_F(RemainingSpecialtyLobbySerializationTest, LearningOnlyCurrentWorldAndLobbyRoundtripAndOldPrefixRefusal)
{
	families = 1;
	currentRoundtripAndOldRefusal();
}
TEST_F(RemainingSpecialtyLobbySerializationTest, SwampOnlyCurrentWorldAndLobbyRoundtripAndOldPrefixRefusal)
{
	families = 2;
	currentRoundtripAndOldRefusal();
}
TEST_F(RemainingSpecialtyLobbySerializationTest, ReclamationOnlyCurrentWorldAndLobbyRoundtripAndOldPrefixRefusal)
{
	families = 4;
	currentRoundtripAndOldRefusal();
}
TEST_F(RemainingSpecialtyLobbySerializationTest, AllFamiliesCurrentWorldAndLobbyRoundtripAndOldPrefixRefusal)
{
	families = 7;
	currentRoundtripAndOldRefusal();
}
TEST_F(RemainingSpecialtyLobbySerializationTest, AbsentFamiliesAllowActualRecoveryWorldAndLobbyRoundtrip)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	auto source = packet();
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_RECOVERY_SPECIALTIES;
	ASSERT_NO_THROW(source.serialize(old.oser));
	LobbyStartGame decoded;
	ASSERT_NO_THROW(decoded.serialize(old.iser));
	ASSERT_NE(decoded.initializedGameState, nullptr);
	samePolicies(*decoded.initializedGameState);
}
