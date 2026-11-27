/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class JsonNode;
class CHero;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
bool resolveNecropolisSpecialtyPresentation(const CHero &, const JsonNode &,
	const CGHeroInstance *, SpecialtyPresentation &);
}
