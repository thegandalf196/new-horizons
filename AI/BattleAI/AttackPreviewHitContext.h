/*
 * AttackPreviewHitContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class HypotheticBattle;
class BattleAttackInfo;
struct FortuneStrikeProjection;
namespace battle { class CUnitState; }
namespace newHorizonsArmorer { struct ArmorerLastStandDamageResult; }

/// Synchronous borrowed context for the existing preview's Last Stand/Luck
/// hit phases. The caller owns the real effect branch and activation flag for
/// this object's entire lifetime; no callback, native/configuration lease or
/// independent graph is synthesized. Unknown Luck remains unknown.
class AttackPreviewHitContext
{
	const std::shared_ptr<HypotheticBattle> & fortunePreview;
	bool & endedActiveActivationByLastStand;
public:
	AttackPreviewHitContext(const std::shared_ptr<HypotheticBattle> & branch, bool & endedActivation);
	newHorizonsArmorer::ArmorerLastStandDamageResult resolveLastStandDamage(
		const BattleAttackInfo & attack, battle::CUnitState * target, int64_t damage) const;
	void applyLastStandDefend(battle::CUnitState * target, bool reactiveForLastStand) const;
	bool captureAndProjectFortuneStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits,
		battle::CUnitState * actor, FortuneStrikeProjection & receipt) const;
};
