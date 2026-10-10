/*
 * CMap.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "CMapEvent.h"
#include "../pathfinder/NewHorizonsLighthouse.h"
#include "CMapHeader.h"
#include "TerrainTile.h"
#include "MapTilesStorage.h"
#include <optional>

#include "../mapObjects/CGObjectInstance.h"
#include "../callback/GameCallbackHolder.h"
#include "../networkPacks/TradeItem.h"
#include "../scripting/IScriptVariablesHost.h"
#include "../scripting/ScriptVariablesStorage.h"

class CArtifactInstance;
class CArtifactSet;
class CGObjectInstance;
class CGHeroInstance;
class CGHeroPlaceholder;
class CCommanderInstance;
class CGameState;
class CGCreature;
class Quest;
class CGTownInstance;
class IModableArt;
class CInputStream;
class CMapEditManager;
class JsonSerializeFormat;
class IGameSettings;
class GameSettings;
struct TeleportChannel;
enum class EGameSettings;

/// The rumor struct consists of a rumor name and text.
struct DLL_LINKAGE Rumor
{
	std::string name;
	MetaString text;

	Rumor() = default;
	~Rumor() = default;

	template <typename Handler>
	void serialize(Handler & h)
	{
		h & name;
		h & text;
	}

	void serializeJson(JsonSerializeFormat & handler);
};

/// The map contains the map header, the tiles of the terrain, objects, heroes, towns, rumors...
class DLL_LINKAGE CMap : public CMapHeader, public GameCallbackHolder, public IScriptVariablesHost
{
	friend class CSerializer;

	std::unique_ptr<GameSettings> gameSettings;

	/// All artifacts that exists on map, whether on map, in hero inventory, or stored in some object
	std::vector<std::shared_ptr<CArtifactInstance>> artInstances;
	/// All heroes that are currently free for recruitment in taverns and are not present on map
	std::vector<std::shared_ptr<CGHeroInstance> > heroesPool;

	/// Precomputed indices of all towns on map
	std::vector<ObjectInstanceID> towns;

	/// Precomputed indices of all heroes on map. Does not includes heroes in prisons
	std::vector<ObjectInstanceID> heroesOnMap;

	void deserializeHeroPool(const std::vector<std::shared_ptr<CGHeroInstance> > &);

public:
	/// Central lists of items in game. Position of item in the vectors below is their (instance) id.
	/// TODO: make private
	std::vector<std::shared_ptr<CGObjectInstance>> objects;

	/// Live values of script variables owned by this map, namespaced by mod scope.
	ScriptVariablesStorage scriptVariables;
	/// Declarations (name, initial value, campaign flags) used to seed scriptVariables at game start.
	std::vector<ScriptVariableDefinition> scriptVariableDefinitions;
	/// Generated Lua source for the map's event scripts (empty if the map has no event system).
	std::string scriptSource;

	ScriptVariablesStorage & getScriptVariables() override { return scriptVariables; }
	const ScriptVariablesStorage & getScriptVariables() const override { return scriptVariables; }

	explicit CMap(IGameInfoCallback *cb);
	~CMap();
	void initTerrain();
	/// Appends a new, empty level filled with the layer type's default terrain.
	void addLevel(const MapLayerId & layerType);

	CMapEditManager * getEditManager();
	inline TerrainTile & getTile(const int3 & tile);
	inline const TerrainTile & getTile(const int3 & tile) const;
	bool isCoastalTile(const int3 & pos) const;
	inline bool isInTheMap(const int3 & pos) const;

	bool canMoveBetween(const int3 &src, const int3 &dst) const;
	bool checkForVisitableDir(const int3 & src, const TerrainTile * pom, const int3 & dst) const;
	int3 guardingCreaturePosition (int3 pos) const;

	void calculateGuardingGreaturePositions();
	void calculateGuardingGreaturePositions(int3 topleft, int3 bottomright);

	/// Creates instance of spell scroll artifact with provided spell
	CArtifactInstance * createScroll(const SpellID & spellId);

	/// Creates instance of requested artifact
	/// For combined artifact this method will also create alll required components
	/// For scrolls this method will also initialize its spell
	CArtifactInstance * createArtifact(const ArtifactID & artId, const SpellID & spellId = SpellID::NONE);

	/// Creates single instance of requested artifact
	/// Does NOT creates components for combined artifacts
	/// Does NOT initializes spell when spell scroll artifact is created
	CArtifactInstance * createArtifactComponent(const ArtifactID & artId);

	/// Returns pointer to requested Artifact Instance. Throws on invalid ID
	CArtifactInstance * getArtifactInstance(const ArtifactInstanceID & artifactID);
	/// Returns pointer to requested Artifact Instance. Throws on invalid ID
	const CArtifactInstance * getArtifactInstance(const ArtifactInstanceID & artifactID) const;

	/// Completely removes artifact instance from the game
	void eraseArtifactInstance(const ArtifactInstanceID art);

	void moveArtifactInstance(CArtifactSet & srcSet, const ArtifactPosition & srcSlot, CArtifactSet & dstSet, const ArtifactPosition & dstSlot);
	void putArtifactInstance(CArtifactSet & set, const ArtifactInstanceID art, const ArtifactPosition & slot);
	void removeArtifactInstance(CArtifactSet & set, const ArtifactPosition & slot);

	/// Generates unique string identifier for provided object instance
	void generateUniqueInstanceName(CGObjectInstance * target);

	/// Generates new, unique numeric identifier that can be used for creation of a new object
	ObjectInstanceID allocateUniqueInstanceID();

	/// Adds provided object to the map
	/// Throws on error, for example if object is already on map
	void addNewObject(std::shared_ptr<CGObjectInstance> obj);

	/// Moves anchor position of requested object to specified coordinates and updates map state
	/// Throws in invalid object instance ID
	void moveObject(ObjectInstanceID target, const int3 & dst);

	/// Clamps visitable x/y into the map and shifts anchor by the same delta.
	/// Returns true if object position was changed.
	bool adjustToMapBounds(CGObjectInstance * obj);

	/// Hides object from map without actually removing it from object list
	void hideObject(CGObjectInstance * obj);

	/// Shows previously hiiden object on map
	void showObject(CGObjectInstance * obj);

	/// Remove objects and shifts object indicies.
	/// Only for use in map editor / RMG
	std::shared_ptr<CGObjectInstance> removeObject(ObjectInstanceID oldObject);

	/// Replaced map object with specified ID with new object
	/// Old object must exist and will be removed from map
	/// Returns pointer to old object, which can be manipulated or dropped
	std::shared_ptr<CGObjectInstance> replaceObject(ObjectInstanceID oldObject, const std::shared_ptr<CGObjectInstance> & newObject);

	/// Erases object from map without shifting indices
	/// Returns pointer to old object, which can be manipulated or dropped
	std::shared_ptr<CGObjectInstance> eraseObject(ObjectInstanceID oldObject);

	bool isHeroOnMap(const ObjectInstanceID &heroId) const;
	void heroAddedToMap(const CGHeroInstance * hero);
	void heroRemovedFromMap(const CGHeroInstance * hero);
	void townAddedToMap(const CGTownInstance * town);
	void townRemovedFromMap(const CGTownInstance * town);

	/// Adds provided hero to map pool. Hero with same identity must not exist
	void addToHeroPool(std::shared_ptr<CGHeroInstance> hero);

	/// Attempts to take hero of specified identity from pool. Returns nullptr on failure
	/// Hero is removed from pool on success
	std::shared_ptr<CGHeroInstance> tryTakeFromHeroPool(HeroTypeID hero);

	/// Attempts to access hero of specified identity in pool. Returns nullptr on failure
	CGHeroInstance * tryGetFromHeroPool(HeroTypeID hero);

	/// Returns list of identities of heroes currently present in pool
	std::vector<HeroTypeID> getHeroesInPool() const;
	/// Validate all serialized hero receipts before an enclosing writer's prefix.
	void validateNewHorizonsProspectorSerialization(bool supported) const;
	const std::vector<int3> & getProtectedAdventureMobilityTiles() const;
	/// Authoring-only immutable scenario metadata; canonicalize before installation.
	void setProtectedAdventureMobilityTiles(std::vector<int3> tiles);
	void validateProtectedAdventureMobilitySerialization(bool supported, const JsonNode * capturedRules = nullptr) const;
	void validateNewHorizonsScholarSerialization(bool supported) const;
	void validateNewHorizonsRecruitersContactsSerialization(bool supported) const;
	void validateNewHorizonsLegendaryReputationSerialization(bool supported, int32_t currentMonth = -1) const;
	void validateRecruitmentTrainingSerialization(bool supported, bool cohortsSupported = true) const;
	void validateNewHorizonsSageSerialization(bool supported) const;
	void validateNewHorizonsMagnateSerialization(bool supported) const;
	void validateNewHorizonsHasteSpecialtySerialization(bool supported) const;
	void validateNewHorizonsThantReanimateSerialization(bool supported) const;
	void validateNewHorizonsFrailtySpecialtySerialization(bool supported) const;
	void validateNewHorizonsAenainFrailtySpecialtySerialization(bool supported) const;
	void validateNewHorizonsDefensiveStartSpecialtySerialization(bool supported) const;
	void validateNewHorizonsOffensiveStartSpecialtySerialization(bool supported) const;
	void validateNewHorizonsRemainingSpellSpecialtySerialization(bool supported) const;
	void validateNewHorizonsLighthouseSerialization(bool supported) const;
	void validateNewHorizonsArtifactManaRegenerationSerialization(bool supported) const;
	void validateNewHorizonsGlyphsOfFearSerialization(bool supported) const;
	void validateNewHorizonsStartingDevelopmentSerialization(bool supported) const;
	void validateNavigationStartSerialization(bool supported) const;
	void validateDefaultCreatureLineSerialization(bool supported) const;
	void validateNewHorizonsStartingBookSerialization(bool supported) const;
	void validateNewHorizonsRemainingStartSerialization(bool supported) const;
	void validateNewHorizonsWaterWalkDayEndSerialization(bool supported) const;
	void validateCanonicalSpellClausesSerialization(bool supported) const;
	void validateCombatScalarSerialization(bool finalLuckSupported, bool moraleSupported) const;
	void validateNewHorizonsCoroniusHolyWrathSerialization(bool supported) const;
	void validatePlagueRulesSerialization(bool supported) const;
	void validateCrisisCommandSerialization(bool supported) const;

	CGObjectInstance * getObject(ObjectInstanceID obj);
	const CGObjectInstance * getObject(ObjectInstanceID obj) const;

	void attachToBonusSystem(CGameState & gs);

	/// Returns all valid objects of specified class present on map
	template<typename ObjectType = CGObjectInstance>
	std::vector<const ObjectType *> getObjects() const
	{
		std::vector<const ObjectType *> result;
		for (const auto & object : objects)
		{
			auto casted = dynamic_cast<const ObjectType*>(object.get());
			if (casted)
				result.push_back(casted);
		}
		return result;
	}

	/// Returns all valid objects of specified class present on map
	template<typename ObjectType = CGObjectInstance>
	std::vector<ObjectType *> getObjects()
	{
		std::vector<ObjectType *> result;
		for (const auto & object : objects)
		{
			auto casted = dynamic_cast<ObjectType*>(object.get());
			if (casted)
				result.push_back(casted);
		}
		return result;
	}

	/// Returns all valid artifacts present on map
	std::vector<CArtifactInstance *> getArtifacts()
	{
		std::vector<CArtifactInstance *> result;
		for (const auto & art : artInstances)
			if (art)
				result.push_back(art.get());

		return result;
	}

	bool isWaterMap() const;
	bool calculateWaterContent();
	void banWaterArtifacts();
	void banHero(const HeroTypeID& id);
	void unbanHero(const HeroTypeID & id);
	void banWaterSpells();
	void banWaterSkills();
	void banWaterContent();

	/// Gets object of specified type on requested position
	const CGObjectInstance * getObjectiveObjectFrom(const int3 & pos, Obj type);
	const CGHeroPlaceholder * findHeroPlaceholder(const int3 & position) const;
	const CGHeroPlaceholder * isHeroPlaceholderObjective(const EventCondition & condition) const;

	/// Returns pointer to hero of specified type if hero is present on map
	CGHeroInstance * getHero(HeroTypeID heroId);
	const CGHeroInstance * getHero(HeroTypeID heroId) const;

	/// Returns ID's of all heroes that are currently present on map
	/// Includes all garrisoned and imprisoned heroes
	const std::vector<ObjectInstanceID> & getHeroesOnMap() const;

	/// Returns ID's of all towns present on map
	const std::vector<ObjectInstanceID> & getAllTowns() const;

	/// Sets the victory/loss condition objectives ??
	void resolveHeroPlaceholderObjectives();
	void checkForObjectives();

	void resolveQuestIdentifiers();

	void reindexObjects();

	std::vector<Rumor> rumors;
	std::set<SpellID> allowedSpells;
	std::set<ArtifactID> allowedArtifact;
	std::set<SecondarySkill> allowedAbilities;
	std::vector<CMapEvent> events;
	int3 grailPos;
	int grailRadius;

	//Helper lists
	std::map<TeleportChannelID, std::shared_ptr<TeleportChannel> > teleportChannels;

	std::unique_ptr<CMapEditManager> editManager;
	MapTilesStorage<int3> guardingCreaturePositions;

	std::map<std::string, std::shared_ptr<CGObjectInstance> > instanceNames;

	bool waterMap;

	ui8 obeliskCount = 0; //how many obelisks are on map
	std::map<TeamID, ui8> obelisksVisited; //map: team_id => how many obelisks has been visited

	std::vector<ArtifactID> townMerchantArtifacts;

	void overrideGameSettings(const JsonNode & input);
	void captureCombatScalarSettings();
	void overrideGameSetting(EGameSettings option, const JsonNode & input);
	const IGameSettings & getSettings() const;
	/// Copied authored magic override: absent and explicit null are distinct.
	std::optional<JsonNode> getMagicOverride() const;

	void parseUidCounter();
	static bool compareObjectBlitOrder(const CGObjectInstance * a, const CGObjectInstance * b);

private:
	std::vector<int3> protectedAdventureMobilityTiles;

	/// a 3-dimensional array of terrain tiles
	MapTilesStorage<TerrainTile> terrain;

	si32 uidCounter; 

public:
	template <typename Handler>
	void serialize(Handler &h)
	{
		if(h.saving)
			validateCombatScalarSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_FINAL_LUCK),
				h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE));
		if(h.saving)
			validateCanonicalSpellClausesSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_IMPLOSION));
		if(h.saving) validateCrisisCommandSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
		if(h.saving)
			validateProtectedAdventureMobilitySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS));
		if(h.saving)
			validatePlagueRulesSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER));
		if(h.saving)
			validateRecruitmentTrainingSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING), h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_COHORTS));
		if(h.saving)
			validateNewHorizonsFrailtySpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_FRAILTY_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsAenainFrailtySpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
		if(h.saving)
			validateNewHorizonsDefensiveStartSpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsOffensiveStartSpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsRemainingSpellSpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsArtifactManaRegenerationSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION));
		if(h.saving)
			validateNewHorizonsGlyphsOfFearSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA));
		if(h.saving)
			validateNewHorizonsLighthouseSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE));
		if(h.saving)
			validateNewHorizonsStartingDevelopmentSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
		if(h.saving)
			validateNewHorizonsRemainingStartSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
		if(h.saving)
			validateNewHorizonsWaterWalkDayEndSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_WATER_WALK_DAY_END));
		if(h.saving)
			validateNewHorizonsCoroniusHolyWrathSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
		if(h.saving)
			validateNewHorizonsStartingBookSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS));
		if(h.saving)
			validateNavigationStartSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS));
		if(h.saving)
			validateDefaultCreatureLineSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_DEFAULT_CREATURE_LINE_SUCCESSORS));
		if(h.saving)
			validateNewHorizonsThantReanimateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_THANT_REANIMATE));
		if(h.saving)
			validateNewHorizonsHasteSpecialtySerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsMagnateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGNATE));
		if(h.saving)
			validateNewHorizonsProspectorSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_PROSPECTOR));
		if(h.saving)
			validateNewHorizonsScholarSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_SCHOLAR));
		if(h.saving)
			validateNewHorizonsRecruitersContactsSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITERS_CONTACTS));
		if(h.saving)
			validateNewHorizonsLegendaryReputationSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_LEGENDARY_REPUTATION));
		if(h.saving)
			validateNewHorizonsSageSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_SAGE_GUILD_VISITS));
		h & static_cast<CMapHeader&>(*this);
		h & triggeredEvents; //from CMapHeader
		h & rumors;
		h & allowedSpells;
		h & allowedAbilities;
		h & allowedArtifact;
		h & events;
		h & grailPos;
		h & artInstances;

		if (h.saving)
			h & heroesPool;
		else
		{
			std::vector<std::shared_ptr<CGHeroInstance> > poolFromSave;
			h & poolFromSave;
			deserializeHeroPool(poolFromSave);
		}

		//TODO: viccondetails
		h & terrain;
		h & guardingCreaturePositions;

		h & objects;
		h & heroesOnMap;

		h & teleportChannels;
		h & towns;
		h & artInstances;

		// static members
		h & obeliskCount;
		h & obelisksVisited;
		h & townMerchantArtifacts;
		h & instanceNames;
		h & *gameSettings;
		h & uidCounter;

		if(h.hasFeature(Handler::Version::SCRIPT_VARIABLES))
		{
			h & scriptVariables;
			h & scriptVariableDefinitions;
			h & scriptSource;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS))
		{
			h & protectedAdventureMobilityTiles;
			if(!h.saving)
				validateProtectedAdventureMobilitySerialization(true);
		}
		else if(!h.saving)
			protectedAdventureMobilityTiles.clear();
	}
};

inline bool CMap::isInTheMap(const int3 & pos) const
{
	// Check whether coord < 0 is done implicitly. Negative signed int overflows to unsigned number larger than all signed ints.
	return
		static_cast<uint32_t>(pos.x) < static_cast<uint32_t>(width) &&
		static_cast<uint32_t>(pos.y) < static_cast<uint32_t>(height) &&
		static_cast<uint32_t>(pos.z) <= levels() - 1;
}

inline TerrainTile & CMap::getTile(const int3 & tile)
{
	assert(isInTheMap(tile));
	return terrain[tile];
}

inline const TerrainTile & CMap::getTile(const int3 & tile) const
{
	assert(isInTheMap(tile));
	return terrain[tile];
}
