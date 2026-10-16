/*
 * ProjectedBonusView.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IBonusBearer.h"
namespace battle { class Unit; class CUnitState; }

/// A graph query has already evaluated the bound unit's complete form sources.
/// Detached units must not repeat legacy temporary live-context form evaluation.
class DLL_LINKAGE ProjectedBonusView : public IBonusBearer
{
public:
	virtual const battle::Unit * evaluatedUnit() const = 0;
	/// Synchronous opt-in staging hook; requires an already bound unit.
	virtual void onBattleFormChanged(const battle::CUnitState & unit) const = 0;
	bool isBonusCacheable() const override { return false; }
};
