/*
 * StackEffectRemoval.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackEffectRemoval.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "../bonuses/Bonus.h"
namespace battle
{
bool matchesStackEffectRemoval(const Bonus & selected, const Bonus & candidate)
{
	const bool legacyMatch = selected.duration == candidate.duration
		&& selected.type == candidate.type
		&& selected.subtype == candidate.subtype
		&& selected.source == candidate.source
		&& selected.val == candidate.val
		&& selected.sid == candidate.sid
		&& selected.valType == candidate.valType
		&& selected.effectRange == candidate.effectRange
		&& (selected.type != BonusType::CONFUSION_PENDING || selected.spellCasterOwner == candidate.spellCasterOwner);
	if(!legacyMatch || selected.type != BonusType::BIND_EFFECT)
		return legacyMatch;
	if(!newHorizonsSerpentsEmbrace::isMarker(selected) && !newHorizonsSerpentsEmbrace::isMarker(candidate))
		return true;
	return newHorizonsSerpentsEmbrace::sourceId(selected) == newHorizonsSerpentsEmbrace::sourceId(candidate);
}
}
