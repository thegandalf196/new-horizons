/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleAdventureSpecialtyFixture.h"
#include "../mock/GameHandlerTestServer.h"
#include "../SpellPointTestUtils.h"
#include "../../lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/networkPacks/AcademySpecialtyPacks.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/battles/BattleAcademySpecialties.h"

namespace
{
using namespace castleSpecialtyTest;

/// Academy adventure-map specialties on the shipped rules, or on a 0.94.0-beta.1 capture
/// without the named Academy rows (`strippedRows`).
class AcademyAdventureSpecialtyTest : public CastleSpecialtyFixture
{
protected:
	std::vector<std::string> strippedRows;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;

	void mapLoaded(CMap * loaded) override
	{
		CastleSpecialtyFixture::mapLoaded(loaded);
		if(strippedRows.empty())
			return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		for(const auto & key : strippedRows)
			heroes["revisedSpecialties"].Struct().erase(key);
		// The beta.1 starting book table had no Solmyr row.
		if(vstd::contains(strippedRows, "stormLord"))
			heroes["startingSkills"]["startingBookReplacements"].Struct().erase("core:solmyr");
		for(auto & [hero, rows] : heroes["startingSkills"]["startingPerkProfiles"].Struct())
			for(auto & row : rows.Vector())
				if(row["perk"].String() == ESTATES + ".prosperity" || row["perk"].String() == ESTATES + ".landSurveyor")
					row["perk"].String() = ESTATES + ".supervisor";
		heroes.setOverrideFlag(true); // Replace, rather than merge over, the installed rules.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroes));
	}

	void startAcademy(const char * hero, int level = 1)
	{
		ASSERT_NO_FATAL_FAILURE(startWith(hero));
		specialist->level = level;
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
	}

	int academyResource(EGameResID resource) const
	{
		return gameState()->getPlayerState(PLAYER)->resources[resource];
	}

	int heroSpecialtyIncome(const CGHeroInstance & hero, EGameResID resource) const
	{
		int result = 0;
		for(const auto & bonus : hero.getExportedBonusList())
			if(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL
				&& bonus->subtype == BonusSubtypeID(GameResID(resource)))
				result += bonus->val;
		return result;
	}

	std::unique_ptr<CGameState> reload() const
	{
		auto world = std::make_unique<CGameState>();
		world->preInit(LIBRARY);
		world->loadFromMemory(gameState()->saveToMemory());
		return world;
	}
};

class BetaOneAcademyAdventureTest : public AcademyAdventureSpecialtyTest
{
protected:
	BetaOneAcademyAdventureTest()
	{
		strippedRows = {"hardened", "spoilsOfAlchemy", "practiceMakesPerfect", "wellspringOfWealth", "stormLord",
			"serpentsEmbraceBound", "goldStandardDiscount", "inexhaustibleKnowledge", "halonNullifyingShot", "focusMagicSpecialty",
			"sageOfTheAges"};
	}
};
}

TEST_F(AcademyAdventureSpecialtyTest, WellspringOfWealthPaysGoldFromHerManaInsteadOfFlatGold)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:aine"));
	EXPECT_EQ(heroSpecialtyIncome(*specialist, EGameResID::GOLD), 0) << "The flat 350 Gold is retired";
	setTestSpellPointTotal(specialist, 40);
	EXPECT_EQ(newHorizonsHeroes::wellspringOfWealthGold(*specialist), 40 * 1575 / 1000) << "x1.575 at level 1";
	const auto withMana = specialist->dailyIncome()[EGameResID::GOLD];
	setTestSpellPointTotal(specialist, 0);
	EXPECT_EQ(withMana - specialist->dailyIncome()[EGameResID::GOLD], 63) << "Her own income carries it; spent Mana is income lost";
	specialist->level = 20;
	setTestSpellPointTotal(specialist, 40);
	EXPECT_EQ(newHorizonsHeroes::wellspringOfWealthGold(*specialist), 120) << "x3 from level 20";
	specialist->level = 30;
	EXPECT_EQ(newHorizonsHeroes::wellspringOfWealthGold(*specialist), 120);
	EXPECT_EQ(newHorizonsHeroes::wellspringOfWealthGold(*ordinary), 0);
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Wellspring of Wealth");
}

