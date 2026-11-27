/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsNecropolisCombatSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLevelScaledRows.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"
namespace newHorizonsHeroes
{
void validateNecropolisCombatSpecialtyRows(const JsonNode & row)
{
	for(const auto * key : {"bloodScent", "soulHarvest", "ancientOathsDiscount"})
	{
		if(!row.Struct().contains(key)) continue;
		const auto & value = row[key];
		if(std::string_view(key) == "soulHarvest")
		{
			detail::fields(value, {"percentPerLevel"});
			detail::require(detail::integer(value["percentPerLevel"], 1, 100), "Soul Harvest growth");
		}
		else if(std::string_view(key) == "bloodScent")
		{
			detail::fields(value, {"baseCapPermille", "capPermillePerLevel", "maximumCapPermille"});
			detail::requireScaledRow(value, "baseCapPermille", "capPermillePerLevel", "maximumCapPermille", 1000, key);
		}
		else
		{
			detail::fields(value, {"basePercent", "percentPerLevel", "maximumPercent"});
			detail::requireScaledRow(value, "basePercent", "percentPerLevel", "maximumPercent", 100, key);
		}
	}
	if(row.Struct().contains("residualLife"))
	{
		const auto & value = row["residualLife"];
		detail::fields(value, {"damagePerBuffer", "damagePerBufferReductionPerLevel", "minimumDamagePerBuffer", "maximumPerCombat"});
		for(const auto * key : {"damagePerBuffer", "damagePerBufferReductionPerLevel", "minimumDamagePerBuffer", "maximumPerCombat"})
			detail::require(detail::integer(value[key], 1, 10000), "Residual Life coefficient");
		detail::require(value["minimumDamagePerBuffer"].Integer() <= value["damagePerBuffer"].Integer(), "Residual Life threshold");
	}
	if(row.Struct().contains("earthboundDrain"))
	{
		const auto & value = row["earthboundDrain"];
		detail::fields(value, {"percentPerLevel", "maximumPercent"});
		detail::require(detail::integer(value["percentPerLevel"], 1, 100), "Earthbound Drain growth");
		detail::require(detail::integer(value["maximumPercent"], 1, 100), "Earthbound Drain cap");
	}
	for(const auto * key : {"eternalServitude", "unyieldingBones"})
	{
		if(!row.Struct().contains(key)) continue;
		const auto & value = row[key];
		const bool permille = std::string_view(key) == "unyieldingBones";
		const auto * base = permille ? "basePermille" : "basePercent";
		const auto * growth = permille ? "permillePerLevel" : "percentPerLevel";
		const auto * maximum = permille ? "maximumPermille" : "maximumPercent";
		detail::fields(value, {base, growth, maximum});
		detail::requireScaledRow(value, base, growth, maximum, permille ? 750 : 100, key);
	}
}
NecropolisCombatSpecialtyRules necropolisCombatSpecialtyRules(const JsonNode & development)
{
	NecropolisCombatSpecialtyRules result;
	if(!usesRules(development) || !development.Struct().contains("revisedSpecialties")) return result;
	const auto & row = development["revisedSpecialties"];
	validateNecropolisCombatSpecialtyRows(row);
	if(row.Struct().contains("residualLife"))
	{
		const auto & value = row["residualLife"];
		result.residualLife = ResidualLifeRules{static_cast<int>(value["damagePerBuffer"].Integer()),
			static_cast<int>(value["damagePerBufferReductionPerLevel"].Integer()),
			static_cast<int>(value["minimumDamagePerBuffer"].Integer()), static_cast<int>(value["maximumPerCombat"].Integer())};
	}
	if(row.Struct().contains("earthboundDrain"))
		result.earthboundDrain = LevelScaledValue{static_cast<int>(row["earthboundDrain"]["percentPerLevel"].Integer()),
			static_cast<int>(row["earthboundDrain"]["percentPerLevel"].Integer()), static_cast<int>(row["earthboundDrain"]["maximumPercent"].Integer())};
	if(row.Struct().contains("eternalServitude"))
		result.eternalServitude = detail::scaledRow(row["eternalServitude"], "basePercent", "percentPerLevel", "maximumPercent");
	if(row.Struct().contains("unyieldingBones"))
		result.unyieldingBones = detail::scaledRow(row["unyieldingBones"], "basePermille", "permillePerLevel", "maximumPermille");
	return result;
}
const NecropolisCombatSpecialtyRules * necropolisCombatRules(const CGHeroInstance * hero, const char * key)
{
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == key && hero->compiledDevelopment()
		? &hero->compiledDevelopment()->necropolisCombatSpecialties : nullptr;
}
int earthboundDrainPercent(const CGHeroInstance * hero)
{
	const auto * rules = necropolisCombatRules(hero, "core:aislinn");
	return rules && rules->earthboundDrain ? rules->earthboundDrain->at(hero->level) : 0;
}
int eternalServitudePercent(const CGHeroInstance * hero)
{
	const auto * rules = necropolisCombatRules(hero, "core:thant");
	return rules && rules->eternalServitude ? rules->eternalServitude->at(hero->level) : 0;
}
int unyieldingBonesPermille(const CGHeroInstance * hero)
{
	const auto * rules = necropolisCombatRules(hero, "core:xsi");
	return rules && rules->unyieldingBones ? rules->unyieldingBones->at(hero->level) : 0;
}
int residualLifeThreshold(const CGHeroInstance * hero)
{
	const auto * rules = necropolisCombatRules(hero, "core:septienna");
	if(!rules || !rules->residualLife) return 0;
	const auto & row = *rules->residualLife;
	return static_cast<int>(std::max<int64_t>(row.minimumDamagePerBuffer,
		static_cast<int64_t>(row.damagePerBuffer) - static_cast<int64_t>(std::max(1, static_cast<int>(hero->level))) * row.damagePerBufferReductionPerLevel));
}
int residualLifeLimit(const CGHeroInstance * hero)
{
	const auto * rules = necropolisCombatRules(hero, "core:septienna");
	return rules && rules->residualLife ? rules->residualLife->maximumPerCombat : 0;
}
bool requiresNecropolisSpecialtyFormat(const JsonNode & document)
{
	if(!document.isStruct()) return false;
	const auto claims = [](const JsonNode & rules)
	{
		const auto starts = rules.Struct().find("startingSkills");
		if(starts != rules.Struct().end() && starts->second.isStruct())
		{
			const auto books = starts->second.Struct().find("startingBookReplacements");
			if(books != starts->second.Struct().end() && books->second.isStruct()
				&& books->second.Struct().contains("core:aislinn"))
				return true;
		}
		const auto found = rules.Struct().find("revisedSpecialties");
		if(found == rules.Struct().end() || !found->second.isStruct()) return false;
		for(const auto * key : {"hoardedPower", "residualLife", "earthboundDrain", "eternalServitude", "unyieldingBones",
			"bloodScent", "soulHarvest", "ancientOathsDiscount"})
			if(found->second.Struct().contains(key)) return true;
		return false;
	};
	if(claims(document)) return true;
	const auto heroes = document.Struct().find("heroes");
	if(heroes == document.Struct().end() || !heroes->second.isStruct()) return false;
	for(const auto * key : {"newHorizons", "newHorizonsPerks"})
	{
		const auto found = heroes->second.Struct().find(key);
		if(found != heroes->second.Struct().end() && found->second.isStruct() && claims(found->second)) return true;
	}
	return false;
}
bool necropolisCombatRetiresHistorical(const CGHeroInstance & hero)
{
	if(const auto * row = necropolisCombatRules(&hero, "core:septienna"); row && row->residualLife) return true;
	if(const auto * row = necropolisCombatRules(&hero, "core:aislinn"); row && row->earthboundDrain) return true;
	if(const auto * row = necropolisCombatRules(&hero, "core:thant"); row && row->eternalServitude) return true;
	if(const auto * row = necropolisCombatRules(&hero, "core:xsi"); row && row->unyieldingBones) return true;
	return false;
}
}
