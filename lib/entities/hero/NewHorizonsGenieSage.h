/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include "../../GameConstants.h"
#include <array>
class JsonNode;
class CGHeroInstance;
class CHero;
struct Bonus;
namespace battle { class Unit; }
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE void validateGenieSageSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool usesGenieSage(const JsonNode & rules);
DLL_LINKAGE bool hasSageOfTheAges(const CGHeroInstance * hero);
DLL_LINKAGE bool hasWheelOfFortune(const CGHeroInstance * hero);
DLL_LINKAGE std::array<int, GameConstants::PRIMARY_SKILLS> sageGrowth(const CGHeroInstance & hero);
DLL_LINKAGE std::array<int, GameConstants::PRIMARY_SKILLS> completeSageGrowth(const CGHeroInstance & hero,
	std::array<int, GameConstants::PRIMARY_SKILLS> gains);
DLL_LINKAGE void refreshWheelOfFortune(CGHeroInstance & hero);
DLL_LINKAGE SpellID genieLuckSpell(bool hostile);
DLL_LINKAGE bool isGenieLuckSpell(SpellID spell);
DLL_LINKAGE bool isExplicitGenieLuckCast(const battle::Unit & caster, SpellID spell);
DLL_LINKAGE SpellID genieLuckStatusSpell(const battle::Unit & unit, SpellID family);
DLL_LINKAGE bool countersGenieLuck(const Bonus & bonus, SpellID ordinaryFamily);
DLL_LINKAGE bool resolveGenieSagePresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result);
}