TEST_F(BetaOneAcademyAdventureTest, HistoricalAineKeepsHerDailyGold)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:aine"));
	EXPECT_GT(heroSpecialtyIncome(*specialist, EGameResID::GOLD), 0);
	setTestSpellPointTotal(specialist, 40);
	EXPECT_EQ(newHorizonsHeroes::wellspringOfWealthGold(*specialist), 0);
}

TEST_F(AcademyAdventureSpecialtyTest, SpoilsOfAlchemyDistilsMercuryFromDestroyedEnemies)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:rissa"));
	EXPECT_EQ(heroSpecialtyIncome(*specialist, EGameResID::MERCURY), 0) << "The daily Mercury is retired";
	EXPECT_EQ(newHorizonsHeroes::spoilsGoldPerMercury(specialist), 1950);
	const auto before = academyResource(EGameResID::MERCURY);
	// 20 Angels (3000 Gold each) and a Ballista, which does not count.
	battleAcademySpecialties::grantSpoilsOfAlchemy(*handler, BattleID(0), specialist,
		{{creature("core:angel"), 20}, {creature("core:ballista"), 1}});
	EXPECT_EQ(academyResource(EGameResID::MERCURY), before + 10) << "60000 / 1950 is 30, capped at 10";
	battleAcademySpecialties::grantSpoilsOfAlchemy(*handler, BattleID(0), specialist, {{creature("core:pikeman"), 30}});
	EXPECT_EQ(academyResource(EGameResID::MERCURY), before + 10 + 30 * 60 / 1950);
	specialist->level = 20;
	EXPECT_EQ(newHorizonsHeroes::spoilsGoldPerMercury(specialist), 1000);
	EXPECT_EQ(newHorizonsHeroes::spoilsOfAlchemyMercury(specialist, 5999), 5);
	EXPECT_EQ(newHorizonsHeroes::spoilsOfAlchemyMercury(ordinary, 60000), 0);
	EXPECT_EQ(newHorizonsHeroes::spoilsRecruitmentGold(*specialist), int64_t{60}) << "Her one Pikeman, for the AI";
}

TEST_F(BetaOneAcademyAdventureTest, HistoricalRissaKeepsHerDailyMercury)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:rissa"));
	EXPECT_GT(heroSpecialtyIncome(*specialist, EGameResID::MERCURY), 0);
	EXPECT_EQ(newHorizonsHeroes::spoilsOfAlchemyMercury(specialist, 60000), 0);
}

TEST_F(AcademyAdventureSpecialtyTest, GoldStandardUpgradeCostsLessWithHerLevel)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:josephine"));
	const auto iron = creature("core:stoneGolem");
	const auto gold = creature("core:goldGolem");
	specialist->setCreature(SlotID(0), iron, 10);
	const auto * stack = specialist->getStackPtr(SlotID(0));
	ASSERT_NE(stack, nullptr);
	const auto ordinaryCost = gold.toCreature()->getFullRecruitCost() - iron.toCreature()->getFullRecruitCost();
	const auto quote = [&]()
	{
		UpgradeInfo info(iron);
		specialist->fillUpgradeInfo(info, *stack);
		return info.hasUpgrades() ? info.getUpgradeCostsFor(gold)[EGameResID::GOLD] : -1;
	};
	EXPECT_EQ(quote(), ordinaryCost[EGameResID::GOLD] * 90 / 100) << "10% less at level 1";
	specialist->level = 21;
	EXPECT_EQ(quote(), ordinaryCost[EGameResID::GOLD] * 70 / 100) << "+1% per level, up to 30%";
}

