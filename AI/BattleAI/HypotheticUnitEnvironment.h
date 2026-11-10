/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/IUnitInfo.h"
class HypotheticBattle;
/// Unit-stat dispatch must remain on the branch, including detached state copies.
/// This adapter is deliberately not a CBattleInfoCallback: it never falls through
/// to a live hero's stat policy via CUnitState's native callback fast path.
class HypotheticUnitEnvironment final : public battle::IUnitEnvironment
{
	const HypotheticBattle & owner;
public:
	explicit HypotheticUnitEnvironment(const HypotheticBattle & owner) : owner(owner) {}
	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
	std::optional<int> unitMagicResistance(const battle::Unit * unit) const override;
	int unitSylvanTacticsSpeed(const battle::Unit * unit, int turn) const override;
	int unitSylvanTacticsInitiativePercent(const battle::Unit * unit) const override;
	int unitFortuneSpeed(const battle::Unit * unit) const override;
	int unitSpeedBonus(const battle::Unit * unit) const override;
	int unitWarfareStatBonus(const battle::Unit * unit) const override;
	bool unitSlowAffectsSpeed(const battle::Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const battle::Unit * unit) const override;
	newHorizonsElementalAttunement::UnitPassives unitElementalAttunement(const battle::Unit * unit) const override;
	int unitAdditionalRetaliations(const battle::Unit * unit) const override;
	int unitBloodragePainIncrement(const battle::Unit * unit) const override;
	bool unitHasVeteranCohesion(const battle::Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit * unit) const override;
};
