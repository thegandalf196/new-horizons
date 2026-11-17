/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "NewHorizonsFocusMagic.h"
#include <optional>
#include <span>

namespace newHorizonsFocusMagic
{
/// Owned native Focus records only. Admission excludes executable/shared payloads.
struct DLL_LINKAGE ValueState
{
	int charges = 0;
	bool converted = false;
	std::vector<Bonus> records;
};
struct DLL_LINKAGE StrikeTransition
{
	ValueState after;
	Consumption effects;
	bool focusRanged = false;
	bool convertedRanged = false;
	bool spendNativeAmmo = false;
};
struct DLL_LINKAGE EnchantmentTransition
{
	ValueState after;
	Consumption effects;
};
/// Empty records are valid absence; unsupported native shapes throw before publication.
DLL_LINKAGE ValueState valueState(std::vector<Bonus> records);
/// nullopt means reference fallback, not absence. No Unit/provider survives capture.
DLL_LINKAGE std::optional<ValueState> captureValueState(const battle::Unit & unit);
/// A miss/zero damage is still accepted. Preserve final-shot mode before removal.
DLL_LINKAGE StrikeTransition prepareStrike(const ValueState &, bool accepted, bool ranged);
DLL_LINKAGE EnchantmentTransition prepareEnchantment(const ValueState &, bool nativeShooter,
	SpellID spell, int32_t additionalRangedPercent = 0);
/// Shared pure recipe; callers determine the original shooter excluding Focus.
DLL_LINKAGE std::vector<Bonus> enchantment(bool nativeShooter, SpellID spell, int32_t additionalRangedPercent = 0);
/// Native bridge retains arbitrary historical records; typed forecasts admit first.
DLL_LINKAGE Consumption consumeRecords(std::span<const Bonus> records, int remaining);
inline bool spendsNativeAmmo(bool accepted, bool ranged, bool convertedBeforeStrike)
{
	return accepted && ranged && !convertedBeforeStrike;
}
}
