/*
 * CGMarket.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CGMarket.h"

#include "CGHeroInstance.h"
#include "CGTownInstance.h"

//#include "../CCreatureHandler.h"
#include "../CPlayerState.h"
//#include "../CSkillHandler.h"
#include "../IGameSettings.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/CSpell.h"
#include "../callback/IGameEventCallback.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameRandomizer.h"
#include "../mapObjectConstructors/AObjectTypeHandler.h"
#include "../mapObjectConstructors/CObjectClassesHandler.h"
#include "../mapObjectConstructors/MarketInstanceConstructor.h"
#include "../networkPacks/PacksForClient.h"
#include "../texts/TextIdentifier.h"

ObjectInstanceID CGMarket::getObjInstanceID() const
{
	return id;
}

void CGMarket::initObj(IGameRandomizer & gameRandomizer)
{
	getObjectHandler()->configureObject(this, gameRandomizer);
}

void CGMarket::onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const
{
	gameEvents.showObjectWindow(this, EOpenWindowMode::MARKET_WINDOW, h, true);
}

MetaString CGMarket::getPopupText(PlayerColor player) const
{
	if (!getMarketHandler()->hasDescription())
		return getHoverText(player);

	MetaString message = MetaString::createFromRawString("{%s}\r\n\r\n%s");
	message.replaceTextID(getObjectNameTextID());
	message.replaceTextID(getMarketHandler()->getDescriptionTextID());
	return message;
}

MetaString CGMarket::getPopupText(const CGHeroInstance * hero) const
{
	return getPopupText(hero->getOwner());
}

int CGMarket::getMarketEfficiency() const
{
	return getMarketHandler()->getMarketEfficiency();
}

int CGMarket::availableUnits(EMarketMode mode, int marketItemSerial) const
{
	return -1;
}

std::vector<TradeItemBuy> CGMarket::availableItemsIds(EMarketMode mode) const
{
	const auto & tradeableResources = getMarketHandler()->getTradeableResources();

	// market that trades only some of the resources, e.g. Warlock's Lab
	if(!tradeableResources.empty() && mode == EMarketMode::RESOURCE_RESOURCE)
		return std::vector<TradeItemBuy>(tradeableResources.begin(), tradeableResources.end());

	return IMarket::availableItemsIds(mode);
}

double CGMarket::getMarketExchangeEffectiveness() const
{
	// market with exchange effectiveness set explicitly in its config, e.g. Warlock's Lab
	if(double effectiveness = getMarketHandler()->getMarketExchangeEffectiveness(); effectiveness > 0)
		return effectiveness;

	return IMarket::getMarketExchangeEffectiveness();
}

std::shared_ptr<MarketInstanceConstructor> CGMarket::getMarketHandler() const
{
	const auto & baseHandler = getObjectHandler();
	const auto & ourHandler = std::dynamic_pointer_cast<MarketInstanceConstructor>(baseHandler);
	return ourHandler;
}

std::set<EMarketMode> CGMarket::availableModes() const
{
	return getMarketHandler()->availableModes();
}

CGMarket::CGMarket(IGameInfoCallback *cb)
	: CGObjectInstance(cb)
	, IMarket(cb)
{}

std::vector<TradeItemBuy> CGBlackMarket::availableItemsIds(EMarketMode mode) const
{
	switch(mode)
	{
	case EMarketMode::RESOURCE_ARTIFACT:
		{
			std::vector<TradeItemBuy> ret;
			for(const auto & a : artifacts)
				ret.push_back(a);
			return ret;
		}
	default:
		return std::vector<TradeItemBuy>();
	}
}

void CGBlackMarket::newTurn(IGameEventCallback & gameEvents, IGameRandomizer & gameRandomizer) const
{
	int resetPeriod = cb->getSettings().getInteger(EGameSettings::MARKETS_BLACK_MARKET_RESTOCK_PERIOD);

	int currentDay = cb->getCalendar().getCurrentDay();
	bool isFirstDay = currentDay == 1;
	bool regularResetTriggered = resetPeriod != 0 && ((currentDay-1) % resetPeriod) == 0;

	if (!isFirstDay && !regularResetTriggered)
		return;

	SetAvailableArtifacts saa;
	saa.id = id;
	saa.arts = gameRandomizer.rollMarketArtifactSet();
	gameEvents.sendAndApply(saa);
}

std::vector<TradeItemBuy> CGUniversity::availableItemsIds(EMarketMode mode) const
{
	switch (mode)
	{
		case EMarketMode::RESOURCE_SKILL:
			return skills;

		default:
			return std::vector<TradeItemBuy>();
	}
}

std::string CGUniversity::getSpeechTranslated() const
{
	return getMarketHandler()->getSpeechTranslated();
}

void CGUniversity::onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const
{
	ChangeObjectVisitors cow;
	cow.object = id;
	cow.mode = ChangeObjectVisitors::VISITOR_ADD_PLAYER;
	cow.hero = h->id;
	gameEvents.sendAndApply(cow);

	gameEvents.showObjectWindow(this, EOpenWindowMode::UNIVERSITY_WINDOW, h, true);
}

bool CGUniversity::wasVisited (PlayerColor player) const
{
	return cb->getPlayerState(player)->visitedObjects.count(id) != 0;
}

std::vector<Component> CGUniversity::getPopupComponents(PlayerColor player) const
{
	std::vector<Component> result;

	if (!wasVisited(player))
		return result;

	for (auto const & skill : skills)
		result.emplace_back(ComponentType::SEC_SKILL, skill.as<SecondarySkill>());

	return result;
}

namespace newHorizonsUniversity
{
namespace
{
int configuredAmount(const JsonNode & config, const char * key, int fallback)
{
	const auto & value = config[key];
	if(!value.isNumber() || value.Float() < 0 || std::floor(value.Float()) != value.Float())
		return fallback;
	return value.Integer();
}
}

bool usesNewHorizonsTuition(const IMarket * market, const JsonNode & magicRules)
{
	const auto * town = dynamic_cast<const CGTownInstance *>(market);
	return town
		&& town->getFactionID() == FactionID::CONFLUX
		&& town->hasBuilt(BuildingID::SPECIAL_2)
		&& town->allowsTrade(EMarketMode::RESOURCE_SKILL)
		&& newHorizonsMagic::rulesActive(magicRules);
}

TResources tuition(const IMarket * market, const JsonNode & magicRules, const IGameSettings & settings)
{
	TResources result;
	if(!usesNewHorizonsTuition(market, magicRules))
	{
		result[EGameResID::GOLD] = settings.getInteger(EGameSettings::MARKETS_UNIVERSITY_GOLD_COST);
		return result;
	}

	// Keep the fixed design values as a compatibility fallback for an older
	// settings snapshot which predates the new setting. New Horizons worlds
	// still receive the exact canonical tuition rather than a zero price.
	const auto & config = settings.getValue(EGameSettings::MARKETS_NEW_HORIZONS_UNIVERSITY_COST);
	result[EGameResID::GOLD] = configuredAmount(config, "gold", 5000);
	result[EGameResID::MERCURY] = configuredAmount(config, "mercury", 2);
	result[EGameResID::SULFUR] = configuredAmount(config, "sulfur", 2);
	result[EGameResID::CRYSTAL] = configuredAmount(config, "crystal", 2);
	result[EGameResID::GEMS] = configuredAmount(config, "gems", 2);
	return result;
}
}

namespace newHorizonsHouseOfWisdom
{
bool eligible(const IMarket * market, const JsonNode & magicRules)
{
	const auto * town = dynamic_cast<const CGTownInstance *>(market);
	return town
		&& town->getFactionID() == FactionID::CONFLUX
		&& newHorizonsMagic::rulesActive(magicRules);
}

bool active(const IMarket * market, const JsonNode & magicRules)
{
	const auto * town = dynamic_cast<const CGTownInstance *>(market);
	return eligible(market, magicRules)
		&& town->hasBuilt(BuildingID::SPECIAL_2)
		&& town->allowsTrade(EMarketMode::RESOURCE_SKILL);
}

TResources price(const SpellID & spell)
{
	TResources result;
	const auto * definition = spell.toSpell();
	if(!definition)
		return result;

	// Level-one scrolls cost 1,000 gold; higher-level scrolls scale linearly.
	// This keeps the storefront useful early while making rare high-level
	// effects a meaningful purchase without introducing a second currency.
	result[EGameResID::GOLD] = 1000 * std::max(1, definition->getLevel());
	return result;
}
}
