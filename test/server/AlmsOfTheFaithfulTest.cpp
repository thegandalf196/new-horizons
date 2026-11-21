/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleAdventureSpecialtyFixture.h"
#include "../../AI/Nullkiller2/Helpers/TreasureChestChoice.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/rewardable/NewHorizonsChestDonation.h"

namespace
{
using namespace castleSpecialtyTest;

Rewardable::Configuration chest(int gold, int experience)
{
	Rewardable::Configuration result;
	Rewardable::VisitInfo goldOption;
	goldOption.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	goldOption.reward.resources[EGameResID::GOLD] = gold;
	goldOption.reward.removeObject = true;
	Rewardable::VisitInfo experienceOption;
	experienceOption.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	experienceOption.reward.heroExperience = experience;
	experienceOption.reward.primaryExperienceReward = true;
	experienceOption.reward.removeObject = true;
	result.info = {goldOption, experienceOption};
	result.selectMode = Rewardable::SELECT_PLAYER;
	return result;
}

class AlmsOfTheFaithfulTest : public CastleSpecialtyFixture
{
protected:
	int gold() const
	{
		return gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	}
};

class HistoricalAlmsTest : public AlmsOfTheFaithfulTest
{
protected:
	HistoricalAlmsTest()
	{
		historicalSpecialties = true;
	}
};
}

TEST_F(AlmsOfTheFaithfulTest, ExperienceChoiceDonatesALevelScaledShareOfTheDeclinedGold)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:caitlin"));
	const auto rewards = chest(1500, 1000);
	specialist->level = 1;
	EXPECT_EQ(newHorizonsRewardable::declinedChestGold(rewards, 1, *specialist, Obj::TREASURE_CHEST), 1500);
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 300);
	specialist->level = 10;
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 570);
	specialist->level = 21;
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 900);
	specialist->level = 50;
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 900);
	// Taking the Gold, another object or another hero donates nothing.
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 0, *specialist, Obj::TREASURE_CHEST), 0);
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREE_OF_KNOWLEDGE), 0);
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *ordinary, Obj::TREASURE_CHEST), 0);
}

TEST_F(AlmsOfTheFaithfulTest, DeclinedGoldIsMeasuredAfterTaxCollector)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:caitlin"));
	ASSERT_NO_FATAL_FAILURE(select(specialist, "taxCollector"));
	const auto rewards = chest(1500, 1000);
	specialist->level = 1;
	EXPECT_EQ(newHorizonsRewardable::declinedChestGold(rewards, 1, *specialist, Obj::TREASURE_CHEST), 2250);
	EXPECT_EQ(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 450);
	specialist->level = 21;
	EXPECT_LT(newHorizonsRewardable::chestDonation(rewards, 1, *specialist, Obj::TREASURE_CHEST), 2250);
}

TEST_F(AlmsOfTheFaithfulTest, ChoosingTheExperienceAtAChestPaysTheDonation)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:caitlin"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.onAdvInterfaceReady(PLAYER);
	auto object = handler.createNewObject({12, 12, 0}, Obj::TREASURE_CHEST, MapObjectSubID(0));
	ASSERT_NE(object, nullptr);
	handler.newObject(object, PLAYER);
	auto * treasure = dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(object->id));
	ASSERT_NE(treasure, nullptr);
	treasure->configuration.info = chest(1500, 1000).info;
	specialist->level = 1;
	const auto before = gold();
	const auto experience = specialist->exp;
	treasure->blockingDialogAnswered(handler, specialist, 2);
	EXPECT_EQ(gold(), before + 300);
	EXPECT_GT(specialist->exp, experience);
}

TEST_F(AlmsOfTheFaithfulTest, RevisedCaitlinNoLongerGeneratesDailyGold)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:caitlin"));
	for(const auto & bonus : specialist->getExportedBonusList())
		EXPECT_FALSE(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL);
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Alms of the Faithful");
}

TEST_F(HistoricalAlmsTest, HistoricalCaptureKeepsDailyGoldAndNoDonation)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:caitlin"));
	int income = 0;
	for(const auto & bonus : specialist->getExportedBonusList())
		if(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL)
			income += bonus->val;
	EXPECT_EQ(income, 350);
	EXPECT_EQ(newHorizonsRewardable::chestDonation(chest(1500, 1000), 1, *specialist, Obj::TREASURE_CHEST), 0);
}

TEST(AlmsOfTheFaithfulAI, ChestChoiceValuesTheDonation)
{
	// Main heroes keep taking the Experience; the donation only adds to it.
	EXPECT_FALSE(NK2AI::prefersChestGold(true, false, 0));
	EXPECT_FALSE(NK2AI::prefersChestGold(true, false, 60));
	// Secondary heroes and gold-pressed kingdoms take the Gold unless half of it comes back.
	EXPECT_TRUE(NK2AI::prefersChestGold(false, false, 0));
	EXPECT_TRUE(NK2AI::prefersChestGold(true, true, 20));
	EXPECT_TRUE(NK2AI::prefersChestGold(false, false, 48));
	EXPECT_FALSE(NK2AI::prefersChestGold(false, false, 50));
	EXPECT_FALSE(NK2AI::prefersChestGold(true, true, 60));
}

TEST(AlmsOfTheFaithfulRules, ShippedRowIsStrict)
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto rules = newHorizonsHeroes::castleAdventureSpecialties(heroes);
	ASSERT_TRUE(rules && rules->almsDonationPercent);
	EXPECT_EQ(rules->almsDonationPercent->at(1), 20);
	EXPECT_EQ(rules->almsDonationPercent->at(21), 60);
	auto invalid = heroes;
	invalid["revisedSpecialties"]["almsOfTheFaithful"]["maximumDonationPercent"].Integer() = 101;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
}
