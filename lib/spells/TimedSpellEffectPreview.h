/* TimedSpellEffectPreview.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "SpellEffectPreview.h"
class JsonNode;
namespace newHorizonsMagic
{
/// Internal two-family adapter. Scalar inputs come from the common current-cast resolver.
bool hasTimedSpellEffectPreview(const CSpell & spell, const JsonNode & rules);
SpellEffectPreview timedSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, bool followup, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower);
}
