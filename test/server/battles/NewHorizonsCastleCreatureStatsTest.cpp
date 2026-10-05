/*
 * NewHorizonsCastleCreatureStatsTest.cpp, part of VCMI engine
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
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"

#include <algorithm>
#include <array>
#include <string>

namespace
{
CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

struct ExpectedCastleCreature
{
	CreatureID id;
	int attack;
	int defense;
	int damageMin;
	int damageMax;
	int hitPoints;
	int speed;
	int initiative;
	int growth;
	int gold;
	int leadership;
	newHorizonsCreatures::CreatureCategory category;
};

class NewHorizonsCastleCreatureStatsTest : public TinyMapGameTest
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
		builder.size(36, false).name("NewHorizonsCastleCreatureStats")
			.playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};
}

TEST_F(NewHorizonsCastleCreatureStatsTest, LoadedCastleFormsUseCanonicalStatsEconomyCategoriesAndAbilities)
{
	ASSERT_NO_FATAL_FAILURE(startTestMap());

	const std::array<ExpectedCastleCreature, 4> expected = {{
		{creature("core:swordsman"), 8, 10, 5, 7, 30, 5, 5, 7, 250, 150,
			newHorizonsCreatures::CreatureCategory::CORE},
		{creature("core:crusader"), 10, 11, 6, 8, 35, 6, 6, 7, 350, 180,
			newHorizonsCreatures::CreatureCategory::CORE},
		{creature("core:griffin"), 9, 9, 4, 7, 30, 7, 7, 5, 300, 220,
			newHorizonsCreatures::CreatureCategory::ELITE},
		{creature("core:royalGriffin"), 11, 11, 6, 9, 40, 9, 9, 5, 450, 260,
			newHorizonsCreatures::CreatureCategory::ELITE}
	}};

	for(const auto & row : expected)
	{
		ASSERT_NE(row.id, CreatureID::NONE);
		const auto * definition = LIBRARY->creh->getById(row.id);
		ASSERT_NE(definition, nullptr);

		EXPECT_EQ(definition->getBaseAttack(), row.attack);
		EXPECT_EQ(definition->getBaseDefense(), row.defense);
		EXPECT_EQ(definition->getBaseDamageMin(), row.damageMin);
		EXPECT_EQ(definition->getBaseDamageMax(), row.damageMax);
		EXPECT_EQ(definition->getBaseHitPoints(), row.hitPoints);
		EXPECT_EQ(definition->getBaseSpeed(), row.speed);
		EXPECT_EQ(definition->getBaseInitiative(), row.initiative);
		EXPECT_EQ(gameState()->getCreatureBaseGrowth(row.id), row.growth);
		EXPECT_EQ(definition->getRecruitCost(GameResID(EGameResID::GOLD)), row.gold);
		EXPECT_EQ(newHorizonsHeroes::capabilityCreatureLeadershipRequirement(
			gameState()->getHeroCapabilityRules(), row.id), row.leadership);

		const auto category = gameState()->getCreatureCategory(row.id);
		ASSERT_TRUE(category);
		EXPECT_EQ(category->category, row.category);
	}

	const auto swordsmanId = creature("core:swordsman");
	const auto crusaderId = creature("core:crusader");
	const auto griffinId = creature("core:griffin");
	const auto royalGriffinId = creature("core:royalGriffin");
	const auto * swordsman = LIBRARY->creh->getById(swordsmanId);
	const auto * crusader = LIBRARY->creh->getById(crusaderId);
	const auto * griffin = LIBRARY->creh->getById(griffinId);
	const auto * royalGriffin = LIBRARY->creh->getById(royalGriffinId);
	const auto * swordsmanType = swordsmanId.toCreature();
	const auto * griffinType = griffinId.toCreature();
	ASSERT_NE(swordsman, nullptr);
	ASSERT_NE(crusader, nullptr);
	ASSERT_NE(griffin, nullptr);
	ASSERT_NE(royalGriffin, nullptr);
	ASSERT_NE(swordsmanType, nullptr);
	ASSERT_NE(griffinType, nullptr);

	EXPECT_TRUE(swordsmanType->upgrades.contains(crusaderId));
	EXPECT_TRUE(griffinType->upgrades.contains(royalGriffinId));
	EXPECT_EQ(crusader->getBonusBearer()->valOfBonuses(BonusType::ADDITIONAL_ATTACK), 1);
	EXPECT_EQ(griffin->getBonusBearer()->valOfBonuses(BonusType::ADDITIONAL_RETALIATION), 1);
	EXPECT_TRUE(royalGriffin->getBonusBearer()->hasBonus(Selector::type()(BonusType::UNLIMITED_RETALIATIONS)));
}
