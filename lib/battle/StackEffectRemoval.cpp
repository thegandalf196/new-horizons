/*
 * StackEffectRemoval.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackEffectRemoval.h"
#include "ArcaneInspiration.h"
#include "ConsecratedArms.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "../bonuses/Bonus.h"
namespace battle
{
bool matchesStackEffectRemoval(const Bonus & selected, const Bonus & candidate)
{
	if(!creatureAbility::sameDescriptor(selected.abilityDescriptor, candidate.abilityDescriptor))
		return false;
	if(arcaneInspiration::isEffect(&selected) || arcaneInspiration::isEffect(&candidate))
		return arcaneInspiration::isEffect(&selected) && arcaneInspiration::isEffect(&candidate)
			&& selected.stacking == candidate.stacking && selected.spellCasterOwner == candidate.spellCasterOwner
			&& selected.duration == candidate.duration && selected.type == candidate.type
			&& selected.val == candidate.val && selected.subtype == candidate.subtype;
	const bool legacyMatch = selected.duration == candidate.duration
		&& selected.type == candidate.type
		&& selected.subtype == candidate.subtype
		&& selected.source == candidate.source
		&& selected.val == candidate.val
		&& selected.sid == candidate.sid
		&& selected.valType == candidate.valType
		&& selected.effectRange == candidate.effectRange
		&& (selected.type != BonusType::CONFUSION_PENDING || selected.spellCasterOwner == candidate.spellCasterOwner);
	// Prayer components share source and numerical fields across distinct prayers.
	// Removing one captured prayer must preserve the other five identities.
	if(consecratedArms::identity(&selected) || consecratedArms::identity(&candidate))
		return legacyMatch && selected.statusIdentity == candidate.statusIdentity
			&& selected.stacking == candidate.stacking;
	if(!legacyMatch || selected.type != BonusType::BIND_EFFECT)
		return legacyMatch;
	if(!newHorizonsSerpentsEmbrace::isMarker(selected) && !newHorizonsSerpentsEmbrace::isMarker(candidate))
		return true;
	return newHorizonsSerpentsEmbrace::sourceId(selected) == newHorizonsSerpentsEmbrace::sourceId(candidate);
}
}
