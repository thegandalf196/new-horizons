/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "ConsecratedArms.h"
#include "BattleSide.h"
class CBattleInfoCallback;
class IBattleState;
struct SetStackEffect;
namespace consecratedArms
{
/// No RNG or state mutation. Called only after native Order acceptance.
/// One packet replaces refreshed prayer records atomically; shields are separate
/// state and must not be cleared between its removal and addition stages.
DLL_LINKAGE SetStackEffect orderEffects(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & recipients, const std::vector<Prayer> & selected,
	const Policy & policy, int actualFaith);
/// The candidates that can hold a prayer (living, owned, ordinary stacks), sorted and unique.
DLL_LINKAGE std::vector<uint32_t> prayerHolders(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & candidates);
/// Version 4: the chosen prayer(s) reach only `chosenRecipient`; Living Liturgy
/// still refreshes existing prayers on every other recipient of the Order.
DLL_LINKAGE SetStackEffect chosenOrderEffects(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<uint32_t> & recipients, uint32_t chosenRecipient, const std::vector<Prayer> & selected,
	const Policy & policy, int actualFaith);
/// Rebuilds only materialized Divine Purpose components after a prayer expires.
DLL_LINKAGE void refreshDivinePurpose(IBattleState & state, const battle::Unit & unit);
}
