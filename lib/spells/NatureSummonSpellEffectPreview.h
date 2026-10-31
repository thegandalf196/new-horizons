/* NatureSummonSpellEffectPreview.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "SpellEffectPreview.h"
namespace newHorizonsMagic
{
std::optional<SpellEffectPreview> natureSummonSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower);
}
