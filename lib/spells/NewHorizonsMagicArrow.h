/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"
#include <optional>
#include <string_view>

class CGHeroInstance;

namespace newHorizonsMagic
{
struct DLL_LINKAGE MagicArrowOverchargeModifiers
{
	int maximumBonus = 0;
	bool operator==(const MagicArrowOverchargeModifiers &) const = default;
};

DLL_LINKAGE bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell);
DLL_LINKAGE bool requiresMagicArrowChargeRules(const JsonNode & rules);
DLL_LINKAGE void validateMagicArrowChargeSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE void validateMagicArrowSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row);
DLL_LINKAGE MagicArrowOverchargeModifiers magicArrowOverchargeModifiers(const CGHeroInstance * hero);
/// Supply the unscaled hero Spell Power rating, not School/Spellcraft or cast bonuses.
DLL_LINKAGE int magicArrowMaxOvercharge(const JsonNode & rules, SpellID spell, int32_t rawSpellPower,
	MagicArrowOverchargeModifiers modifiers = MagicArrowOverchargeModifiers{});
DLL_LINKAGE bool magicArrowSelectionValid(const JsonNode & rules, SpellID spell,
	const CGHeroInstance * hero, int32_t effectPower, int overcharge, bool manualHeroRoute = true);
/// Missing means invalid selection/overflow; the ordinary cost already includes its normal discounts.
DLL_LINKAGE std::optional<int32_t> magicArrowManaCost(const JsonNode & rules, SpellID spell,
	int32_t ordinaryCost, int overcharge);
DLL_LINKAGE int magicArrowMaximumAffordableOvercharge(const JsonNode & rules, SpellID spell,
	int32_t rawSpellPower, int32_t ordinaryCost, int32_t availableMana,
	MagicArrowOverchargeModifiers modifiers = MagicArrowOverchargeModifiers{});
/// Damage power can include accepted cast bonuses; capPower remains the raw attribute.
/// A negative omitted capPower uses damage power for standalone callers.
DLL_LINKAGE std::optional<int64_t> magicArrowDamageBasisPoints(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers = MagicArrowOverchargeModifiers{},
	int coefficientBasisPoints = 10000, int empowerPercent = 0, int damageSpecialtyPercent = 0,
	int32_t capPower = -1);
DLL_LINKAGE std::optional<int64_t> magicArrowDamage(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers = MagicArrowOverchargeModifiers{},
	int coefficientPercent = 100, int empowerPercent = 0);
}
