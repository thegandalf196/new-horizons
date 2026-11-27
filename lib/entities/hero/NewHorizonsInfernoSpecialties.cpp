/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "NewHorizonsLevelScaledRows.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"

namespace newHorizonsHeroes
{
namespace
{
void infernoFields(const JsonNode & row, const char * key, std::initializer_list<std::string_view> fields)
{
	if(!row.Struct().contains(key)) return;
	const auto & child = row[key];
	detail::fields(child, fields);
	for(const auto field : fields)
		detail::require(detail::integer(child[std::string(field)], 1, 100), "Inferno specialty value");
}
int infernoLevel(const CGHeroInstance * hero)
{
	return hero ? std::max(1, static_cast<int>(hero->level)) : 1;
}
}
void validateInfernoSpecialtyRows(const JsonNode & row)
{
	infernoFields(row, "cutpurse", {"basePercent", "percentPerLevelBasisPoints", "maximumPercent"});
	infernoFields(row, "sulfurSense", {"baseSulfur", "levelsPerAdditionalSulfur"});
	infernoFields(row, "kindling", {"percentPerCast", "capPermillePerLevel", "maximumPercent"});
	infernoFields(row, "markedForTheKill", {"stacks", "levelsPerStack"});
	infernoFields(row, "bloodfire", {"attack", "attackPerFiveLevels", "maximumAttack", "duration"});
	infernoFields(row, "burnAway", {"effects", "levelsPerEffect", "maximumEffects"});
	if(row.Struct().contains("cutpurse"))
		detail::require(row["cutpurse"]["maximumPercent"].Integer() >= row["cutpurse"]["basePercent"].Integer(), "Cutpurse cap");
	if(row.Struct().contains("bloodfire"))
		detail::require(row["bloodfire"]["maximumAttack"].Integer() >= row["bloodfire"]["attack"].Integer(), "Bloodfire cap");
	if(row.Struct().contains("burnAway"))
		detail::require(row["burnAway"]["maximumEffects"].Integer() >= row["burnAway"]["effects"].Integer(), "Burn Away cap");
	if(row.Struct().contains("hellfireBattery"))
	{
		const auto & child = row["hellfireBattery"];
		detail::fields(child, {"firePermille", "firePermillePerLevel", "maximumFirePermille"});
		detail::requireScaledRow(child, "firePermille", "firePermillePerLevel", "maximumFirePermille", 1000, "Hellfire Battery");
	}
}
InfernoSpecialtyRules infernoSpecialtyRules(const JsonNode & development)
{
	InfernoSpecialtyRules result;
	if(!usesRules(development) || !development.Struct().contains("revisedSpecialties")) return result;
	const auto & row = development["revisedSpecialties"];
	validateInfernoSpecialtyRows(row);
	const auto value = [&](const char * key, const char * field) { return static_cast<int>(row[key][field].Integer()); };
	if(row.Struct().contains("cutpurse"))
		result.cutpurse = CutpurseRules{value("cutpurse", "basePercent"), value("cutpurse", "percentPerLevelBasisPoints"), value("cutpurse", "maximumPercent")};
	if(row.Struct().contains("sulfurSense"))
		result.sulfurSense = SulfurSenseRules{value("sulfurSense", "baseSulfur"), value("sulfurSense", "levelsPerAdditionalSulfur")};
	if(row.Struct().contains("kindling"))
		result.kindling = KindlingRules{value("kindling", "percentPerCast"), value("kindling", "capPermillePerLevel"), value("kindling", "maximumPercent")};
	if(row.Struct().contains("markedForTheKill"))
		result.markedForTheKill = MarkedForTheKillRules{value("markedForTheKill", "stacks"), value("markedForTheKill", "levelsPerStack")};
	if(row.Struct().contains("bloodfire"))
		result.bloodfire = BloodfireRules{value("bloodfire", "attack"), value("bloodfire", "attackPerFiveLevels"), value("bloodfire", "maximumAttack"), value("bloodfire", "duration")};
	if(row.Struct().contains("burnAway"))
		result.burnAway = BurnAwayRules{value("burnAway", "effects"), value("burnAway", "levelsPerEffect"), value("burnAway", "maximumEffects")};
	if(row.Struct().contains("hellfireBattery"))
		result.hellfireBattery = detail::scaledRow(row["hellfireBattery"], "firePermille", "firePermillePerLevel", "maximumFirePermille");
	return result;
}
const InfernoSpecialtyRules * infernoRules(const CGHeroInstance * hero, const char * key)
{
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == key && hero->compiledDevelopment()
		? &hero->compiledDevelopment()->infernoSpecialties : nullptr;
}
bool usesKindlingHistory(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:xyron");
	return rules && rules->kindling.has_value();
}
int hellfireBatteryFirePermille(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:pyre");
	return rules && rules->hellfireBattery ? rules->hellfireBattery->at(infernoLevel(hero)) : 0;
}
int markedForTheKillStacks(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:olema");
	return rules && rules->markedForTheKill ? rules->markedForTheKill->stacks + infernoLevel(hero) / rules->markedForTheKill->levelsPerStack : 0;
}
int bloodfireAttack(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:ash");
	return rules && rules->bloodfire ? std::min(rules->bloodfire->maximumAttack,
		rules->bloodfire->attack + infernoLevel(hero) / 5 * rules->bloodfire->attackPerFiveLevels) : 0;
}
int bloodfireDuration(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:ash");
	return rules && rules->bloodfire ? rules->bloodfire->duration : 0;
}
int burnAwayEffects(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:xarfax");
	return rules && rules->burnAway ? std::min(rules->burnAway->maximumEffects,
		rules->burnAway->effects + infernoLevel(hero) / rules->burnAway->levelsPerEffect) : 0;
}
bool infernoSpecialtyRetiresHistorical(const CGHeroInstance & hero)
{
	if(!hero.getHeroType() || !hero.compiledDevelopment()) return false;
	const auto key = hero.getHeroType()->getJsonKey();
	const auto & rules = hero.compiledDevelopment()->infernoSpecialties;
	return (key == "core:pyre" && rules.hellfireBattery)
		|| (key == "core:xyron" && rules.kindling)
		|| (key == "core:olema" && rules.markedForTheKill)
		|| (key == "core:ash" && rules.bloodfire)
		|| (key == "core:xarfax" && rules.burnAway)
		|| (key == "core:octavia" && rules.cutpurse)
		|| (key == "core:calid" && rules.sulfurSense);
}
}
