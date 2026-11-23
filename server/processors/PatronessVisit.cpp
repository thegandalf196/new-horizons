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
	info.text.appendRawString("Patroness: the people of %s present their patroness with part of the town's income.");
	info.text.replaceTextID(town.getNameTextID());
	for(const auto & resource : LIBRARY->resourceTypeHandler->getAllObjects())
		if(collection[resource] > 0)
			info.components.emplace_back(ComponentType::RESOURCE, resource, static_cast<int>(collection[resource]));
	handler.sendAndApply(info);
}
}
