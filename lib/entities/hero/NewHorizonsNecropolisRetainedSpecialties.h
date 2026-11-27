/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class JsonNode;
class CGHeroInstance;
namespace newHorizonsHeroes
{
DLL_LINKAGE bool bloodScentUsesPermille(const CGHeroInstance &);
DLL_LINKAGE int bloodScentCapPermille(const CGHeroInstance &);
DLL_LINKAGE int bloodScentCapPercent(const CGHeroInstance &);
DLL_LINKAGE int soulHarvestPercent(const CGHeroInstance *);
DLL_LINKAGE int ancientOathsDiscountPercent(const CGHeroInstance *);
}
