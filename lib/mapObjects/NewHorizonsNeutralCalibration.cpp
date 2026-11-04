/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsNeutralCalibration.h"
#include "NewHorizonsRecruitmentGrowth.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "../IGameSettings.h"
#include "../entities/hero/CHeroClassHandler.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/faction/CTownHandler.h"
#include "../entities/faction/CTown.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../json/JsonNode.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <set>

namespace newHorizonsNeutralArmies::calibration
{
namespace
{
constexpr int64_t MAX_POINT = 1000000000;

void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(std::string("Neutral Leadership calibration: ") + message);
}

int integer(const JsonNode & value, int minimum, int maximum)
{
	require(value.getType() == JsonNode::JsonType::DATA_INTEGER
		&& value.Integer() >= minimum && value.Integer() <= maximum, "invalid integer");
	return static_cast<int>(value.Integer());
}

void fields(const JsonNode & object, const std::set<std::string> & allowed)
{
	require(object.isStruct(), "expected object");
	for(const auto & [key, ignored] : object.Struct())
		require(allowed.contains(key), "unknown object field");
}

bool active(const JsonNode & value)
{
	return value.isStruct() && value.Struct().contains("enabled") && value["enabled"].Bool();
}

int64_t formationValue(const std::vector<FormationStack> & formation)
{
	int64_t total = 0;
	for(const auto & stack : formation)
	{
		require(stack.count >= 0 && stack.unitValue > 0, "invalid formation member");
		const auto value = static_cast<int64_t>(stack.count) * stack.unitValue;
		require(value <= std::numeric_limits<int64_t>::max() - total, "formation value overflow");
		total += value;
	}
	return total;
}

std::vector<const CCreature *> referenceLines(const CHeroClass & heroClass,
	const newHorizonsCreatures::CreatureCategoryRules & categories, bool categorized)
{
	const auto * faction = heroClass.faction.toFaction();
	std::vector<const CCreature *> result;
	if(!faction || !faction->town)
		return result;
	for(const auto & line : faction->town->creatures)
		if(!line.empty() && line.front().hasValue())
			result.push_back(line.front().toCreature());
	if(categorized)
		std::stable_sort(result.begin(), result.end(), [&](const auto * lhs, const auto * rhs)
		{
			const auto left = categories.lookup(lhs->getJsonKey());
			const auto right = categories.lookup(rhs->getJsonKey());
			return std::tuple(left ? static_cast<int>(left->category) : 3, lhs->getLevel())
				< std::tuple(right ? static_cast<int>(right->category) : 3, rhs->getLevel());
		});
	return result;
}

BudgetPoint classReference(const IGameSettings & settings, const CHeroClass & heroClass,
	const JsonNode & capability, const JsonNode & stage, const JsonNode & unlockWeeks,
	const newHorizonsCreatures::CreatureCategoryRules & categories)
{
	const int weeks = integer(stage["weeks"], 1, 52);
	const int level = integer(stage["heroLevel"], 1, 100);
	const int unlocked = integer(stage["unlockedLines"], 1, 7);
	const int gold = integer(stage["troopGold"], 1, 1000000);
	const int configuredGrowthPercent = newHorizonsRecruitmentGrowth::percentage(settings);
	// Zero disables real recruitment, but a captured calibration reference must
	// remain positive. Only that reference uses unscaled authored supply at zero.
	const int referenceGrowthPercent = configuredGrowthPercent == 0 ? 100 : configuredGrowthPercent;
	const auto resolved = newHorizonsHeroes::resolveCapabilityRules(capability, heroClass.getId());
	const int leadership = newHorizonsHeroes::capabilityLeadershipRating(resolved, level);
	const auto originalLines = referenceLines(heroClass, categories, false);
	const auto newLines = referenceLines(heroClass, categories, true);
	std::vector<ReferenceStack> original;
	std::vector<ReferenceStack> fieldable;
	const auto add = [&](const auto & lines, bool limited, auto & army)
	{
		for(size_t i = 0; i < lines.size() && i < static_cast<size_t>(unlocked); ++i)
		{
			const auto * creature = lines[i];
			const int activeWeeks = std::max(0, weeks - integer(unlockWeeks.Vector().at(i), 1, 52) + 1);
			const int growth = limited ? categories.weeklyBaseGrowth(creature->getJsonKey()).value_or(creature->getGrowth()) : creature->getGrowth();
			int64_t count = static_cast<int64_t>(activeWeeks) * (limited
				? newHorizonsRecruitmentGrowth::scale(growth, referenceGrowthPercent) : growth);
			if(limited)
			{
				const int requirement = newHorizonsHeroes::capabilityCreatureLeadershipRequirement(resolved, creature->getId());
				if(requirement <= 0)
					continue;
				count = std::min<int64_t>(count, leadership / requirement);
			}
			const int value = creature->getAIValue();
			const int price = creature->getFullRecruitCost()[EGameResID::GOLD];
			if(count > 0 && value > 0 && price > 0)
				army.push_back({count, value, price});
		}
	};
	add(originalLines, false, original);
	add(newLines, true, fieldable);
	// Reference capacity is independent of the encounter percentage. The caller
	// applies that percentage once, after mapping the raw encounter budget.
	const auto oldValue = affordableValue(original, gold);
	return {oldValue, std::min(oldValue, affordableValue(fieldable, gold))};
}

}

