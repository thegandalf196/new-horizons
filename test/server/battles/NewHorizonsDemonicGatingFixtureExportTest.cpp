/*
 * NewHorizonsDemonicGatingFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <cstdlib>
#include <algorithm>
#include <zlib.h>

class NewHorizonsDemonicGatingFixtureExportTest : public BattleTestFixture
{
protected:
	TinyH3M::TinyH3MBuilder ordinaryMap(HeroTypeID marius, HeroTypeID orrin, CreatureID imp, CreatureID pikeman)
	{
		const std::string name = "NHDemonicGatingGuiCrash";
		const int3 redHeroPosition{14, 14, 0}, blueHeroPosition{17, 14, 0};
		const int3 redTownPosition{8, 10, 0}, blueTownPosition{29, 28, 0};
		using Builder = TinyH3M::TinyH3MBuilder;
		Builder builder(EMapFormat::SOD);
		builder.size(36, false).name(name)
			.description("Demonic Gating GUI crash check, not a balanced scenario. Red human Marius starts with "
				"20 living Imps; his level 1 Inferno Leadership limit permits all 20. Blue computer Orrin has "
				"12 Pikemen and begins three clear tiles east. Both players have an owned town away from the "
				"heroes. On Red's first turn, open Marius's army and move 5 Imps into Demonic Reserve. Move "
				"Marius east onto Orrin to start the ordinary hero battle. When an Inferno stack becomes active, "
				"open Gate, hover a legal placement hex, cancel, reopen Gate, and place the 5 Imps. This fixture "
				"starts with no reserve troops and injects no battle state.")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town(redTownPosition, FactionID::INFERNO, PlayerColor(0)).townGarrison({})
			.town(blueTownPosition, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
			.hero(redHeroPosition, marius, PlayerColor(0)).heroExperience(0)
			.heroGarrison({{imp, 20}})
			.hero(blueHeroPosition, orrin, PlayerColor(1)).heroExperience(0)
			.heroGarrison({{pikeman, 12}});
		return builder;
	}
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsDemonicGatingFixtureExportTest, ExportOrdinaryInfernoGatingCrashFixture)
{
	const auto * enabled = std::getenv("NH_EXPORT_GATING_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_GATING_FIXTURE=1";

	const std::string name = "NHDemonicGatingGuiCrash";
	const int3 redHeroPosition{14, 14, 0};
	const int3 blueHeroPosition{17, 14, 0};
	const int3 redTownPosition{8, 10, 0};
	const int3 blueTownPosition{29, 28, 0};
	const auto mariusIndex = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), HeroTypeID::entityType(), "core:marius", true);
	ASSERT_TRUE(mariusIndex) << "The fixture requires the core Marius hero identifier";
	const HeroTypeID marius(*mariusIndex);
	const auto orrinIndex = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), HeroTypeID::entityType(), "core:orrin", true);
	ASSERT_TRUE(orrinIndex) << "The fixture requires the core Orrin hero identifier";
	const HeroTypeID orrin(*orrinIndex);
	const auto imp = creatureByName("core:imp");
	const auto pikeman = creatureByName("core:pikeman");
	ASSERT_NE(imp, CreatureID::NONE);
	ASSERT_NE(pikeman, CreatureID::NONE);

	auto builder = ordinaryMap(marius, orrin, imp, pikeman);
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	ASSERT_EQ(header->width, 36);
	ASSERT_EQ(header->height, 36);
	ASSERT_EQ(header->levels(), 1);
	ASSERT_TRUE(header->players[0].canHumanPlay);
	ASSERT_TRUE(header->players[1].canComputerPlay);

	// Use the active New Horizons engine preset and normal map initialization.
	// No map setting overrides, hero mutations, reserve injection, or battle setup.
	startWithMap(builder);
	const JsonNode canonicalGrowth(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_EQ(gameState()->getHeroDevelopmentRules(), canonicalGrowth);
	const JsonNode canonicalCombat(JsonPath::builtin("config/newHorizonsCombat"));
	ASSERT_EQ(gameState()->getHeroCommandRules(), canonicalCombat["combat"]["heroCommands"]);

	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_EQ(human->getHeroTypeID(), marius);
	ASSERT_EQ(computer->getHeroTypeID(), orrin);
	ASSERT_EQ(human->getFactionID(), FactionID::INFERNO);
	ASSERT_EQ(human->pos, redHeroPosition);
	ASSERT_EQ(computer->pos, blueHeroPosition);
	ASSERT_GE(human->getPerkSkillRank("new-horizons:demonicGating"),
		static_cast<int>(MasteryLevel::BASIC));
	ASSERT_EQ(human->level, 1u);
	ASSERT_EQ(computer->level, 1u);

	const auto * redTown = dynamic_cast<const CGTownInstance *>(findObjectAt(redTownPosition));
	const auto * blueTown = dynamic_cast<const CGTownInstance *>(findObjectAt(blueTownPosition));
	ASSERT_NE(redTown, nullptr);
	ASSERT_NE(blueTown, nullptr);
	ASSERT_EQ(redTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(blueTown->getOwner(), PlayerColor(1));
	ASSERT_GT(redTown->visitablePos().dist2dSQ(human->visitablePos()), 0);
	ASSERT_GT(blueTown->visitablePos().dist2dSQ(computer->visitablePos()), 0);

	ASSERT_EQ(human->stacksCount(), 1);
	const auto * impStack = human->getStackPtr(SlotID(0));
	ASSERT_NE(impStack, nullptr);
	ASSERT_EQ(impStack->getCreatureID(), imp);
	ASSERT_EQ(impStack->getCount(), 20);
	ASSERT_FALSE(imp.toCreature()->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_FALSE(imp.toCreature()->hasBonusOfType(BonusType::NON_LIVING));
	const auto impSlotCapacity = human->getLeadershipSlotCapacity(imp);
	ASSERT_TRUE(impSlotCapacity);
	ASSERT_EQ(impSlotCapacity->maximum, 20);
	ASSERT_LE(impStack->getCount(), impSlotCapacity->maximum);
	const auto humanLeadership = human->getLeadershipCapacity();
	ASSERT_TRUE(humanLeadership);
	ASSERT_FALSE(humanLeadership->overCapacity());

	ASSERT_EQ(computer->stacksCount(), 1);
	const auto * pikemanStack = computer->getStackPtr(SlotID(0));
	ASSERT_NE(pikemanStack, nullptr);
	ASSERT_EQ(pikemanStack->getCreatureID(), pikeman);
	ASSERT_EQ(pikemanStack->getCount(), 12);
	const auto pikemanSlotCapacity = computer->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanSlotCapacity);
	ASSERT_LE(pikemanStack->getCount(), pikemanSlotCapacity->maximum);
	const auto computerLeadership = computer->getLeadershipCapacity();
	ASSERT_TRUE(computerLeadership);
	ASSERT_FALSE(computerLeadership->overCapacity());

	const auto impCategory = gameState()->getCreatureCategory(imp);
	ASSERT_TRUE(impCategory);
	ASSERT_EQ(impCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
	ASSERT_TRUE(human->getDemonicReserve().empty());

	for(const auto * hero : {human, computer})
	{
		const auto * tile = gameState()->getTile(hero->visitablePos());
		ASSERT_NE(tile, nullptr);
		ASSERT_TRUE(tile->isLand());
		ASSERT_TRUE(tile->getTerrain()->isPassable());
	}
	ASSERT_LE(std::max(std::abs(human->visitablePos().x - computer->visitablePos().x),
		std::abs(human->visitablePos().y - computer->visitablePos().y)), 3);
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(human);
	CGPath battlePath;
	ASSERT_TRUE(route->getPath(battlePath, computer->visitablePos()));
	const auto * battleNode = route->getPathInfo(computer->visitablePos());
	ASSERT_NE(battleNode, nullptr);
	ASSERT_EQ(battleNode->turns, 0);
	ASSERT_EQ(battleNode->action, EPathNodeAction::BATTLE);

	// Publish the named GUI map only after canonical-profile and real-init checks pass.
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(path.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
	std::cout << "Demonic Gating fixture " << name << ": " << path.string()
		<< "; Marius at " << human->visitablePos().toString()
		<< " with 20 Imps; Orrin at " << computer->visitablePos().toString()
		<< " with 12 Pikemen; initial reserve empty" << std::endl;
}

TEST_F(NewHorizonsDemonicGatingFixtureExportTest, OrdinaryMariusBattleAdvertisesTemporaryGateWithoutReserve)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires New Horizons content";
	const HeroTypeID marius(HeroTypeID::decode("core:marius"));
	const HeroTypeID orrin(HeroTypeID::decode("core:orrin"));
	const auto imp = creatureByName("core:imp"), pikeman = creatureByName("core:pikeman");
	ASSERT_NE(marius, HeroTypeID::NONE);
	ASSERT_NE(orrin, HeroTypeID::NONE);
	ASSERT_NE(imp, CreatureID::NONE);
	ASSERT_NE(pikeman, CreatureID::NONE);
	ASSERT_NO_FATAL_FAILURE(startWithMap(ordinaryMap(marius, orrin, imp, pikeman)));
	auto * human = findHeroByOwner(PlayerColor(0));
	auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_EQ(human->getHeroTypeID(), marius);
	ASSERT_EQ(human->getStackPtr(SlotID(0))->getCreatureID(), imp);
	ASSERT_EQ(human->getStackPtr(SlotID(0))->getCount(), 20);
	EXPECT_TRUE(human->getDemonicReserve().empty());
	EXPECT_TRUE(newHorizonsDemonicGating::usesTemporaryGating(human))
		<< human->getPerkState().rules["demonicGatingPolicy"].toCompactString();
	EXPECT_EQ(human->getPerkSkillRank("new-horizons:demonicGating"), MasteryLevel::BASIC);
	const auto worldCategory = gameState()->getCreatureCategory(imp);
	EXPECT_TRUE(worldCategory.has_value()) << gameState()->getCreatureCategoryRules().getRules().toCompactString();
	if(worldCategory) EXPECT_EQ(worldCategory->category, newHorizonsCreatures::CreatureCategory::CORE);

	// Use the production battle entry point and ordinary generated bodies;
	// never set policy, slots, counters, marks, statuses, reserves or allowances.
	server.gameState = gameState();
	gameHandler = std::make_shared<CGameHandler>(server, gameState());
	gameHandler->battles->startBattle(human, computer);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(source->creatureId(), imp);
	ASSERT_EQ(source->getCount(), 20);
	ASSERT_EQ(source->unitSide(), BattleSide::ATTACKER);
	CPlayerBattleCallback visible(battle(), PlayerColor(0));
	const auto side = source->unitSide();
	const auto * hero = visible.battleGetFightingHero(side);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero, human);
	EXPECT_TRUE(newHorizonsDemonicGating::usesTemporaryGating(hero));
	EXPECT_EQ(hero->getPerkSkillRank("new-horizons:demonicGating"), MasteryLevel::BASIC);
	EXPECT_TRUE(source->alive());
	EXPECT_FALSE(source->isGhost());
	EXPECT_FALSE(source->isClone());
	EXPECT_FALSE(source->isSummoned());
	EXPECT_EQ(source->unitSlot(), SlotID(0));
	EXPECT_TRUE(source->unitSlot().validSlot());
	EXPECT_FALSE(source->hasOpenedDemonicGate());
	EXPECT_EQ(visible.battleGetOwner(source), visible.sideToPlayer(side));
	EXPECT_FALSE(source->isTimeStopped());
	EXPECT_FALSE(source->isFrozen());
	EXPECT_FALSE(source->hasBonusOfType(BonusType::NOT_ACTIVE));
	EXPECT_EQ(source->creatureId().toCreature()->getFactionID(), FactionID::INFERNO);
	const auto category = visible.battleGetCreatureCategory(imp);
	EXPECT_TRUE(category.has_value()) << battle()->getCreatureCategoryRules().getRules().toCompactString();
	if(category) EXPECT_EQ(category->category, newHorizonsCreatures::CreatureCategory::CORE);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*source), 0);
	EXPECT_TRUE(newHorizonsDemonicGating::eligibleOriginalStack(visible, *source, side));
	EXPECT_EQ(newHorizonsDemonicGating::availableTypes(visible, *source, side), std::vector<CreatureID>{imp});
	const BattleClientInterfaceData input{{}, 0};
	const auto actions = visible.getClientActionsForStack(battle()->getStack(source->unitId()), input);
	EXPECT_TRUE(std::ranges::any_of(actions, [](const auto & value)
		{ return value.get() == PossiblePlayerBattleAction::DEMONIC_GATE; }));
}
