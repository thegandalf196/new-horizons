/*
 * CPlayerState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "CPlayerState.h"
#include "mapObjects/NewHorizonsEstatesPolicy.h"
#include "GameLibrary.h"
#include "callback/IGameInfoCallback.h"
#include "mapObjects/CGHeroInstance.h"
#include "mapObjects/CGTownInstance.h"
#include "gameState/CGameState.h"
#include "gameState/QuestInfo.h"
#include "texts/CGeneralTextHandler.h"
#include "json/JsonNode.h"

PlayerState::PlayerState(IGameInfoCallback *cb)
	: CBonusSystemNode(BonusNodeType::PLAYER)
	, GameCallbackHolder(cb)
	, color(-1)
	, human(false)
	, cheated(false)
	, playerLocalSettings(std::make_unique<JsonNode>())
	, enteredWinningCheatCode(false)
	, enteredLosingCheatCode(false)
	, status(EPlayerStatus::INGAME)
{
}

PlayerState::~PlayerState() = default;

std::string PlayerState::nodeName() const
{
	return "Player " + color.toString();
}

PlayerColor PlayerState::getId() const
{
	return color;
}

int32_t PlayerState::getIndex() const
{
	return color.getNum();
}

int32_t PlayerState::getIconIndex() const 
{
	return color.getNum();
}

std::string PlayerState::getJsonKey() const
{
	return color.toString();
}

std::string PlayerState::getModScope() const
{
	return "core";
}

std::string PlayerState::getNameTranslated() const
{
	return LIBRARY->generaltexth->translate(getNameTextID());
}

std::string PlayerState::getNameTextID() const
{
	return TextIdentifier("core.plcolors", color.getNum()).get();
}

void PlayerState::registerIcons(const IconRegistar & cb) const
{
	//We cannot register new icons for players
}

TeamID PlayerState::getTeam() const
{
	return team;
}

bool PlayerState::isHuman() const
{
	return human;
}

const IBonusBearer * PlayerState::getBonusBearer() const
{
	return this;
}

int PlayerState::getResourceAmount(int type) const
{
	return resources[type];
}

template<typename T>
std::vector<T> PlayerState::getObjectsOfType() const
{
	std::vector<T> result;
	for (const ObjectInstanceID & objectID : ownedObjects)
	{
		auto objectPtr = cb->gameState().getObjInstance(objectID);
		auto casted = dynamic_cast<T>(objectPtr);
		if (casted)
			result.push_back(casted);
	}
	return result;
}

std::vector<const CGHeroInstance *> PlayerState::getHeroes() const
{
	return getObjectsOfType<const CGHeroInstance *>();
}

std::vector<const CGTownInstance *> PlayerState::getTowns() const
{
	// optimized due to numerous AI access
	using T = const CGTownInstance *;
	std::vector<T> result;
	for (const ObjectInstanceID & objectID : ownedObjects)
	{
		const auto * objectPtr = cb->gameState().getObjInstance(objectID);
		if (objectPtr->ID != MapObjectID::TOWN)
			continue;

		assert(dynamic_cast<T>(objectPtr) != nullptr);
		auto casted = static_cast<T>(objectPtr);
		result.push_back(casted);
	}
	return result;
}

std::vector<CGHeroInstance *> PlayerState::getHeroes()
{
	return getObjectsOfType<CGHeroInstance *>();
}

std::vector<CGTownInstance *> PlayerState::getTowns()
{
	return getObjectsOfType<CGTownInstance *>();
}

std::vector<const CGObjectInstance *> PlayerState::getOwnedObjects() const
{
	return getObjectsOfType<const CGObjectInstance *>();
}

void PlayerState::addOwnedObject(CGObjectInstance * object)
{
	assert(object->asOwnable() != nullptr);
	ownedObjects.push_back(object->id);
	markObjectControlled(object->id);
}

void PlayerState::removeOwnedObject(CGObjectInstance * object)
{
	vstd::erase(ownedObjects, object->id);
	// Ownership/removal packets apply this on both replicas. An old appointment
	// cannot reactivate if its former steward or town is acquired again later.
	std::erase_if(estates.stewards, [object](const auto & entry)
	{
		return entry.first == object->id || entry.second == object->id;
	});
}

bool PlayerState::validStewardAssignments(const newHorizonsEstates::KingdomState & candidate) const
{
	const auto heroes = getHeroes();
	const auto towns = getTowns();
	for(const auto & [townID, heroID] : candidate.stewards)
	{
		const auto town = std::find_if(towns.begin(), towns.end(), [townID](const auto * value) { return value->id == townID; });
		const auto hero = std::find_if(heroes.begin(), heroes.end(), [heroID](const auto * value) { return value->id == heroID; });
		if(town == towns.end() || hero == heroes.end() || (*town)->getOwner() != color || (*hero)->getOwner() != color
			|| !newHorizonsEstates::hasRevisedPerk(*hero, "new-horizons:estates.stewardship"))
			return false;
	}
	return true;
}

void PlayerState::clearInvalidStewards()
{
	std::erase_if(estates.stewards, [this](const auto & entry)
	{
		newHorizonsEstates::KingdomState single;
		single.stewards.insert(entry);
		return !validStewardAssignments(single);
	});
}

void PlayerState::markObjectControlled(ObjectInstanceID objectID)
{
	if(objectID != ObjectInstanceID::NONE)
		everControlledObjects.insert(objectID);
}

bool PlayerState::hasEverControlled(ObjectInstanceID objectID) const
{
	return everControlledObjects.count(objectID) != 0;
}
bool PlayerState::wasKeymasterVisited(MapObjectSubID keymasterColor) const
{
	return visitedObjectsGlobal.count({Obj::KEYMASTER, keymasterColor}) != 0;
}


void PlayerState::validateOwnedBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported && context.passiveFactionSkillsSupported) return;
	validateExportedBonusSerialization(context);
	for(const auto & bonus : battleBonuses)
		bonus.validateAdjacencyCapabilitySerialization(context);
}
