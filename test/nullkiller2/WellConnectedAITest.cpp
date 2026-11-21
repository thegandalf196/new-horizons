/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/GameConstants.h"
#include "lib/CCreatureHandler.h"
#include "lib/entities/hero/NewHorizonsCastleAdventureSpecialties.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/serializer/CMemorySerializer.h"

namespace
{
const PlayerColor PLAYER(0);

class WellConnectedAITest : public NullkillerTest
{
protected:
	CGHeroInstance * haart = nullptr;
	CGHeroInstance * other = nullptr;
	CGDwelling * dwelling = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	CreatureID swordsman() const { return CreatureID(CreatureID::decode("core:swordsman")); }

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:lordHaart")), PLAYER)
			.hero({9, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
			.dwelling({20, 20, 0}, MapObjectSubID(58), PLAYER);
		startWithMap(std::move(builder));
		haart = findHeroAt({5, 5, 0});
		other = findHeroAt({9, 5, 0});
		dwelling = expectAt<CGDwelling>({20, 20, 0});
		ASSERT_TRUE(haart && other && dwelling);
		for(auto * hero : {haart, other})
		{
			hero->level = 21;
			hero->clearSlots();
			ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 1));
		}
		dwelling->creatures = {{20, {swordsman()}}};
		gateway = makeGateway(PLAYER);
	}
	int affordable(const CGHeroInstance * buyer, const TResources & budget)
	{
		const auto offers = gateway->nullkiller->armyManager->getArmyAvailableToBuy(buyer, dwelling, budget, 0, buyer);
		const auto found = std::ranges::find(offers, swordsman(), &NK2AI::creInfo::creID);
		return found == offers.end() ? 0 : found->count;
	}
};
}

TEST_F(WellConnectedAITest, RecruitmentForecastUsesHaartsDiscountedQuote)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(haart), 150);
	const auto discounted = dwelling->getTotalRecruitmentCost(swordsman(), 10, haart);
	const auto ordinary = dwelling->getTotalRecruitmentCost(swordsman(), 10, other);
	ASSERT_LT(discounted[EGameResID::GOLD], ordinary[EGameResID::GOLD]);
	const auto before = gameState()->saveToMemory();
	// The same budget buys the full ten for Haart, fewer for an ordinary hero.
	EXPECT_EQ(affordable(haart, discounted), 10);
	EXPECT_LT(affordable(other, discounted), 10);
	EXPECT_EQ(dwelling->getTotalRecruitmentCost(swordsman(), affordable(haart, discounted), haart), discounted);
	EXPECT_EQ(gameState()->saveToMemory(), before); // Forecasts never mutate the world.
}
