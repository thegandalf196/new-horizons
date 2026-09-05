/*
 * AutocombatPreferences.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Unit.h"

struct AutocombatPreferences
{
	enum class Mode
	{
		SELECTIVE,
		FULL_BATTLE
	};

	bool enableSpellsUsage = true;
	bool enableTacticsUsage = true;
	bool enableUnitsUsage = true;
	bool enableCatapultUsage = true;
	bool enableBallistaUsage = true;
	bool enableFirstAidTentUsage = true;

	/// Delegation policy only; does not grant eligibility for a manually controlled turn.
	bool controlsUnit(const battle::Unit & unit, Mode mode) const
	{
		if(mode == Mode::FULL_BATTLE || unit.isTurret())
			return true;

		if(unit.isCatapult())
			return enableCatapultUsage;
		if(unit.isBallista())
			return enableBallistaUsage;
		if(unit.isFirstAidTent())
			return enableFirstAidTentUsage;

		return enableUnitsUsage;
	}
};

