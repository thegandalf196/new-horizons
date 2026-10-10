/*
 * NewHorizonsGlyphsOfFearTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../battles/BattleTestFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/NewHorizonsGlyphsOfFear.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#endif

namespace
{
struct GlyphsPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	size_t fields = 0;
	bool hasFeature(Version feature) const { return feature != Version::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA; }
	template<class T> GlyphsPrefixProbe & operator&(T &) { ++fields; return *this; }
};

#ifdef ENABLE_BATTLE_AI
class GlyphsEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GlyphsEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsGlyphsOfFearTest : public BattleTestFixture
{
protected:
	bool legacy = false;
	bool original = false;
	int customRadius = 8;
	int customMorale = -1;
	CGTownInstance * fortress = nullptr;
	CGTownInstance * second = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		auto rules = original ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		if(!original)
		{
			ASSERT_TRUE(rules.Struct().contains("glyphsOfFearAura"));
			ASSERT_EQ(rules["glyphsOfFearAura"]["radius"].Integer(), 8);
			ASSERT_EQ(rules["glyphsOfFearAura"]["morale"].Integer(), -1);
			rules["glyphsOfFearAura"]["radius"].Integer() = customRadius;
			rules["glyphsOfFearAura"]["morale"].Integer() = customMorale;
			if(legacy)
				rules.Struct().erase("glyphsOfFearAura");
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			original ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}
	void prepare(bool built = true, int3 heroTile = {26, 18, 0})
	{
		const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, true).name("GlyphsFearAura")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero(heroTile, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 1}})
			.hero({28, 18, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 1}})
			.town({18, 18, 0}, FactionID::FORTRESS, PlayerColor(1)).townGarrison({})
			.town({18, 24, 0}, FactionID::FORTRESS, PlayerColor(1)).townGarrison({});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt(heroTile);
		defenderSideHero = findHeroAt({28, 18, 0});
		fortress = expectAt<CGTownInstance>({18, 18, 0});
		second = expectAt<CGTownInstance>({18, 24, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(fortress, nullptr);
		ASSERT_NE(second, nullptr);
		if(heroTile == int3(26, 18, 0))
		{
			// The town anchor is not its visitable aura center.
			const auto edge = fortress->visitablePos() + int3(8, 0, 0);
			map()->getTile(edge).terrainType = ETerrainId::GRASS;
			map()->getTile(edge + int3(1, 0, 0)).terrainType = ETerrainId::GRASS;
			ChangeObjPos relocate;
			relocate.objid = attackerSideHero->id;
			// ChangeObjPos takes a visitable tile; its visitor adds the anchor offset.
			relocate.nPos = edge;
			relocate.initiator = attackerSideHero->getOwner();
			gameHandler->sendAndApply(relocate);
			ASSERT_EQ(attackerSideHero->visitablePos(), edge);
			ASSERT_EQ(attackerSideHero->visitablePos() - fortress->visitablePos(), int3(8, 0, 0));
		}
		if(built)
			ASSERT_TRUE(gameHandler->buildStructure(fortress->id, BuildingID::SPECIAL_3, true));
	}
	void combatAt(int3 tile)
	{
		BattleSideArray<const CGHeroInstance *> heroes = {attackerSideHero, defenderSideHero};
		BattleSideArray<const CArmedInstance *> armies = {attackerSideHero, defenderSideHero};
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		const BattleField field(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),
			"battlefield", std::string("core:sand_shore")));
		BattleStart start;
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, field, armies, heroes, layout, nullptr);
		start.battleID = BattleID(0);
		gameHandler->sendAndApply(start);
		ASSERT_NE(battle(), nullptr);
		battle()->obstacles.clear();
	}
	int auraAt(int3 tile) const
	{
		return newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero, tile)->totalValue();
	}
};
}

TEST_F(NewHorizonsGlyphsOfFearTest, InclusiveEuclideanRadiusAndSameLevelUseVisitableTownTile)
{
	prepare();
	const auto center = fortress->visitablePos();
	EXPECT_EQ(auraAt(center + int3(8, 0, 0)), -1);
	EXPECT_EQ(auraAt(center + int3(9, 0, 0)), 0);
	EXPECT_EQ(auraAt(center + int3(5, 6, 0)), -1);
	EXPECT_EQ(auraAt(center + int3(6, 6, 0)), 0);
	EXPECT_EQ(auraAt(center + int3(0, 0, 1)), 0);
}

TEST_F(NewHorizonsGlyphsOfFearTest, ConfigurableCapturedRadiusAndPenaltyControlActualQuery)
{
	customRadius = 3;
	customMorale = -2;
	prepare();
	const auto center = fortress->visitablePos();
	EXPECT_EQ(auraAt(center + int3(3, 0, 0)), -2);
	EXPECT_EQ(auraAt(center + int3(4, 0, 0)), 0);
	EXPECT_EQ(newHorizonsHeroes::capabilityGlyphsOfFearAura(
		attackerSideHero->getCapabilityRules())->radius, 3);
}

TEST_F(NewHorizonsGlyphsOfFearTest, ActualBuildImmediatelyChangesHeroAndStrategicCreatureMorale)
{
	prepare(false);
	const int heroBefore = attackerSideHero->moraleVal();
	const int creatureBefore = attackerSideHero->getStackPtr(SlotID(0))->moraleVal();
	ASSERT_TRUE(gameHandler->buildStructure(fortress->id, BuildingID::SPECIAL_3, true));
	EXPECT_EQ(attackerSideHero->moraleVal(), heroBefore - 1);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->moraleVal(), creatureBefore - 1);
	EXPECT_EQ(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->size(), 1u);
}

TEST_F(NewHorizonsGlyphsOfFearTest, ActualMoveOutAndBackUpdatesWithoutStoredBonuses)
{
	prepare();
	gameHandler->onNewTurn();
	const auto inside = attackerSideHero->visitablePos();
	const auto outside = inside + int3(1, 0, 0);
	const int baseline = attackerSideHero->moraleVal();
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(outside),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->moraleVal(), baseline + 1);
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->empty());
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(inside),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->moraleVal(), baseline);
	for(const auto & bonus : attackerSideHero->getExportedBonusList())
		EXPECT_NE(bonus->source, BonusSource::TOWN_STRUCTURE);
}

TEST_F(NewHorizonsGlyphsOfFearTest, ActualOwnershipChangeRemovesAndRestoresHostileAura)
{
	prepare();
	EXPECT_EQ(auraAt(attackerSideHero->visitablePos()), -1);
	gameHandler->setOwner(fortress, PlayerColor(0));
	EXPECT_EQ(auraAt(attackerSideHero->visitablePos()), 0);
	gameHandler->setOwner(fortress, PlayerColor(1));
	EXPECT_EQ(auraAt(attackerSideHero->visitablePos()), -1);
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(defenderSideHero)->empty());
}

TEST_F(NewHorizonsGlyphsOfFearTest, AlliedDifferentOwnerDoesNotCountAsEnemy)
{
	prepare();
	const auto team = gameState()->players.at(PlayerColor(0)).team;
	gameState()->teams.at(team).players.insert(PlayerColor(1));
	gameState()->players.at(PlayerColor(1)).team = team;
	ASSERT_EQ(gameState()->getPlayerRelations(PlayerColor(0), PlayerColor(1)), PlayerRelations::ALLIES);
	EXPECT_EQ(auraAt(attackerSideHero->visitablePos()), 0);
}

TEST_F(NewHorizonsGlyphsOfFearTest, DistinctFortressesStackOnceEachWithinExistingFinalCaps)
{
	prepare(true, {24, 21, 0});
	ASSERT_TRUE(gameHandler->buildStructure(second->id, BuildingID::SPECIAL_3, true));
	const auto bonuses = newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero);
	ASSERT_EQ(bonuses->size(), 2u);
	EXPECT_EQ(bonuses->totalValue(), -2);
	EXPECT_NE((*bonuses)[0]->sid, (*bonuses)[1]->sid);
	auto negative = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::OTHER, -100, BonusSourceID());
	attackerSideHero->addNewBonus(negative);
	const auto limits = newHorizonsMagic::moraleLimits(attackerSideHero->getMagicRules());
	ASSERT_TRUE(limits.has_value());
	EXPECT_EQ(attackerSideHero->moraleVal(), limits->first);
}

TEST_F(NewHorizonsGlyphsOfFearTest, UndeadImmunityAndPositiveMoraleFloorRemainAuthoritative)
{
	prepare();
	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 1));
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->moraleVal(), 0);
	auto floor = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MINIMUM_MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	floor->valType = BonusValueType::INDEPENDENT_MAX;
	attackerSideHero->addNewBonus(floor);
	EXPECT_GE(attackerSideHero->moraleVal(), 0);
}

TEST_F(NewHorizonsGlyphsOfFearTest, BattleUsesSavedAdventureLocationNotStationaryHeroOrigin)
{
	prepare();
	ASSERT_EQ(auraAt(attackerSideHero->visitablePos()), -1);
	combatAt(fortress->visitablePos() + int3(9, 0, 0));
	CPlayerBattleCallback view(battle(), PlayerColor(0));
	const auto * unit = battle()->getStack(0);
	ASSERT_NE(unit, nullptr);
	EXPECT_EQ(view.battleGetMorale(unit), unit->moraleVal());
	EXPECT_EQ(auraAt(battle()->getLocation()), 0);
}

TEST_F(NewHorizonsGlyphsOfFearTest, ActualBattleMoraleReceivesAuraAndNativeSiegeDefenseDataIsUnchanged)
{
	prepare();
	combatAt(attackerSideHero->visitablePos());
	CPlayerBattleCallback view(battle(), PlayerColor(0));
	const auto * unit = battle()->getStack(0);
	ASSERT_NE(unit, nullptr);
	EXPECT_EQ(view.battleGetMorale(unit), unit->moraleValWithBonus(-1));
	const auto & bonuses = fortress->getTown()->buildings.at(BuildingID::SPECIAL_3)->defendingHeroBonuses;
	EXPECT_EQ(bonuses.valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE))), 20);
}

TEST_F(NewHorizonsGlyphsOfFearTest, CapturedLegacyAndReinitNeverAdoptCurrentAura)
{
	legacy = true;
	prepare();
	EXPECT_FALSE(newHorizonsHeroes::capabilityGlyphsOfFearAura(attackerSideHero->getCapabilityRules()));
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->empty());
	attackerSideHero->initHero(*gameHandler->randomizer);
	EXPECT_FALSE(newHorizonsHeroes::capabilityGlyphsOfFearAura(attackerSideHero->getCapabilityRules()));
}

TEST_F(NewHorizonsGlyphsOfFearTest, OriginalCapabilityContextRetainsNoAura)
{
	original = true;
	prepare();
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->empty());
}

TEST_F(NewHorizonsGlyphsOfFearTest, CurrentCapturedHeroRoundTripAndOldOuterPrefixes)
{
	prepare();
	const auto saved = attackerSideHero->getCapabilityRules()["glyphsOfFearAura"];
	CMemorySerializer wire;
	wire.oser & *attackerSideHero;
	wire.iser.cb = gameState().get();
	CGHeroInstance restored(gameState().get());
	wire.iser & restored;
	EXPECT_EQ(restored.getCapabilityRules()["glyphsOfFearAura"], saved);
	GlyphsPrefixProbe heroWriter, mapWriter, worldWriter, lobbyWriter;
	EXPECT_THROW(attackerSideHero->serialize(heroWriter), std::runtime_error);
	EXPECT_EQ(heroWriter.fields, 0u);
	EXPECT_THROW(map()->serialize(mapWriter), std::runtime_error);
	EXPECT_EQ(mapWriter.fields, 0u);
	EXPECT_THROW(gameState()->serialize(worldWriter), std::runtime_error);
	EXPECT_EQ(worldWriter.fields, 0u);
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	EXPECT_THROW(lobby.serialize(lobbyWriter), std::runtime_error);
	EXPECT_EQ(lobbyWriter.fields, 0u);
}

TEST_F(NewHorizonsGlyphsOfFearTest, RawSettingsPresenceAndMalformedAuraRejectBeforeInstallation)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
	ASSERT_TRUE(rules.Struct().contains("glyphsOfFearAura"));
	for(int corruption = 0; corruption < 4; ++corruption)
	{
		auto invalid = rules;
		if(corruption == 0) invalid["glyphsOfFearAura"] = JsonNode();
		if(corruption == 1) invalid["glyphsOfFearAura"]["radius"].Integer() = -1;
		if(corruption == 2) invalid["glyphsOfFearAura"]["morale"].Integer() = 1;
		if(corruption == 3) invalid["glyphsOfFearAura"]["radius"].Float() = 8.5;
		EXPECT_THROW(newHorizonsHeroes::validateCapabilityRules(invalid, false), std::runtime_error);
	}
	const auto previous = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA) - 1);
	JsonNode overrides;
	overrides["heroes"]["newHorizonsCapabilities"] = rules;
	CMemorySerializer old;
	old.oser.version = previous;
	old.iser.version = previous;
	old.oser & overrides;
	GameSettings settings;
	EXPECT_THROW(settings.serialize(old.iser), std::runtime_error);
	CMemorySerializer unchanged;
	ASSERT_NO_THROW(settings.serialize(unchanged.oser));
	JsonNode emitted;
	ASSERT_NO_THROW(unchanged.iser & emitted);
	EXPECT_TRUE(emitted["heroes"]["newHorizonsCapabilities"].isNull());
	auto absent = rules;
	absent.Struct().erase("glyphsOfFearAura");
	EXPECT_NO_THROW(newHorizonsHeroes::validateGlyphsOfFearSerialization(absent, false));
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsGlyphsOfFearTest, DetachedBranchesUseSameBattleAuraAndDoNotCreatePersistentState)
{
	prepare();
	combatAt(attackerSideHero->visitablePos());
	const auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	GlyphsEnvironment environment(gameState());
	HypotheticBattle parent(&environment, live);
	const auto parentView = std::make_shared<CPlayerBattleCallback>(&parent, PlayerColor(0));
	HypotheticBattle child(&environment, parentView);
	HypotheticBattle sibling(&environment, parentView);
	EXPECT_EQ(parent.getLocation(), battle()->getLocation());
	EXPECT_EQ(child.getLocation(), battle()->getLocation());
	EXPECT_EQ(sibling.getLocation(), battle()->getLocation());
	const auto * actual = battle()->getStack(0);
	ASSERT_NE(actual, nullptr);
	const int expected = live->battleGetMorale(actual);
	EXPECT_EQ(parent.battleGetMorale(parent.battleGetUnitByID(actual->unitId())), expected);
	EXPECT_EQ(child.battleGetMorale(child.battleGetUnitByID(actual->unitId())), expected);
	EXPECT_EQ(sibling.battleGetMorale(sibling.battleGetUnitByID(actual->unitId())), expected);
	EXPECT_EQ(expected, actual->moraleValWithBonus(-1));
	EXPECT_EQ(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->size(), 1u);
	// Aura evaluation stays dynamic; even an owned child unit has no stored aura.
	const auto originalBonuses = actual->getBonusesOfType(BonusType::MORALE)->size();
	auto childUnit = child.getForUpdate(actual->unitId());
	ASSERT_NE(childUnit, nullptr);
	EXPECT_EQ(child.battleGetMorale(childUnit.get()), expected);
	EXPECT_EQ(childUnit->getBonusesOfType(BonusType::MORALE)->size(), originalBonuses);
	EXPECT_EQ(parent.battleGetMorale(parent.battleGetUnitByID(actual->unitId())), expected);
	EXPECT_EQ(sibling.battleGetMorale(sibling.battleGetUnitByID(actual->unitId())), expected);
	EXPECT_EQ(live->battleGetMorale(actual), expected);
	EXPECT_EQ(actual->getBonusesOfType(BonusType::MORALE)->size(), originalBonuses);
}
#endif
