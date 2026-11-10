/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGCreature.h"
#include "CGHeroInstance.h"
#include "../GameLibrary.h"
#include "../callback/IGameEventCallback.h"
#include "../entities/ResourceTypeHandler.h"
#include "../networkPacks/PacksForClient.h"

void CGCreature::giveReward(IGameEventCallback & gameEvents, const CGHeroInstance * hero) const
{
	InfoWindow window;
	window.player = hero->tempOwner;
	if(!resources.empty())
	{
		gameEvents.giveResources(hero->tempOwner, resources);
		for(const auto & resource : LIBRARY->resourceTypeHandler->getAllObjects())
			if(resources[resource] > 0)
				window.components.emplace_back(ComponentType::RESOURCE, resource, resources[resource]);
	}
	if(gainedArtifact != ArtifactID::NONE)
	{
		gameEvents.giveHeroNewArtifact(hero, gainedArtifact, ArtifactPosition::FIRST_AVAILABLE);
		window.components.emplace_back(ComponentType::ARTIFACT, gainedArtifact);
	}
	if(!window.components.empty())
	{
		window.type = EInfoWindowMode::AUTO;
		window.text.appendTextID("core.advevent.183");
		window.text.replaceTextID(hero->getNameTextID());
		gameEvents.showInfoDialog(&window);
	}
}
