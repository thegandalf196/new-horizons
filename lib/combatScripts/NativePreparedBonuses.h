/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NativeDamageFacts.h"
class BonusList;
class CSelector;
namespace nativeDamage
{
/// Shared native stat assembly; environment contribution is an explicit captured input.
DLL_LINKAGE int ordinaryStatWithWarfare(int ordinary, int warfare);
DLL_LINKAGE CSelector preparedRangeSelector(BonusType type, bool shooting);
/// Native selectors, stacking and totalValue reducers only. This is a refresh-
/// boundary operation, never a per-damage-probe bonus provider/query layer.
/// effective is already native-limited and globally stacked, with output aliases
/// preserved. base owns identity/current health/count; no provider is read.
DLL_LINKAGE std::optional<PreparedUnit> prepareOwnedBonuses(const BonusList & effective,
	PreparedUnit base, int warfare, bool shooting,
	const std::bitset<bonusTypeCount> * dirty = nullptr);
}
