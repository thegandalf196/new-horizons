/*
 * StackEffectRemoval.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackEffectRemoval.h"
#include "../bonuses/Bonus.h"
namespace battle
{
bool matchesStackEffectRemoval(const Bonus & selected, const Bonus & candidate)
{
	return selected.duration == candidate.duration
		&& selected.type == candidate.type
		&& selected.subtype == candidate.subtype
		&& selected.source == candidate.source
		&& selected.val == candidate.val
		&& selected.sid == candidate.sid
		&& selected.valType == candidate.valType
		&& selected.effectRange == candidate.effectRange
		&& (selected.type != BonusType::CONFUSION_PENDING || selected.spellCasterOwner == candidate.spellCasterOwner);
}
}
