/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "AI/Nullkiller2/Helpers/DwellingGrowthValue.h"
#include "lib/GameConstants.h"
#include "lib/CCreatureHandler.h"
#include "lib/entities/faction/CTown.h"
#include "lib/entities/building/CBuilding.h"
#include "lib/mapObjects/NewHorizonsEstates.h"
#include "lib/serializer/CMemorySerializer.h"
#include "AI/Nullkiller2/Pathfinding/Actors.h"
#include "lib/CPlayerState.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER(1);

class NewHorizonsRecruitmentGrowthAITest : public NullkillerTest
{
protected:
	int percent = 50;
	CGHeroInstance * hero = nullptr;
	CGDwelling * dwelling = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	CreatureID swordsman() const { return CreatureID(CreatureID::decode("core:swordsman")); }
	CreatureID pikeman() const { return CreatureID(CreatureID::decode("core:pikeman")); }

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(percent));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(int capturedPercent = 50, FactionID faction = FactionID::CASTLE)
	{
		percent = capturedPercent;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER).playerActive(OTHER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
			.town({10, 10, 0}, faction, PLAYER)
			.dwelling({20, 20, 0}, MapObjectSubID(58), OTHER)
			.town({26, 26, 0}, FactionID::CASTLE, OTHER);
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PLAYER);
		town = expectAt<CGTownInstance>({10, 10, 0});
		dwelling = expectAt<CGDwelling>({20, 20, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		ASSERT_NE(dwelling, nullptr);
		hero->level = 10; // Both historical/fresh Knight capacities fit this small forecast.
		hero->clearSlots();
		ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman(), 1));
		dwelling->creatures = {{2, {swordsman()}}}; // Existing stock is not growth.
		for(auto & row : town->creatures)
			row = {0, {}};
		for(const auto building : {BuildingID::CASTLE, BuildingID::CITADEL,
			BuildingID::HORDE_1, BuildingID::HORDE_2, BuildingID::GRAIL, BuildingID::SPECIAL_3})
			town->removeBuilding(building);
		for(int resource = 0; resource < 7; ++resource)
			grantResources(PLAYER, GameResID(resource), 100000);
		gameState()->day = 1;
		gateway = makeGateway(PLAYER);
	}
	int forecast(const CGDwelling * source, CreatureID creature, uint8_t turn = 7)
	{
		const auto offers = gateway->nullkiller->armyManager->getArmyAvailableToBuy(
			hero, source, gateway->cc->getResourceAmount(), turn, hero);
		const auto result = std::ranges::find(offers, creature, &NK2AI::creInfo::creID);
		return result == offers.end() ? 0 : result->count;
	}
	NK2AI::BuildingInfo building(BuildingID id, CreatureID creature)
	{
		return NK2AI::BuildingInfo(town->getTown()->buildings.at(id).get(),
			creature.toCreature(), creature, town, gateway->nullkiller->armyManager);
	}
};
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, HalfOddExternalGrowthAgreesForStockForecastAndCaptureValue)
{
	prepare();
	ASSERT_EQ(gateway->cc->getCreatureBaseGrowth(swordsman()), 7);
	EXPECT_EQ(dwelling->normalWeeklyGrowth(0), 4);
	EXPECT_EQ(forecast(dwelling, swordsman()), 6); // Existing2 plus ceil(7/2).
	EXPECT_EQ(NK2AI::getDwellingArmyGrowth(gateway->cc.get(), dwelling, PLAYER),
		4u * swordsman().toCreature()->getAIValue());
	EXPECT_EQ(dwelling->creatures[0].first, 2u);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, CapturedHundredPercentRetainsFullExternalForecast)
{
	prepare(100);
	EXPECT_EQ(gateway->cc->getSettings().getInteger(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT), 100);
	EXPECT_EQ(dwelling->normalWeeklyGrowth(0), 7);
	EXPECT_EQ(forecast(dwelling, swordsman()), 9);
	EXPECT_EQ(NK2AI::getDwellingArmyGrowth(gateway->cc.get(), dwelling, PLAYER),
		7u * swordsman().toCreature()->getAIValue());
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, ZeroGrowthRetainsExistingStockAndHasNoFutureCaptureValue)
{
	prepare(0);
	EXPECT_EQ(dwelling->normalWeeklyGrowth(0), 0);
	EXPECT_EQ(forecast(dwelling, swordsman()), 2);
	EXPECT_EQ(NK2AI::getDwellingArmyGrowth(gateway->cc.get(), dwelling, PLAYER), 0u);
	EXPECT_EQ(dwelling->creatures[0].first, 2u);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, SameWeekDoesNotAddFutureGrowth)
{
	prepare();
	EXPECT_EQ(forecast(dwelling, swordsman(), 0), 2);
	EXPECT_EQ(dwelling->creatures[0].first, 2u);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, TownTotalsAreNotScaledAgainByForecastOrBuildingValue)
{
	prepare();
	const auto id = BuildingID::getDwellingFromLevel(0, 0);
	town->addBuilding(id);
	town->creatures[0] = {2, {pikeman()}};
	ASSERT_EQ(town->creatureBaseGrowth(pikeman()), 14);
	ASSERT_EQ(town->creatureGrowth(0), 7);
	EXPECT_EQ(forecast(town, pikeman()), 9);
	EXPECT_EQ(building(id, pikeman()).creatureGrowth, 7);
	const NK2AI::RewardEvaluator evaluator(gateway->nullkiller.get());
	EXPECT_EQ(evaluator.townArmyGrowth(town), 7u * pikeman().toCreature()->getAIValue());
	EXPECT_EQ(town->creatures[0].first, 2u);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, UnbuiltOddDwellingForecastScalesFinalSubtotalOnce)
{
	prepare();
	const auto id = BuildingID::getDwellingFromLevel(3, 0);
	town->removeBuilding(id);
	EXPECT_EQ(building(id, swordsman()).creatureGrowth, 4);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, LibraryOneOnOddTotalHasNoRoundedMarginalGrowth)
{
	prepare(50, FactionID::TOWER);
	const auto mage = CreatureID(CreatureID::decode("core:mage"));
	const auto & rows = town->getTown()->creatures;
	const auto mageRow = std::ranges::find_if(rows,
		[mage](const auto & row) { return vstd::contains(row, mage); });
	ASSERT_NE(mageRow, rows.end());
	const int rowIndex = static_cast<int>(std::distance(rows.begin(), mageRow));
	ASSERT_EQ(rowIndex, 4); // Curated Tower moves Mages to the fifth dwelling.
	const auto id = BuildingID::getDwellingFromLevel(rowIndex, 0);
	town->addBuilding(id);
	town->creatures[rowIndex] = {0, {mage}};
	ASSERT_FALSE(town->hasBuilt(BuildingID::CASTLE));
	ASSERT_FALSE(town->hasBuilt(BuildingID::CITADEL));
	ASSERT_FALSE(town->hasBuilt(BuildingID::GRAIL));
	ASSERT_FALSE(town->hasBuilt(BuildingID::SPECIAL_3));
	ASSERT_EQ(town->creatureBuildingGrowth(BuildingID::SPECIAL_3, mage), 1);
	ASSERT_EQ(town->getGrowthInfo(rowIndex).handicapPercentage, 100);
	ASSERT_EQ(town->getGrowthInfo(rowIndex).recruitmentGrowthPercentage, 50);
	const int base = town->getGrowthInfo(rowIndex).unscaledGrowth();
	if(base % 2 == 0)
		town->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CREATURE_GROWTH, BonusSource::ARTIFACT, 1, ArtifactID(4),
			BonusCustomSubtype::creatureLevel(rowIndex + 1)));
	const int rawBefore = town->getGrowthInfo(rowIndex).unscaledGrowth();
	ASSERT_EQ(rawBefore % 2, 1);
	const int before = town->creatureGrowth(rowIndex);
	ASSERT_EQ(before, (rawBefore + 1) / 2);
	EXPECT_EQ(building(BuildingID::SPECIAL_3, mage).creatureGrowth, 0);
	town->addBuilding(BuildingID::SPECIAL_3);
	EXPECT_EQ(town->getGrowthInfo(rowIndex).unscaledGrowth(), rawBefore + 1);
	EXPECT_EQ(town->creatureGrowth(rowIndex), before);
	EXPECT_EQ(building(BuildingID::SPECIAL_3, mage).creatureGrowth, 0);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, ConscriptionForecastUsesExactAggregateQuoteAndLeavesParentUnchanged)
{
 prepare(0);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 dwelling->creatures = {{20, {swordsman()}}};
 const auto budget = dwelling->getTotalRecruitmentCost(swordsman(), 10, hero);
 const auto before = gameState()->saveToMemory();
 const auto candidates = gateway->nullkiller->armyManager->getArmyAvailableToBuy(hero, dwelling, budget, 0, hero);
 ASSERT_EQ(candidates.size(), 1u);
 EXPECT_EQ(candidates.front().count, 10);
 EXPECT_EQ(gameState()->saveToMemory(), before);
 EXPECT_EQ(dwelling->getTotalRecruitmentCost(swordsman(), candidates.front().count, hero), budget);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, BareTownForecastCannotBorrowVisitingHerosConscription)
{
 prepare(0);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 town->setVisitingHero(hero); town->clearSlots(); town->creatures = {{20, {pikeman()}}};
 ASSERT_EQ(newHorizonsEstates::recruitmentBuyer(town->getUpperArmy()), nullptr);
 const auto budget = town->getTotalRecruitmentCost(pikeman(), 10, hero);
 const auto expected = town->getRecruitmentAmount(pikeman(), budget, 20);
 ASSERT_LT(expected, 10);
 const auto candidates = gateway->nullkiller->armyManager->getArmyAvailableToBuy(town->getUpperArmy(), town, budget, 0);
 ASSERT_EQ(candidates.size(), 1u);
 EXPECT_EQ(candidates.front().count, expected);
 town->setVisitingHero(nullptr); town->setGarrisonedHero(hero);
 const auto garrison = gateway->nullkiller->armyManager->getArmyAvailableToBuy(town->getUpperArmy(), town, budget, 0);
 ASSERT_EQ(garrison.size(), 1u);
 EXPECT_EQ(garrison.front().count, 10);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, BuildingInfoUsesSameResidentArchitectQuoteAsAuthority)
{
 prepare(0);
 const auto id = BuildingID(BuildingID::DWELL_LVL_1);
 const auto base = town->getBuildingCost(id);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.architect"});
 town->setVisitingHero(hero);
 const auto quoted = town->getBuildingCost(id);
 EXPECT_EQ(quoted, newHorizonsEstates::buildingCost(base, true));
 const auto forecast = building(id, pikeman());
 EXPECT_EQ(forecast.buildCost, quoted);
 EXPECT_EQ(forecast.buildCostWithPrerequisites, quoted);
 town->setVisitingHero(nullptr);
 EXPECT_EQ(building(id, pikeman()).buildCost, base);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, ExactDiscountedUnitBudgetAdmitsOrdinaryOwnedDwellingVisitAndValue)
{
 prepare(0);
 hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:recruitment")), 0, ChangeValueMode::ABSOLUTE);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 GameHandlerTestServer server(gameState(), PLAYER); CGameHandler handler(server, gameState());
 handler.setOwner(dwelling, PLAYER); dwelling->creatures = {{1, {swordsman()}}};
 const auto budget = dwelling->getTotalRecruitmentCost(swordsman(), 1, hero);
 gameState()->getPlayerState(PLAYER)->resources = budget;
 ASSERT_FALSE(budget.canAfford(dwelling->getTotalRecruitmentCost(swordsman(), 1)));
 ASSERT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
 const auto before = gameState()->saveToMemory();
 EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, dwelling));
 NK2AI::RewardEvaluator rewards(gateway->nullkiller.get());
 EXPECT_EQ(rewards.getArmyReward(dwelling, hero, hero, true), swordsman().toCreature()->getAIValue());
 EXPECT_EQ(rewards.getGoldCost(dwelling, hero, hero), budget.marketValue());
 EXPECT_EQ(gameState()->saveToMemory(), before);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, DwellingExchangeUsesReceivingBuyerQuoteForGateAndStoredReservation)
{
 prepare(0);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 dwelling->creatures = {{10, {swordsman()}}};
 const auto quote = dwelling->getTotalRecruitmentCost(swordsman(), 10, hero);
 gameState()->getPlayerState(PLAYER)->resources = quote;
 NK2AI::DwellingActor source(dwelling, 2, false, 1);
 ASSERT_FALSE(quote.canAfford(source.armyCost));
 EXPECT_EQ(source.getArmyCostForBuyer(hero), quote);
 const auto before = gameState()->saveToMemory();
 NK2AI::HeroActor carrier(hero, NK2AI::MAIN, 1, gateway->nullkiller.get());
 const auto result = static_cast<const NK2AI::ChainActor &>(carrier).tryExchangeNoLock(&source);
 ASSERT_TRUE(result.lockAcquired);
 ASSERT_NE(result.actor, nullptr);
 EXPECT_EQ(result.actor->armyCost, quote);
 EXPECT_EQ(source.armyCost, dwelling->getTotalRecruitmentCost(swordsman(), 10));
 EXPECT_EQ(gameState()->saveToMemory(), before);
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, TownMusterPricesActualUpperArmyWhileLeadershipUsesFinalHero)
{
 prepare(0);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:recruitment")), 1, ChangeValueMode::ABSOLUTE);
 town->addBuilding(BuildingID::DWELL_LVL_1); town->clearSlots(); town->creatures = {{0, {pikeman()}}};
 town->setVisitingHero(hero);
 const auto budget = town->getTotalRecruitmentCost(pikeman(), 2, hero);
 gameState()->getPlayerState(PLAYER)->resources = budget;
 ASSERT_EQ(town->getRecruitmentAmount(pikeman(), budget, 2), 1);
 EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), pikeman().toCreature()->getAIValue());
 town->setVisitingHero(nullptr); town->setGarrisonedHero(hero);
 EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), 2u * pikeman().toCreature()->getAIValue());
}

TEST_F(NewHorizonsRecruitmentGrowthAITest, DirectTownHeroPurchaseReservesItsSharedQuoteExactlyOnce)
{
 prepare(0);
 const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
 hero->setSecSkillLevel(estates, 1, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
 hero->setSecSkillLevel(estates, 2, ChangeValueMode::ABSOLUTE);
 hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.conscription"});
 town->clearSlots(); town->creatures = {{10, {swordsman()}}};
 const auto quote = town->getTotalRecruitmentCost(swordsman(), 10, hero);
 gameState()->getPlayerState(PLAYER)->resources = quote;
 NK2AI::TownGarrisonActor source(town, 2);
 NK2AI::HeroActor carrier(hero, NK2AI::MAIN, 1, gateway->nullkiller.get());
 const auto before = gameState()->saveToMemory();
 const auto result = static_cast<const NK2AI::ChainActor &>(carrier).tryExchangeNoLock(&source);
 ASSERT_TRUE(result.lockAcquired);
 ASSERT_NE(result.actor, nullptr);
 EXPECT_EQ(result.actor->armyCost, quote);
 EXPECT_TRUE(quote.canAfford(result.actor->armyCost));
 EXPECT_EQ(gameState()->saveToMemory(), before);
}
