/*
 * NewHorizonsSkeletonTransformerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/SkeletonTransformer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

#include <array>
#include <limits>

namespace
{
using namespace newHorizonsSkeletonTransformer;

CreatureID pikeman()
{
	return CreatureID(CreatureID::decode("core:pikeman"));
}

CreatureID halberdier()
{
	return CreatureID(CreatureID::decode("core:halberdier"));
}

/// Supply exact HP, including deliberately extreme values, through the same
/// virtual adventure-stack accessor used by the production planner.
class TransformerHealthStack final : public CStackInstance
{
public:
	TransformerHealthStack(CreatureID creature, int count, uint32_t health)
		: CStackInstance(nullptr, creature, count), health(health)
	{}

	uint32_t getMaxHealth() const override { return health; }

private:
	uint32_t health;
};

class TransformerRecordingServer final : public GameHandlerTestServer
{
public:
	explicit TransformerRecordingServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PlayerColor(0))
	{}

	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<CGarrisonOperationPack *>(&pack))
			++armyUpdates;
		GameHandlerTestServer::applyPack(pack);
	}

	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		if(const auto * applied = dynamic_cast<const PackageApplied *>(&pack))
			requestResult = applied->result;
		GameHandlerTestServer::sendPack(pack, connection);
	}

	size_t armyUpdates = 0;
	std::optional<bool> requestResult;
};

class NewHorizonsSkeletonTransformerTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	virtual bool enablesTransformer() const { return true; }

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		rules = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		if(enablesTransformer())
			rules["skeletonTransformer"]["aggregateHealthPercent"].Integer() = 50;
		else
		{
			// A new-map setting override merges into installed defaults. Omission
			// would inherit today's marker; explicit null removes it from the
			// captured world. Loading a saved world reads its own snapshot instead.
			rules["skeletonTransformer"] = JsonNode();
		}
		for(auto & [identity, profile] : rules["classProfiles"].Struct())
		{
			profile["base"].Integer() = 120;
			profile["perLevel"].Integer() = 1;
		}
		rules["leadership"]["creatureRequirements"]["core:skeleton"].Integer() = 10;
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
	}

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID::NECROPOLIS, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		hero->clearSlots();
		town->clearSlots();
		skeletonHealth = CreatureID(CreatureID::SKELETON).toCreature()->getMaxHealth();
		ASSERT_GT(skeletonHealth, 0u);
		const auto capacity = hero->getLeadershipSlotCapacity(CreatureID::SKELETON);
		ASSERT_TRUE(capacity);
		ASSERT_EQ(capacity->maximum, 12);
	}

	void put(CArmedInstance & army, int slot, int count, uint32_t health,
		CreatureID creature = pikeman())
	{
		army.putStack(SlotID(slot), std::make_unique<TransformerHealthStack>(creature, count, health));
	}

	Plan project(const CArmedInstance & army, std::initializer_list<int> slots)
	{
		std::vector<SlotID> selected;
		for(int slot : slots)
			selected.emplace_back(slot);
		return plan(army, selected, rules);
	}

	void expectOriginalProjection(const CArmedInstance & army, const Plan & projection)
	{
		EXPECT_FALSE(projection.isReady());
		EXPECT_TRUE(projection.outputs.empty());
		ASSERT_EQ(projection.projectedArmy.size(), army.Slots().size());
		for(const auto & stack : projection.projectedArmy)
		{
			ASSERT_TRUE(army.hasStackAtSlot(stack.slot));
			EXPECT_EQ(stack.creature, army.getCreature(stack.slot)->getId());
			EXPECT_EQ(stack.count, army.getStackCount(stack.slot));
		}
	}

	std::pair<bool, size_t> submit(std::initializer_list<int> slots, bool heroArmy = false)
	{
		TransformerRecordingServer server(gameState());
		CGameHandler handler(server, gameState());
		gameState()->actingPlayers.insert(PlayerColor(0));
		TradeOnMarketplace request;
		request.marketId = town->id;
		request.heroId = heroArmy ? hero->id : ObjectInstanceID::NONE;
		request.mode = EMarketMode::CREATURE_UNDEAD;
		request.player = PlayerColor(0);
		request.requestID = 1;
		for(int slot : slots)
			request.r1.emplace_back(SlotID(slot));
		// Exercise the production server visitor with the actual serialized
		// marketplace vector rather than calling the conversion method directly.
		auto restored = CMemorySerializer::deepCopy(request);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *restored);
		EXPECT_TRUE(server.requestResult.has_value());
		return {server.requestResult.value_or(false), server.armyUpdates};
	}

	void enableMarket()
	{
		town->addBuilding(BuildingID::SPECIAL_3);
		ASSERT_TRUE(town->allowsTrade(EMarketMode::CREATURE_UNDEAD));
	}

	void placeHeroAtMarket()
	{
		town->setVisitingHero(hero);
		hero->setAnchorPos(hero->convertFromVisitablePos(town->visitablePos()));
	}

	JsonNode rules;
	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	uint32_t skeletonHealth = 0;
};

class NewHorizonsLegacySkeletonTransformerTest : public NewHorizonsSkeletonTransformerTest
{
protected:
	bool enablesTransformer() const override { return false; }
};
}

TEST_F(NewHorizonsSkeletonTransformerTest, PoolsSelectionBeforeRoundingAndDiscardsRemainder)
{
	put(*town, 0, 1, skeletonHealth + 1);
	put(*town, 1, 1, skeletonHealth + 1);
	const auto projection = project(*town, {1, 0});
	ASSERT_TRUE(projection.isReady());
	EXPECT_EQ(projection.sacrificedHitPoints, 2 * (skeletonHealth + 1));
	EXPECT_EQ(projection.convertedHitPoints, skeletonHealth + 1);
	EXPECT_EQ(projection.skeletonCount, 1);
	ASSERT_EQ(projection.outputs.size(), 1u);
	EXPECT_EQ(projection.outputs.front().slot, SlotID(0));
	EXPECT_EQ(projection.outputs.front().count, 1);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(town->getStackCount(SlotID(1)), 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, UsesEffectiveAdventureStackHealthAndCreatureDefinitionOutputHealth)
{
	town->setCreature(SlotID(0), pikeman(), 3);
	auto * stack = town->getStackPtr(SlotID(0));
	const auto originalHealth = stack->getMaxHealth();
	stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 2 * skeletonHealth, BonusSourceID()));
	ASSERT_GT(stack->getMaxHealth(), originalHealth);
	const int64_t sacrificed = 3LL * stack->getMaxHealth();
	const auto projection = project(*town, {0});
	ASSERT_TRUE(projection.isReady());
	EXPECT_EQ(projection.sacrificedHitPoints, sacrificed);
	EXPECT_EQ(projection.convertedHitPoints, sacrificed / 2);
	EXPECT_EQ(projection.skeletonCount, (sacrificed / 2) / skeletonHealth);
}

TEST_F(NewHorizonsSkeletonTransformerTest, RejectsZeroOutputWithoutRemovingLastHeroStack)
{
	put(*hero, 0, 1, 1);
	const auto projection = project(*hero, {0});
	EXPECT_EQ(projection.status, PlanStatus::ZERO_OUTPUT);
	expectOriginalProjection(*hero, projection);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, PositiveConversionOfLastHeroStackPreservesOccupiedArmy)
{
	put(*hero, 4, 1, 2 * skeletonHealth);
	const auto projection = project(*hero, {4});
	ASSERT_TRUE(projection.isReady());
	ASSERT_EQ(projection.projectedArmy.size(), 1u);
	EXPECT_EQ(projection.projectedArmy.front().slot, SlotID(4));
	EXPECT_EQ(projection.projectedArmy.front().creature, CreatureID::SKELETON);
	EXPECT_EQ(projection.projectedArmy.front().count, 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, FillsExistingSkeletonsThenFreedSlotsToLeadershipLimit)
{
	put(*hero, 0, 10, skeletonHealth, CreatureID::SKELETON);
	put(*hero, 1, 1, 26 * skeletonHealth);
	put(*hero, 2, 1, 4 * skeletonHealth);
	put(*hero, 6, 3, 1, halberdier());
	const auto projection = project(*hero, {2, 1});
	ASSERT_TRUE(projection.isReady());
	EXPECT_EQ(projection.skeletonCount, 15);
	ASSERT_EQ(projection.outputs.size(), 3u);
	EXPECT_EQ(projection.outputs[0].slot, SlotID(0));
	EXPECT_EQ(projection.outputs[0].count, 12);
	EXPECT_EQ(projection.outputs[1].slot, SlotID(1));
	EXPECT_EQ(projection.outputs[1].count, 12);
	EXPECT_EQ(projection.outputs[2].slot, SlotID(2));
	EXPECT_EQ(projection.outputs[2].count, 1);
	ASSERT_EQ(projection.projectedArmy.size(), 4u);
	EXPECT_EQ(projection.projectedArmy.back().slot, SlotID(6));
	EXPECT_EQ(projection.projectedArmy.back().creature, halberdier());
	EXPECT_EQ(projection.projectedArmy.back().count, 3);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(hero->getCreature(SlotID(1))->getId(), pikeman());
}

TEST_F(NewHorizonsSkeletonTransformerTest, RejectsResidualOverflowWithoutPartialProjectionOrArmyMutation)
{
	put(*hero, 0, 11, skeletonHealth, CreatureID::SKELETON);
	put(*hero, 1, 1, 28 * skeletonHealth);
	const auto projection = project(*hero, {1});
	EXPECT_EQ(projection.skeletonCount, 14);
	EXPECT_EQ(projection.status, PlanStatus::OUTPUT_CAPACITY);
	expectOriginalProjection(*hero, projection);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, DoesNotUseUnselectedEmptySlotsForOverflow)
{
	put(*hero, 3, 1, 26 * skeletonHealth);
	const auto projection = project(*hero, {3});
	EXPECT_EQ(projection.status, PlanStatus::OUTPUT_CAPACITY);
	expectOriginalProjection(*hero, projection);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SelectedSkeletonsAreSacrificedAndUntouchedOnesRetained)
{
	put(*hero, 0, 4, skeletonHealth, CreatureID::SKELETON);
	put(*hero, 1, 3, skeletonHealth, CreatureID::SKELETON);
	const auto projection = project(*hero, {0});
	ASSERT_TRUE(projection.isReady());
	EXPECT_EQ(projection.skeletonCount, 2);
	ASSERT_EQ(projection.projectedArmy.size(), 1u);
	EXPECT_EQ(projection.projectedArmy.front().slot, SlotID(1));
	EXPECT_EQ(projection.projectedArmy.front().count, 5);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 3);
}

TEST_F(NewHorizonsSkeletonTransformerTest, RejectsEmptyDuplicateAndInvalidSelection)
{
	put(*town, 0, 2, 2 * skeletonHealth);
	for(const auto & [slots, expected] : std::array{
		std::pair{std::vector<SlotID>{}, PlanStatus::EMPTY_SELECTION},
		std::pair{std::vector<SlotID>{SlotID(0), SlotID(0)}, PlanStatus::DUPLICATE_SLOT},
		std::pair{std::vector<SlotID>{SlotID(0), SlotID(1)}, PlanStatus::INVALID_SLOT},
		std::pair{std::vector<SlotID>{SlotID(-1)}, PlanStatus::INVALID_SLOT}})
	{
		const auto projection = plan(*town, slots, rules);
		EXPECT_EQ(projection.status, expected);
		expectOriginalProjection(*town, projection);
	}
}

TEST_F(NewHorizonsSkeletonTransformerTest, RejectsZeroCountAndCheckedAggregateHealthOverflow)
{
	put(*town, 0, 1, skeletonHealth);
	// Direct fixture mutation creates a corrupt count that normal commands cannot.
	town->getStackPtr(SlotID(0))->setCount(0);
	EXPECT_EQ(project(*town, {0}).status, PlanStatus::INVALID_COUNT);
	town->clearSlots();
	put(*town, 0, std::numeric_limits<int>::max(), std::numeric_limits<uint32_t>::max());
	put(*town, 1, std::numeric_limits<int>::max(), std::numeric_limits<uint32_t>::max());
	const auto projection = project(*town, {0, 1});
	EXPECT_EQ(projection.status, PlanStatus::HP_OVERFLOW);
	expectOriginalProjection(*town, projection);
}

TEST_F(NewHorizonsSkeletonTransformerTest, TownOutputSplitsAtRepresentableStackCountAndRejectsExcess)
{
	const int maximum = std::numeric_limits<int>::max();
	put(*town, 0, maximum, 4 * skeletonHealth);
	put(*town, 1, 1, 1);
	const auto projection = project(*town, {0, 1});
	ASSERT_TRUE(projection.isReady());
	ASSERT_EQ(projection.outputs.size(), 2u);
	EXPECT_EQ(projection.outputs[0].count, maximum);
	EXPECT_EQ(projection.outputs[1].count, maximum);
	const auto rejected = project(*town, {0});
	EXPECT_EQ(rejected.status, PlanStatus::OUTPUT_CAPACITY);
	expectOriginalProjection(*town, rejected);
}

TEST_F(NewHorizonsSkeletonTransformerTest, AbsentSavedMarkerKeepsLegacyRules)
{
	put(*hero, 0, 3, 2 * skeletonHealth);
	for(auto legacy : {rules, JsonNode()})
	{
		if(!legacy.isNull())
			legacy.Struct().erase("skeletonTransformer");
		const std::array selected{SlotID(0)};
		const auto projection = plan(*hero, selected, legacy);
		EXPECT_EQ(projection.status, PlanStatus::LEGACY_RULES);
		expectOriginalProjection(*hero, projection);
	}
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedTownRequestPoolsSourcesAndAppliesExactWholeOutput)
{
	enableMarket();
	put(*town, 0, 1, skeletonHealth + 1);
	put(*town, 1, 1, skeletonHealth + 1);
	put(*town, 6, 2, 1, halberdier());
	const auto [accepted, updates] = submit({1, 0});
	ASSERT_TRUE(accepted);
	EXPECT_GT(updates, 0u);
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), CreatureID::SKELETON);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 1);
	EXPECT_FALSE(town->hasStackAtSlot(SlotID(1)));
	EXPECT_EQ(town->getCreature(SlotID(6))->getId(), halberdier());
	EXPECT_EQ(town->getStackCount(SlotID(6)), 2);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedHeroRequestMatchesSharedLeadershipProjection)
{
	enableMarket();
	placeHeroAtMarket();
	put(*hero, 0, 10, skeletonHealth, CreatureID::SKELETON);
	put(*hero, 1, 1, 26 * skeletonHealth);
	put(*hero, 2, 1, 4 * skeletonHealth);
	const auto projection = project(*hero, {2, 1});
	ASSERT_TRUE(projection.isReady());
	const auto [accepted, updates] = submit({2, 1}, true);
	ASSERT_TRUE(accepted);
	EXPECT_GT(updates, 0u);
	ASSERT_EQ(hero->Slots().size(), projection.projectedArmy.size());
	for(const auto & stack : projection.projectedArmy)
	{
		EXPECT_EQ(hero->getCreature(stack.slot)->getId(), stack.creature);
		EXPECT_EQ(hero->getStackCount(stack.slot), stack.count);
	}
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedZeroOutputRequestSendsNoArmyChanges)
{
	enableMarket();
	placeHeroAtMarket();
	put(*hero, 0, 1, 1);
	const auto [accepted, updates] = submit({0}, true);
	EXPECT_FALSE(accepted);
	EXPECT_EQ(updates, 0u);
	ASSERT_EQ(hero->Slots().size(), 1u);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedOverflowRequestRejectsAllSourcesAndSkeletonAdditions)
{
	enableMarket();
	placeHeroAtMarket();
	put(*hero, 0, 11, skeletonHealth, CreatureID::SKELETON);
	put(*hero, 1, 1, 52 * skeletonHealth);
	put(*hero, 2, 1, 1);
	const auto [accepted, updates] = submit({1, 2}, true);
	EXPECT_FALSE(accepted);
	EXPECT_EQ(updates, 0u);
	ASSERT_EQ(hero->Slots().size(), 3u);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(hero->getCreature(SlotID(1))->getId(), pikeman());
	EXPECT_EQ(hero->getCreature(SlotID(2))->getId(), pikeman());
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(2)), 1);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedRequestCannotConvertWithoutTransformerBuilding)
{
	town->removeBuilding(BuildingID::SPECIAL_3);
	ASSERT_FALSE(town->allowsTrade(EMarketMode::CREATURE_UNDEAD));
	put(*town, 0, 4, 2 * skeletonHealth);
	const auto [accepted, updates] = submit({0});
	EXPECT_FALSE(accepted);
	EXPECT_EQ(updates, 0u);
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(town->getStackCount(SlotID(0)), 4);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedDuplicateOrStaleSourceRejectsWholeTransaction)
{
	enableMarket();
	put(*town, 0, 4, 2 * skeletonHealth);
	for(auto slots : {std::initializer_list<int>{0, 0}, std::initializer_list<int>{0, 1}})
	{
		const auto [accepted, updates] = submit(slots);
		EXPECT_FALSE(accepted);
		EXPECT_EQ(updates, 0u);
		ASSERT_EQ(town->Slots().size(), 1u);
		EXPECT_EQ(town->getCreature(SlotID(0))->getId(), pikeman());
		EXPECT_EQ(town->getStackCount(SlotID(0)), 4);
	}
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedRemoteHeroRequestPreservesOrdinaryVisitAdmission)
{
	enableMarket();
	put(*hero, 0, 4, 2 * skeletonHealth);
	const auto [accepted, updates] = submit({0}, true);
	EXPECT_FALSE(accepted);
	EXPECT_EQ(updates, 0u);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
}

TEST_F(NewHorizonsSkeletonTransformerTest, SerializedForeignTownRequestPreservesOrdinaryOwnershipAdmission)
{
	enableMarket();
	put(*town, 0, 4, 2 * skeletonHealth);
	town->tempOwner = PlayerColor(1);
	const auto [accepted, updates] = submit({0});
	EXPECT_FALSE(accepted);
	EXPECT_EQ(updates, 0u);
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), pikeman());
	EXPECT_EQ(town->getStackCount(SlotID(0)), 4);
}

TEST_F(NewHorizonsLegacySkeletonTransformerTest, SerializedLegacyRequestKeepsPerStackCountSubstitution)
{
	enableMarket();
	const auto & savedWorldRules = gameState()->getHeroCapabilityRules();
	ASSERT_TRUE(savedWorldRules["skeletonTransformer"].isNull());
	ASSERT_FALSE(newHorizonsHeroes::capabilitySkeletonTransformerHealthPercent(savedWorldRules));
	// Old worlds serialize their snapshot, not a settings patch. In particular,
	// an absent marker stays absent even while installed defaults enable it.
	auto historicalSnapshot = savedWorldRules;
	historicalSnapshot.Struct().erase("skeletonTransformer");
	CMemorySerializer serializer;
	serializer.oser & historicalSnapshot;
	JsonNode restoredSnapshot;
	serializer.iser & restoredSnapshot;
	ASSERT_FALSE(restoredSnapshot.Struct().contains("skeletonTransformer"));
	ASSERT_FALSE(newHorizonsHeroes::capabilitySkeletonTransformerHealthPercent(restoredSnapshot));
	const JsonNode installedRules(JsonPath::builtin("config/newHorizonsCapabilities"));
	ASSERT_EQ(newHorizonsHeroes::capabilitySkeletonTransformerHealthPercent(installedRules), 50);
	put(*town, 0, 3, 4 * skeletonHealth);
	put(*town, 1, 5, 1);
	const auto [accepted, updates] = submit({0, 1});
	ASSERT_TRUE(accepted);
	EXPECT_GT(updates, 0u);
	EXPECT_EQ(town->getCreature(SlotID(0))->getId(), CreatureID::SKELETON);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(town->getCreature(SlotID(1))->getId(), CreatureID::SKELETON);
	EXPECT_EQ(town->getStackCount(SlotID(1)), 5);
}
