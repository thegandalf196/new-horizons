/*
 * NewHorizonsBankCreatureRewardTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/VisitQueries.h"

namespace
{
class TestBankObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using CRewardableObject::isGuarded;
	using CRewardableObject::wasVisitedBefore;
};
}

class NewHorizonsBankCreatureRewardTest : public HeroCommandFixture
{
protected:
	bool useLeadershipRules = true;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		for(auto & [key, profile] : rules["classProfiles"].Struct())
			profile["base"].Integer() = 1190;
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, useLeadershipRules ? rules : JsonNode());
	}

	std::shared_ptr<TestBankObject> makeBank()
	{
		auto bank = std::make_shared<TestBankObject>(gameState().get());
		bank->ID = Obj::CREATURE_BANK;
		bank->subID = MapObjectSubID(0);
		bank->id = ObjectInstanceID(gameState()->getMap().objects.size());
		bank->tempOwner = PlayerColor::UNFLAGGABLE;
		bank->pos = int3(6, 5, 0);
		bank->configuration.visitMode = Rewardable::VISIT_ONCE;
		gameState()->getMap().objects.push_back(bank);
		return bank;
	}

	void fillHero(const CGHeroInstance * hero, CreatureID creature)
	{
		auto * mutableHero = const_cast<CGHeroInstance *>(hero);
		mutableHero->clearSlots();
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			ASSERT_TRUE(mutableHero->setCreature(SlotID(i), creature, 1));
	}

	std::unique_ptr<CCreatureSet> reward(CreatureID creature, int count)
	{
		auto result = std::make_unique<CCreatureSet>();
		EXPECT_TRUE(result->setCreature(SlotID(0), creature, count));
		return result;
	}

	int total(const CArmedInstance & army, CreatureID creature)
	{
		int count = 0;
		for(const auto & [slot, stack] : army.Slots())
			if(stack->getCreatureID() == creature)
				count += stack->getCount();
		return count;
	}

	void visit(const TestBankObject & bank, const CGHeroInstance & hero)
	{
		gameHandler->queries->addQuery(std::make_shared<MapObjectVisitQuery>(gameHandler.get(), &bank, &hero));
		bank.onHeroVisit(*gameHandler, &hero);
	}
};

TEST_F(NewHorizonsBankCreatureRewardTest, FourWyvernsAutomaticallyUseTwoLegalSlots)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	const auto capacity = attackerSideHero->getLeadershipSlotCapacity(wyvern);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->requirement, 430);
	ASSERT_EQ(capacity->maximum, 2);
	attackerSideHero->clearSlots();
	auto bank = makeBank();
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 2);
	EXPECT_EQ(bank->stacksCount(), 0);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
	EXPECT_FALSE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(2)), wyvern.toCreature(), 3));
}

TEST_F(NewHorizonsBankCreatureRewardTest, MatchingSlotsFillBeforeNewSlotsAndKeepExactRemainder)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	const auto pikeman = creatureByName("core:pikeman");
	fillHero(attackerSideHero, pikeman);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), wyvern, 1));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), wyvern, 1));
	auto bank = makeBank();
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 2);
	EXPECT_EQ(total(*bank, wyvern), 2);
	EXPECT_TRUE(bank->hasPendingBankCreatureReward());
	EXPECT_FALSE(bank->isGuarded());
	bank->garrisonDialogClosed(*gameHandler, attackerSideHero);
	EXPECT_EQ(total(*bank, wyvern), 2);
	EXPECT_TRUE(bank->hasPendingBankCreatureReward());
	EXPECT_FALSE(gameHandler->moveStack(StackLocation(bank->id, SlotID(0)),
		StackLocation(attackerSideHero->id, SlotID(0)), 1));
	EXPECT_EQ(total(*bank, wyvern) + total(*attackerSideHero, wyvern), 6);
}

TEST_F(NewHorizonsBankCreatureRewardTest, MixedRewardIsGrantedOnceAndAnotherHeroCanCollectLeftovers)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	fillHero(attackerSideHero, creatureByName("core:pikeman"));
	defenderSideHero->clearSlots();
	auto bank = makeBank();
	Rewardable::VisitInfo info;
	info.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	info.reward.resources[GameResID::GOLD] = 100;
	const ArtifactID artifact(ArtifactID::decode("core:centaurAxe"));
	info.reward.grantedArtifacts.push_back(artifact);
	info.reward.creatures.emplace_back(wyvern, 4);
	bank->configuration.info.push_back(info);
	const auto gold = gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD];
	visit(*bank, *attackerSideHero);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD], gold + 100);
	EXPECT_EQ(total(*bank, wyvern), 4);
	EXPECT_TRUE(attackerSideHero->hasArt(artifact));
	const auto artifactCount = attackerSideHero->artifactsWorn.size() + attackerSideHero->artifactsInBackpack.size();
	EXPECT_TRUE(bank->wasVisited(attackerSideHero));
	const auto components = bank->getPopupComponents(attackerSideHero);
	ASSERT_EQ(components.size(), 1);
	EXPECT_EQ(components[0].type, ComponentType::CREATURE);
	EXPECT_EQ(components[0].subType, ComponentSubType(wyvern));
	EXPECT_EQ(components[0].value, 4);
	MetaString exactTroops;
	exactTroops.appendNumber(4);
	exactTroops.appendRawString(" ");
	exactTroops.appendName(wyvern, 4);
	const auto exactText = exactTroops.toString(LIBRARY->staticTexts());
	EXPECT_NE(bank->getHoverText(attackerSideHero).toString(LIBRARY->staticTexts()).find(exactText), std::string::npos);
	EXPECT_NE(bank->getPopupText(attackerSideHero).toString(LIBRARY->staticTexts()).find(exactText), std::string::npos);
	EXPECT_TRUE(bank->getPopupComponents(PlayerColor(1)).empty());
	const auto top = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(top, nullptr);
	ASSERT_TRUE(gameHandler->queryReply(top->queryID, 0, PlayerColor(0)));
	EXPECT_EQ(total(*bank, wyvern), 4);

	// The existing exchange remains available despite the cleared reward state.
	visit(*bank, *defenderSideHero);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD], gold + 100);
	EXPECT_EQ(total(*bank, wyvern) + total(*defenderSideHero, wyvern), 4);
	// Existing joining planner moves one legal slot; finish via validated exchange.
	if(bank->stacksCount() != 0)
	{
		EXPECT_FALSE(gameHandler->arrangeStacks(bank->id, defenderSideHero->id, 3, SlotID(0), SlotID(1), 3, PlayerColor(1)));
		ASSERT_TRUE(gameHandler->arrangeStacks(bank->id, defenderSideHero->id, 3, SlotID(0), SlotID(1), 2, PlayerColor(1)));
	}
	bank->garrisonDialogClosed(*gameHandler, defenderSideHero);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
	EXPECT_EQ(total(*defenderSideHero, wyvern), 4);
	EXPECT_FALSE(defenderSideHero->hasArt(artifact));
	EXPECT_EQ(attackerSideHero->artifactsWorn.size() + attackerSideHero->artifactsInBackpack.size(), artifactCount);
	EXPECT_FALSE(bank->isGuarded());
}

TEST_F(NewHorizonsBankCreatureRewardTest, PendingLootSurvivesRefillDateAndCurrentSerialization)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	fillHero(attackerSideHero, creatureByName("core:pikeman"));
	auto bank = makeBank();
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	bank->configuration.resetParameters.days = 1;
	bank->configuration.resetParameters.rewards = true;
	bank->configuration.resetParameters.visitors = true;
	gameState()->day = 8;
	bank->newTurn(*gameHandler, *gameHandler->randomizer);
	EXPECT_EQ(total(*bank, wyvern), 4);
	EXPECT_TRUE(bank->hasPendingBankCreatureReward());

	CMemorySerializer memory;
	bank->serialize(memory.oser);
	uint32_t sentinel = 0x12345678;
	memory.oser & sentinel;
	TestBankObject loaded(gameState().get());
	memory.iser.cb = gameState().get();
	loaded.serialize(memory.iser);
	uint32_t after = 0;
	memory.iser & after;
	EXPECT_EQ(after, sentinel);
	EXPECT_TRUE(loaded.hasPendingBankCreatureReward());
	EXPECT_FALSE(loaded.isGuarded());
	EXPECT_EQ(total(loaded, wyvern), 4);
	EXPECT_EQ(loaded.configuration.resetParameters.days, 1);

	// Collection releases the refill lock; ordinary visitor renewal resumes.
	gameHandler->setObjPropertyValue(bank->id, ObjProperty::REWARD_CLEARED, true);
	defenderSideHero->clearSlots();
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(bank->id, SlotID(0)), StackLocation(defenderSideHero->id, SlotID(0)), 2));
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(bank->id, SlotID(0)), StackLocation(defenderSideHero->id, SlotID(1)), 2));
	bank->garrisonDialogClosed(*gameHandler, defenderSideHero);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
	bank->configuration.resetParameters.rewards = false;
	bank->newTurn(*gameHandler, *gameHandler->randomizer);
	EXPECT_FALSE(bank->wasVisitedBefore(attackerSideHero));
}

TEST_F(NewHorizonsBankCreatureRewardTest, HistoricalClearedArmyNeverAcquiresLootIdentity)
{
	startGame();
	auto bank = makeBank();
	ASSERT_TRUE(bank->setCreature(SlotID(0), creatureByName("core:wyvern"), 4));
	gameHandler->setObjPropertyValue(bank->id, ObjProperty::REWARD_CLEARED, true);
	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = ESerializationVersion::NEW_HORIZONS_DREAD_CAVALRY;
	bank->serialize(memory.oser);
	TestBankObject loaded(gameState().get());
	loaded.ID = Obj::CREATURE_BANK;
	loaded.setPropertyDer(ObjProperty::BANK_PENDING_CREATURE_REWARD, NumericID(1));
	memory.iser.cb = gameState().get();
	loaded.serialize(memory.iser);
	EXPECT_FALSE(loaded.hasPendingBankCreatureReward());
	EXPECT_TRUE(loaded.isGuarded());
}

TEST_F(NewHorizonsBankCreatureRewardTest, PendingLootRejectsUnsupportedWriterBeforeObjectPrefix)
{
	startGame();
	auto bank = makeBank();
	gameHandler->setObjPropertyValue(bank->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, true);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_DREAD_CAVALRY;
	EXPECT_THROW(bank->serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}

TEST_F(NewHorizonsBankCreatureRewardTest, UnclearedBankAndNonbankRewardsKeepOriginalAdmissionRules)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	attackerSideHero->clearSlots();
	auto bank = makeBank();
	ASSERT_TRUE(bank->setCreature(SlotID(0), wyvern, 1));
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
	EXPECT_EQ(total(*bank, wyvern), 1);
	EXPECT_EQ(total(*attackerSideHero, wyvern), 0);

	bank->clearSlots();
	bank->ID = Obj::PANDORAS_BOX;
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	EXPECT_EQ(bank->stacksCount(), 0);
	EXPECT_EQ(attackerSideHero->stacksCount(), 0);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
}

TEST_F(NewHorizonsBankCreatureRewardTest, NumericSplitRequiresActiveExchangeAndReceivingHeroOwnership)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	attackerSideHero->clearSlots();
	auto bank = makeBank();
	ASSERT_TRUE(bank->setCreature(SlotID(0), wyvern, 4));
	gameHandler->setObjPropertyValue(bank->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, true);
	EXPECT_FALSE(gameHandler->arrangeStacks(bank->id, attackerSideHero->id, 3, SlotID(0), SlotID(0), 1, PlayerColor(0)));
	gameHandler->showGarrisonDialog(bank->id, attackerSideHero->id, true, MetaString());
	EXPECT_FALSE(gameHandler->arrangeStacks(bank->id, attackerSideHero->id, 3, SlotID(0), SlotID(0), 1, PlayerColor(1)));
	EXPECT_FALSE(gameHandler->arrangeStacks(bank->id, attackerSideHero->id, 3, SlotID(0), SlotID(0), 3, PlayerColor(0)));
	EXPECT_EQ(total(*bank, wyvern), 4);
	EXPECT_EQ(total(*attackerSideHero, wyvern), 0);
	ASSERT_TRUE(gameHandler->arrangeStacks(bank->id, attackerSideHero->id, 3, SlotID(0), SlotID(0), 1, PlayerColor(0)));
	EXPECT_EQ(total(*bank, wyvern), 3);
	EXPECT_EQ(total(*attackerSideHero, wyvern), 1);
	gameHandler->setObjPropertyValue(bank->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, false);
	EXPECT_FALSE(gameHandler->arrangeStacks(bank->id, attackerSideHero->id, 3, SlotID(0), SlotID(1), 1, PlayerColor(0)));
	EXPECT_EQ(total(*bank, wyvern), 3);
}

TEST_F(NewHorizonsBankCreatureRewardTest, DepositedTroopsRemainSharedLootWithoutDiscardOnClose)
{
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	fillHero(attackerSideHero, creatureByName("core:pikeman"));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), wyvern, 2));
	auto bank = makeBank();
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	ASSERT_TRUE(gameHandler->arrangeStacks(attackerSideHero->id, bank->id, 3, SlotID(0), SlotID(0), 5, PlayerColor(0)));
	bank->garrisonDialogClosed(*gameHandler, attackerSideHero);
	EXPECT_EQ(total(*bank, wyvern), 5);
	EXPECT_EQ(total(*attackerSideHero, wyvern), 1);
	EXPECT_TRUE(bank->hasPendingBankCreatureReward());
	EXPECT_FALSE(bank->isGuarded());
}

TEST_F(NewHorizonsBankCreatureRewardTest, LegacyBankWithoutPerSlotLeadershipRetainsOldLifecycle)
{
	useLeadershipRules = false;
	startGame();
	const auto wyvern = creatureByName("core:wyvern");
	EXPECT_FALSE(attackerSideHero->getLeadershipSlotCapacity(wyvern));
	attackerSideHero->clearSlots();
	int slot = 0;
	for(const auto * name : {"core:pikeman", "core:archer", "core:griffin", "core:swordsman", "core:monk", "core:cavalier", "core:angel"})
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(slot++), creatureByName(name), 1));
	auto bank = makeBank();
	const auto troops = reward(wyvern, 4);
	gameHandler->giveCreatures(bank.get(), attackerSideHero, *troops, false);
	EXPECT_FALSE(bank->hasPendingBankCreatureReward());
	EXPECT_EQ(total(*bank, wyvern), 4);
	bank->garrisonDialogClosed(*gameHandler, attackerSideHero);
	EXPECT_EQ(bank->stacksCount(), 0);
}
