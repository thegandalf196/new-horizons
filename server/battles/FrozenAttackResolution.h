/*
 * FrozenAttackResolution.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/networkPacks/SetStackEffect.h"

class CBattleInfoCallback;
class CGameHandler;
class CStack;
struct BattleAttack;

/// Borrowed inputs for one immediate prepare/commit/resolve sequence in makeAttack.
/// The defender is the already redirected target. This context owns no game state
/// and must not be retained or dispatched asynchronously.
struct FrozenAttackContext
{
	const CBattleInfoCallback & battle;
	const CStack * attacker;
	const CStack * defender;
	int distance;
	bool ranged;
	bool counterAttack;
};

/// Pre-injury facts plus the removal packet, held across BattleAttack publication.
/// This is local execution data, never serialized or used as an alternate snapshot.
struct FrozenAttackResolution
{
	SetStackEffect shatter;
	bool primaryFrozenBeforeHit = false;
	bool freezingTouch = false;
};

/// Marks the pending attack's direct hits and captures their pre-injury Frozen state.
FrozenAttackResolution prepareFrozenAttack(const FrozenAttackContext & context, BattleAttack & attack);

/// Authoritative continuation: caller must publish that BattleAttack first.
/// Removes Shatter, then rolls Freezing Touch before later attack reactions.
void resolveFrozenAttack(CGameHandler & gameHandler, const FrozenAttackContext & context,
	FrozenAttackResolution & resolution);
