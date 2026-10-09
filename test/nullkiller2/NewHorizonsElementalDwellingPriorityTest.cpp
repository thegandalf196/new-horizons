/*
 * NewHorizonsElementalDwellingPriorityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "AI/Nullkiller2/Goals/BuildThis.h"
#include "lib/CCreatureHandler.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/callback/CCallback.h"
#include "lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "lib/entities/faction/CTown.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr std::array<const char *, 4> BASE_KEYS = {
	"core:airElemental", "core:waterElemental", "core:fireElemental", "core:earthElemental"
};

class NewHorizonsElementalDwellingPriorityTest : public NullkillerTest
{
protected:
	bool categoriesEnabled = true;
	CGTownInstance * town = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			categoriesEnabled ? JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")) : JsonNode());
	}

	void prepare(bool enabled = true)
	{
		categoriesEnabled = enabled;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsElementalDwellingPriority")
			.playerActive(PLAYER).town({5, 5, 0}, FactionID::CONFLUX, PLAYER).townGarrison({})
			.playerActive(PlayerColor(1)).town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
		town = dynamic_cast<CGTownInstance *>(findObjectAt({5, 5, 0}));
		ASSERT_NE(town, nullptr);
		// TinyH3M's default town samples starting-dwelling chances. These
		// controls evaluate a future build, so explicitly leave the four Elite
		// lines unbuilt before the AI analyzes their real prerequisite graph.
		for(int row = 1; row <= 4; ++row)
			for(int upgrade = 0; upgrade <= 1; ++upgrade)
			{
				const auto dwelling = BuildingID::getDwellingFromLevel(row, upgrade);
				town->removeBuilding(dwelling);
				ASSERT_FALSE(town->hasBuilt(dwelling));
			}
		town->addBuilding(BuildingID::MAGES_GUILD_1);
		for(int resource = 0; resource < 7; ++resource)
			grantResources(PLAYER, GameResID(resource), 100000);
		gateway = makeGateway(PLAYER);
	}

	NK2AI::BuildingInfo building(int row, bool upgraded = false)
	{
		const auto id = BuildingID::getDwellingFromLevel(row, upgraded ? 1 : 0);
		auto & ai = *gateway->nullkiller;
		return NK2AI::BuildAnalyzer::getBuildingOrPrerequisite(town, id, ai.armyManager, ai.cc);
	}

	NK2AI::EvaluationContext context(const NK2AI::BuildingInfo & info)
	{
		NK2AI::TownDevelopmentInfo development(town);
		return gateway->nullkiller->priorityEvaluator->buildEvaluationContext(
			NK2AI::Goals::sptr(NK2AI::Goals::BuildThis(info, development)));
	}
};
}

TEST_F(NewHorizonsElementalDwellingPriorityTest, FourIndependentEliteAltarsHaveEqualHintWithoutChangingArmyValue)
{
	prepare();
	ASSERT_NE(gateway, nullptr);
	std::set<uint64_t> strengths;
	for(int row = 1; row <= 4; ++row)
	{
		const auto info = building(row);
		ASSERT_EQ(info.creatureID.toCreature()->getJsonKey(), BASE_KEYS[row - 1]);
		ASSERT_EQ(info.creatureID, info.baseCreatureID);
		EXPECT_EQ(info.creatureLevel, row + 1) << "Keep legacy level for growth-row fallback";
		ASSERT_EQ(info.prerequisitesCount, 1);
		ASSERT_FALSE(info.isMissingResources);
		EXPECT_EQ(info.creatureGrowth, 4);
		EXPECT_EQ(info.buildCost[EGameResID::GOLD], 2500);
		const auto category = gateway->cc->getCreatureCategory(info.creatureID);
		ASSERT_TRUE(category);
		EXPECT_EQ(category->category, newHorizonsCreatures::CreatureCategory::ELITE);
		const auto value = context(info);
		EXPECT_FLOAT_EQ(value.strategicalValue, 0.7f);
		EXPECT_FLOAT_EQ(value.armyReward, info.armyStrength * 1.5f * town->getTownLevel());
		strengths.insert(info.armyStrength);
	}
	EXPECT_GT(strengths.size(), 1u) << "Equal category hints do not flatten actual creature strength";
}

TEST_F(NewHorizonsElementalDwellingPriorityTest, RealPrerequisiteCountStillDividesTheEqualCategoryHint)
{
	prepare();
	ASSERT_NE(gateway, nullptr);
	town->removeBuilding(BuildingID::MAGES_GUILD_1);
	ASSERT_FALSE(town->hasBuilt(BuildingID::MAGES_GUILD_1));
	for(int row = 1; row <= 4; ++row)
	{
		const auto info = building(row);
		ASSERT_EQ(info.id, BuildingID::MAGES_GUILD_1);
		ASSERT_EQ(info.prerequisitesCount, 2);
		ASSERT_FALSE(info.isMissingResources);
		EXPECT_FLOAT_EQ(context(info).strategicalValue, 0.35f);
	}
}

TEST_F(NewHorizonsElementalDwellingPriorityTest, AbsentSavedCategoriesKeepLegacyTierHints)
{
	prepare(false);
	ASSERT_NE(gateway, nullptr);
	for(int row = 1; row <= 4; ++row)
	{
		const auto info = building(row);
		EXPECT_FALSE(gateway->cc->getCreatureCategory(info.creatureID));
		EXPECT_FLOAT_EQ(context(info).strategicalValue, 0.5f + 0.1f * info.creatureLevel);
	}
}

TEST_F(NewHorizonsElementalDwellingPriorityTest, OtherConfluxLinesKeepTheirExistingHint)
{
	prepare();
	ASSERT_NE(gateway, nullptr);
	for(int row : {0, 5, 6})
	{
		// Isolate the presentation-independent hint from differing prerequisite
		// graphs; use the real row creature and its unmodified historical level.
		NK2AI::BuildingInfo info;
		info.id = BuildingID::getDwellingFromLevel(row, 0);
		info.creatureID = town->getTown()->creatures.at(row).front();
		info.baseCreatureID = info.creatureID;
		info.creatureLevel = info.creatureID.toCreature()->getLevel();
		info.prerequisitesCount = 1;
		EXPECT_FLOAT_EQ(context(info).strategicalValue, 0.5f + 0.1f * info.creatureLevel);
	}
}

TEST_F(NewHorizonsElementalDwellingPriorityTest, UpgradesKeepExistingActualUpgradeReward)
{
	prepare();
	ASSERT_NE(gateway, nullptr);
	for(int row = 1; row <= 4; ++row)
	{
		town->addBuilding(BuildingID::getDwellingFromLevel(row, 0));
		ASSERT_TRUE(town->setCreature(SlotID(row - 1),
			town->getTown()->creatures.at(row).front(), 10));
	}
	town->addBuilding(BuildingID::MAGES_GUILD_2);
	gateway->nullkiller->armyManager->update();
	for(int row = 1; row <= 4; ++row)
	{
		const auto info = building(row, true);
		ASSERT_NE(info.creatureID, info.baseCreatureID);
		ASSERT_EQ(info.prerequisitesCount, 1);
		ASSERT_FALSE(info.isMissingResources);
		const NK2AI::RewardEvaluator rewards(gateway->nullkiller.get());
		ASSERT_GT(rewards.getUpgradeArmyReward(town, info), 0u);
		EXPECT_FLOAT_EQ(context(info).strategicalValue,
			std::min(2.0f, rewards.getUpgradeArmyReward(town, info) / 10000.0f));
	}
}
