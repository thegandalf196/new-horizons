/*
 * NewHorizonsElementalTerrainFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"

/// Real battle setup with explicit captured context; the general fixture's
/// coastal sand_shore arena is unsuitable for inland-terrain assertions.
class NewHorizonsElementalTerrainFixture : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the production-active New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startTerrainBattle(TerrainId terrain, const std::string & arena = "core:grass_hills")
	{
		const int3 tile(4, 4, 0);
		gameState()->getMap().getTile(tile).terrainType = terrain;
		const BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", arena));
		BattleSideArray<const CGHeroInstance *> heroes = {attackerSideHero, defenderSideHero};
		BattleSideArray<const CArmedInstance *> armies = {attackerSideHero, defenderSideHero};
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		BattleStart start;
		start.battleID = BattleID(0);
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield,
			armies, heroes, layout, nullptr);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->tacticDistance = 0;
		battle()->obstacles.clear();
	}

	void expectRegisteredPerk(const std::string & id)
	{
		const JsonNode registry(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto & perk : registry["skills"]["new-horizons:elementalRebirth"]["perks"].Vector())
			if(perk["id"].String() == id)
			{
				ASSERT_EQ(perk["effect"]["status"].String(), "active");
				return;
			}
		FAIL() << "Missing shipped Rebirth perk " << id;
	}
};
