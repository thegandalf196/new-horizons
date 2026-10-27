/*
 * NewHorizonsBalanceDataExportTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include <array>
#include <cstdlib>
#include <set>
#include <string_view>

class NewHorizonsBalanceDataExportTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
};

// Explicit data collection only: these facts do not assert that balance is correct.
TEST_F(NewHorizonsBalanceDataExportTest, DISABLED_ExportSelectedMergedCreatureFacts)
{
	const auto * enabled = std::getenv("NH_EXPORT_MERGED_CREATURE_FACTS");
	if(!enabled || std::string_view(enabled) != "1")
		GTEST_SKIP() << "Requires explicit private data-collection invocation";
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1));
	startWithMap(std::move(builder)); // Capture ordinary effective rules; no fixture overrides.
	const auto & capabilities = gameState()->getHeroCapabilityRules();
	const bool hasCapabilities = capabilities.isStruct() && !capabilities.Struct().empty();
	const std::array<const char *, 42> keys{
		"skeleton", "skeletonWarrior", "walkingDead", "zombieLord", "wight", "wraith", "vampire", "vampireLord",
		"lich", "powerLich", "blackKnight", "dreadKnight", "boneDragon", "ghostDragon",
		"pikeman", "halberdier", "archer", "marksman", "griffin", "royalGriffin", "swordsman", "crusader",
		"monk", "zealot", "cavalier", "champion", "angel", "archangel",
		"centaur", "centaurCaptain", "dwarf", "battleDwarf", "woodElf", "grandElf", "pegasus", "silverPegasus",
		"dendroidGuard", "dendroidSoldier", "unicorn", "warUnicorn", "greenDragon", "goldDragon"};
	JsonNode result;
	result["formatVersion"].Integer() = 1;
	result["collectionOnly"].Bool() = true;
	result["statsContext"].String() = "mergedCreatureBaseWithoutHeroTerrainOrTownModifiers";
	result["categoryRulesetVersion"].Integer() = gameState()->getCreatureCategoryRules().getRulesetVersion();
	result["leadershipRulesetVersion"].Integer() = hasCapabilities ? capabilities["rulesetVersion"].Integer() : 0;
	std::set<CreatureID> resolvedIds;
	std::set<std::string> resolvedKeys;
	for(const auto * key : keys)
	{
		const CreatureID id(CreatureID::decode(std::string("core:") + key));
		ASSERT_TRUE(id.hasValue()) << key;
		const auto * creature = id.toCreature();
		ASSERT_NE(creature, nullptr) << key;
		ASSERT_TRUE(resolvedIds.insert(id).second) << key;
		ASSERT_TRUE(resolvedKeys.insert(creature->getJsonKey()).second) << key;
		JsonNode row;
		row["key"].String() = creature->getJsonKey();
		row["id"].Integer() = id.getNum();
		row["factionID"].Integer() = creature->getFactionID().getNum();
		row["hitPoints"].Integer() = creature->getBaseHitPoints();
		row["attack"].Integer() = creature->getBaseAttack();
		row["defense"].Integer() = creature->getBaseDefense();
		row["damageMin"].Integer() = creature->getBaseDamageMin();
		row["damageMax"].Integer() = creature->getBaseDamageMax();
		row["speed"].Integer() = creature->getBaseSpeed();
		row["initiative"].Integer() = creature->getBaseInitiative();
		row["libraryGrowth"].Integer() = creature->getGrowth();
		row["capturedWeeklyBaseGrowth"].Integer() = gameState()->getCreatureBaseGrowth(id);
		row["goldCost"].Integer() = creature->getRecruitCost(GameResID::GOLD);
		const auto & cost = creature->getFullRecruitCost();
		for(size_t index = 0; index < cost.size(); ++index)
			row["recruitmentCostByResourceID"].Vector().emplace_back(cost[index]);
		row["category"] = JsonNode();
		if(const auto category = gameState()->getCreatureCategory(id))
			row["category"].String() = std::array{"core", "elite", "champion"}.at(static_cast<size_t>(category->category));
		row["leadershipBaseRequirement"] = JsonNode();
		if(hasCapabilities)
			row["leadershipBaseRequirement"].Integer() = newHorizonsHeroes::capabilityCreatureLeadershipRequirement(capabilities, id);
		result["creatures"].Vector().push_back(std::move(row));
	}
	ASSERT_EQ(result["creatures"].Vector().size(), keys.size());
	RecordProperty("mergedCreatureFacts", result.toString()); // Private guarded XML only.
}
