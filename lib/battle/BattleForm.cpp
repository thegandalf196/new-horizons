/*
 * BattleForm.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleForm.h"
#include "AccessibilityInfo.h"
#include "CUnitState.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/NewHorizonsSorcery.h"

namespace battle
{
Bonus polymorphMarker(SpellID spell, PlayerColor caster)
{
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
	marker.statusTags = {BonusStatusTag::DEBUFF};
	marker.statusIdentity = "polymorph";
	marker.spellCasterOwner = caster;
	marker.description.appendRawString("Polymorph");
	return marker;
}

bool isPolymorphMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::SPELL_EFFECT
		&& bonus->duration == BonusDuration::ONE_BATTLE && bonus->statusIdentity == "polymorph"
		&& bonus->statusTags == std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF};
}

bool battleFormDurationPaused(const Unit & unit)
{
	if(unit.isTimeStopped())
		return true;
	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto lock = unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock)));
	if(!lock)
		return false;
	bool activeLock = false;
	bool preservesHostile = false;
	for(const auto & bonus : *lock)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;
		activeLock |= bonus->type == BonusType::MAGIC_RESISTANCE;
		preservesHostile |= bonus->type == BonusType::NONE && bonus->val < 0;
	}
	return activeLock && preservesHostile;
}

bool endBattleFormAtNearestLegalPosition(CUnitState & state, const AccessibilityInfo & accessibility)
{
	if(!state.hasBattleForm())
		return true;
	// Corpses do not reserve a footprint. Restore their health/provenance without
	// teleporting remains or competing with living units.
	if(!state.alive())
	{
		state.endBattleForm();
		return true;
	}
	const auto destination = accessibility.nearestLegalPosition(state.getPosition(),
		state.battleFormOriginalCreature().toCreature()->isDoubleWide(), state.unitSide());
	if(!destination)
	{
		state.deferBattleFormRestoration();
		return false;
	}
	state.endBattleForm();
	state.setPosition(*destination);
	return true;
}
}
