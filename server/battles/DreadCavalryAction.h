/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/DreadCavalryState.h"
class CGameHandler;
class CBattleInfoCallback;
class CStack;
struct BattleAttack;
namespace dreadCavalryAction
{
/// Ephemeral provenance for one accepted ordinary melee action, never serialized.
struct Context
{
	battle::DreadCavalryState before;
	BattleSide controller = BattleSide::NONE;
	uint32_t actor = battle::DreadCavalryState::NO_VICTIM;
	uint32_t firstVictim = battle::DreadCavalryState::NO_VICTIM;
	int percent = 100;
	int movementBudget = 0;
	int movementSpent = 0;
	int newAllowance = 0;
	int sharedTailCap = 0;
	int vanish = 0;
	bool enabled = false;
};
Context begin(const CBattleInfoCallback & battle, const CStack * source, bool allowContinuation);
bool acceptedBlowStillLegal(const CBattleInfoCallback & battle, const CStack * source,
	const CStack * target, const Context & context);
/// Only the ordinary offensive action and its native physical Cleave pass this
/// context to makeAttack; reactions and spell casts never receive it.
void observeKilled(const CBattleInfoCallback & battle, const CStack * source,
	const BattleAttack & resolved, Context & context);
void complete(CGameHandler & handler, const CBattleInfoCallback & battle,
	const Context & context);
void decline(CGameHandler & handler, const CBattleInfoCallback & battle, const CStack * source);
}
