/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleAdventureSpecialtyFixture.h"
#include "../../lib/mapObjects/EstatesRecruitmentQuote.h"

namespace
{
using namespace castleSpecialtyTest;

/// A local authored price, restored even when an assertion returns early.
class ScopedPrice
{
	ResourceSet & cost;
	ResourceSet original;
public:
	ScopedPrice(CreatureID id, const ResourceSet & price)
		: cost(const_cast<ResourceSet &>(id.toCreature()->getFullRecruitCost())), original(cost)
	{
		cost = price;
	}
	~ScopedPrice() { cost = original; }
};

ResourceSet price(int gold, int gems = 0)
{
	ResourceSet result;
	result[EGameResID::GOLD] = gold;
	result[EGameResID::GEMS] = gems;
	return result;
}

class WellConnectedSpecialtyTest : public CastleSpecialtyFixture
{
protected:
	CreatureID recruit = creature("core:pikeman");
	void prepareTown()
	{
		ASSERT_NO_FATAL_FAILURE(startWith("core:lordHaart"));
		town->setVisitingHero(specialist);
		town->creatures = {{100, {recruit}}};
	}
};

class HistoricalWellConnectedTest : public WellConnectedSpecialtyTest
{
protected:
	HistoricalWellConnectedTest()
	{
		historicalSpecialties = true;
	}
};
}

TEST(WellConnectedQuote, DiscountAddsToConscriptionWithinTheCombinedCap)
{
	using newHorizonsEstates::quoteRecruitment;
	const auto unit = price(100, 1);
	// Haart alone, 5% at level 1: Gold 1000 -> 950; ten Gems discount floor(0.5) = 0.
	const auto alone = quoteRecruitment(unit, 10, {}, false, {50, 25});
	EXPECT_EQ(alone.cost[EGameResID::GOLD], 950);
	EXPECT_EQ(alone.cost[EGameResID::GEMS], 10);
	EXPECT_EQ(alone.after, alone.before); // Haart never advances the Conscription ledger.
	// Conscription 10% plus Haart 15%: exactly the 25% cap.
	const auto both = quoteRecruitment(unit, 10, {}, true, {150, 25});
	EXPECT_EQ(both.cost[EGameResID::GOLD], 750);
	EXPECT_EQ(both.after[EGameResID::GOLD], 1000);
	EXPECT_EQ(quoteRecruitment(unit, 10, {}, true).after, both.after);
	// A lower combined cap limits the sum, never the Conscription share itself.
	EXPECT_EQ(quoteRecruitment(unit, 10, {}, true, {150, 20}).cost[EGameResID::GOLD], 800);
	EXPECT_EQ(quoteRecruitment(unit, 10, {}, true, {150, 5}).cost[EGameResID::GOLD], 900);
	// Affordability uses the same quote.
	EXPECT_EQ(newHorizonsEstates::affordableRecruitment(unit, both.cost, 100, {}, true, {150, 25}), 10);
	EXPECT_THROW(quoteRecruitment(unit, 1, {}, false, {1001, 25}), std::runtime_error);
	EXPECT_THROW(quoteRecruitment(unit, 1, {}, false, {50, 101}), std::runtime_error);
}

TEST_F(WellConnectedSpecialtyTest, HaartRecruitsCheaperByLevelAndPaysTheSameQuote)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	ScopedPrice scope(recruit, price(100, 1));
	specialist->level = 1;
	EXPECT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(specialist), 50);
	EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10, specialist)[EGameResID::GOLD], 950);
	specialist->level = 11;
	EXPECT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(specialist), 100);
	EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10, specialist)[EGameResID::GOLD], 900);
	specialist->level = 21;
	EXPECT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(specialist), 150);
	specialist->level = 40;
	EXPECT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(specialist), 150);
	const auto quote = town->getTotalRecruitmentCost(recruit, 10, specialist);
	EXPECT_EQ(quote[EGameResID::GOLD], 850);
	EXPECT_EQ(quote[EGameResID::GEMS], 9);
	// Other buyers and garrison recruitment pay the ordinary price.
	EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10, ordinary), price(1000, 10));
	EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10), price(1000, 10));
	// External dwellings use the same discount.
	const int swordsmanGold = dwelling->getTotalRecruitmentCost(creature("core:swordsman"), 1, ordinary)[EGameResID::GOLD];
	ASSERT_GT(swordsmanGold, 0);
	EXPECT_EQ(dwelling->getTotalRecruitmentCost(creature("core:swordsman"), 1, specialist)[EGameResID::GOLD],
		swordsmanGold - swordsmanGold * 150 / 1000);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, quote);
	EXPECT_EQ(town->getRecruitmentAmount(recruit, quote, 100, specialist), 10);
	const auto army = specialist->getStackCount(SlotID(0));
	ASSERT_TRUE(handler.recruitCreatures(town->id, specialist->id, recruit, 10, 0, PLAYER));
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	EXPECT_EQ(specialist->getStackCount(SlotID(0)), army + 10);
}

TEST_F(WellConnectedSpecialtyTest, ConscriptionAndWellConnectedStopAtTwentyFivePercent)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	ScopedPrice scope(recruit, price(100));
	ASSERT_NO_FATAL_FAILURE(select(specialist, "taxCollector", "conscription"));
	specialist->level = 21;
	const auto quote = town->getTotalRecruitmentCost(recruit, 10, specialist);
	EXPECT_EQ(quote[EGameResID::GOLD], 750);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, quote);
	ASSERT_TRUE(handler.recruitCreatures(town->id, specialist->id, recruit, 10, 0, PLAYER));
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	// The kingdom ledger advances by the undiscounted base price only.
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates.recruitmentBaseCost[EGameResID::GOLD], 1000);
}

TEST_F(WellConnectedSpecialtyTest, WarMachinesAndTheEstatesSpecialtyAreNotDiscounted)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	const auto ballista = creature("core:ballista");
	EXPECT_EQ(newHorizonsEstates::recruitmentSpecialtyDiscount(specialist, ballista).permille, 0);
	EXPECT_EQ(newHorizonsEstates::recruitmentSpecialtyDiscount(specialist, recruit).permille, 50);
	const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
	EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ESTATES)), 0);
	EXPECT_GT(specialist->getSecSkillLevel(estates), 0);
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Well Connected");
}

TEST_F(HistoricalWellConnectedTest, HistoricalCaptureKeepsTheEstatesSpecialtyAndFullPrices)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	ScopedPrice scope(recruit, price(100));
	EXPECT_EQ(newHorizonsHeroes::wellConnectedDiscountPermille(specialist), 0);
	EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10, specialist), price(1000));
	EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ESTATES)), 20);
	EXPECT_NE(specialist->getSpecialtyPresentation().name, "Well Connected");
}

TEST(WellConnectedSpecialtyRules, ShippedRowIsStrict)
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto rules = newHorizonsHeroes::castleAdventureSpecialties(heroes);
	ASSERT_TRUE(rules && rules->wellConnectedDiscountPermille);
	EXPECT_EQ(rules->wellConnectedDiscountPermille->at(1), 50);
	EXPECT_EQ(rules->wellConnectedDiscountPermille->at(21), 150);
	EXPECT_EQ(rules->wellConnectedCombinedCapPercent, 25);
	auto invalid = heroes;
	invalid["revisedSpecialties"]["wellConnected"]["combinedDiscountCapPercent"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
}
