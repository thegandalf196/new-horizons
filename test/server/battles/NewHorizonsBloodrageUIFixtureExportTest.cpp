/*
 * NewHorizonsBloodrageUIFixtureExportTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include <array>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <zlib.h>

class NewHorizonsBloodrageUIFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsBloodrageUIFixtureExportTest, ExportCanonicalCragHackWithReachableTwoStackEnemy)
{
	const auto * enabled = std::getenv("NH_EXPORT_BLOODRAGE_UI_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Opt-in export requires NH_EXPORT_BLOODRAGE_UI_FIXTURE=1";
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the actual curated New Horizons module";
	const std::string name = "NHBloodrageUICragHackUP004v1";
	const auto output = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	ASSERT_FALSE(boost::filesystem::exists(output)) << "Use a fresh disposable export profile; never overwrite an existing map";
	const HeroTypeID cragHack(HeroTypeID::decode("core:cragHack"));
	const HeroTypeID orrin(HeroTypeID::decode("core:orrin"));
	ASSERT_NE(cragHack, HeroTypeID::NONE);
	ASSERT_NE(orrin, HeroTypeID::NONE);
	const auto orc = creatureByName("core:orc");
	const auto peasant = creatureByName("core:peasant");
	const auto dendroid = creatureByName("core:dendroidGuard");
	for(const auto creature : {orc, peasant, dendroid})
	{
		ASSERT_NE(creature, CreatureID::NONE);
	}
	// Ordinary hero armies are used instead of a fabricated mixed neutral:
	// CGCreature's map format serializes one type/amount and encounters resplit
	// that single type. Both opposing stacks here survive normal map loading.
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("UP004 Bloodrage UI diagnostic, not a balanced scenario. Human Red owns Stronghold8,10. "
			"Crag Hack at17,10 retains his canonical starting Basic Bloodrage and has5 ordinary Orcs. "
			"No secondary ranks, perks, rage values, creature stats or RNG are injected. Blue computer "
			"Orrin at19,10 has1 unrestricted legacy Peasant and2 Dendroid Guards within Leadership capacity. "
			"Blue also owns Castle30,30. Attack Orrin normally, inspect Red's initial Bloodrage0/20%, "
			"then use the Orc's ordinary ranged attack on the one-Peasant stack. Its whole-stack death "
			"should update Bloodrage to5/20%; the Dendroids keep combat active. Do not cast a spell or "
			"issue an Order. This seed verifies initialization, legal armies and reachable combat; "
			"it does not claim an executed attack, rendered feedback or active-battle save acceptance.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::STRONGHOLD, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, cragHack, PlayerColor(0)).heroExperience(0)
		.heroGarrison({{orc, 5}})
		.hero({19, 10, 0}, orrin, PlayerColor(1)).heroExperience(0)
		.heroGarrison({{peasant, 1}, {dendroid, 2}});
	const auto expected = builder.build();
	// No mapLoaded/settings override and no gameplay mutation after loading.
	startWithMap(builder);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_EQ(human->getHeroTypeID(), cragHack);
	ASSERT_EQ(computer->getHeroTypeID(), orrin);
	ASSERT_EQ(newHorizonsBloodrage::rank(human), MasteryLevel::BASIC);
	ASSERT_FALSE(newHorizonsBloodrage::hasWarDrums(human));
	// These are the real initialized hero's production setup inputs; the UI
	// tester must separately verify the actual battle's captured current/cap.
	ASSERT_EQ(newHorizonsBloodrage::capForHero(human), 20);
	ASSERT_EQ(newHorizonsBloodrage::initialDamagePercent(human), 0);
	ASSERT_EQ(newHorizonsBloodrage::incrementForRank(newHorizonsBloodrage::rank(human)), 5);
	ASSERT_EQ(human->stacksCount(), 1);
	ASSERT_EQ(computer->stacksCount(), 2);
	const auto * shooter = human->getStackPtr(SlotID(0));
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(shooter->getCreatureID(), orc);
	ASSERT_EQ(shooter->getCount(), 5);
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SHOOTER));
	ASSERT_GT(shooter->valOfBonuses(BonusType::SHOTS), 0);
	const auto capacity = human->getLeadershipSlotCapacity(orc);
	ASSERT_TRUE(capacity);
	ASSERT_TRUE(capacity->accepts(shooter->getCount()));
	const std::array<std::pair<CreatureID, int>, 2> opposing{{{peasant, 1}, {dendroid, 2}}};
	ASSERT_TRUE(newHorizonsHeroes::usesRules(computer->getCapabilityRules()));
	ASSERT_GE(computer->getCapabilityRules()["rulesetVersion"].Integer(), 2);
	for(size_t slot = 0; slot < opposing.size(); ++slot)
	{
		const auto * stack = computer->getStackPtr(SlotID(static_cast<int>(slot)));
		ASSERT_NE(stack, nullptr);
		ASSERT_EQ(stack->getCreatureID(), opposing[slot].first);
		ASSERT_EQ(stack->getCount(), opposing[slot].second);
		const auto enemyCapacity = computer->getLeadershipSlotCapacity(stack->getCreatureID());
		if(stack->getCreatureID() == peasant)
		{
			// Peasant is intentionally absent from the captured faction creature
			// requirements. The same authoritative initialization/admission contract
			// leaves an unrepresented legacy creature unrestricted, not illegal.
			ASSERT_EQ(newHorizonsHeroes::capabilityCreatureLeadershipRequirement(
				computer->getCapabilityRules(), peasant), 0);
			ASSERT_FALSE(enemyCapacity);
		}
		else
		{
			ASSERT_TRUE(enemyCapacity);
			ASSERT_TRUE(enemyCapacity->accepts(stack->getCount()));
		}
	}
	const auto * redTown = dynamic_cast<const CGTownInstance *>(findObjectAt({8, 10, 0}));
	const auto * blueTown = dynamic_cast<const CGTownInstance *>(findObjectAt({30, 30, 0}));
	ASSERT_NE(redTown, nullptr);
	ASSERT_NE(blueTown, nullptr);
	ASSERT_EQ(redTown->getFactionID(), FactionID::STRONGHOLD);
	ASSERT_EQ(redTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(blueTown->getOwner(), PlayerColor(1));
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(human);
	CGPath path;
	ASSERT_TRUE(route->getPath(path, computer->visitablePos()));
	const auto * node = route->getPathInfo(computer->visitablePos());
	ASSERT_NE(node, nullptr);
	ASSERT_EQ(node->turns, 0);
	ASSERT_EQ(node->action, EPathNodeAction::BATTLE);
	ASSERT_EQ(builder.buildAndDump(name), expected);
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(output.string().c_str(), "rb"), &gzclose);
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
	RecordProperty("exported_map", output.string());
	std::cout << "Bloodrage fixture " << name << ": " << output.string()
		<< "; canonical Basic setup current=0 cap=20; Orc5 -> Peasant1" << std::endl;
}
