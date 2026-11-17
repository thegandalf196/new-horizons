/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <vector>

namespace battle { class Unit; }
namespace newHorizonsFocusMagic
{
inline constexpr const char * SPELL = "new-horizons:focusMagic";
inline constexpr int MAX_CHARGES = 3;
DLL_LINKAGE bool isEnchantmentRecord(const Bonus & bonus);
/// Native boundary receipt; typed value admission must validate before retaining.
DLL_LINKAGE std::vector<Bonus> nativeRecords(const battle::Unit & unit);
DLL_LINKAGE bool isChargeMarker(const Bonus & bonus);
DLL_LINKAGE int charges(const battle::Unit & unit);
DLL_LINKAGE bool active(const battle::Unit & unit);
DLL_LINKAGE bool converted(const battle::Unit & unit);
DLL_LINKAGE std::vector<Bonus> enchantment(const battle::Unit & unit, SpellID spell, int32_t additionalRangedPercent = 0);
struct DLL_LINKAGE Consumption
{
	std::vector<Bonus> remove;
	std::vector<Bonus> add;
};
/// One accepted ranged strike, including a miss/zero damage. Never called for melee.
DLL_LINKAGE Consumption consume(const battle::Unit & unit);
}
