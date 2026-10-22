/*
 * EnemyBypassReachability.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/ReachabilityInfo.h"
#include <memory>

class DamageCache;
class HypotheticBattle;
class CBattleInfoCallback;
namespace battle { class Unit; }

// Shared controller predicate for exchange and enemy-bypass consumers.
bool controlledByDifferentPlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second);

DLL_LINKAGE ReachabilityInfo getReachabilityWithEnemyBypass(
	const battle::Unit * activeStack, DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> state);
