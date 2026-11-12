/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OrderFormation.h"
#include "HeroCommand.h"
#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace orderFormation
{
namespace
{
void fields(const JsonNode & value, std::initializer_list<const char *> keys)
{
	if(!value.isStruct() || value.Struct().size() != keys.size()) throw std::runtime_error("Invalid formation formula fields");
	for(const auto * key : keys)
		if(!value.Struct().contains(key)) throw std::runtime_error("Missing formation formula field");
}
int integer(const JsonNode & value, int low, int high)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < low || value.Integer() > high)
		throw std::runtime_error("Invalid formation integer");
	return value.Integer();
}
double coefficient(const JsonNode & value)
{
	if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < 0 || value.Float() > 1000)
		throw std::runtime_error("Invalid formation coefficient");
	return value.Float();
}
JsonNode legacyDefinition(JsonNode result, HeroCommand order)
{
	if(order == HeroCommand::FEINT)
	{
		auto & feint = result["effects"]["retaliationDamageReductionPercent"];
		feint["leadership"] = feint["formationRating"];
		feint.Struct().erase("formationRating");
	}
	if(order == HeroCommand::VANGUARD)
	{
		auto & duration = result["duration"];
		duration["leadershipPerRound"] = duration["formationRatingPerRound"];
		duration.Struct().erase("formationRatingPerRound");
		duration.Struct().erase("base");
	}
	if(order == HeroCommand::SECOND_WIND)
	{
		auto & wind = result["effects"]["additionalActivationDamagePercent"];
		wind.Struct().erase("formationRating");
		wind.Struct().erase("cap");
		wind["attack"].Integer() = 0;
		wind["defense"].Integer() = 0;
	}
	return result;
}
}

int64_t rating(const JsonNode & rules, int attack, int defense, int64_t command)
{
	const auto & formula = rules["formationRating"];
	integer(formula["version"], 1, 1);
	const int divisor = integer(formula["attributeDivisor"], 1, 1000);
	const int multiplier = integer(formula["commandMultiplier"], 0, 1000);
	const int64_t primary = (static_cast<int64_t>(std::max(0, attack)) + std::max(0, defense)) / divisor;
	command = std::max<int64_t>(0, command);
	if(multiplier && command > (std::numeric_limits<int64_t>::max() - primary) / multiplier)
		throw std::overflow_error("Formation rating overflow");
	return primary + command * multiplier;
}

int64_t command(const CGHeroInstance & hero)
{
	const auto captured = hero.getArmyCommand();
	if(!captured) return 0;
	return captured->effective();
}

int secondWindPercent(const JsonNode & rules, int attack, int defense, int64_t command, int efficiency,
	bool crownAndAltar)
{
	const auto & formula = rules["commands"]["secondWind"]["effects"]["additionalActivationDamagePercent"];
	const double derived = coefficient(formula["formationRating"]) * rating(rules, attack, defense, command)
		* std::max(0, efficiency) / 100.0 * (crownAndAltar ? 1.2 : 1.0);
	return static_cast<int>(std::clamp(coefficient(formula["base"]) + std::round(derived),
		0.0, static_cast<double>(integer(formula["cap"], 0, 100))));
}

heroCommands::RevisedOrderSnapshot snapshot(const JsonNode & rules, HeroCommand order,
	int32_t round, int attack, int defense, int64_t command,
	int attackEfficiency, int defenseEfficiency, int formationEfficiency, bool crown)
{
	const int64_t rawRating = rating(rules, attack, defense, command);
	const auto original = legacyDefinition(rules["commands"][heroCommands::key(order)], order);
	// Crown's established potency treatment remains; duration has no Crown multiplier.
	const auto definition = crown ? heroCommands::crownRevisedOrderDefinition(original, order) : original;
	auto result = heroCommands::scaledRevisedOrderSnapshot(order, round, attack, defense, rawRating,
		definition, attackEfficiency, defenseEfficiency, formationEfficiency);
	if(order == HeroCommand::VANGUARD)
	{
		const auto & duration = rules["commands"]["vanguard"]["duration"];
		const double derived = static_cast<double>(rawRating) * std::max(0, formationEfficiency) / 100.0;
		const int rounds = static_cast<int>(std::clamp(1.0
			+ std::floor(derived / coefficient(duration["formationRatingPerRound"])),
			duration["minimum"].Float(), duration["maximum"].Float()));
		if(round > std::numeric_limits<int32_t>::max() - rounds + 1) throw std::overflow_error("Vanguard expiry overflow");
		result.expiresAfterRound = round + rounds - 1;
	}
	return result;
}

void validateRules(const JsonNode & rules)
{
	fields(rules, {"schemaVersion", "rulesetVersion", "commands", "perkBonuses", "warfarePolicyVersion", "formationRating"});
	integer(rules["warfarePolicyVersion"], 1, 1);
	fields(rules["formationRating"], {"version", "attributeDivisor", "commandMultiplier"});
	(void)rating(rules, 0, 0, 0);
	const auto & feint = rules["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"];
	fields(feint, {"base", "formationRating", "cap"});
	coefficient(feint["formationRating"]);
	const auto & wind = rules["commands"]["secondWind"]["effects"]["additionalActivationDamagePercent"];
	fields(wind, {"base", "formationRating", "cap"});
	coefficient(wind["formationRating"]);
	integer(wind["cap"], 0, 100);
	const auto & duration = rules["commands"]["vanguard"]["duration"];
	fields(duration, {"formationRatingPerRound", "minimum", "maximum"});
	if(coefficient(duration["formationRatingPerRound"]) <= 0) throw std::runtime_error("Invalid formation duration divisor");
	// Validate the unchanged roster, targeting, potency, bounds and fixed bases too.
	auto legacy = rules;
	legacy.Struct().erase("warfarePolicyVersion");
	legacy.Struct().erase("formationRating");
	legacy["commandResourcePolicyVersion"].Integer() = 1;
	for(const auto order : {HeroCommand::FEINT, HeroCommand::VANGUARD, HeroCommand::SECOND_WIND})
		legacy["commands"][heroCommands::key(order)] = legacyDefinition(rules["commands"][heroCommands::key(order)], order);
	heroCommands::validateRevisedRules(legacy);
}
}
