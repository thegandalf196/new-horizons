/*
 * NewHorizonsNavigationSuccessorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#ifdef ENABLE_NULLKILLER2_AI
#include "../../AI/Nullkiller2/Pathfinding/Actors.h"
#endif

namespace
{
constexpr auto parent = "new-horizons:logistics";
constexpr auto navigation = "new-horizons:logistics.navigation";
struct OldNavigationPrefix
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version version) const { return version < Version::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS; }
	template<typename T> OldNavigationPrefix & operator&(T &)
	{
		++fields; throw std::runtime_error("Unexpected old Navigation prefix");
	}
};

class NewHorizonsNavigationSuccessorTest : public TinyMapGameTest
{
protected:
	bool absent = false;
	bool disabled = false;
	bool legacy = false;
	bool preset = false;
	bool presetLogistics = false;
	CGHeroInstance * hero = nullptr;
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		ASSERT_TRUE(newHorizonsHeroes::usesNavigationStartReplacement(rules)) << "Requires shipped captured Navigation successor profile";
		ASSERT_TRUE(rules["startingSkills"]["startingDevelopmentProfiles"].Struct().contains("core:sylvia"));
		ASSERT_TRUE(rules["startingSkills"]["startingDevelopmentProfiles"].Struct().contains("core:voy"));
		rules.Struct().erase("defaultCreatureLineReplacements");
		if(absent) rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		if(disabled) rules["skillSpecialties"]["navigationStartReplacements"].Bool() = false;
		if(legacy) rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(legacy) loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}
	void prepare(const char * name = "core:sylvia")
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(name)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1}});
		if(presetLogistics) builder.heroSecondarySkills({{SecondarySkill::LOGISTICS, MasteryLevel::BASIC}});
		else if(preset) builder.heroSecondarySkills({{SecondarySkill::OFFENCE, MasteryLevel::ADVANCED}})
			.heroSpells({SpellID::MAGIC_ARROW});
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		ASSERT_NE(actor(), nullptr);
	}
	CGHeroInstance * actor() const { return hero; }
	int markerCount(const CGHeroInstance & hero) const
	{
		const auto identity = "new-horizons:skill-specialty:" + std::to_string(hero.getHeroTypeID().getNum())
			+ ":" + std::to_string(SecondarySkill::LOGISTICS);
		return static_cast<int>(std::ranges::count_if(hero.getExportedBonusList(), [&identity](const auto & bonus)
			{ return bonus && bonus->stacking == identity; }));
	}
	void expectProducer(const CGHeroInstance & hero, bool converted)
	{
		const auto original = hero.getHeroType()->navigationSpecialtyProducer;
		ASSERT_NE(original, nullptr); ASSERT_NE(original->updater, nullptr);
		EXPECT_TRUE(original->stacking.empty());
		const auto & local = hero.getExportedBonusList();
		if(!converted)
		{
			EXPECT_NE(std::ranges::find(local, original), local.end());
			EXPECT_EQ(markerCount(hero), 0);
			return;
		}
		EXPECT_EQ(std::ranges::find(local, original), local.end());
		const auto inert = "new-horizons:navigation-specialty-inert:" + std::to_string(hero.getHeroTypeID().getNum());
		const auto count = std::ranges::count_if(local, [&inert](const auto & bonus)
		{
			return bonus && bonus->stacking == inert && bonus->val == 0 && !bonus->updater;
		});
		EXPECT_EQ(count, 1); EXPECT_EQ(markerCount(hero), 1);
	}
};

class BothNavigationSuccessors : public NewHorizonsNavigationSuccessorTest,
	public ::testing::WithParamInterface<const char *> {};

TEST_P(BothNavigationSuccessors, FreshDefaultInstallsExactParentOwnFactionAndBasicNavigationWithoutChangingClass)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->secSkills.size(), 2);
	EXPECT_EQ(actor()->getPerkSkillRank(parent), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkSkillRank(std::string(GetParam()) == "core:sylvia"
		? "new-horizons:divineMandate" : "new-horizons:bulwarkOfTheMire"), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkState().selected, (std::vector<newHorizonsHeroes::PerkSelection>{{parent, navigation}}));
	EXPECT_TRUE(actor()->hasActivePerk(parent, navigation));
	EXPECT_EQ(actor()->getHeroClass(), actor()->getHeroType()->heroClass);
	EXPECT_EQ(actor()->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)), 20);
	expectProducer(*actor(), true);
}

TEST_P(BothNavigationSuccessors, PresetDevelopmentAndBookStayExplicitButFreshSpecialtyHasNoUnlearnedCoreMovementToBoost)
{
	preset = true; ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->getPerkSkillRank(parent), 0);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:offense"), MasteryLevel::ADVANCED);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
	expectProducer(*actor(), true);
	const auto turn = actor()->getTurnInfo(0);
	EXPECT_EQ(turn->getMovePointsLimitLand(), 200); EXPECT_EQ(turn->getMovePointsLimitWater(), 200);
}

TEST_P(BothNavigationSuccessors, AbsentCapturedOptInKeepsExistingStartsAndNativeProducerWithoutRetroactiveReinit)
{
	absent = true; ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->getPerkSkillRank(parent), 0); EXPECT_TRUE(actor()->getPerkState().selected.empty());
	EXPECT_EQ(actor()->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)), 0);
	expectProducer(*actor(), false);
	GameRandomizer randomizer(*gameState()); actor()->initHero(randomizer);
	EXPECT_EQ(markerCount(*actor()), 0); EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_P(BothNavigationSuccessors, AllLogisticsRanksScaleOnlyCoreLandAndSeaWhileNavigationStaysTwentyFivePercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	GameHandlerTestServer server(gameState()); CGameHandler handler(server, gameState());
	const SecondarySkill logistics(SecondarySkill::decode(parent));
	const std::array<int, 3> land{224, 248, 272};
	TurnInfoCache cache(actor());
	for(size_t rank = 0; rank < land.size(); ++rank)
	{
		handler.changeSecSkill(actor(), logistics, rank + MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		TurnInfo forecast(&cache, actor(), 0);
		EXPECT_EQ(forecast.getMovePointsLimitLand(), land[rank]);
		EXPECT_EQ(forecast.getMovePointsLimitWater(), land[rank] + 50);
	}
}

INSTANTIATE_TEST_SUITE_P(ExactHeroes, BothNavigationSuccessors, ::testing::Values("core:sylvia", "core:voy"),
	[](const auto & info) { return std::string(info.param).substr(5); });

TEST_F(NewHorizonsNavigationSuccessorTest, FalseOptInAndLegacyRosterDoNotConvert)
{
	disabled = true; ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank(parent), 0); expectProducer(*actor(), false);
}

TEST_F(NewHorizonsNavigationSuccessorTest, LegacyWorldRetainsAuthoredNavigationAndSharedPrototype)
{
	legacy = true; ASSERT_NO_FATAL_FAILURE(prepare("core:voy"));
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().selected.empty()); expectProducer(*actor(), false);
}

TEST_F(NewHorizonsNavigationSuccessorTest, SavedWorldAndRepeatedInitializationRetainOneMarkerOneInertCloneAndNoRepeatedPerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:voy"));
	CGameState restored; restored.preInit(LIBRARY); restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID()); ASSERT_NE(loaded, nullptr);
	expectProducer(*loaded, true);
	GameRandomizer randomizer(restored); loaded->initHero(randomizer); loaded->initHero(randomizer);
	expectProducer(*loaded, true);
	EXPECT_EQ(loaded->getPerkState().selected, (std::vector<newHorizonsHeroes::PerkSelection>{{parent, navigation}}));
}

TEST_F(NewHorizonsNavigationSuccessorTest, ActualLandEmbarkAndSeaMovementMatchSharedPathForecastWithoutReducingNavigationCostAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); revealMap(PlayerColor(0));
	GameHandlerTestServer server(gameState()); CGameHandler handler(server, gameState());
	auto & tile = map()->getTile(actor()->visitablePos() + int3(1, 0, 0));
	tile.terrainType = ETerrainId::GRASS; tile.roadType = RoadId::NO_ROAD;
	const auto land = actor()->visitablePos() + int3(1, 0, 0);
	handler.setMovePoints(actor()->id, 224);
	PathfinderCache landCache(gameState().get(), PathfinderOptions(*gameState()));
	const auto paths = landCache.getPathsInfo(actor()); const auto * step = paths->getNode(land, EPathfindingLayer::LAND);
	ASSERT_NE(step, nullptr); ASSERT_TRUE(step->reachable()); ASSERT_EQ(step->moveRemains, 214);
	ASSERT_TRUE(handler.moveHero(actor()->id, actor()->convertFromVisitablePos(land), EMovementMode::STANDARD,
		false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(actor()->movementPointsRemaining(), 214);
	const auto sea = land + int3(1, 0, 0), nextSea = sea + int3(1, 0, 0);
	for(const auto position : {sea, nextSea}) map()->getTile(position).terrainType = ETerrainId::WATER;
	handler.createBoat(sea, BoatId::CASTLE, PlayerColor(0)); handler.setMovePoints(actor()->id, 224);
	CPathfinderHelper boarding(*gameState(), actor(), PathfinderOptions(*gameState()));
	ASSERT_TRUE(boarding.getTurnInfo()->hasNewHorizonsNavigation());
	EXPECT_EQ(boarding.getMovementCost(land, sea, EPathfindingLayer::SAIL, 224, false), 5);
	PathfinderCache seaCache(gameState().get(), PathfinderOptions(*gameState()));
	const auto sailing = seaCache.getPathsInfo(actor()); const auto * embark = sailing->getNode(sea, EPathfindingLayer::SAIL);
	ASSERT_NE(embark, nullptr); ASSERT_TRUE(embark->reachable()); EXPECT_EQ(embark->moveRemains, 137);
	ASSERT_TRUE(handler.moveHero(actor()->id, actor()->convertFromVisitablePos(sea), EMovementMode::STANDARD,
		false, PlayerColor(0), EPathfindingLayer::SAIL));
	EXPECT_EQ(actor()->movementPointsRemaining(), embark->moveRemains);
	PathfinderCache aboardCache(gameState().get(), PathfinderOptions(*gameState()));
	const auto aboard = aboardCache.getPathsInfo(actor()); const auto * sail = aboard->getNode(nextSea, EPathfindingLayer::SAIL);
	ASSERT_NE(sail, nullptr); ASSERT_TRUE(sail->reachable()); ASSERT_EQ(sail->moveRemains, 127);
	ASSERT_TRUE(handler.moveHero(actor()->id, actor()->convertFromVisitablePos(nextSea), EMovementMode::STANDARD,
		false, PlayerColor(0), EPathfindingLayer::SAIL));
	EXPECT_EQ(actor()->movementPointsRemaining(), sail->moveRemains);
}

TEST_F(NewHorizonsNavigationSuccessorTest, UnrelatedMovementBonusAndForcedMarchStateAreNotSpecialized)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	actor()->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::heroMovementLand), BonusValueType::PERCENT_TO_BASE));
	const auto turn = actor()->getTurnInfo(0);
	EXPECT_EQ(turn->getMovePointsLimitLand(), 244); EXPECT_EQ(turn->getMovePointsLimitWater(), 274);
	EXPECT_EQ(actor()->getNewHorizonsForcedMarchLastUseDay(), -1);
	EXPECT_EQ(actor()->getNewHorizonsForcedMarchPenaltyDay(), -1);
}

TEST_F(NewHorizonsNavigationSuccessorTest, LegalForcedMarchKeepsItsTenPercentRecoveryWithoutAnotherSpecialtyMultiplier)
{
	presetLogistics = true; ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(actor()->getPerkSkillRank(parent), MasteryLevel::BASIC);
	ASSERT_TRUE(actor()->getPerkState().selected.empty());
	actor()->applyPerkSelection({parent, "new-horizons:logistics.forcedMarch"});
	ASSERT_TRUE(actor()->hasActivePerk(parent, "new-horizons:logistics.forcedMarch"));
	GameHandlerTestServer server(gameState()); CGameHandler handler(server, gameState());
	const auto destination = actor()->visitablePos() + int3(1, 0, 0);
	map()->getTile(destination).terrainType = ETerrainId::GRASS; map()->getTile(destination).roadType = RoadId::NO_ROAD;
	handler.setMovePoints(actor()->id, 10);
	ASSERT_EQ(actor()->movementPointsLimit(), 224);
	ASSERT_TRUE(handler.moveHero(actor()->id, actor()->convertFromVisitablePos(destination), EMovementMode::STANDARD,
		false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(actor()->movementPointsRemaining(), 22); // floor(224 / 10), not a second +20% boost.
	EXPECT_EQ(actor()->getNewHorizonsForcedMarchLastUseDay(), gameState()->getCalendar().getCurrentDay());
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(NewHorizonsNavigationSuccessorTest, AdventureAIArmyProjectionUsesSharedSpecializedBudgetsWithoutMutatingHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:voy"));
	const auto position = actor()->visitablePos();
	const auto movement = actor()->movementPointsRemaining();
	const auto selected = actor()->getPerkState().selected;
	NK2AI::ChainActor projected;
	projected.hero = actor(); projected.creatureSet = actor();
	EXPECT_EQ(projected.maxMovePoints(EPathfindingLayer::LAND), 224);
	EXPECT_EQ(projected.maxMovePoints(EPathfindingLayer::SAIL), 274);
	EXPECT_EQ(actor()->visitablePos(), position); EXPECT_EQ(actor()->movementPointsRemaining(), movement);
	EXPECT_EQ(actor()->getPerkState().selected, selected); expectProducer(*actor(), true);
}
#endif

TEST_F(NewHorizonsNavigationSuccessorTest, PresenceAndStrictBooleanGuardsDistinguishAbsentFalseAndNull)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
	rules.Struct().erase("defaultCreatureLineReplacements");
	EXPECT_NO_THROW(newHorizonsHeroes::validateNavigationStartSerialization(rules, false));
	rules["skillSpecialties"]["navigationStartReplacements"].Bool() = false;
	EXPECT_THROW(newHorizonsHeroes::validateNavigationStartSerialization(rules, false), std::runtime_error);
	rules["skillSpecialties"]["navigationStartReplacements"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(rules, true), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateNavigationStartSerialization(rules, false), std::runtime_error);
}

TEST_F(NewHorizonsNavigationSuccessorTest, InvalidOrInactiveProfileRejectsBeforeChangingAnyCapturedSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto selected = actor()->getPerkState().selected;
	auto rules = actor()->getPrimaryGrowthRules();
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:sylvia"]["startingPerks"].Vector().front()["perk"].String()
		= "new-horizons:logistics.roadmaster";
	EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(rules, actor()->getPerkState(),
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	auto inactive = actor()->getPerkState(); inactive.selected.clear();
	for(auto & perk : inactive.rules["skills"][parent]["perks"].Vector())
		if(perk["id"].String() == navigation) perk["effect"]["status"].String() = "planned";
	EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(actor()->getPrimaryGrowthRules(), inactive,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().selected, selected);
}

TEST_F(NewHorizonsNavigationSuccessorTest, ActiveAndFalseKeyRejectSettingsHeroMapWorldLobbyBeforeOldPrefixAndRawReaderRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto reject = [](auto & value)
	{
		OldNavigationPrefix old; EXPECT_THROW(value.serialize(old), std::runtime_error); EXPECT_EQ(old.fields, 0);
	};
	reject(*actor()); reject(gameState()->getMap()); reject(*gameState());
	LobbyStartGame lobby; lobby.initializedGameState = gameState(); reject(lobby);
	GameSettings settings; settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, actor()->getPrimaryGrowthRules()); reject(settings);
	auto falseRules = actor()->getPrimaryGrowthRules(); falseRules["skillSpecialties"]["navigationStartReplacements"].Bool() = false;
	GameSettings falseSettings; falseSettings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, falseRules); reject(falseSettings);
	JsonNode raw; raw["heroes"]["newHorizons"] = actor()->getPrimaryGrowthRules();
	CMemorySerializer bytes; bytes.oser & raw;
	bytes.iser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS) - 1);
	GameSettings decoded; EXPECT_THROW(decoded.serialize(bytes.iser), std::runtime_error);
}
}
