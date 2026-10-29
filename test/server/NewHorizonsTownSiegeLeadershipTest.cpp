/*
 * NewHorizonsTownSiegeLeadershipTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CCreatureHandler.h"
#include <array>
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../server/CGameHandler.h"
#include <map>
#include <tuple>
#include <vector>

namespace
{
class SiegeTransferHandler : public CGameHandler
{
public:
	using CGameHandler::CGameHandler;
	bool moveStack(const StackLocation & source, const StackLocation & destination, TQuantity count) override
	{
		moves.emplace_back(source.slot, destination.slot, count);
		const bool accepted = source.slot != failSource && CGameHandler::moveStack(source, destination, count);
		rejected += !accepted;
		return accepted;
	}
	bool swapStacks(const StackLocation & source, const StackLocation & destination) override
	{
		swaps.emplace_back(source.slot, destination.slot);
		const bool accepted = CGameHandler::swapStacks(source, destination);
		rejected += !accepted;
		return accepted;
	}
	SlotID failSource;
	int rejected = 0;
	std::vector<std::tuple<SlotID, SlotID, TQuantity>> moves;
	std::vector<std::pair<SlotID, SlotID>> swaps;
};

class NewHorizonsTownSiegeLeadershipTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		capabilities["classProfiles"]["core:knight"]["base"] = JsonNode(baseLeadership);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			legacy ? JsonNode() : capabilities);
	}
	bool startScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0))
			.heroExperience(0).heroGarrison({})
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:edric")), PlayerColor(1));
		startWithMap(std::move(builder));
		town = findFirst<CGTownInstance>();
		hero = findHeroByOwner(PlayerColor(0));
		if(!town || !hero)
			return false;
		town->clearSlots();
		hero->clearSlots();
		town->setVisitingHero(hero);
		return true;
	}
	static CreatureID elf() { return CreatureID(CreatureID::decode("core:woodElf")); }
	static CreatureID pikeman() { return CreatureID(CreatureID::decode("core:pikeman")); }
	void fullHero(int elves = 4)
	{
		ASSERT_TRUE(hero->setCreature(SlotID(0), elf(), elves));
		for(int slot = 1; slot < GameConstants::ARMY_SIZE; ++slot)
			ASSERT_TRUE(hero->setCreature(SlotID(slot), pikeman(), 1));
	}
	std::map<CreatureID, int64_t> combinedCounts() const
	{
		std::map<CreatureID, int64_t> counts;
		const std::array<const CArmedInstance *, 2> armies{town, hero};
		for(const auto * army : armies)
			for(const auto & [slot, stack] : army->Slots())
				counts[stack->getCreatureID()] += stack->getCount();
		return counts;
	}
	void assemble(SiegeTransferHandler & handler)
	{
		const auto counts = combinedCounts();
		const auto resources = gameState()->getPlayerState(PlayerColor(0))->resources;
		const auto rules = hero->getCapabilityRules();
		town->mergeGarrisonOnSiege(handler);
		EXPECT_EQ(combinedCounts(), counts);
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resources);
		EXPECT_EQ(hero->getCapabilityRules(), rules);
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(const auto capacity = hero->getLeadershipSlotCapacity(stack->getCreatureID()))
			{
				EXPECT_LE(stack->getCount(), capacity->maximum);
			}
		}
	}
	bool legacy = false;
	int baseLeadership = 650;
	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, ThirteenElvesAtFullFourCapacityDoNotStarveThreeLowerSources)
{
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero());
	const auto capacity = hero->getLeadershipSlotCapacity(elf());
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->leadership, 650);
	ASSERT_EQ(capacity->requirement, 140);
	ASSERT_EQ(capacity->maximum, 4);
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	for(int slot = 1; slot < 4; ++slot)
		ASSERT_TRUE(town->setCreature(SlotID(slot), pikeman(), 1));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 13);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 4);
	ASSERT_EQ(handler.moves.size(), 3u);
	for(int index = 0; index < 3; ++index)
		EXPECT_EQ(std::get<0>(handler.moves[index]), SlotID(index + 1));
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, PartialMatchingRoomIsFilledAndExcessStaysInTown)
{
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero(3));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 12);
	ASSERT_EQ(handler.moves.size(), 1u);
	EXPECT_EQ(std::get<2>(handler.moves.front()), 1);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, FullMatchingSlotUsesEmptyAlternateWithoutOverfilling)
{
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero());
	hero->eraseStack(SlotID(6));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(hero->getCreature(SlotID(6))->getId(), elf());
	EXPECT_EQ(hero->getStackCount(SlotID(6)), 4);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 9);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, SeveralEmptySlotsReceiveLegalPortionsInStableOrder)
{
	ASSERT_TRUE(startScenario());
	ASSERT_TRUE(hero->setCreature(SlotID(0), elf(), 4));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_EQ(town->stacksCount(), 0);
	ASSERT_EQ(handler.moves.size(), 4u);
	for(int index = 0; index < 4; ++index)
	{
		EXPECT_EQ(std::get<1>(handler.moves[index]), SlotID(index + 1));
		EXPECT_EQ(std::get<2>(handler.moves[index]), index < 3 ? 4 : 1);
	}
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, WholeLegalStrongerSourceSwapsWithFirstWeakestHeroSlot)
{
	ASSERT_TRUE(startScenario());
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), pikeman(), 1));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 4));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	ASSERT_EQ(handler.swaps.size(), 1u);
	EXPECT_EQ(handler.swaps.front().second, SlotID(0));
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), elf());
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(town->getStackCount(SlotID(0)), 1);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, CallbackFailureIsNotRetriedAndDoesNotStarveLegalLowerSource)
{
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero(3));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 1));
	ASSERT_TRUE(town->setCreature(SlotID(1), pikeman(), 1));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	handler.failSource = SlotID(0);
	assemble(handler);
	EXPECT_EQ(handler.rejected, 1);
	ASSERT_EQ(handler.moves.size(), 2u);
	EXPECT_EQ(std::get<0>(handler.moves[0]), SlotID(0));
	EXPECT_EQ(std::get<0>(handler.moves[1]), SlotID(1));
	EXPECT_EQ(town->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, ZeroCapacitySourceIsSkippedWithoutAdmissionAttempt)
{
	baseLeadership = 100;
	ASSERT_TRUE(startScenario());
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman(), 1));
	ASSERT_EQ(hero->getLeadershipSlotCapacity(elf())->maximum, 0);
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_TRUE(handler.moves.empty());
	EXPECT_TRUE(handler.swaps.empty());
	EXPECT_EQ(town->getStackCount(SlotID(0)), 13);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, HistoricalUncappedSnapshotKeepsWholeFirstMatchingMerge)
{
	legacy = true;
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero());
	ASSERT_FALSE(hero->getLeadershipSlotCapacity(elf()));
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	EXPECT_EQ(handler.rejected, 0);
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 17);
	ASSERT_EQ(handler.moves.size(), 1u);
	EXPECT_EQ(std::get<1>(handler.moves.front()), SlotID(0));
	EXPECT_EQ(std::get<2>(handler.moves.front()), -1);
}

TEST_F(NewHorizonsTownSiegeLeadershipTest, ExistingDefeatPolicyClearsUntransferredTownRemainder)
{
	ASSERT_TRUE(startScenario());
	ASSERT_NO_FATAL_FAILURE(fullHero());
	ASSERT_TRUE(town->setCreature(SlotID(0), elf(), 13));
	GameHandlerTestServer server(gameState());
	SiegeTransferHandler handler(server, gameState());
	assemble(handler);
	ASSERT_EQ(town->getStackCount(SlotID(0)), 13);
	const auto * attacker = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(attacker, nullptr);
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	// Characterize the existing callback, without claiming full siege execution.
	town->battleFinished(handler, attacker, result);
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(town->getOwner(), PlayerColor(1));
}
