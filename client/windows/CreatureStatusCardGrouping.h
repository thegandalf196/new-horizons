/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/bonuses/Bonus.h"

/// Which received bonuses share one creature-window card. Pure; no widgets or state.
namespace creatureStatusCardGrouping
{
/// A NONE-type status marker that carries its own icon (Alagar's Brittle) is a card of
/// its own. Prayer markers and the other NONE markers keep the shared NONE group.
inline bool ownsCard(const Bonus & bonus)
{
	return bonus.type == BonusType::NONE && !bonus.customIconPath.empty() && !consecratedArms::identity(&bonus);
}

/// Cards group by bonus type and subtype; a card-owning marker groups only with copies
/// of its own stacking key.
inline bool sameCard(const Bonus & left, const Bonus & right)
{
	if(ownsCard(left) || ownsCard(right))
		return ownsCard(left) && ownsCard(right) && left.stacking == right.stacking;
	return left.type == right.type && left.subtype == right.subtype;
}
}
