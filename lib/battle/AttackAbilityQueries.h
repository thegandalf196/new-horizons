/* Part of VCMI engine. GPL-2.0-or-later; authors listed in AUTHORS. */
#pragma once
#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"
#include <set>
#include <vector>
class IBonusBearer;
namespace attackAbilityQueries
{
struct Parameters
{
	/// Retain the original selected-list lifetime through the native passive cast.
	TConstBonusListPtr queryOwner;
	int32_t spellLevel = 0;
	int chance = 0;
	bool castMe = false;
};
DLL_LINKAGE std::vector<SpellID> activeSpells(const IBonusBearer & target);
DLL_LINKAGE std::set<SpellID> spellsForAttackCasting(const TConstBonusListPtr & spells, const IBonusBearer & target);
DLL_LINKAGE Parameters parameters(const IBonusBearer & source, BonusType attackMode, SpellID spell, bool ranged);
}
