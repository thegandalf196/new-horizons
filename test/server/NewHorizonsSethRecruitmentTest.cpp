/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsEstatesFixture.h"
#include "../../lib/mapObjects/TownRecruitmentOffers.h"
#include "../../lib/entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsLearningSpecialties.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include <array>
#include <tuple>

namespace
{
using namespace estatesTest;
constexpr size_t TOMB = 2;

class NewHorizonsSethRecruitmentTest : public Fixture
{
protected:
	void prepare(bool upgraded = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({7, 7, 0}, FactionID::NECROPOLIS, PLAYER).townGarrison({})
			.town({7, 30, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
			.town({30, 30, 0}, FactionID::NECROPOLIS, PlayerColor(1)).townGarrison({})
			.hero({12, 10, 0}, HeroTypeID(HeroTypeID::decode("core:nimbus")), PLAYER)
			.heroGarrison({{creature("core:wight"), 1}})
			.hero({12, 14, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
			.heroGarrison({{creature("core:pikeman"), 1}})
			.dwelling({24, 20, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));
		first = findHeroAt({12, 10, 0});
		second = findHeroAt({12, 14, 0});
		town = expectAt<CGTownInstance>({7, 7, 0});
		dwelling = expectAt<CGDwelling>({24, 20, 0});
		ASSERT_TRUE(first && second && town && dwelling);
		ASSERT_TRUE(newHorizonsHeroes::hasAncientOaths(first));
		town->addBuilding(BuildingID::DWELL_LVL_3);
		if(upgraded)
			town->addBuilding(BuildingID::DWELL_LVL_3_UP);
		town->creatures.resize(7);
		town->creatures[TOMB] = {10, {creature("core:wight")}};
		if(upgraded)
			town->creatures[TOMB].second.push_back(creature("core:wraith"));
		town->setVisitingHero(first);
	}
	std::vector<CreatureID> choices(const CGDwelling & source, size_t row = TOMB) const
	{
		const auto result = townRecruitmentOffers::query(source, row, PLAYER);
		return result ? result->choices : std::vector<CreatureID>{};
	}
	int count(const CArmedInstance & army, CreatureID id) const
	{
		int result = 0;
		for(const auto & [slot, stack] : army.Slots())
			if(stack->getCreatureID() == id) result += stack->getCount();
		return result;
	}
	std::vector<std::tuple<SlotID, CreatureID, int64_t>> armySnapshot(const CArmedInstance & army) const
	{
		std::vector<std::tuple<SlotID, CreatureID, int64_t>> result;
		for(const auto & [slot, stack] : army.Slots())
			result.emplace_back(slot, stack->getCreatureID(), stack->getCount());
		return result;
	}
	void expectRejected(CGameHandler & handler, CreatureID id, ObjectInstanceID target, int row = TOMB, int amount = 1)
	{
		const auto resources = gameState()->getPlayerState(PLAYER)->resources;
		const auto stock = town->creatures;
		const auto armies = std::array{armySnapshot(*first), armySnapshot(*second), armySnapshot(*town)};
		EXPECT_FALSE(handler.recruitCreatures(town->id, target, id, amount, row, PLAYER));
		EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resources);
		EXPECT_EQ(town->creatures, stock);
		EXPECT_EQ((std::array{armySnapshot(*first), armySnapshot(*second), armySnapshot(*town)}), armies);
	}
};
}

TEST_F(NewHorizonsSethRecruitmentTest, BaseUpgradeAndUpgradedOnlyBuildingsKeepOneUnchangedRow)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto wight = creature("core:wight");
	const auto wraith = creature("core:wraith"), royal = creature("new-horizons:royalMummy");
	const auto raw = town->creatures;
	EXPECT_EQ(choices(*town), (std::vector<CreatureID>{wight}));
	EXPECT_EQ(town->creatures, raw);
	EXPECT_EQ(townRecruitmentOffers::query(*town, TOMB, PLAYER)->available, 10u);
	town->addBuilding(BuildingID::DWELL_LVL_3_UP);
	town->creatures[TOMB].second.push_back(wraith);
	EXPECT_EQ(choices(*town), (std::vector<CreatureID>{wight, wraith, royal}));
	town->removeBuilding(BuildingID::DWELL_LVL_3);
	EXPECT_EQ(choices(*town), (std::vector<CreatureID>{wight, wraith, royal}));
	town->creatures[TOMB].first = 0;
	EXPECT_EQ(townRecruitmentOffers::query(*town, TOMB, PLAYER)->available, 0u);
	EXPECT_EQ(choices(*town).size(), 3u);
	EXPECT_FALSE(townRecruitmentOffers::query(*town, town->creatures.size(), PLAYER));
}

TEST_F(NewHorizonsSethRecruitmentTest, BaseMummyAndRoyalBeforeUpgradeRejectWithoutChangingExistingMummies)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mummy = creature("core:mummy"), royal = creature("new-horizons:royalMummy");
	ASSERT_TRUE(first->setCreature(SlotID(1), mummy, 4));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(royal, 100));
	expectRejected(handler, mummy, first->id);
	expectRejected(handler, royal, first->id);
	// Stale or custom Wraith stock cannot substitute for the actual building.
	town->creatures[TOMB].second.push_back(creature("core:wraith"));
	expectRejected(handler, royal, first->id);
	town->addBuilding(BuildingID::DWELL_LVL_3_UP);
	expectRejected(handler, mummy, first->id);
	EXPECT_EQ(count(*first, mummy), 4);
}

