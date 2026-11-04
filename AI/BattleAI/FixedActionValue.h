/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "AttackPossibility.h"
#include <optional>
#include <span>
#include <map>
namespace fixedActionValue
{
/// New-policy heuristic only. Capture ordinary legal actions once per parent;
/// outcome queries reprice those same attacks without selecting new targets,
/// movement, Hero actions or a combat queue. This is not full-search parity.
struct Action
{
	BattleAttackInfo attack;
	BattleHex from;
	bool perfectMoment = false;
	float before = 0;
};
class Reference
{
	std::shared_ptr<HypotheticBattle> parent;
	std::vector<Action> actions;
	std::map<uint32_t, std::vector<size_t>> touchedActions;
	PlayerColor player;
	const DamageCache * originalDamage;
	float positive;
	float negative;
public:
	Reference(std::shared_ptr<HypotheticBattle> parent, PlayerColor player,
		const DamageCache & damage, float positiveEffectMultiplier, float negativeEffectMultiplier);
	/// Includes ordinary signed HP marginal once, then fixed-action pressure
	/// differences including native attack/retaliation effects and Morale chance.
	/// Death/removal of an action endpoint prices that action as unavailable.
	std::optional<float> marginal(const std::shared_ptr<HypotheticBattle> & outcome,
		std::span<const uint32_t> changedRecipients) const;
	size_t actionCount() const { return actions.size(); }
};
}