int64_t affordableValue(const std::vector<ReferenceStack> & army, int gold)
{
	require(gold >= 0, "negative reference Gold budget");
	constexpr auto maximum = std::numeric_limits<int64_t>::max();
	int64_t cost = 0;
	for(const auto & stack : army)
	{
		require(stack.available >= 0 && stack.unitValue > 0 && stack.gold > 0, "invalid reference member");
		require(stack.available <= maximum / stack.gold, "reference cost product overflow");
		const auto memberCost = stack.available * stack.gold;
		require(memberCost <= maximum - cost, "reference cost sum overflow");
		cost += memberCost;
	}
	int64_t value = 0;
	for(const auto & stack : army)
	{
		int64_t count = stack.available;
		if(cost > gold)
		{
			require(gold == 0 || stack.available <= maximum / gold, "reference affordability product overflow");
			count = stack.available * gold / cost;
		}
		require(count <= maximum / stack.unitValue, "reference value product overflow");
		const auto memberValue = count * stack.unitValue;
		require(memberValue <= maximum - value, "reference value sum overflow");
		value += memberValue;
	}
	return value;
}

JsonNode disabledSnapshot()
{
	JsonNode result;
	result["version"].Integer() = 1;
	result["enabled"].Bool() = false;
	return result;
}

void validate(const JsonNode & value)
{
	if(value.isNull() || (value.isStruct() && value.Struct().empty()))
		return;
	require(value.isStruct(), "rules must be an object");
	for(const auto & [key, ignored] : value.Struct())
		require(key == "version" || key == "enabled" || key == "curve" || key == "stages" || key == "unlockWeeks", "unknown rule field");
	integer(value["version"], 1, 1);
	require(value["enabled"].getType() == JsonNode::JsonType::DATA_BOOL, "enabled must be boolean");
	if(!active(value) && !value.Struct().contains("curve")
		&& !value.Struct().contains("stages") && !value.Struct().contains("unlockWeeks"))
		return;
	if(value.Struct().contains("curve"))
	{
		require(!value.Struct().contains("stages") && !value.Struct().contains("unlockWeeks"), "captured and generation fields cannot mix");
		require(value["curve"].isVector() && !value["curve"].Vector().empty()
			&& value["curve"].Vector().size() <= 16, "invalid captured curve");
		int64_t previousInput = 0;
		int64_t previousOutput = 0;
		for(const auto & point : value["curve"].Vector())
		{
			fields(point, {"original", "fieldable"});
			const int64_t input = integer(point["original"], 1, MAX_POINT);
			const int64_t output = integer(point["fieldable"], 1, MAX_POINT);
			require(input > previousInput && output >= previousOutput && output <= input, "nonmonotonic captured curve");
			previousInput = input;
			previousOutput = output;
		}
		return;
	}
	require(value["stages"].isVector() && !value["stages"].Vector().empty()
		&& value["stages"].Vector().size() <= 15, "missing reference stages");
	require(value["unlockWeeks"].isVector() && value["unlockWeeks"].Vector().size() == 7, "seven unlock weeks required");
	for(const auto & week : value["unlockWeeks"].Vector())
		integer(week, 1, 52);
	for(const auto & stage : value["stages"].Vector())
	{
		fields(stage, {"weeks", "heroLevel", "unlockedLines", "troopGold"});
		integer(stage["weeks"], 1, 52);
		integer(stage["heroLevel"], 1, 100);
		integer(stage["unlockedLines"], 1, 7);
		integer(stage["troopGold"], 1, 1000000);
	}
}

