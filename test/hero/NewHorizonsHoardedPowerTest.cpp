/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/entities/hero/NewHorizonsHoardedPower.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/rewardable/Reward.h"
using namespace newHorizonsHeroes;

TEST(HoardedPowerRules, LevelOneBaseThenGrowthCapsAndHistoricalAbsence)
{
	const HoardedPowerRules rules{10, 1, 50};
	EXPECT_EQ(hoardedPowerCap(rules, 1, 100), 10);
	EXPECT_EQ(hoardedPowerCap(rules, 10, 100), 19);
	EXPECT_EQ(hoardedPowerCap(rules, 41, 100), 50);
	EXPECT_EQ(hoardedPowerCap(rules, 99, 100), 50);
	EXPECT_EQ(hoardedPowerCap(rules, 1, 99), 9);
	EXPECT_FALSE(hoardedPowerRules(JsonNode()));
	JsonNode development;
	auto & row = development["revisedSpecialties"]["hoardedPower"];
	row["baseCapPercent"].Integer() = 10;
	row["capPercentPerLevel"].Integer() = 1;
	row["maximumCapPercent"].Integer() = 50;
	ASSERT_TRUE(hoardedPowerRules(development));
	row["maximumCapPercent"].Integer() = 9;
	EXPECT_THROW(hoardedPowerRules(development), std::runtime_error);
}

class HoardedPowerRecovery : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void startNagash()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:nagash")), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

TEST_F(HoardedPowerRecovery, FixedRecoveryOverflowsThenCapsEveryDirectBufferGrant)
{
	ASSERT_NO_FATAL_FAILURE(startNagash());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	hero->level = 41;
	const auto maximum = hero->manaLimit();
	ASSERT_GT(maximum, 2);
	const auto cap = hoardedPowerCap(*hero);
	ASSERT_TRUE(cap);
	hero->initializeSpellPoints(maximum - 2, 0);
	ASSERT_TRUE(restoreFixedSpellPoints(*hero, 5));
	EXPECT_EQ(hero->getNormalSpellPoints(), maximum);
	EXPECT_EQ(hero->getBufferSpellPoints(), std::min(3, *cap));
	ASSERT_TRUE(hero->grantBufferSpellPoints(100000));
	EXPECT_EQ(hero->getBufferSpellPoints(), *cap);
	ASSERT_TRUE(restoreFixedSpellPoints(*hero, 100000));
	EXPECT_EQ(hero->getBufferSpellPoints(), *cap);
	EXPECT_FALSE(restoreFixedSpellPoints(*hero, -1));
	EXPECT_EQ(hero->getBufferSpellPoints(), *cap);
}

TEST_F(HoardedPowerRecovery, AbsoluteRefillNeverCreatesBufferAndDailyRecoveryKeepsOnlyWastedShare)
{
	ASSERT_NO_FATAL_FAILURE(startNagash());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	hero->initializeSpellPoints(0, 0);
	hero->setNormalSpellPoints(hero->manaLimit());
	EXPECT_EQ(hero->getBufferSpellPoints(), 0);
	const auto grant = dailyHoardedPower(*hero, true);
	EXPECT_EQ(grant, std::min<int32_t>(hero->manaRegain(), *hoardedPowerCap(*hero)));
	hero->initializeSpellPoints(0, 0);
	EXPECT_EQ(dailyHoardedPower(*hero, true), 0);
}

TEST_F(HoardedPowerRecovery, BlackHarvestAllowsOverflowAfterTemporaryBufferExpiresAndSavePreservesPools)
{
	ASSERT_NO_FATAL_FAILURE(startNagash());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto maximum = hero->manaLimit();
	const auto cap = *hoardedPowerCap(*hero);
	hero->initializeSpellPoints(maximum, cap);
	EXPECT_EQ(blackHarvestRecoveryLimit(*hero), maximum);
	EXPECT_EQ(blackHarvestRecoveryLimit(*hero, cap), maximum + cap);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * decoded = restored.getHero(hero->id);
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(decoded->getNormalSpellPoints(), maximum);
	EXPECT_EQ(decoded->getBufferSpellPoints(), cap);
	EXPECT_EQ(hoardedPowerCap(*decoded), cap);
}

TEST_F(HoardedPowerRecovery, CapacityDropDiscardsExcessAndTemporaryProvenanceBeforeAReincrease)
{
	ASSERT_NO_FATAL_FAILURE(startNagash());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	auto knowledge = [&](int value)
	{
		SetPrimarySkill pack;
		pack.id = hero->id;
		pack.which = PrimarySkill::KNOWLEDGE;
		pack.val = value;
		pack.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(pack);
	};
	knowledge(100);
	const auto highCap = *hoardedPowerCap(*hero);
	hero->initializeSpellPoints(hero->manaLimit(), highCap);
	knowledge(20);
	const auto lowCap = *hoardedPowerCap(*hero);
	ASSERT_LT(lowCap, highCap);
	EXPECT_EQ(hero->getBufferSpellPoints(), lowCap);
	knowledge(100);
	EXPECT_EQ(hero->getBufferSpellPoints(), lowCap);
	SideInBattle side(nullptr);
	side.temporaryBufferRemaining = highCap;
	hero->initializeSpellPoints(hero->manaLimit(), highCap);
	knowledge(20);
	// Explicit helper also covers load reconciliation with a saved battle side.
	hero->initializeSpellPoints(hero->manaLimit(), highCap);
	reconcileHoardedPowerBuffer(*hero, &side);
	EXPECT_EQ(hero->getBufferSpellPoints(), lowCap);
	EXPECT_EQ(side.temporaryBufferRemaining, lowCap);
}

TEST_F(HoardedPowerRecovery, RewardQuoteUsesActualBufferHeadroomForFixedRecoveryAndSpringGrants)
{
	ASSERT_NO_FATAL_FAILURE(startNagash());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto maximum = hero->manaLimit();
	const auto cap = *hoardedPowerCap(*hero);
	hero->initializeSpellPoints(maximum, 0);
	EXPECT_EQ(rewardBufferGain(*hero, maximum, 100, 25, 0), cap);
	hero->initializeSpellPoints(maximum, cap);
	EXPECT_EQ(rewardBufferGain(*hero, maximum, 100, 25, 0), 0);
	hero->initializeSpellPoints(0, 0);
	EXPECT_EQ(rewardBufferGain(*hero, 0, maximum, 0, 0), 0);
	EXPECT_EQ(rewardBufferGain(*hero, 0, maximum + cap, 0, 0), cap);
}
