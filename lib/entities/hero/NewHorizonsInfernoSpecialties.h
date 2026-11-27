/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCastleAdventureSpecialties.h"
#include <optional>
class JsonNode;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct CutpurseRules
{
	int basePercent = 0;
	int percentPerLevelBasisPoints = 0;
	int maximumPercent = 0;
	bool operator==(const CutpurseRules &) const = default;
};
struct SulfurSenseRules
{
	int baseSulfur = 0;
	int levelsPerAdditionalSulfur = 0;
	bool operator==(const SulfurSenseRules &) const = default;
};
struct KindlingRules
{
	int percentPerCast = 0;
	int capPermillePerLevel = 0;
	int maximumPercent = 0;
	bool operator==(const KindlingRules &) const = default;
};
struct MarkedForTheKillRules
{
	int stacks = 0;
	int levelsPerStack = 0;
	bool operator==(const MarkedForTheKillRules &) const = default;
};
struct BloodfireRules
{
	int attack = 0;
	int attackPerFiveLevels = 0;
	int maximumAttack = 0;
	int duration = 0;
	bool operator==(const BloodfireRules &) const = default;
};
struct BurnAwayRules
{
	int effects = 0;
	int levelsPerEffect = 0;
	int maximumEffects = 0;
	bool operator==(const BurnAwayRules &) const = default;
};
struct DLL_LINKAGE InfernoSpecialtyRules
{
	std::optional<CutpurseRules> cutpurse;
	std::optional<SulfurSenseRules> sulfurSense;
	std::optional<KindlingRules> kindling;
	std::optional<MarkedForTheKillRules> markedForTheKill;
	std::optional<BloodfireRules> bloodfire;
	std::optional<BurnAwayRules> burnAway;
	std::optional<LevelScaledValue> hellfireBattery;
	bool operator==(const InfernoSpecialtyRules &) const = default;
};
DLL_LINKAGE void validateInfernoSpecialtyRows(const JsonNode &);
DLL_LINKAGE InfernoSpecialtyRules infernoSpecialtyRules(const JsonNode &);
/// Captured own-hero identity is required, so historical captures stay inert.
DLL_LINKAGE const InfernoSpecialtyRules * infernoRules(const CGHeroInstance *, const char * heroKey);
DLL_LINKAGE bool usesKindlingHistory(const CGHeroInstance *);
DLL_LINKAGE int hellfireBatteryFirePermille(const CGHeroInstance *);
DLL_LINKAGE int markedForTheKillStacks(const CGHeroInstance *);
DLL_LINKAGE int bloodfireAttack(const CGHeroInstance *);
DLL_LINKAGE int bloodfireDuration(const CGHeroInstance *);
DLL_LINKAGE int burnAwayEffects(const CGHeroInstance *);
DLL_LINKAGE bool infernoSpecialtyRetiresHistorical(const CGHeroInstance &);
}
