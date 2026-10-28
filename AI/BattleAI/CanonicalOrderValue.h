/*
 * CanonicalOrderValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/battle/CBattleInfoEssentials.h"
#include "../../lib/battle/CBattleInfoCallback.h"

#include <memory>

class Environment;
class HypotheticBattle;

namespace heroOrderValue
{
/// Revised Feint/Vanguard only. Accepted projection is valued with finite stat
/// estimates on a fixed DPS-reduction reference; never searches combat actions.
bool usesRevisedArmyHeuristic(const CBattleInfoCallback & battle, HeroCommand command);
float revisedArmyUtility(std::shared_ptr<CBattleInfoCallback> before,
	const std::shared_ptr<HypotheticBattle> & projected, BattleSide side, HeroCommand command);

struct DirectOrderValue
{
	std::optional<float> direct;
	float holdPercent = 0.0f;
};

/// One call-local canonical Order estimate, borrowing the original visible
/// callback and target tuple. Second Wind may use an optional call-local child
/// activation forecast; no scorer or projected world is retained by this object.
/// Direct damage utility intentionally remains in its historical raw-HP domain.
class CanonicalOrderValue
{
private:
	// Keep the original all-unit snapshot alive until the estimate is destroyed,
	// just like the local snapshot in the former monolithic implementation.
	battle::Units units;

public:
	CanonicalOrderValue(const CBattleInfoCallback & battle, BattleSide side,
		HeroCommand command, const std::vector<uint32_t> & targetIds);
	CanonicalOrderValue(const CanonicalOrderValue &) = delete;
	CanonicalOrderValue & operator=(const CanonicalOrderValue &) = delete;

	/// Absence of a direct result means Hold's shared spell/morale forecast must
	/// run in the evaluator, using the prepared fields and returned coefficient.
	/// Without matching projection context, Second Wind keeps its historical prior.
	DirectOrderValue directValue(std::optional<int> focusFireSnapshotPercent,
		const Environment * environment = nullptr,
		const std::shared_ptr<CBattleInfoCallback> & projectionSource = nullptr) const;
	float meleeDamage(const battle::Unit * attackerUnit, const battle::Unit * defenderUnit) const;

	// Prepared Hold forecast inputs; do not mutate between directValue and the
	// evaluator's forecast. The owning estimate must outlive those borrowed views.
	const CGHeroInstance * hero = nullptr;
	PlayerColor ownPlayer;
	std::vector<const battle::Unit *> ownUnits;
	std::vector<const battle::Unit *> enemyUnits;

private:
	// Prepared snapshots keep their original enclosing-estimate lifetime.
	std::optional<HeroActionAllowanceState::Selection> orderAllowance;
	std::optional<HeroOrderState> preparedOrder;
	const CBattleInfoCallback & battle;
	const BattleSide side;
	const HeroCommand command;
	const std::vector<uint32_t> & targetIds;
	const JsonNode * commandRules = nullptr;
	bool combinedArmsEnabled = false;
	int warcastingBonus = 0;
	int divineMandateEfficiencyBonusPercent = 0;

	float coefficient(const char * commandKey, const char * effectKey) const;
	float anyDamage(const battle::Unit * attackerUnit, const battle::Unit * defenderUnit) const;
	float bestOwnMeleeDamage(const battle::Unit * defenderUnit) const;
	float bestEnemyMeleeDamage(const battle::Unit * defenderUnit) const;
	float sumOwnMeleeDamage() const;
	float focusFire(std::optional<int> focusFireSnapshotPercent) const;
	float charge(float ownMeleePotential, float chargePercent) const;
	float riposte(float riposteReduction, float riposteDamage) const;
	float brace(float braceDamage) const;
	float protect(float protectReduction) const;
	float flank(float flankDamage, float combinedArmsRangedPercent) const;
	float secondWind(const Environment * environment,
		const std::shared_ptr<CBattleInfoCallback> & projectionSource) const;
	DamageEstimation estimateExtraCounter(const battle::Unit * own,
		const battle::Unit * enemy, int64_t projectedDamage) const;
};
}
