/* KnownUtilitySpellEffectPreview.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "SpellEffectPreview.h"
class JsonNode;
namespace newHorizonsMagic
{
std::optional<SpellEffectPreview> knownUtilitySpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, const JsonNode & rules, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, int32_t additionalComponentPercent);
}
