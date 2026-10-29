/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../lib/battle/DreadCavalryState.h"
#include "../../lib/battle/BattleUnitTurnReason.h"
#include "../../lib/battle/BattleHex.h"
#include <memory>
class Environment;
class CBattleInfoCallback;
class HypotheticBattle;
class AttackPossibility;
class DamageCache;
class CStack;
struct BattleAction;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace dreadCavalryAI
{
/// Immutable admission inputs across the same action's movement/reaction recursion.
/// Each candidate copies this value before recording its own first killed victim.
struct Action
{
	battle::DreadCavalryState before;
	BattleSide controller = BattleSide::NONE;
	uint32_t actor = 0;
	uint32_t firstVictim = battle::DreadCavalryState::NO_VICTIM;
	int movementBudget = 0;
	int movementCost = 0;
	bool enabled = false;
	bool luckyBefore = false;
	bool anyKilled = false;
};
struct ContinuationValue { float value = 0; float ownDamage = 0; float enemyDamage = 0; };
bool hasActivationState(const battle::Unit * unit);
bool acceptsBlow(const std::shared_ptr<HypotheticBattle> & model, const Action & action,
	const battle::Unit * source, const battle::Unit * target);
void retireForActorHandoff(HypotheticBattle & world, uint32_t nextActor, BattleUnitTurnReason reason);
/// Complete at most one optional continuation within the same activation.
ContinuationValue projectPending(const std::shared_ptr<HypotheticBattle> & world, uint32_t actor, DamageCache & cache);
void captureOutput(const CBattleInfoCallback & battle, BattleAttackInfo & attack);
Action begin(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, BattleHex from);
void observeDeath(const CBattleInfoCallback & battle, Action & action, const battle::Unit & source,
	const battle::Unit & target, bool wasAlive, bool mayRebirth);
void finish(AttackPossibility & preview, const Action & action, bool vanishKill);
BattleAction chooseContinuation(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & battle, const CStack * source);
}
