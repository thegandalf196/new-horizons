/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
#include <memory>
#include "NewHorizonsElementalAttunement.h"
class HypotheticBattle;
class CBattleInfoCallback;
namespace battle
{
class LiveUnitEnvironmentContext;
class OwnedUnitEnvironment;
/// Opaque current army effects, never prospective hero policy. No world,
/// native pointer, perk identity or activation operation survives capture.
class DLL_LINKAGE CommittedElementalEffects final
{
	friend class LiveUnitEnvironmentContext;
	friend class OwnedUnitEnvironment;
	// Narrow AI adapter: the branch must validate its existing exact source,
	// provider and typed unit ancestry before querying an admitted transition.
	// This friendship exports neither the table nor a generic probe operation.
	friend class ::HypotheticBattle;
	using FamilyEffects = std::array<newHorizonsElementalAttunement::UnitPassives, 6>;
	BattleSideArray<FamilyEffects> effects;
	explicit CommittedElementalEffects(const CBattleInfoCallback & observer);
	newHorizonsElementalAttunement::UnitPassives evaluate(const Unit & unit) const;
};
}
