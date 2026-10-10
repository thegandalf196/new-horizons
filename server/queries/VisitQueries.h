/*
 * VisitQueries.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CQuery.h"
#include "../../lib/GameConstants.h"

class CGTownInstance;

//Created when hero visits object.
//Removed when query above is resolved (or immediately after visit if no queries were created)
class VisitQuery : public CQuery
{
protected:
	VisitQuery(CGameHandler * owner, const CGObjectInstance * Obj, const CGHeroInstance * Hero, QueryType type);

public:
	ObjectInstanceID visitedObject;
	ObjectInstanceID visitingHero;

	bool blocksPack(const CPackForServer * pack) const final;
};

class MapObjectVisitQuery final : public VisitQuery
{
	std::vector<ObjectInstanceID> deferredBattleLevelUps;
	bool processingDeferredBattleLevelUps = false;

public:
	static constexpr QueryType TYPE = QueryType::MapObjectVisit;

	bool removeObjectAfterVisit;
	/// Server-only accepted encounter context; no benefit until actual troop intake.
	bool trackingNeutralRecruitment = false;
	bool admittedNeutralRecruitment = false;
	/// Captured only when a real payable offer was quoted as free.
	bool legendaryOfferCaptured = false;
	int32_t legendaryOfferMonth = -1;
	int32_t legendaryOfferPreviousMonth = -1;
	CreatureID legendaryOfferCreature;
	SlotID legendaryOfferSlot = SlotID(-1);
	TQuantity legendaryOfferQuantity = 0;
	int64_t legendaryOfferNormalGold = 0;
	uint64_t legendaryOfferArmyValue = 0;
	bool legendaryOfferRecruitmentPact = false;
	bool legendaryOfferAccepted = false;
	bool legendaryOfferAdmitted = false;
	CreatureID acceptedNeutralCreature = CreatureID::NONE;
	TQuantity acceptedNeutralRemaining = 0;
	SlotID acceptedNeutralSlot = SlotID(-1);

	MapObjectVisitQuery(CGameHandler * owner, const CGObjectInstance * Obj, const CGHeroInstance * Hero);

	void onRemoval(PlayerColor color) final;
	void onExposure(QueryPtr topQuery) final;
};

class TownBuildingVisitQuery final : public VisitQuery
{
	struct BuildingVisit
	{
		const CGHeroInstance * hero;
		BuildingID building;
	};

	const CGTownInstance * visitedTown;
	std::vector<BuildingVisit> visitedBuilding;

public:
	static constexpr QueryType TYPE = QueryType::TownBuildingVisit;

	TownBuildingVisitQuery(CGameHandler * owner, const CGTownInstance * Obj, std::vector<const CGHeroInstance *> heroes, std::vector<BuildingID> buildingToVisit);

	void onAdded(PlayerColor color) final;
	void onExposure(QueryPtr topQuery) final;
};
