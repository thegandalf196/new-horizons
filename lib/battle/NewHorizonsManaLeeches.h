/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <array>
#include <cstdint>
#include "BattleSide.h"
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsManaLeeches
{
struct DLL_LINKAGE Entitlement
{
	BattleSide beneficiary = BattleSide::NONE;
	BattleSide victim = BattleSide::NONE;
	int amount = 0;
	bool restore = false;
};
struct DLL_LINKAGE Transfer
{
	int stolen = 0;
	int restored = 0;
};
/// Eligibility uses effective allegiance and action control separately.
DLL_LINKAGE Entitlement entitlement(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary);
DLL_LINKAGE Transfer transfer(const Entitlement & rule, int64_t primaryDamage, bool sequenceSpent,
	int64_t enemyAvailable, int32_t ownNormal, int32_t ownCapacity);
}
