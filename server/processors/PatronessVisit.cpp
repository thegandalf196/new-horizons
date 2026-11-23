/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PatronessVisit.h"

#include "../CGameHandler.h"

#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/gameState/CGameState.h"

namespace newHorizonsPatroness
{
void collectOnArrival(CGameHandler & handler, const CGTownInstance & town, const CGHeroInstance & hero)
{
	const auto collection = newHorizonsHeroes::patronessCollection(hero, town);
	if(collection.empty())
		return;
	// The marker precedes the payment, so a reentrant visit cannot collect twice.
	GiveBonus marker(GiveBonus::ETarget::OBJECT, hero.id, newHorizonsHeroes::patronessMarker(hero, town));
	handler.giveHeroBonus(&marker);
	handler.giveResources(hero.getOwner(), collection);
	logGlobal->debug("Patroness: %s collects %s from %s", hero.getNameTextID(), collection.toString(), town.getNameTextID());
	InfoWindow info;
	info.player = hero.getOwner();
	info.type = EInfoWindowMode::AUTO;
	info.text.appendTextID("new-horizons.rampart.patroness.collect");
	info.text.replaceTextID(town.getNameTextID());
	for(const auto & resource : LIBRARY->resourceTypeHandler->getAllObjects())
		if(collection[resource] > 0)
			info.components.emplace_back(ComponentType::RESOURCE, resource, static_cast<int>(collection[resource]));
	handler.sendAndApply(info);
}

void collectAfterBattle(CGameHandler & handler, ObjectInstanceID object, ObjectInstanceID hero)
{
	// Look both up now: a defeated creature or a fallen hero no longer exists.
	const auto * town = dynamic_cast<const CGTownInstance *>(handler.gameState().getObjInstance(object));
	const auto * visitor = handler.gameState().getHero(hero);
	// Only a town her player now owns pays; the weekly marker prevents a second collection.
	if(town && visitor)
		collectOnArrival(handler, *town, *visitor);
}
}
