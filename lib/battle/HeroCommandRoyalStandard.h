/*
 * HeroCommandRoyalStandard.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>

enum class BattleSide : int8_t;
namespace battle { class Unit; }
namespace heroCommands
{
/// Entitlement follows its native paused timer, and only the captured issuing side.
DLL_LINKAGE bool hasVanguardRoyalStandardEntitlement(const battle::Unit * unit, BattleSide currentOwnerSide);
}
