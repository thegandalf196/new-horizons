/*
 * UnitMagicResistanceQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "IBattleInfoCallback.h"

namespace battle
{
/// Shared spell-rejection arithmetic for live/projected units and adventure
/// readouts. Aura is zero when no battlefield adjacency is defined.
DLL_LINKAGE int combinedMagicResistance(int ordinary, int intrinsic, int aura, bool newHorizonsRules);

/// One shared live/projected resistance formula. Derived contexts supply only
/// adjacency and captured rules; no casts to a mutable/live battle unit.
class DLL_LINKAGE UnitMagicResistanceQuery
{
protected:
	virtual void requireSubject(const Unit &) const {}
	virtual Units adjacentUnits(const Unit & unit) const = 0;
	virtual bool newHorizonsRulesActive() const = 0;
public:
	virtual ~UnitMagicResistanceQuery() = default;
	int query(const Unit * unit) const;
};
}
