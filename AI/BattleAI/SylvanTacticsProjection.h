/*
 * SylvanTacticsProjection.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../lib/battle/CUnitState.h"

class HypotheticBattle;
namespace sylvanTacticsAI
{
struct SecondaryHit
{
	uint32_t attackerId;
	uint32_t targetId;
	int64_t requestedDamage;
};
/// Replay a captured collateral hit without another attack, Luck roll or resource use.
void replaySecondary(HypotheticBattle & battle, const SecondaryHit & hit);

/// Only perks that mutate strike history need the existing effect branch.
bool needsProjection(const CBattleInfoCallback & battle);
bool dynamicDamage(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * defender);

/// Collect only the current strike's already selected victims. Qualifications
/// and transitions are shared with simulation; this owner only journals damage.
class StrikeProjection
{
	struct Pending
	{
		newHorizonsSylvanTactics::StrikeSnapshot snapshot;
		BattleSide side;
		std::vector<std::pair<uint32_t, int64_t>> hits;
		std::vector<uint32_t> destroyed;
	};
	HypotheticBattle * branch;
	std::vector<Pending> pending;
public:
	explicit StrikeProjection(HypotheticBattle * value) : branch(value) {}
	void damage(const CBattleInfoCallback & world, const BattleAttackInfo & attack,
		battle::CUnitState & target, int64_t & amount, battle::DamageProvenance provenance);
	/// Called at the existing physical strike boundary, before another strike.
	void finish(uint32_t attackerId);
};
}
