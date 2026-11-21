/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RevisedEstatesFixture.h"
#include "../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../lib/mapObjects/EstatesStewardship.h"

namespace
{
using namespace estatesTest;

/// Estates policy 2: every owned hero may be a steward.
class CoreStewardshipTest : public RevisedFixture
{
protected:
	CoreStewardshipTest()
	{
		estatesPolicyVersion = 2;
	}
};

/// Clavius in a Necropolis town, with the shipped (revised) or a historical specialty capture.
class LordOfTheCryptsTest : public RevisedFixture
{
protected:
	bool historicalSpecialties = false;
	CGHeroInstance * clavius = nullptr;
	CGHeroInstance * other = nullptr;
	CGTownInstance * necropolis = nullptr;
	CGTownInstance * castle = nullptr;

	LordOfTheCryptsTest()
	{
		estatesPolicyVersion = 2;
	}

	void mapLoaded(CMap * loaded) override
	{
		RevisedFixture::mapLoaded(loaded);
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historicalSpecialties)
		{
			heroes.Struct().erase("revisedSpecialties");
			heroes.setOverrideFlag(true); // Replace, rather than merge over, the installed rules.
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroes));
	}

	void startNecropolisGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.town({7, 7, 0}, FactionID::NECROPOLIS, PLAYER).townGarrison({})
			.town({7, 20, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
			.hero({30, 30, 0}, HeroTypeID(HeroTypeID::decode("core:clavius")), PLAYER)
				.heroGarrison({{creature("core:skeleton"), 1}})
			.hero({30, 26, 0}, HeroTypeID(1), PLAYER).heroGarrison({{creature("core:pikeman"), 1}});
		startWithMap(std::move(builder));
		clavius = findHeroAt({30, 30, 0});
		other = findHeroAt({30, 26, 0});
		necropolis = expectAt<CGTownInstance>({7, 7, 0});
		castle = expectAt<CGTownInstance>({7, 20, 0});
		ASSERT_TRUE(clavius && other && necropolis && castle);
		necropolis->creatures = {{0, {creature("core:skeleton"), creature("core:skeletonWarrior")}}};
		castle->creatures = {{0, {creature("core:pikeman"), creature("core:halberdier")}}};
	}

	int specialtyGold() const
	{
		int result = 0;
		for(const auto & bonus : clavius->getExportedBonusList())
			if(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL
				&& bonus->sid == BonusSourceID(clavius->getHeroTypeID()))
				result += bonus->val;
		return result;
	}

	int crypts(const CGTownInstance & target) const
	{
		const auto * steward = newHorizonsEstates::townSteward(target);
		return steward ? newHorizonsHeroes::lordOfTheCryptsGrowth(*steward, target, 0) : 0;
	}
};
}

TEST_F(CoreStewardshipTest, AnyOwnedHeroStewardsFor500AndTheStewardshipPerkFor1000)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto treasury = gameState()->getPlayerState(PLAYER)->resources;
	const auto ordinary = town->dailyIncome()[EGameResID::GOLD];
	EXPECT_TRUE(newHorizonsEstates::eligibleSteward(first));
	ASSERT_TRUE(handler.assignTownSteward(town->id, first->id, PLAYER));
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), first);
	EXPECT_EQ(newHorizonsEstates::stewardedTown(*first), town);
	EXPECT_EQ(town->dailyIncome()[EGameResID::GOLD], ordinary + newHorizonsEstates::CORE_STEWARD_GOLD);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury); // Appointment never collects income.
	ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "stewardship"));
	ASSERT_TRUE(handler.assignTownSteward(town->id, second->id, PLAYER));
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), second);
	EXPECT_EQ(newHorizonsEstates::stewardedTown(*first), nullptr);
	EXPECT_EQ(town->dailyIncome()[EGameResID::GOLD], ordinary + newHorizonsEstates::STEWARDSHIP_PERK_GOLD);
	ASSERT_TRUE(handler.assignTownSteward(town->id, ObjectInstanceID::NONE, PLAYER));
	EXPECT_EQ(town->dailyIncome()[EGameResID::GOLD], ordinary);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury);
}

