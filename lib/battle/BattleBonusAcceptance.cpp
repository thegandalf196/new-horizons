/*
 * BattleBonusAcceptance.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleBonusAcceptance.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"

bool battle::acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus)
{
	if(state.hasBattleForm() && state.battleFormCreature() != state.battleFormOriginalCreature()
		&& bonus.sid == BonusSourceID(state.battleFormOriginalCreature())
		&& (bonus.source == BonusSource::CREATURE_ABILITY || bonus.source == BonusSource::STACK_EXPERIENCE))
		return false;

	// Temporary summons do not inherit Sylvan Luck rank effects unless they
	// carry Nature-spell provenance and their hero owns Wild Chance.  Filtering
	// by source skill preserves native creature Luck and unrelated bonuses.
	if(!state.summoned || bonus.source != BonusSource::SECONDARY_SKILL)
		return true;

	// Identifier resolution depends on VLC and therefore must never run during
	// namespace-scope initialization (notably in the standalone test binary).
	const SecondarySkill sylvanLuckSkill(SecondarySkill::decode("new-horizons:sylvanLuck"));
	if(bonus.sid.as<SecondarySkill>() != sylvanLuckSkill)
		return true;

	const auto * hero = original.getMyHero();
	return state.natureSummoned && hero
		&& hero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.wildChance");
}
