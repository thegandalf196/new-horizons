/*
 * NewHorizonsEstatesIncomeAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#include "lib/CSkillHandler.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/CPlayerState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{
const PlayerColor PLAYER(0);

class NewHorizonsEstatesIncomeAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER);
		for(size_t index = 0; index < 11; ++index)
		{
			const int x = 4 + static_cast<int>(index % 4) * 7;
			const int y = 4 + static_cast<int>(index / 4) * 7;
			builder.town({x, y, 0}, FactionID::RAMPART, PLAYER);
		}
		builder.hero({32, 32, 0}, HeroTypeID(0), PLAYER)
			.hero({32, 28, 0}, HeroTypeID(1), PLAYER);
		startWithMap(std::move(builder));

		firstHero = findHeroAt({32, 32, 0});
		secondHero = findHeroAt({32, 28, 0});
		ASSERT_NE(firstHero, nullptr);
		ASSERT_NE(secondHero, nullptr);
	}

	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
};
} // namespace

TEST_F(NewHorizonsEstatesIncomeAITest, BuildAnalyzerUsesTheSharedCappedIncomeForEachActiveHolder)
{
	startGame();
	const int estatesSkill = SecondarySkill::decode("new-horizons:estates");
	ASSERT_GE(estatesSkill, 0);
	for(auto * hero : {firstHero, secondHero})
	{
		hero->setSecSkillLevel(SecondarySkill(estatesSkill), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
		EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 625);
	}

	const auto * playerState = gameState()->getPlayerState(PLAYER);
	const auto forecast = NK2AI::BuildAnalyzer::calculateDailyIncome(
		playerState->getOwnedObjects(), playerState->getTowns());
	int townIncome = 0;
	for(const auto * town : playerState->getTowns())
		townIncome += town->dailyIncome()[EGameResID::GOLD];
	EXPECT_EQ(forecast[EGameResID::GOLD], townIncome + 2 * 625);
}
