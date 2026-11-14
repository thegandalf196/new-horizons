/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/IBonusBearer.h"
#include <vcmi/FactionMember.h>
#include <cstdint>

namespace creatureDefensePresentation
{
struct Values
{
	int64_t general;
	int64_t physical;
};

inline Values query(const AFactionMember & subject, bool ranged)
{
	// Native damage capture skips both Defense channels while Frenzied.
	if(subject.getBonusBearer()->hasBonusOfType(BonusType::IN_FRENZY))
		return {0, 0};
	const int64_t general = subject.getDefense(ranged);
	// Native damage resolution adds this channel only for physical attacks.
	// Read the actual bonus graph: specialties own count-dependent formulas.
	return {general, general + subject.getBonusBearer()->valOfBonuses(BonusType::PHYSICAL_DEFENSE)};
}
}
