/*
 * CGDwelling.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "IOwnableObject.h"

#include "army/CArmedInstance.h"

class CGDwelling;

class DLL_LINKAGE CGDwellingRandomizationInfo
{
public:
	std::set<FactionID> allowedFactions;

	std::string instanceId;//vcmi map instance identifier
	int32_t identifier = 0;//h3m internal identifier

	uint8_t minLevel = 1;
	uint8_t maxLevel = 7; //minimal and maximal level of creature in dwelling: <1, 7>

	void serializeJson(JsonSerializeFormat & handler);
};

class DLL_LINKAGE CGDwelling : public CArmedInstance, public IOwnableObject
{
public:
	using TCreaturesSet = std::vector<std::pair<ui32, std::vector<CreatureID> > >;

	std::optional<CGDwellingRandomizationInfo> randomizationInfo; //random dwelling options; not serialized
	TCreaturesSet creatures; //creatures[level] -> <vector of alternative ids (base creature and upgrades, creatures amount>
	/// Absolute week in which New Horizons Recruitment Muster last affected this
	/// dwelling.  The marker lives on every CGDwelling so the serialized object
	/// model remains ready for future external-dwelling Muster rules; the current
	/// playable slice authorizes only town targets.
	int32_t newHorizonsMusterLastWeek = -1;

	CGDwelling(IGameInfoCallback *cb, BonusNodeType nodeType);
	CGDwelling(IGameInfoCallback *cb);
	~CGDwelling() override;

	const IOwnableObject * asOwnable() const final;
	ResourceSet dailyIncome() const override;
	std::vector<CreatureID> providedCreatures() const override;
	int32_t getNewHorizonsMusterLastWeek() const { return newHorizonsMusterLastWeek; }
	void markNewHorizonsMusterUsed(int32_t week) { newHorizonsMusterLastWeek = week; }
	AnimationPath getKingdomOverviewImage() const;

protected:
	void serializeJsonOptions(JsonSerializeFormat & handler) override;

private:
	FactionID randomizeFaction(IGameRandomizer & gameRandomizer);
	int randomizeLevel(vstd::RNG & rand);

	void pickRandomObject(IGameRandomizer & gameRandomizer) override;
	void initObj(IGameRandomizer & gameRandomizer) override;
	void onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const override;
	void newTurn(IGameEventCallback & gameEvents, IGameRandomizer & gameRandomizer) const override;
	void setPropertyDer(ObjProperty what, ObjPropertyID identifier) override;
	void battleFinished(IGameEventCallback & gameEvents, const CGHeroInstance *hero, const BattleResult &result) const override;
	void blockingDialogAnswered(IGameEventCallback & gameEvents, const CGHeroInstance *hero, int32_t answer) const override;
	std::vector<Component> getPopupComponents(PlayerColor player) const override;
	bool wasVisited (PlayerColor player) const override;

	void updateGuards(IGameEventCallback & gameEvents) const;
	void heroAcceptsCreatures(IGameEventCallback & gameEvents, const CGHeroInstance *h) const;

public:
	template <typename Handler> void serialize(Handler &h)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & creatures;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MUSTER))
			h & newHorizonsMusterLastWeek;
		else if(h.saving && newHorizonsMusterLastWeek != -1)
			throw std::runtime_error("New Horizons Muster state requires the new save format");
		else if(!h.saving)
			newHorizonsMusterLastWeek = -1;
	}
};