TEST_F(CoreStewardshipTest, OwnershipLossEndsTheAppointment)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.assignTownSteward(town->id, first->id, PLAYER));
	handler.setOwner(town, PlayerColor::NEUTRAL);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->estates.stewards.empty());
	EXPECT_EQ(newHorizonsEstates::stewardedTown(*first), nullptr);
}


TEST_F(LordOfTheCryptsTest, StewardClaviusAddsOneSkeletonPerLevelToHisNecropolisTown)
{
	ASSERT_NO_FATAL_FAILURE(startNecropolisGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	clavius->level = 1;
	const int before = necropolis->getGrowthInfo(0).totalGrowth();
	ASSERT_TRUE(handler.assignTownSteward(necropolis->id, clavius->id, PLAYER));
	EXPECT_EQ(crypts(*necropolis), 1);
	EXPECT_GT(necropolis->getGrowthInfo(0).totalGrowth(), before);
	clavius->level = 20;
	EXPECT_EQ(crypts(*necropolis), 20);
	EXPECT_TRUE(newHorizonsHeroes::benefitsFromStewardship(*clavius, *necropolis));
	EXPECT_FALSE(newHorizonsHeroes::benefitsFromStewardship(*clavius, *castle));
	EXPECT_EQ(newHorizonsHeroes::lordOfTheCryptsGrowth(*clavius, *necropolis, 1), 0); // Only the Skeleton dwelling.
	ASSERT_TRUE(handler.assignTownSteward(castle->id, clavius->id, PLAYER));
	EXPECT_EQ(crypts(*castle), 0);
	EXPECT_EQ(crypts(*necropolis), 0);
	EXPECT_EQ(necropolis->getGrowthInfo(0).totalGrowth(), before);
}

TEST_F(LordOfTheCryptsTest, OtherStewardsAddNoSkeletonGrowth)
{
	ASSERT_NO_FATAL_FAILURE(startNecropolisGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.assignTownSteward(necropolis->id, other->id, PLAYER));
	EXPECT_EQ(crypts(*necropolis), 0);
	EXPECT_FALSE(newHorizonsHeroes::benefitsFromStewardship(*other, *necropolis));
}

TEST_F(LordOfTheCryptsTest, RevisedClaviusNoLongerGeneratesDailyGold)
{
	ASSERT_NO_FATAL_FAILURE(startNecropolisGame());
	EXPECT_EQ(specialtyGold(), 0);
	EXPECT_EQ(clavius->getSpecialtyPresentation().name, "Lord of the Crypts");
}

namespace
{
class HistoricalClaviusTest : public LordOfTheCryptsTest
{
protected:
	HistoricalClaviusTest()
	{
		historicalSpecialties = true;
	}
};
}

TEST_F(HistoricalClaviusTest, HistoricalCaptureKeepsGoldAndAddsNoGrowth)
{
	ASSERT_NO_FATAL_FAILURE(startNecropolisGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	EXPECT_EQ(specialtyGold(), 350);
	ASSERT_TRUE(handler.assignTownSteward(necropolis->id, clavius->id, PLAYER));
	EXPECT_EQ(crypts(*necropolis), 0);
}

TEST(RevisedSpecialtyRules, LordOfTheCryptsRowIsStrict)
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto rules = newHorizonsHeroes::lordOfTheCryptsRules(heroes);
	ASSERT_TRUE(rules.has_value());
	EXPECT_EQ(rules->skeletonGrowthPerLevel, 1);
	auto invalid = heroes;
	invalid["revisedSpecialties"]["lordOfTheCrypts"]["skeletonGrowthPerLevel"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
	invalid = heroes;
	invalid["revisedSpecialties"]["unknown"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
	heroes.Struct().erase("revisedSpecialties");
	EXPECT_FALSE(newHorizonsHeroes::lordOfTheCryptsRules(heroes).has_value());
}
