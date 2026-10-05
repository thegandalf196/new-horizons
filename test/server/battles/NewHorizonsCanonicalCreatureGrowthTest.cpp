/*
 * NewHorizonsCanonicalCreatureGrowthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <array>

namespace
{
CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

struct ExpectedGrowth
{
	const char * identifier;
	int weeklyGrowth;
};

class NewHorizonsCanonicalCreatureGrowthTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		const auto activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void startTestMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsCanonicalCreatureGrowth")
			.playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID::CONFLUX, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};
}

TEST_F(NewHorizonsCanonicalCreatureGrowthTest, LoadedBaseGrowthMatchesEveryCanonicalFactionRow)
{
	ASSERT_NO_FATAL_FAILURE(startTestMap());

	static constexpr std::array<ExpectedGrowth, 64> expected = {{
		{"core:pikeman", 14}, {"core:archer", 10}, {"core:swordsman", 7}, {"core:griffin", 5},
		{"core:monk", 4}, {"core:cavalier", 3}, {"core:angel", 1},
		{"core:centaur", 14}, {"core:dwarf", 8}, {"core:woodElf", 7}, {"core:pegasus", 5},
		{"core:dendroidGuard", 3}, {"core:unicorn", 3}, {"core:greenDragon", 1},
		{"core:gremlin", 16}, {"core:stoneGargoyle", 9}, {"core:stoneGolem", 6}, {"core:mage", 3},
		{"core:genie", 4}, {"core:naga", 3}, {"core:giant", 1},
		{"core:imp", 15}, {"core:gog", 8}, {"core:hellHound", 5}, {"core:demon", 4},
		{"core:pitFiend", 3}, {"core:efreet", 2}, {"core:devil", 1},
		{"core:skeleton", 18}, {"core:walkingDead", 8}, {"core:wight", 7}, {"core:vampire", 4},
		{"core:lich", 3}, {"core:blackKnight", 2}, {"core:boneDragon", 1},
		{"core:troglodyte", 14}, {"core:harpy", 8}, {"core:beholder", 7}, {"core:medusa", 4},
		{"core:minotaur", 3}, {"core:manticore", 2}, {"core:redDragon", 1},
		{"core:goblin", 15}, {"core:goblinWolfRider", 8}, {"core:orc", 7}, {"core:ogre", 4},
		{"core:roc", 3}, {"core:cyclop", 2}, {"core:behemoth", 1},
		{"core:gnoll", 14}, {"core:lizardman", 9}, {"core:serpentFly", 8}, {"core:basilisk", 4},
		{"core:gorgon", 3}, {"core:wyvern", 2}, {"core:hydra", 1},
		{"core:pixie", 14}, {"core:sprite", 10}, {"core:airElemental", 5}, {"core:waterElemental", 5},
		{"core:fireElemental", 4}, {"core:earthElemental", 4}, {"core:magicElemental", 3}, {"core:phoenix", 1}
	}};

	for(const auto & row : expected)
	{
		const auto id = creature(row.identifier);
		ASSERT_NE(id, CreatureID::NONE) << row.identifier;
		ASSERT_NE(LIBRARY->creh->getById(id), nullptr) << row.identifier;
		EXPECT_EQ(gameState()->getCreatureBaseGrowth(id), row.weeklyGrowth) << row.identifier;

		const auto * creatureType = id.toCreature();
		ASSERT_NE(creatureType, nullptr) << row.identifier;
		for(const auto upgradedId : creatureType->upgrades)
			EXPECT_EQ(gameState()->getCreatureBaseGrowth(upgradedId), row.weeklyGrowth)
				<< row.identifier << " upgrade";
	}

	const auto psychicElemental = creature("core:psychicElemental");
	const auto magicElemental = creature("core:magicElemental");
	ASSERT_NE(psychicElemental.toCreature(), nullptr);
	ASSERT_NE(magicElemental.toCreature(), nullptr);
	ASSERT_TRUE(psychicElemental.toCreature()->upgrades.contains(magicElemental));
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(psychicElemental), 3);

	const auto firebird = creature("core:firebird");
	const auto phoenix = creature("core:phoenix");
	ASSERT_NE(firebird.toCreature(), nullptr);
	ASSERT_NE(phoenix.toCreature(), nullptr);
	ASSERT_TRUE(firebird.toCreature()->upgrades.contains(phoenix));
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(firebird), 1);
}

TEST_F(NewHorizonsCanonicalCreatureGrowthTest, CurrentAndLegacyCategorySnapshotsRoundTripGrowthSemantics)
{
	ASSERT_NO_FATAL_FAILURE(startTestMap());

	newHorizonsCreatures::CreatureCategoryRules current = gameState()->getCreatureCategoryRules();
	CMemorySerializer currentWire;
	currentWire.oser & current;
	newHorizonsCreatures::CreatureCategoryRules restoredCurrent;
	currentWire.iser & restoredCurrent;
	EXPECT_EQ(restoredCurrent.getRules(), current.getRules());
	EXPECT_EQ(restoredCurrent.weeklyBaseGrowth("core:airElemental"), 5);

	// Existing v2 saves capture their own rows; a new installed row must not
	// silently be merged into a historical snapshot that omitted it.
	auto historicalSnapshot = current.getRules();
	historicalSnapshot["growthLines"].Struct().erase("core:airElemental");
	newHorizonsCreatures::CreatureCategoryRules historical(historicalSnapshot);
	CMemorySerializer historicalWire;
	historicalWire.oser & historical;
	newHorizonsCreatures::CreatureCategoryRules restoredHistorical;
	historicalWire.iser & restoredHistorical;
	EXPECT_EQ(restoredHistorical.getRules(), historical.getRules());
	EXPECT_EQ(restoredHistorical.getRulesetVersion(), 2);
	EXPECT_FALSE(restoredHistorical.weeklyBaseGrowth("core:airElemental"));
	EXPECT_EQ(restoredHistorical.weeklyBaseGrowth("core:fireElemental"), 4);

	auto oldSnapshot = current.getRules();
	oldSnapshot["rulesetVersion"].Integer() = 1;
	oldSnapshot.Struct().erase("growthLines");
	newHorizonsCreatures::CreatureCategoryRules legacy(oldSnapshot);
	CMemorySerializer legacyWire;
	legacyWire.oser & legacy;
	newHorizonsCreatures::CreatureCategoryRules restoredLegacy;
	legacyWire.iser & restoredLegacy;
	EXPECT_EQ(restoredLegacy.getRules(), legacy.getRules());
	EXPECT_EQ(restoredLegacy.getRulesetVersion(), 1);
	EXPECT_FALSE(restoredLegacy.weeklyBaseGrowth("core:airElemental"));
}
