/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCastleAdventureSpecialties.h"
#include <optional>
class CGHeroInstance;
class JsonNode;
namespace newHorizonsHeroes
{
struct ResidualLifeRules
{
	int damagePerBuffer = 50;
	int damagePerBufferReductionPerLevel = 1;
	int minimumDamagePerBuffer = 25;
	int maximumPerCombat = 20;
	bool operator==(const ResidualLifeRules &) const = default;
};
struct DLL_LINKAGE NecropolisCombatSpecialtyRules
{
	std::optional<ResidualLifeRules> residualLife;
	std::optional<LevelScaledValue> earthboundDrain;
	std::optional<LevelScaledValue> eternalServitude;
	std::optional<LevelScaledValue> unyieldingBones;
	bool operator==(const NecropolisCombatSpecialtyRules &) const = default;
};
DLL_LINKAGE void validateNecropolisCombatSpecialtyRows(const JsonNode &);
DLL_LINKAGE NecropolisCombatSpecialtyRules necropolisCombatSpecialtyRules(const JsonNode &);
DLL_LINKAGE const NecropolisCombatSpecialtyRules * necropolisCombatRules(const CGHeroInstance *, const char *);
DLL_LINKAGE int earthboundDrainPercent(const CGHeroInstance *);
DLL_LINKAGE int eternalServitudePercent(const CGHeroInstance *);
DLL_LINKAGE int unyieldingBonesPermille(const CGHeroInstance *);
DLL_LINKAGE int residualLifeThreshold(const CGHeroInstance *);
DLL_LINKAGE int residualLifeLimit(const CGHeroInstance *);
DLL_LINKAGE bool requiresNecropolisSpecialtyFormat(const JsonNode &);
DLL_LINKAGE bool necropolisCombatRetiresHistorical(const CGHeroInstance &);
}
