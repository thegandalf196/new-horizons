/*
 * TimeStopState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TimeStopState.h"

#include "../bonuses/BonusParameters.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsSorcery.h"

namespace timeStopState
{
bool isTimeStopBonus(const Bonus & bonus)
{
	// The dedicated marker is authoritative even when a legacy/content-light
	// loader cannot resolve the script's SpellID. The source check below keeps
	// the old NONE/NOT_ACTIVE/INVINCIBLE fallback markers recognizable.
	if(bonus.type == BonusType::TIME_STOP)
		return true;

	if(bonus.source != BonusSource::SPELL_EFFECT || !bonus.sid.as<SpellID>().hasValue())
		return false;

	const auto * spell = bonus.sid.as<SpellID>().toSpell();
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

bool isStateBonus(const Bonus & bonus)
{
	return isTimeStopBonus(bonus)
		&& (bonus.type == BonusType::TIME_STOP
			|| bonus.type == BonusType::NONE
			|| bonus.type == BonusType::NOT_ACTIVE
			|| bonus.type == BonusType::INVINCIBLE);
}

bool belongsToSide(const Bonus & bonus, BattleSide side)
{
	if(!isTimeStopBonus(bonus))
		return false;

	// Early development snapshots did not carry the caster side in addInfo.
	// They are safe to clean up on either hero action rather than leaving a
	// permanent stale marker in a loaded battle.
	if(!bonus.parameters)
		return true;

	try
	{
		return bonus.parameters->toNumber() == static_cast<int32_t>(side);
	}
	catch(const std::exception &)
	{
		return false;
	}
}
}
