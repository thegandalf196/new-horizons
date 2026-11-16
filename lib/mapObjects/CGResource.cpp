/*
 * MiscObjects.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CGResource.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstatesPolicy.h"
#include <limits>

#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../callback/IGameRandomizer.h"
#include "../mapObjectConstructors/CommonConstructors.h"
#include "../texts/CGeneralTextHandler.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../gameState/CGameState.h"
#include "../serializer/JsonSerializeFormat.h"
#include "../CSoundBase.h"
#include "../entities/ResourceTypeHandler.h"

#include <vstd/RNG.h>

std::shared_ptr<ResourceInstanceConstructor> CGResource::getResourceHandler() const
{
	const auto & baseHandler = getObjectHandler();
	const auto & ourHandler = std::dynamic_pointer_cast<ResourceInstanceConstructor>(baseHandler);
	return ourHandler;
}

int CGResource::getAmountMultiplier() const
{
	return getResourceHandler()->getAmountMultiplier();
}

uint32_t CGResource::getAmount() const
{
	return amount;
}

void CGResource::setAmount(uint32_t value)
{
	amount = value;
}

const MetaString & CGResource::getMessage() const
{
	return message;
}

void CGResource::setMessage(MetaString && value)
{
	message = std::move(value);
}

GameResID CGResource::resourceID() const
{
	return getResourceHandler()->getResourceType();
}

MetaString CGResource::getHoverText(PlayerColor player) const
{
	return MetaString::createFromName(resourceID());
}

void CGResource::pickRandomObject(IGameRandomizer & gameRandomizer)
{
	assert(ID == Obj::RESOURCE || ID == Obj::RANDOM_RESOURCE);

	if (ID == Obj::RANDOM_RESOURCE)
	{
		ID = Obj::RESOURCE;
		subID = gameRandomizer.getDefault().nextInt(EGameResID::WOOD, LIBRARY->resourceTypeHandler->getAllObjects().size() - 1);
		setType(ID, subID);

		amount *= getAmountMultiplier();
	}
}

void CGResource::initObj(IGameRandomizer & gameRandomizer)
{
	blockVisit = true;
	getResourceHandler()->randomizeObject(this, gameRandomizer);
}

void CGResource::onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const
{
	if(stacksCount())
	{
		if(!message.empty())
		{
			BlockingDialog ynd(true,false);
			ynd.player = h->getOwner();
			ynd.text = message;
			gameEvents.showBlockingDialog(this, &ynd);
		}
		else
		{
			blockingDialogAnswered(gameEvents, h, true); //behave as if player accepted battle
		}
	}
	else
		collectRes(gameEvents, h);
}

ResourceSet CGResource::getVisitResources(const CGHeroInstance * hero) const
{
	if(amount > std::numeric_limits<TResource>::max())
		throw std::overflow_error("Resource pickup amount exceeds resource range");
	ResourceSet base;
	base[resourceID()] = static_cast<TResource>(amount);
	return newHorizonsEstates::visitReward(base, hero);
}

void CGResource::collectRes(IGameEventCallback & gameEvents, const CGHeroInstance * hero) const
{
	const auto player = hero->getOwner();
	const auto reward = getVisitResources(hero);
	gameEvents.giveResources(player, reward);
	InfoWindow sii;
	sii.player = player;
	if(!message.empty())
	{
		sii.type = EInfoWindowMode::AUTO;
		sii.text = message;
	}
	else
	{
		sii.type = EInfoWindowMode::INFO;
		sii.text.appendTextID("core.advevent.113");
		sii.text.replaceName(resourceID());
	}
	newHorizonsEstates::appendVisitRewardBonus(sii.text, getVisitResources(nullptr), reward);
	sii.components.emplace_back(ComponentType::RESOURCE, resourceID(), reward[resourceID()]);
	sii.soundID = soundBase::pickup01 + gameEvents.getRandomGenerator().nextInt(6);
	gameEvents.showInfoDialog(&sii);
	gameEvents.removeObject(this, player);
}

void CGResource::battleFinished(IGameEventCallback & gameEvents, const CGHeroInstance *hero, const BattleResult &result) const
{
	if(result.winner == BattleSide::ATTACKER) //attacker won
		collectRes(gameEvents, hero);
}

void CGResource::blockingDialogAnswered(IGameEventCallback & gameEvents, const CGHeroInstance *hero, int32_t answer) const
{
	if(answer)
		gameEvents.startBattle(hero, this);
}

void CGResource::serializeJsonOptions(JsonSerializeFormat & handler)
{
	CArmedInstance::serializeJsonOptions(handler);
	if(!handler.saving && !handler.getCurrent()["guards"].Vector().empty())
		CCreatureSet::serializeJson(handler, "guards", 7);
	handler.serializeInt("amount", amount, 0);
	handler.serializeStruct("guardMessage", message);
}

