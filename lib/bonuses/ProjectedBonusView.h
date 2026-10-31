/*
 * ProjectedBonusView.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IBonusBearer.h"
#include <stdexcept>
namespace battle { class Unit; class CUnitState; }

/// A graph query has already evaluated the bound unit's complete form sources.
/// Detached units must not repeat legacy temporary live-context form evaluation.
class DLL_LINKAGE ProjectedBonusView : public IBonusBearer
{
public:
	virtual const battle::Unit * evaluatedUnit() const = 0;
	/// Exact owned provider identity, not a bool permit or native source import.
	virtual const battle::Unit * completeOwnedFormSource() const { return nullptr; }
	/// Optional owned native-form source evaluation for a retained derived scratch.
	/// Null means legacy handling; a handled empty list must remain engaged.
	virtual TConstBonusListPtr ownedNativeFormBonuses(const battle::CUnitState &, const CSelector &, bool) const { return {}; }
	/// Exact source-local exported spell records, in export order; never effective
	/// or inherited records. Values retain the existing immutable metadata lease.
	virtual std::vector<Bonus> sourceLocalSpellEffects() const
	{
		throw std::logic_error("Projected view has no source-local spell provenance");
	}
	/// Synchronous opt-in staging hook; requires an already bound unit.
	virtual void onBattleFormChanged(const battle::CUnitState & unit) const = 0;
	bool isBonusCacheable() const override { return false; }
};
