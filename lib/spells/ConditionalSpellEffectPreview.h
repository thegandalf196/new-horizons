/* ConditionalSpellEffectPreview.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "SpellEffectPreview.h"
class JsonNode;
namespace newHorizonsMagic
{
std::optional<SpellEffectPreview> conditionalSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t directCoefficient, int32_t numericalCoefficient,
	int32_t warcasting, int32_t empower, const CBattleInfoCallback * callback);
}
