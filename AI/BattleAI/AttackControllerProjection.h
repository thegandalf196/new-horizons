/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include <map>
#include <memory>
#include <vector>
class AttackPossibility;
class CBattleInfoCallback;
class DamageCache;
class HypotheticBattle;
struct BattleAttackInfo;
namespace battle { class CUnitState; }
namespace attackControllerAI
{
bool needsPreview(const CBattleInfoCallback &, const BattleAttackInfo &, BattleHex impact);
/// Shared pre-packet admission: value receipt contains no borrowed world pointers.
newHorizonsAttackControllers::Strike capture(const CBattleInfoCallback &, const BattleAttackInfo &,
	BattleHex impact, int64_t primaryDamage);
struct Injury
{
	std::shared_ptr<battle::CUnitState> before;
	int64_t healthLoss = 0;
};
void applyCripple(HypotheticBattle &, const newHorizonsAttackControllers::Strike &);
/// Deterministic accepted controller stage on an EXISTING candidate/replay world.
/// Uniform Bloodlust is deliberately left to the expected-status value probe.
std::vector<Injury> apply(HypotheticBattle &, const newHorizonsAttackControllers::Strike &);
void score(AttackPossibility &, const std::shared_ptr<HypotheticBattle> &,
	const std::vector<Injury> &, DamageCache &);
}