TEST_F(NewHorizonsSethRecruitmentTest, OnlyOwnedResidentCapturedSethUnlocksCanonicalTombRow)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto wight = creature("core:wight"), wraith = creature("core:wraith");
	const auto ordinary = std::vector<CreatureID>{wight, wraith};
	town->creatures[3] = {11, {wight, wight, wraith}};
	EXPECT_EQ(choices(*town, 3), town->creatures[3].second);
	dwelling->creatures = town->creatures;
	EXPECT_EQ(choices(*dwelling), ordinary);
	EXPECT_EQ(townRecruitmentOffers::query(*town, TOMB, PlayerColor(1))->choices, ordinary);
	town->setVisitingHero(nullptr);
	EXPECT_EQ(choices(*town), ordinary);
	town->setVisitingHero(second);
	EXPECT_EQ(choices(*town), ordinary);
	town->setVisitingHero(nullptr);
	town->setGarrisonedHero(first);
	EXPECT_EQ(choices(*town).size(), 3u);
	auto captured = first->getPrimaryGrowthRules();
	captured["additionalSpecialties"].Struct().erase("ancientOathsPolicyVersion");
	first->replaceCapturedPrimaryGrowthRules(captured);
	EXPECT_EQ(choices(*town), ordinary);
	auto * castle = expectAt<CGTownInstance>({7, 30, 0});
	ASSERT_NE(castle, nullptr);
	captured["additionalSpecialties"]["ancientOathsPolicyVersion"].Integer() = 1;
	first->replaceCapturedPrimaryGrowthRules(captured);
	town->setGarrisonedHero(nullptr);
	castle->creatures.resize(7);
	castle->creatures[TOMB] = {10, ordinary};
	castle->addBuilding(BuildingID::DWELL_LVL_3_UP);
	castle->setVisitingHero(first);
	EXPECT_EQ(choices(*castle), ordinary);
}

TEST_F(NewHorizonsSethRecruitmentTest, SethInAnotherTownCannotAuthorizeRoyalRecruitment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto royal = creature("new-horizons:royalMummy");
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(royal, 100));
	auto * otherTown = expectAt<CGTownInstance>({30, 30, 0});
	ASSERT_NE(otherTown, nullptr);
	otherTown->tempOwner = PLAYER;
	otherTown->addBuilding(BuildingID::DWELL_LVL_3_UP);
	town->setVisitingHero(nullptr);
	otherTown->setVisitingHero(first);
	expectRejected(handler, royal, town->id);
	EXPECT_EQ(choices(*town), town->creatures[TOMB].second);
}

