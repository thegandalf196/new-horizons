/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Interface.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/NewHorizonsEstatesPolicy.h"
#include "../callback/IGameEventCallback.h"
#include "../networkPacks/PacksForClient.h"

namespace
{
std::optional<Obj> experienceRewardSource(const IObjectInterface * object)
{
	const auto * instance = dynamic_cast<const CGObjectInstance *>(object);
	return instance ? std::optional<Obj>(instance->ID) : std::nullopt;
}
}

void Rewardable::Interface::grantRewardWithMessage(IGameEventCallback & gameEvents, const CGHeroInstance * contextHero, int index, bool markAsVisit) const
{
	auto vi = configuration.info.at(index);
	logGlobal->debug("Granting reward %d", index);

	InfoWindow iw;
	iw.player = contextHero->tempOwner;
	iw.text = vi.message;
	newHorizonsEstates::appendVisitRewardBonus(iw.text, vi.reward.resources,
		vi.reward.visitResources(contextHero, experienceRewardSource(getObject())));
	vi.reward.loadComponents(iw.components, contextHero, experienceRewardSource(getObject()));
	iw.type = configuration.infoWindowType;
	configureInfoWindow(iw, contextHero, index);
	gameEvents.showInfoDialog(&iw);

	// grant reward afterwards. Note that it may remove object
	if(markAsVisit)
		markAsVisited(gameEvents, contextHero);
	grantReward(gameEvents, index, contextHero);
}

void Rewardable::Interface::configureInfoWindow(InfoWindow &, const CGHeroInstance *, int) const
{
}

void Rewardable::Interface::selectRewardWithMessage(IGameEventCallback & gameEvents, const CGHeroInstance * contextHero, const std::vector<ui32> & rewardIndices, const MetaString & dialog) const
{
	pendingRewardIndices = rewardIndices;
	const bool teachesSkill = std::any_of(rewardIndices.begin(), rewardIndices.end(), [this, contextHero](ui32 index)
	{
		return rewardTeachesSecondarySkill(configuration.info.at(index), contextHero);
	});
	BlockingDialog sd(configuration.canRefuse || teachesSkill, rewardIndices.size() > 1);
	sd.player = contextHero->tempOwner;
	sd.text = dialog;
	sd.components = loadComponents(contextHero, rewardIndices);
	gameEvents.showBlockingDialog(getObject(), &sd);
}

std::vector<Component> Rewardable::Interface::loadComponents(const CGHeroInstance * contextHero, const std::vector<ui32> & rewardIndices) const
{
	std::vector<Component> result;

	if (rewardIndices.empty())
		return result;

	if (configuration.selectMode != Rewardable::SELECT_FIRST && rewardIndices.size() > 1)
	{
		for (auto index : rewardIndices)
			result.push_back(configuration.info.at(index).reward.getDisplayedComponent(contextHero,
				experienceRewardSource(getObject())));
	}
	else
	{
		configuration.info.at(rewardIndices.front()).reward.loadComponents(result, contextHero,
			experienceRewardSource(getObject()));
	}

	return result;
}


void Rewardable::Reward::loadResourceComponents(std::vector<Component> & comps,
	const CGHeroInstance * h, std::optional<Obj> sourceObject) const
{
	const auto effectiveResources = visitResources(h, sourceObject);
	for (size_t i=0; i<effectiveResources.size(); i++)
	{
		if (effectiveResources[i] !=0)
			comps.emplace_back(ComponentType::RESOURCE, GameResID(i), effectiveResources[i]);
	}
}

void Rewardable::Reward::showVisitMessage(IGameEventCallback & gameEvents, const CGHeroInstance * hero,
	std::optional<Obj> sourceObject, const MetaString & text) const
{
	InfoWindow window;
	window.player = hero->tempOwner;
	window.text = text;
	newHorizonsEstates::appendVisitRewardBonus(window.text, resources, visitResources(hero, sourceObject));
	loadComponents(window.components, hero, sourceObject);
	window.type = EInfoWindowMode::MODAL;
	if(!window.components.empty())
		gameEvents.showInfoDialog(&window);
}
