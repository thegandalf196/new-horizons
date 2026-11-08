/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/OwnedUnitBonusFrame.h"
class HypotheticBattle;
class StackWithBonuses;

/// Exact SWB-to-owned-subject environment mapping. Queries retain their Frame;
/// detached state loans use the bridge's fixed Scratch instead of borrowing HB.
class OwnedStackQuerySource final : public battle::IUnitEnvironment
{
	const HypotheticBattle & owner;
	const StackWithBonuses & subject;
	std::shared_ptr<battle::OwnedUnitBonusFrame> frame;
	bool initialized = false;
	const battle::OwnedUnitBonusFrame & current(const battle::Unit * requested) const;
public:
	OwnedStackQuerySource(const HypotheticBattle & owner, const StackWithBonuses & subject,
		std::shared_ptr<battle::OwnedUnitBonusFrame> frame);
	static const battle::CUnitState & initialUnit(const std::shared_ptr<battle::OwnedUnitBonusFrame> & frame);
	const std::shared_ptr<battle::OwnedUnitBonusFrame> & retainedFrame() const { return frame; }
	void finishInitialization() noexcept { initialized = true; }
	const battle::OwnedUnitBonusFrame & current() const;
	std::shared_ptr<battle::CUnitState> acquireState() const;
	std::vector<Bonus> localSpellEffects() const;
	std::optional<bool> unitLegacyTimeStopSpellMatch(const battle::Unit * unit, SpellID spell) const override;
	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
	std::optional<int> unitMagicResistance(const battle::Unit * unit) const override;
	int unitSylvanTacticsSpeed(const battle::Unit * unit, int turn) const override;
	int unitSylvanTacticsInitiativePercent(const battle::Unit * unit) const override;
	int unitFortuneSpeed(const battle::Unit * unit) const override;
	int unitSpeedBonus(const battle::Unit * unit) const override;
	bool unitSlowAffectsSpeed(const battle::Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const battle::Unit * unit) const override;
	newHorizonsElementalAttunement::UnitPassives unitElementalAttunement(const battle::Unit * unit) const override;
	int unitAdditionalRetaliations(const battle::Unit * unit) const override;
	int unitBloodragePainIncrement(const battle::Unit * unit) const override;
	bool unitHasVeteranCohesion(const battle::Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitLuckLimits(const battle::Unit * unit) const override;
};