TEST_F(NewHorizonsSethRecruitmentTest, MixedPurchasesDebitActualSpeciesPriceAndOneSharedStock)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto wight = creature("core:wight"), royal = creature("new-horizons:royalMummy");
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto payment = town->getTotalRecruitmentCost(wight, 3, first)
		+ town->getTotalRecruitmentCost(royal, 2, first);
	setResources(handler, payment);
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, wight, 3, TOMB, PLAYER));
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, royal, 2, TOMB, PLAYER));
	EXPECT_EQ(town->creatures[TOMB].first, 5u);
	EXPECT_EQ(town->creatures[TOMB].second, (std::vector<CreatureID>{wight, creature("core:wraith")}));
	EXPECT_EQ(count(*first, wight), 4);
	EXPECT_EQ(count(*first, royal), 2);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	expectRejected(handler, royal, first->id);
}

TEST_F(NewHorizonsSethRecruitmentTest, ResidenceUnlockIsSeparateFromRecipientDiscounts)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
	const auto royal = creature("new-horizons:royalMummy");
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto rawPrice = town->getTotalRecruitmentCost(royal, 2);
	const auto discounted = town->getTotalRecruitmentCost(royal, 2, first);
	ASSERT_NE(rawPrice, discounted);
	setResources(handler, discounted);
	expectRejected(handler, royal, town->id, TOMB, 2);
	setResources(handler, rawPrice);
	ASSERT_TRUE(handler.recruitCreatures(town->id, town->id, royal, 2, TOMB, PLAYER));
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	EXPECT_EQ(count(*town, royal), 2);
	town->setVisitingHero(nullptr);
	town->setGarrisonedHero(first);
	town->setVisitingHero(second);
	setResources(handler, town->getTotalRecruitmentCost(royal, 1, second));
	ASSERT_TRUE(handler.recruitCreatures(town->id, second->id, royal, 1, TOMB, PLAYER));
	EXPECT_EQ(count(*second, royal), 1);
	EXPECT_EQ(town->creatures[TOMB].first, 7u);
}

TEST_F(NewHorizonsSethRecruitmentTest, StaleDepartureCaptureBuildingAndWrongRowRejectAtomically)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto royal = creature("new-horizons:royalMummy");
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(royal, 100));
	town->creatures[3] = town->creatures[TOMB];
	expectRejected(handler, royal, first->id, 3);
	town->setVisitingHero(nullptr);
	expectRejected(handler, royal, town->id);
	town->setVisitingHero(first);
	town->tempOwner = PlayerColor(1);
	expectRejected(handler, royal, first->id);
	town->tempOwner = PLAYER;
	town->removeBuilding(BuildingID::DWELL_LVL_3_UP);
	expectRejected(handler, royal, first->id);
	town->removeBuilding(BuildingID::DWELL_LVL_3);
	expectRejected(handler, royal, first->id);
	town->addBuilding(BuildingID::DWELL_LVL_3_UP);
	town->creatures[TOMB].first = 0;
	expectRejected(handler, royal, first->id);
}

TEST_F(NewHorizonsSethRecruitmentTest, FullArmyRejectionPreservesStockAndPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto royal = creature("new-horizons:royalMummy");
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(first->setCreature(SlotID(slot), creature("core:pikeman"), 1));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(royal, 1));
	expectRejected(handler, royal, first->id);
}

TEST_F(NewHorizonsSethRecruitmentTest, UpgradeConstructionPreservesSpentStockAndGrowthBasis)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	town->creatures[TOMB].first = 5;
	const int before = town->getGrowthInfo(TOMB).totalGrowth();
	ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::DWELL_LVL_3_UP, true));
	EXPECT_EQ(town->creatures[TOMB].first, 5u);
	EXPECT_EQ(town->creatures[TOMB].second,
		(std::vector<CreatureID>{creature("core:wight"), creature("core:wraith")}));
	EXPECT_EQ(town->getGrowthInfo(TOMB).totalGrowth(), before);
	const auto raw = town->creatures;
	EXPECT_EQ(choices(*town).size(), 3u);
	EXPECT_EQ(town->creatures, raw);
}

