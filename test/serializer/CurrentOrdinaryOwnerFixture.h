/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "CapturedSpecialtySnapshotFixture.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/PacksForLobby.h"

namespace currentOrdinaryOwnerTest
{
inline JsonNode leafRules()
{
	JsonNode result;
	result["additionalSpecialties"]["version"].Integer() = 1;
	auto & policy = result["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["version"].Integer() = 1;
	policy["marshSharpshooter"].Integer() = 1;
	for(const char * key : {"retaliationDamagePercent", "maximumHealthPercent", "resistanceAuraPoints"})
		policy[key].Integer() = 0;
	return result;
}
}

struct CurrentOrdinaryPolicyCase
{
	const char * hero;
	const char * key;
	const char * name;
	int value;
};
/// CURRENT complete owners; independent historical leaf codecs stay in their
/// own tests. A mandatory current Command/perk profile is never an old world.
class CurrentOrdinaryOwnerFixture : public HeroCommandFixture
{
	const CurrentOrdinaryPolicyCase * selected = nullptr;
	bool omit = false;
protected:
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(omit) rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepareCurrent(const CurrentOrdinaryPolicyCase & row, bool omitted = false)
	{
		selected = &row; omit = omitted;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(row.hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 7}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 7}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
	}
	void sameHero(const CGHeroInstance & source, const CGHeroInstance & restored)
	{
		EXPECT_EQ(restored.id, source.id);
		EXPECT_EQ(restored.getHeroTypeID(), source.getHeroTypeID());
		EXPECT_EQ(restored.getPrimaryGrowthRules(), source.getPrimaryGrowthRules());
		EXPECT_EQ(restored.secSkills, source.secSkills);
		EXPECT_EQ(restored.getExportedBonusList().toJsonNode(), source.getExportedBonusList().toJsonNode());
		EXPECT_EQ(restored.getSpecialtyPresentation().name, source.getSpecialtyPresentation().name);
		EXPECT_EQ(restored.getSpecialtyPresentation().description, source.getSpecialtyPresentation().description);
		EXPECT_EQ(restored.getSpecialtyPresentation().artwork, source.getSpecialtyPresentation().artwork);
	}
	void currentRoundtrips()
	{
		using capturedSpecialtySnapshotTest::encode;
		ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
		EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, selected->name);
		EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"][selected->key].Integer(), selected->value);
		CMemorySerializer heroWire(encode(*attackerSideHero));
		heroWire.iser.cb = gameState().get(); heroWire.iser.loadingGamestate = true;
		CGHeroInstance hero(gameState().get()); hero.serialize(heroWire.iser);
		sameHero(*attackerSideHero, hero);
		hero.initHero(*gameHandler->randomizer);
		sameHero(*attackerSideHero, hero);
		CMemorySerializer mapWire(encode(*map()));
		mapWire.iser.cb = gameState().get(); mapWire.iser.loadingGamestate = true;
		CMap restoredMap(gameState().get()); restoredMap.serialize(mapWire.iser);
		const auto * mapHero = restoredMap.getHero(attackerSideHero->getHeroTypeID());
		ASSERT_NE(mapHero, nullptr); sameHero(*attackerSideHero, *mapHero);
		CGameState world; world.preInit(LIBRARY); world.loadFromMemory(gameState()->saveToMemory());
		const auto * worldHero = world.getHero(attackerSideHero->id);
		ASSERT_NE(worldHero, nullptr); sameHero(*attackerSideHero, *worldHero);
		EXPECT_EQ(world.getHeroDevelopmentRules(), gameState()->getHeroDevelopmentRules());
		EXPECT_EQ(world.getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), map()->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
		const auto poolType = HeroTypeID(HeroTypeID::decode("core:valeska"));
		const auto * sourcePool = map()->tryGetFromHeroPool(poolType);
		const auto * restoredPool = world.getMap().tryGetFromHeroPool(poolType);
		ASSERT_NE(sourcePool, nullptr); ASSERT_NE(restoredPool, nullptr); sameHero(*sourcePool, *restoredPool);
		LobbyStartGame lobby; lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
		lobby.initializedGameState = gameState();
		CMemorySerializer lobbyWire(encode(lobby));
		LobbyStartGame restoredLobby; restoredLobby.serialize(lobbyWire.iser);
		ASSERT_NE(restoredLobby.initializedGameState, nullptr);
		const auto * lobbyHero = restoredLobby.initializedGameState->getHero(attackerSideHero->id);
		ASSERT_NE(lobbyHero, nullptr); sameHero(*attackerSideHero, *lobbyHero);
	}
	template<class Owner> void refusesOlderWriter(Owner & owner)
	{
		for(const auto epoch : {ESerializationVersion::NEW_HORIZONS_ADJACENCY_FILTERS,
			ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES,
			ESerializationVersion::NEW_HORIZONS_FACTION_SKILL_REVISIONS})
		{
			CMemorySerializer old; old.oser.version = epoch;
			EXPECT_THROW(owner.serialize(old.oser), std::runtime_error);
			EXPECT_TRUE(old.extractBuffer().empty());
		}
	}
	void currentOwnersRefuseOlderFormats()
	{
		refusesOlderWriter(*attackerSideHero); refusesOlderWriter(*map()); refusesOlderWriter(*gameState());
		GameSettings settings; settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS,
			map()->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
		refusesOlderWriter(settings);
		LobbyStartGame lobby; lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
		lobby.initializedGameState = gameState(); refusesOlderWriter(lobby);
		CGameState rejected; rejected.preInit(LIBRARY);
		const auto bytes = gameState()->saveToMemory();
		EXPECT_THROW(rejected.loadReplaySnapshot(ReplayPayload{bytes, ESerializationVersion::NEW_HORIZONS_FACTION_SKILL_REVISIONS}), std::runtime_error);
	}
	void malformedCurrentOwnersRejectWithoutChangingTheSource()
	{
		const auto original = attackerSideHero->getPrimaryGrowthRules();
		for(bool unknown : {false, true})
		{
			const char * key = unknown ? "unreservedNativeSpecialtyKey" : selected->key;
			auto invalid = original;
			invalid["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = unknown ? 0 : -1;
			const auto bytes = capturedSpecialtySnapshotTest::constructedSnapshot(*attackerSideHero, attackerSideHero->getPrimaryGrowthRules(), invalid);
			CMemorySerializer reader(bytes); reader.iser.cb = gameState().get(); reader.iser.loadingGamestate = true;
			CGHeroInstance discarded(gameState().get());
			EXPECT_THROW(discarded.serialize(reader.iser), std::runtime_error);
			auto worldInvalid = gameState()->getHeroDevelopmentRules();
			worldInvalid["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = unknown ? 0 : -1;
			const auto worldBytes = capturedSpecialtySnapshotTest::constructedSnapshot(*gameState(), gameState()->getHeroDevelopmentRules(), worldInvalid);
			CGameState rejected; rejected.preInit(LIBRARY);
			EXPECT_THROW(rejected.loadFromMemory(worldBytes), std::runtime_error);
			EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), original);
		}
	}
	void currentAuthoredOmissionSurvivesRoundtrip()
	{
		EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
		CGameState world; world.preInit(LIBRARY); world.loadFromMemory(gameState()->saveToMemory());
		const auto * hero = world.getHero(attackerSideHero->id);
		ASSERT_NE(hero, nullptr); sameHero(*attackerSideHero, *hero);
		EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero));
	}
};
