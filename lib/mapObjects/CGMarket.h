/*
 * CGMarket.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CGObjectInstance.h"
#include "IMarket.h"
#include "../ResourceSet.h"

class MarketInstanceConstructor;
class JsonNode;
class IGameSettings;

class DLL_LINKAGE CGMarket : public CGObjectInstance, public IMarket
{
protected:
	std::shared_ptr<MarketInstanceConstructor> getMarketHandler() const;

public:
	CGMarket(IGameInfoCallback *cb);
	///IObjectInterface
	void onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const override; //open trading window
	void initObj(IGameRandomizer & gameRandomizer) override;//set skills for trade

	MetaString getPopupText(PlayerColor player) const override;
	MetaString getPopupText(const CGHeroInstance * hero) const override;

	///IMarket
	ObjectInstanceID getObjInstanceID() const override;
	int getMarketEfficiency() const override;
	int availableUnits(EMarketMode mode, int marketItemSerial) const override; //-1 if unlimited
	std::vector<TradeItemBuy> availableItemsIds(EMarketMode mode) const override;
	std::set<EMarketMode> availableModes() const override;
	double getMarketExchangeEffectiveness() const override;
};

class DLL_LINKAGE CGBlackMarket : public CGMarket
{
public:
	using CGMarket::CGMarket;

	std::vector<ArtifactID> artifacts; //available artifacts

	void newTurn(IGameEventCallback & gameEvents, IGameRandomizer & gameRandomizer) const override; //reset artifacts for black market every month
	std::vector<TradeItemBuy> availableItemsIds(EMarketMode mode) const override;

	template <typename Handler> void serialize(Handler &h)
	{
		h & static_cast<CGMarket&>(*this);
		h & artifacts;
	}
};

class DLL_LINKAGE CGUniversity : public CGMarket
{
public:
	using CGMarket::CGMarket;

	std::string getSpeechTranslated() const;

	std::vector<TradeItemBuy> skills; //available skills

	std::vector<TradeItemBuy> availableItemsIds(EMarketMode mode) const override;
	void onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const override; //open window
	std::vector<Component> getPopupComponents(PlayerColor player) const override;
	bool wasVisited (PlayerColor player) const override;

	template <typename Handler> void serialize(Handler &h)
	{
		h & static_cast<CGMarket&>(*this);
		h & skills;
	}
};

namespace newHorizonsUniversity
{
/// True only for the Conflux town Magic University while the saved world uses
/// New Horizons magic rules. Adventure-map Universities and legacy towns keep
/// the original gold-only price.
DLL_LINKAGE bool usesNewHorizonsTuition(const IMarket * market, const JsonNode & magicRules);

/// Return the authoritative price for one Basic Magic School Skill. The same
/// helper is used by the server and the confirmation dialog so the UI cannot
/// drift from the transaction validator.
DLL_LINKAGE TResources tuition(const IMarket * market, const JsonNode & magicRules, const IGameSettings & settings);
}

namespace newHorizonsHouseOfWisdom
{
/// True for every Conflux town whose saved world uses New Horizons magic
/// rules, regardless of whether the building has been constructed yet. This
/// is used to author the town-owned stock during authoritative map setup.
DLL_LINKAGE bool eligible(const IMarket * market, const JsonNode & magicRules);

/// True only for the Conflux special building in a saved New Horizons world.
/// Legacy Conflux towns and adventure-map Universities continue to expose
/// secondary skills through the ordinary RESOURCE_SKILL market mode.
DLL_LINKAGE bool active(const IMarket * market, const JsonNode & magicRules);

/// The provisional House of Wisdom price is deliberately a gold-only price,
/// scaled by the saved spell level.  Keeping this helper shared makes the
/// storefront preview and the server-side validator agree exactly.
DLL_LINKAGE TResources price(const SpellID & spell);
}