TEST_F(NewHorizonsSethRecruitmentTest, TownUpgradeNeedsCurrentLocalSethAndUpgradedTombButNoRecruitStock)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto mummy = creature("core:mummy"), royal = creature("new-horizons:royalMummy");
	ASSERT_TRUE(first->setCreature(SlotID(0), mummy, 3));
	town->creatures[TOMB] = {0, {creature("core:wraith")}};
	UpgradeInfo info(mummy);
	gameState()->fillUpgradeInfo(first, SlotID(0), info);
	ASSERT_TRUE(vstd::contains(info.getAvailableUpgrades(), royal));
	town->creatures[3] = town->creatures[TOMB];
	town->creatures[TOMB].second = {creature("core:wight")};
	UpgradeInfo wrongRow(mummy);
	gameState()->fillUpgradeInfo(first, SlotID(0), wrongRow);
	EXPECT_FALSE(vstd::contains(wrongRow.getAvailableUpgrades(), royal));
	town->creatures[TOMB].second = {creature("core:wraith")};
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, info.getUpgradeCostsFor(royal) * 3);
	const auto resources = gameState()->getPlayerState(PLAYER)->resources;
	town->setVisitingHero(nullptr);
	EXPECT_FALSE(handler.upgradeCreature(first->id, SlotID(0), royal));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resources);
	EXPECT_EQ(first->getCreature(SlotID(0))->getId(), mummy);
	town->setVisitingHero(first);
	ASSERT_TRUE(handler.upgradeCreature(first->id, SlotID(0), royal));
	EXPECT_EQ(first->getCreature(SlotID(0))->getId(), royal);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	EXPECT_EQ(town->creatures[TOMB].first, 0u);
}

TEST_F(NewHorizonsSethRecruitmentTest, CurrentWorldRoundtripPreservesSpentPoolAndCapturedEligibility)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto royal = creature("new-horizons:royalMummy");
	ASSERT_TRUE(first->setCreature(SlotID(1), creature("core:mummy"), 4));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(royal, 3));
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, royal, 2, TOMB, PLAYER));
	auto loaded = std::make_shared<CGameState>();
	loaded->preInit(LIBRARY);
	loaded->loadFromMemory(gameState()->saveToMemory());
	const auto * restoredTown = dynamic_cast<const CGTownInstance *>(loaded->getObjInstance(town->id));
	const auto * restoredHero = loaded->getHero(first->id);
	ASSERT_TRUE(restoredTown && restoredHero);
	EXPECT_EQ(restoredTown->creatures, town->creatures);
	EXPECT_EQ(townRecruitmentOffers::query(*restoredTown, TOMB, PLAYER)->available, 8u);
	EXPECT_EQ(count(*restoredHero, royal), 2);
	EXPECT_EQ(count(*restoredHero, creature("core:mummy")), 4);
	EXPECT_TRUE(newHorizonsHeroes::hasAncientOaths(restoredHero));
	GameHandlerTestServer restoredServer(loaded, PLAYER);
	CGameHandler restoredHandler(restoredServer, loaded);
	ASSERT_TRUE(restoredHandler.recruitCreatures(restoredTown->id, restoredHero->id, royal, 1, TOMB, PLAYER));
	EXPECT_EQ(restoredTown->creatures[TOMB].first, 7u);
	EXPECT_TRUE(loaded->getPlayerState(PLAYER)->resources.empty());
}

TEST_F(NewHorizonsSethRecruitmentTest, AncientOathsReplacesPersonalExperienceButKeepsLearning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->level = 10;
	const auto learning = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	first->setSecSkillLevel(learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(first->calculateXp(1000), 1100);
	EXPECT_FALSE(newHorizonsHeroes::learningSpecialtyRules(first));
	EXPECT_EQ(first->getSpecialtyPresentation().name, "Ancient Oaths");
	auto capture = first->getPrimaryGrowthRules();
	capture["additionalSpecialties"].Struct().erase("ancientOathsPolicyVersion");
	first->replaceCapturedPrimaryGrowthRules(capture);
	EXPECT_EQ(first->calculateXp(1000), 1150);
	EXPECT_TRUE(newHorizonsHeroes::learningSpecialtyRules(first));
	EXPECT_EQ(first->getSpecialtyPresentation().name, "Grave Archivist");
}