Rules::Rules(const JsonNode & snapshot)
{
	validate(snapshot);
	if(!active(snapshot))
		return;
	require(snapshot.Struct().contains("curve"), "generation definition used without capture");
	for(const auto & point : snapshot["curve"].Vector())
		points.push_back({point["original"].Integer(), point["fieldable"].Integer()});
}

int64_t Rules::budget(int64_t original) const
{
	require(original >= 0, "negative budget");
	if(points.empty())
		return original;
	BudgetPoint left;
	for(const auto & right : points)
	{
		if(original <= right.original)
			return left.fieldable + (original - left.original) * (right.fieldable - left.fieldable)
				/ (right.original - left.original);
		left = right;
	}
	return left.fieldable;
}

std::vector<TQuantity> Rules::counts(const std::vector<FormationStack> & formation) const
{
	require(formation.size() <= 7, "more than seven formation slots");
	std::vector<TQuantity> result;
	for(const auto & stack : formation)
		result.push_back(stack.count);
	if(!enabled())
		return result;
	const auto original = formationValue(formation);
	const auto target = budget(original);
	if(target == original || original == 0)
		return result;
	if(formation.size() == 1)
	{
		result[0] = static_cast<TQuantity>(std::max<int64_t>(1, (target + formation[0].unitValue - 1) / formation[0].unitValue));
		return result;
	}
	int64_t minimumValue = 0;
	for(size_t i = 0; i < formation.size(); ++i)
	{
		result[i] = formation[i].count > 0 ? 1 : 0;
		minimumValue += static_cast<int64_t>(result[i]) * formation[i].unitValue;
	}
	if(minimumValue >= target)
		return result; // Indivisible formation: keep its species and slots.
	for(size_t i = 0; i < formation.size(); ++i)
		result[i] += static_cast<TQuantity>(static_cast<int64_t>(formation[i].count - result[i])
			* (target - minimumValue) / (original - minimumValue));
	return result;
}

JsonNode capture(const IGameSettings & settings)
{
	const auto & definition = settings.getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION);
	validate(definition);
	if(!active(definition))
		return disabledSnapshot();
	if(definition.Struct().contains("curve"))
		return definition;
	const auto & capability = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES);
	if(!capability.isStruct() || capability.Struct().empty())
		return disabledSnapshot(); // Core/historical contexts do not acquire NH capacity rules.
	newHorizonsHeroes::validateCapabilityRules(capability, false);
	const newHorizonsCreatures::CreatureCategoryRules categories(settings.getValue(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES));
	JsonNode result = disabledSnapshot();
	result["enabled"].Bool() = true;
	for(const auto & stage : definition["stages"].Vector())
	{
		std::vector<int64_t> inputs;
		std::vector<int64_t> outputs;
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
		{
			if(!heroClass || !capability["classProfiles"].Struct().contains(heroClass->getJsonKey()))
				continue;
			const auto point = classReference(settings, *heroClass, capability, stage, definition["unlockWeeks"], categories);
			if(point.original > 0 && point.fieldable > 0)
			{
				inputs.push_back(point.original);
				outputs.push_back(point.fieldable);
			}
		}
		require(!inputs.empty(), "no eligible reference classes");
		std::sort(inputs.begin(), inputs.end());
		std::sort(outputs.begin(), outputs.end());
		const auto input = inputs[(inputs.size() - 1) / 2];
		const auto output = std::min(input, outputs[(outputs.size() - 1) / 2]);
		if(result["curve"].Vector().empty())
		{
			JsonNode knee;
			knee["original"].Integer() = output;
			knee["fieldable"].Integer() = output;
			result["curve"].Vector().push_back(knee);
		}
		if(input == result["curve"].Vector().back()["original"].Integer())
			continue;
		JsonNode point;
		point["original"].Integer() = input;
		point["fieldable"].Integer() = output;
		result["curve"].Vector().push_back(point);
	}
	validate(result);
	return result;
}

void validateOverrides(const JsonNode & settings, bool supported)
{
	const auto & value = settings["creatures"][SETTING_KEY];
	validate(value);
	require(!active(value) || value.Struct().contains("curve"), "serialized calibration must be captured");
	require(supported || !active(value), "calibrated generation requires a supported save format");
}

void validateSerialization(const IGameSettings & settings, bool supported)
{
	const auto & value = settings.getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION);
	validate(value);
	require(!active(value) || value.Struct().contains("curve"), "serialized calibration must be captured");
	require(supported || !active(value), "calibrated generation requires a supported save format");
}
}
