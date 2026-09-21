/*
 * SiegeInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "../GameConstants.h"

//only for use in BattleInfo
struct DLL_LINKAGE SiegeInfo
{
	std::map<EWallPart, EWallState> wallState;
	// Canonical New Horizons fortifications retain their real structural HP in
	// addition to the legacy visual state. Empty/false means legacy battle.
	std::map<EWallPart, int32_t> structuralHP;
	bool canonicalStructuralHP = false;
	EGateState gateState;

	SiegeInfo();

	// return EWallState decreased by value of damage points
	static EWallState applyDamage(EWallState state, unsigned int value);
	static int32_t maximumStructuralHP(EWallPart part);
	static EWallState stateFromStructuralHP(EWallPart part, int32_t hp);

	template <typename Handler> void serialize(Handler &h)
	{
		h & wallState;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CATAPULT_STRUCTURAL_DAMAGE))
		{
			h & structuralHP;
			h & canonicalStructuralHP;
		}
		else if(!h.saving)
		{
			structuralHP.clear();
			canonicalStructuralHP = false;
		}
		h & gateState;
	}
};
