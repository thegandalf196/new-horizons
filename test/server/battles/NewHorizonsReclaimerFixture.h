/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <array>
#include <vector>
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/NewHorizonsRaisedArmy.h"
#include "../../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../mock/TinyH3MBuilder.h"
class ReclaimerRecordingServer : public RecordingGameServer
{
public:
	std::vector<BattleResultsApplied> results;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * result = dynamic_cast<BattleResultsApplied *>(&pack))
			results.push_back(*result);
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsReclaimerFixture : public BattleTestFixture
{
protected:
	ReclaimerRecordingServer recorded;
	CGTownInstance * nearTown = nullptr;
	CGTownInstance * farTown = nullptr;
	bool capturedPolicy = true;
	static CreatureID id(const char * value) { return CreatureID(CreatureID::decode(value)); }
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.Struct().erase("ghostSpecialty");
		rules.setOverrideFlag(true); // This fixture deliberately captures historical Vidomina.
		if(capturedPolicy)
		{
			auto & row = rules["reclamationSpecialty"];
			row["version"].Integer() = 1;
			row["conversionPercentPerLevel"].Integer() = 5;
			row["conversionMaximumPercent"].Integer() = 100;
		}
		else
		{
			rules.Struct().erase("reclamationSpecialty");
			rules.setOverrideFlag(true); // Capture absence instead of merging installed defaults back in.
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		TinyH3M::TinyH3MBuilder map(EMapFormat::SOD);
		map.size(36, false).name("Reclaimer")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:vidomina")), PlayerColor(0))
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1))
			.town({10, 10, 0}, FactionID::NECROPOLIS, PlayerColor(0)).townGarrison({})
			.town({20, 20, 0}, FactionID::NECROPOLIS, PlayerColor(0)).townGarrison({});
		startWithMap(std::move(map));
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		recorded.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(recorded, gameState());
		gameHandler->randomizer->setSeed(seed);
		for(auto * town : gameState()->getMap().getObjects<CGTownInstance>())
		{
			if(town->pos.x == 10) nearTown = town;
			if(town->pos.x == 20) farTown = town;
			town->removeAllBuildings();
			town->clearSlots();
		}
		ASSERT_NE(nearTown, nullptr);
		ASSERT_NE(farTown, nullptr);
		attackerSideHero->level = 20;
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
	}
	const PlayerState * owner() const { return gameState()->getPlayerState(PlayerColor(0)); }
	void dwelling(CGTownInstance & town, bool upgraded = false)
	{
		town.addBuilding(BuildingID::DWELL_LVL_2);
		if(upgraded) town.addBuilding(BuildingID::DWELL_LVL_2_UP);
		ASSERT_GE(town.creatures.size(), 2u);
		auto & pool = town.creatures[1];
		pool.first = 0; // Empty stock still unlocks the specialty.
		pool.second = {id("core:walkingDead")};
		if(upgraded) pool.second.push_back(id("core:zombie"));
	}
	void fullArmy(CArmedInstance & army, bool skeletonRoom)
	{
		army.clearSlots();
		const std::array<const char *, 7> creatures = {"core:skeleton", "core:pikeman", "core:archer",
			"core:swordsman", "core:griffin", "core:monk", "core:cavalier"};
		for(int slot = 0; slot < 7; ++slot)
		{
			int count = 1;
			if(slot == 0 && !skeletonRoom)
				if(const auto * hero = dynamic_cast<const CGHeroInstance *>(&army))
					count = hero->getLeadershipSlotCapacity(id("core:skeleton"))->maximum;
			ASSERT_TRUE(army.setCreature(SlotID(slot), id(creatures[slot]), count));
		}
	}
	newHorizonsNecromancy::PreparedBattleNecromancy plans(int living = 60) const
	{
		const auto ordinary = newHorizonsNecromancy::resolve(1, living, 0, false, false, false, true, true, 0, 100);
		return {ordinary, newHorizonsNecromancy::reclaimResidualSkeletons(ordinary, 100, 0, 100), true};
	}
	std::vector<std::byte> rngState()
	{
		CMemorySerializer wire;
		gameHandler->randomizer->serialize(wire.oser);
		dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(wire.oser);
		return wire.extractBuffer();
	}
};