TEST_F(BetaOneAcademyAdventureTest, HistoricalJosephinePaysTheOrdinaryUpgradeCost)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:josephine", 21));
	const auto iron = creature("core:stoneGolem");
	const auto gold = creature("core:goldGolem");
	specialist->setCreature(SlotID(0), iron, 10);
	UpgradeInfo info(iron);
	specialist->fillUpgradeInfo(info, *specialist->getStackPtr(SlotID(0)));
	ASSERT_TRUE(info.hasUpgrades());
	EXPECT_EQ(info.getUpgradeCostsFor(gold)[EGameResID::GOLD],
		(gold.toCreature()->getFullRecruitCost() - iron.toCreature()->getFullRecruitCost())[EGameResID::GOLD]);
}

TEST_F(AcademyAdventureSpecialtyTest, SageOfTheAgesGrantsTwoAndTwoPerGainedLevel)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:thane"));
	EXPECT_TRUE(newHorizonsHeroes::hasSageOfTheAges(specialist));
	EXPECT_EQ(newHorizonsHeroes::sageGrowth(*specialist), (std::array<int, 4>{0, 0, 2, 2}));
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Sage of the Ages");
	const auto world = reload();
	const auto * loaded = world->getHero(specialist->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(newHorizonsHeroes::sageGrowth(*loaded), (std::array<int, 4>{0, 0, 2, 2})) << "The captured row is saved";
}

TEST_F(BetaOneAcademyAdventureTest, BetaOneThaneKeepsOneAndOneThroughSaveAndLoad)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:thane"));
	EXPECT_TRUE(newHorizonsHeroes::hasSageOfTheAges(specialist)) << "Sage of the Ages itself was already captured";
	EXPECT_EQ(newHorizonsHeroes::sageGrowth(*specialist), (std::array<int, 4>{0, 0, 1, 1}));
	const auto world = reload();
	const auto * loaded = world->getHero(specialist->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(newHorizonsHeroes::sageGrowth(*loaded), (std::array<int, 4>{0, 0, 1, 1})) << "Old captures keep +1/+1";
	EXPECT_NE(loaded->getSpecialtyPresentation().description.find("+1 Spell Power and +1 Knowledge"), std::string::npos);
}

TEST_F(AcademyAdventureSpecialtyTest, PracticeAndRevisedRowsSurviveSaveAndLoad)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:astral", 3));
	SetNewHorizonsSpellPractice practice;
	practice.heroID = specialist->id;
	practice.spell = SpellID(SpellID::MAGIC_ARROW);
	practice.percent = 4;
	handler->sendAndApply(practice);
	EXPECT_EQ(newHorizonsHeroes::practicePercent(specialist, SpellID::MAGIC_ARROW), 4);
	const auto world = reload();
	const auto * loaded = world->getHero(specialist->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(newHorizonsHeroes::practicePercent(loaded, SpellID::MAGIC_ARROW), 4) << "The practice marker is saved";
	EXPECT_EQ(newHorizonsHeroes::practiceCapPercent(loaded), 6);
}

TEST_F(AcademyAdventureSpecialtyTest, RevisedAcademyHeroesShowTheirSpecialtyAndSolmyrStartsWithLightningBolt)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:solmyr"));
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Storm Lord");
	EXPECT_TRUE(specialist->spellbookContainsSpell(SpellID::LIGHTNING_BOLT));
	EXPECT_FALSE(specialist->spellbookContainsSpell(SpellID(SpellID::decode("new-horizons:masterChainLightning"))));
	EXPECT_FALSE(specialist->spellbookContainsSpell(SpellID::CHAIN_LIGHTNING)) << "Learned at level 10";
}

TEST_F(BetaOneAcademyAdventureTest, BetaOneSolmyrKeepsMasterChainLightning)
{
	ASSERT_NO_FATAL_FAILURE(startAcademy("core:solmyr"));
	EXPECT_NE(specialist->getSpecialtyPresentation().name, "Storm Lord");
	EXPECT_FALSE(newHorizonsHeroes::stormLordAllowsSpell(*specialist, SpellID::CHAIN_LIGHTNING));
}
