/*
 * NewHorizonsConfusionControl.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsConfusionControl.h"

namespace newHorizonsConfusionControl
{
void validateMarker(const Bonus & marker)
{
	if(marker.type != BonusType::CONFUSION_PENDING
		|| marker.duration != BonusDuration::ONE_BATTLE
		|| marker.source != BonusSource::SPELL_EFFECT || marker.sid.as<SpellID>().getNum() < 0
		|| (marker.val != 1 && marker.val != 2) || marker.turnsRemain != 0
		|| (!marker.spellCasterOwner.isValidPlayer() && marker.spellCasterOwner != PlayerColor::NEUTRAL)
		|| marker.valType != BonusValueType::ADDITIVE_VALUE
		|| marker.subtype != BonusSubtypeID() || marker.effectRange != BonusLimitEffect::NO_LIMIT
		|| marker.limiter || marker.propagator || marker.updater || marker.propagationUpdater
		|| marker.parameters || !marker.stacking.empty()
		|| marker.statusIdentity != "confusion"
		|| marker.statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF})
		throw std::runtime_error("Invalid Confusion pending marker");
}

bool isPendingMarker(const Bonus * marker)
{
	if(!marker || marker->type != BonusType::CONFUSION_PENDING)
		return false;
	try
	{
		validateMarker(*marker);
		return true;
	}
	catch(const std::runtime_error &)
	{
		return false;
	}
}

Bonus pendingMarker(SpellID spell, PlayerColor caster, bool confounder)
{
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::CONFUSION_PENDING,
		BonusSource::SPELL_EFFECT, confounder ? 2 : 1, BonusSourceID(spell));
	marker.spellCasterOwner = caster;
	marker.statusIdentity = "confusion";
	marker.statusTags = {BonusStatusTag::DEBUFF};
	marker.description.appendRawString("Confusion");
	validateMarker(marker);
	return marker;
}
}
