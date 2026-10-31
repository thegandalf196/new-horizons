/* NewHorizonsTimedBuffMagnitude.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
#include "../bonuses/BonusEnum.h"
#include <optional>
class CSpell;
class CGHeroInstance;
namespace newHorizonsMagic
{
/// One loaded additive timed source; ambiguous/non-additive definitions are not guessed.
DLL_LINKAGE std::optional<double> configuredAdditiveTimedValue(const CSpell & spell, int32_t effectLevel, BonusType type);
/// Known caster-only tail of the legacy timed-enchantment specialty pipeline.
DLL_LINKAGE std::optional<double> casterEnchantValueOverride(const CGHeroInstance * hero, SpellID spell, double value);
DLL_LINKAGE double casterEnchantValue(const CGHeroInstance * hero, SpellID spell, double value);
}
