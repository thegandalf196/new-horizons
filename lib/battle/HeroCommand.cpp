/*
 * HeroCommand.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommand.h"
#include "IBattleState.h"
#include "../bonuses/Bonus.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../callback/IGameInfoCallback.h"
#include <boost/multiprecision/cpp_int.hpp>

namespace heroCommands
{
namespace
{
int exactBoundedCoefficient(const std::array<double, 3> & terms, const std::array<int, 3> & factors)
{
	// Only the overflow fallback uses exact arithmetic. Keep the ordinary double
	// evaluation and its rounding unchanged, including for legacy saved rules.
	using boost::multiprecision::cpp_int;
	constexpr int digits = std::numeric_limits<double>::digits;
	static_assert(std::numeric_limits<double>::radix == 2 && digits <= 63);
	std::array<int64_t, 3> mantissas {};
	std::array<int, 3> exponents {};
	int lowestExponent = 0;
	for(size_t i = 0; i < terms.size(); ++i)
	{
		if(!std::isfinite(terms[i]))
			throw std::runtime_error("Invalid New Horizons command coefficient");
		if(terms[i] == 0 || factors[i] == 0)
			continue;
		const double fraction = std::frexp(terms[i], &exponents[i]);
		mantissas[i] = static_cast<int64_t>(std::ldexp(fraction, digits));
		exponents[i] -= digits;
		lowestExponent = std::min(lowestExponent, exponents[i]);
	}
	cpp_int sum = 0;
	for(size_t i = 0; i < terms.size(); ++i)
	{
		if(mantissas[i] == 0)
			continue;
		cpp_int part = mantissas[i];
		part *= factors[i];
		part <<= exponents[i] - lowestExponent;
		sum += part;
	}
	const bool negative = sum < 0;
	cpp_int magnitude = sum;
	if(negative)
		magnitude = -magnitude;
	if(lowestExponent < 0)
	{
		// Exact half-away-from-zero rounding, like lround, without passing NaN
		// or an unbounded result to a floating-to-integer conversion.
		cpp_int half = 1;
		half <<= -lowestExponent - 1;
		magnitude += half;
		magnitude >>= -lowestExponent;
	}
	cpp_int rounded = magnitude;
	if(negative)
		rounded = -rounded;
	// Integer endpoints make clamping before/after rounding equivalent.
	if(rounded < MIN_EFFECT_PERCENT)
		return MIN_EFFECT_PERCENT;
	if(rounded > MAX_EFFECT_PERCENT)
		return MAX_EFFECT_PERCENT;
	return rounded.convert_to<int>();
}

bool legacyVersionOne(const JsonNode & value)
{
	// Preserve the legacy const Integer() interpretation without unsafe conversion
	// of malformed infinity/NaN or values outside the integer range.
	return value.isNumber() && std::isfinite(value.Float())
		&& value.Float() >= 1 && value.Float() < 2;
}

void exactFields(const JsonNode & node, std::initializer_list<const char *> fields)
{
	if(!node.isStruct() || node.Struct().size() != fields.size())
		throw std::runtime_error("Invalid New Horizons targeted combat fields");
	for(const auto * field : fields)
	{
		if(!node.Struct().contains(field))
			throw std::runtime_error("Missing New Horizons targeted combat field: " + std::string(field));
	}
}

void validateTargetedRules(const JsonNode & rules)
{
	exactFields(rules, {"schemaVersion", "rulesetVersion", "commands"});
	if(rules["schemaVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["schemaVersion"].Integer() != 1
		|| rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["rulesetVersion"].Integer() != TARGETED_RULESET_VERSION)
		throw std::runtime_error("Unsupported New Horizons targeted combat ruleset version");
	exactFields(rules["commands"], {"charge", "holdTheLine", "advance", "aggressive", "defensive", "focusFire"});
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::ADVANCE,
		HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE, HeroCommand::FOCUS_FIRE})
	{
		const auto & definition = rules["commands"][key(command)];
		const bool targeted = command == HeroCommand::FOCUS_FIRE;
		if(targeted)
			exactFields(definition, {"kind", "coverage", "duration", "target", "effects"});
		else
			exactFields(definition, {"kind", "coverage", "duration", "effects"});
		if(!definition["kind"].isString() || !definition["duration"].isString()
			|| !definition["coverage"].isString()
			|| definition["kind"].String() != (isDoctrine(command) ? "doctrine" : "order")
			|| definition["duration"].String() != (isDoctrine(command) ? "battle" : "round")
			|| definition["coverage"].String()
				!= (targeted ? "ownOrdinaryShootersAtIssue" : "ownLivingNonWarMachines"))
			throw std::runtime_error("Invalid New Horizons targeted command definition: " + key(command));
		if(targeted && (!definition["target"].isString() || definition["target"].String() != "enemyUnit"))
			throw std::runtime_error("Invalid New Horizons Focus Fire target policy");
		const auto & effects = definition["effects"];
		if(targeted)
			exactFields(effects, {"rangedDamagePercent"});
		if(!effects.isStruct() || effects.Struct().empty())
			throw std::runtime_error("Missing New Horizons targeted command effects");
		for(const auto & [effect, formula] : effects.Struct())
		{
			if(effect != "meleeDamagePercent" && effect != "rangedDamagePercent"
				&& effect != "damageReductionPercent" && effect != "speedPercent")
				throw std::runtime_error("Unsupported New Horizons command effect: " + effect);
			exactFields(formula, {"base", "attack", "defense"});
			for(const auto * term : {"base", "attack", "defense"})
			{
				if(!formula[term].isNumber() || !std::isfinite(formula[term].Float())
					|| std::abs(formula[term].Float()) > MAX_TARGETED_COEFFICIENT)
					throw std::runtime_error("Invalid New Horizons targeted command coefficient");
			}
		}
	}
}

void validateOrdersOnlyRules(const JsonNode & rules)
{
	exactFields(rules, {"schemaVersion", "rulesetVersion", "commands"});
	if(rules["schemaVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["schemaVersion"].Integer() != 1
		|| rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["rulesetVersion"].Integer() != ORDERS_ONLY_RULESET_VERSION)
		throw std::runtime_error("Unsupported New Horizons Orders-only combat ruleset version");
	const auto & commands = rules["commands"];
	const bool canonical = commands.isStruct() && commands.Struct().size() == 8
		&& commands.Struct().contains("riposte") && commands.Struct().contains("brace")
		&& commands.Struct().contains("protect") && commands.Struct().contains("flank")
		&& commands.Struct().contains("secondWind");
	if(canonical)
		exactFields(commands, {"charge", "holdTheLine", "focusFire", "riposte", "brace", "protect", "flank", "secondWind"});
	else
		exactFields(commands, {"charge", "holdTheLine", "focusFire"});
	const auto validateFormula = [](const JsonNode & formula, const char * error)
	{
		exactFields(formula, {"base", "attack", "defense"});
		for(const auto * term : {"base", "attack", "defense"})
		{
			if(!formula[term].isNumber() || !std::isfinite(formula[term].Float())
				|| std::abs(formula[term].Float()) > MAX_TARGETED_COEFFICIENT)
				throw std::runtime_error(error);
		}
	};
	const auto validateEffects = [&](const JsonNode & effects, std::initializer_list<const char *> allowed, const char * error)
	{
		if(!effects.isStruct() || effects.Struct().empty())
			throw std::runtime_error(error);
		for(const auto & [effect, formula] : effects.Struct())
		{
			if(std::find(allowed.begin(), allowed.end(), effect) == allowed.end())
				throw std::runtime_error(error);
			validateFormula(formula, error);
		}
	};
	const auto validateCommon = [&](const JsonNode & definition, const char * coverage,
		const char * target, const char * trigger, const char * anchor,
		std::initializer_list<const char *> effects)
	{
		std::vector<const char *> fields{"kind", "coverage", "duration", "effects"};
		if(target)
			fields.push_back("target");
		if(trigger)
			fields.push_back("trigger");
		if(anchor)
			fields.push_back("anchor");
		if(!definition.isStruct() || definition.Struct().size() != fields.size())
			throw std::runtime_error("Invalid New Horizons canonical Order fields");
		for(const auto * field : fields)
			if(!definition.Struct().contains(field))
				throw std::runtime_error("Missing New Horizons canonical Order field: " + std::string(field));
		if(!definition["kind"].isString() || definition["kind"].String() != "order"
			|| !definition["duration"].isString() || definition["duration"].String() != "round"
			|| !definition["coverage"].isString() || definition["coverage"].String() != coverage)
			throw std::runtime_error("Invalid New Horizons canonical Order definition");
		if(target && (!definition["target"].isString() || definition["target"].String() != target))
			throw std::runtime_error("Invalid New Horizons canonical Order target policy");
		if(trigger && (!definition["trigger"].isString() || definition["trigger"].String() != trigger))
			throw std::runtime_error("Invalid New Horizons canonical Order trigger policy");
		if(anchor && (!definition["anchor"].isString() || definition["anchor"].String() != anchor))
			throw std::runtime_error("Invalid New Horizons canonical Order anchor policy");
		validateEffects(definition["effects"], effects, "Invalid New Horizons canonical Order effects");
	};
	if(canonical)
	{
		validateCommon(commands["charge"], "ownLivingNonWarMachines", nullptr,
			"firstMeleeAfterMovingAtLeast3", nullptr, {"meleeDamagePercent"});
		validateCommon(commands["holdTheLine"], "ownLivingNonWarMachines", nullptr,
			nullptr, "issuePosition", {"damageReductionPercent"});
		validateCommon(commands["focusFire"], "ownOrdinaryShootersAtIssue", "enemyUnit",
			nullptr, nullptr, {"rangedDamagePercent"});
		validateCommon(commands["riposte"], "ownLivingNonWarMachines", nullptr,
			nullptr, nullptr, {"meleeDamageReductionPercent", "retaliationDamagePercent"});
		validateCommon(commands["brace"], "ownLivingNonWarMachines", nullptr,
			"enemyMeleeMovedAtLeast3", nullptr, {"preemptiveDamagePercent"});
		validateCommon(commands["protect"], "ownLivingNonWarMachines", "protectorAndWard",
			nullptr, nullptr, {"interceptedDamageReductionPercent"});
		validateCommon(commands["flank"], "ownLivingNonWarMachines", "enemyUnit",
			nullptr, nullptr, {"meleeDamagePercent", "additionalSidePercent"});
		validateCommon(commands["secondWind"], "ownLivingNonWarMachines", "friendlyCompletedActivation",
			nullptr, nullptr, {"additionalActivationDamagePercent"});
		return;
	}
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		HeroCommand::FOCUS_FIRE})
	{
		const auto & definition = rules["commands"][key(command)];
		const bool targeted = command == HeroCommand::FOCUS_FIRE;
		if(targeted)
			exactFields(definition, {"kind", "coverage", "duration", "target", "effects"});
		else
			exactFields(definition, {"kind", "coverage", "duration", "effects"});
		if(!definition["kind"].isString() || !definition["duration"].isString()
			|| !definition["coverage"].isString()
			|| definition["kind"].String() != "order"
			|| definition["duration"].String() != "round"
			|| definition["coverage"].String()
				!= (targeted ? "ownOrdinaryShootersAtIssue" : "ownLivingNonWarMachines"))
			throw std::runtime_error("Invalid New Horizons Orders-only command definition: " + key(command));
		if(targeted && (!definition["target"].isString() || definition["target"].String() != "enemyUnit"))
			throw std::runtime_error("Invalid New Horizons Focus Fire target policy");
		const auto & effects = definition["effects"];
		if(targeted)
			exactFields(effects, {"rangedDamagePercent"});
		if(!effects.isStruct() || effects.Struct().empty())
			throw std::runtime_error("Missing New Horizons Orders-only command effects");
		for(const auto & [effect, formula] : effects.Struct())
		{
			if(effect != "meleeDamagePercent" && effect != "rangedDamagePercent"
				&& effect != "damageReductionPercent" && effect != "speedPercent")
				throw std::runtime_error("Unsupported New Horizons command effect: " + effect);
			exactFields(formula, {"base", "attack", "defense"});
			for(const auto * term : {"base", "attack", "defense"})
			{
				if(!formula[term].isNumber() || !std::isfinite(formula[term].Float())
					|| std::abs(formula[term].Float()) > MAX_TARGETED_COEFFICIENT)
					throw std::runtime_error("Invalid New Horizons Orders-only command coefficient");
			}
		}
	}
}
}

std::string key(HeroCommand command)
{
	switch(command)
	{
	case HeroCommand::CHARGE: return "charge";
	case HeroCommand::HOLD_THE_LINE: return "holdTheLine";
	case HeroCommand::ADVANCE: return "advance";
	case HeroCommand::AGGRESSIVE: return "aggressive";
	case HeroCommand::DEFENSIVE: return "defensive";
	case HeroCommand::FOCUS_FIRE: return "focusFire";
	case HeroCommand::RIPOSTE: return "riposte";
	case HeroCommand::BRACE: return "brace";
	case HeroCommand::PROTECT: return "protect";
	case HeroCommand::FLANK: return "flank";
	case HeroCommand::SECOND_WIND: return "secondWind";
	default: return {};
	}
}

bool valid(HeroCommand command)
{
	return isActive(command);
}

bool isActive(HeroCommand command)
{
	return command == HeroCommand::CHARGE || command == HeroCommand::HOLD_THE_LINE
		|| command == HeroCommand::FOCUS_FIRE || command == HeroCommand::RIPOSTE
		|| command == HeroCommand::BRACE || command == HeroCommand::PROTECT
		|| command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND;
}

bool isCanonicalRules(const JsonNode & rules)
{
	return rules.isStruct() && rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["rulesetVersion"].Integer() == ORDERS_ONLY_RULESET_VERSION
		&& rules["commands"].isStruct() && rules["commands"].Struct().size() == 8
		&& rules["commands"].Struct().contains("riposte")
		&& rules["commands"].Struct().contains("brace")
		&& rules["commands"].Struct().contains("protect")
		&& rules["commands"].Struct().contains("flank")
		&& rules["commands"].Struct().contains("secondWind");
}

bool supportedByRules(const JsonNode & rules, HeroCommand command)
{
	// Advance, Aggressive and Defensive are retained solely so old enum values
	// and save slots can be decoded. They are never part of the active command
	// surface, including when a legacy v1/v2 snapshot is loaded.
	if(!isActive(command) || !rules.isStruct() || rules.Struct().empty())
		return false;
	const auto & version = rules["rulesetVersion"];
	if(version.getType() != JsonNode::JsonType::DATA_INTEGER)
		return legacyVersionOne(version) && command != HeroCommand::FOCUS_FIRE;
	if(version.Integer() == RULESET_VERSION)
		return command != HeroCommand::FOCUS_FIRE;
	if(version.Integer() == TARGETED_RULESET_VERSION)
		return command == HeroCommand::CHARGE || command == HeroCommand::HOLD_THE_LINE
			|| command == HeroCommand::FOCUS_FIRE;
	if(version.Integer() == ORDERS_ONLY_RULESET_VERSION)
		return rules["commands"].isStruct() && rules["commands"].Struct().contains(key(command));
	return false;
}

bool isDoctrine(HeroCommand command)
{
	return command == HeroCommand::AGGRESSIVE || command == HeroCommand::DEFENSIVE;
}

void validateRules(const JsonNode & rules)
{
	if(rules.isNull() || (rules.isStruct() && rules.Struct().empty()))
		return;
	if(!rules.isStruct())
		throw std::runtime_error("Invalid New Horizons combat ruleset shape");
	const auto & version = rules["rulesetVersion"];
	if(version.getType() == JsonNode::JsonType::DATA_INTEGER
		&& version.Integer() == ORDERS_ONLY_RULESET_VERSION)
	{
		validateOrdersOnlyRules(rules);
		return;
	}
	if(version.isNumber() && version.Float() == TARGETED_RULESET_VERSION)
	{
		validateTargetedRules(rules);
		return;
	}
	if(!legacyVersionOne(rules["schemaVersion"]) || !legacyVersionOne(version))
		throw std::runtime_error("Unsupported New Horizons combat ruleset version");
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::ADVANCE,
		HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
	{
		const auto & definition = rules["commands"][key(command)];
		if(definition["kind"].String() != (isDoctrine(command) ? "doctrine" : "order")
			|| definition["duration"].String() != (isDoctrine(command) ? "battle" : "round")
			|| definition["coverage"].String() != "ownLivingNonWarMachines"
			|| !definition["effects"].isStruct() || definition["effects"].Struct().empty())
			throw std::runtime_error("Invalid New Horizons command definition: " + key(command));
		for(const auto & [effect, formula] : definition["effects"].Struct())
		{
			if(effect != "meleeDamagePercent" && effect != "rangedDamagePercent"
				&& effect != "damageReductionPercent" && effect != "speedPercent")
				throw std::runtime_error("Unsupported New Horizons command effect: " + effect);
			for(const auto & term : {"base", "attack", "defense"})
			{
				if(!formula[term].isNumber() || !std::isfinite(formula[term].Float()))
					throw std::runtime_error("Invalid New Horizons command coefficient");
			}
		}
	}
}

int coefficient(const JsonNode & effect, int attack, int defense)
{
	const double value = effect["base"].Float() + effect["attack"].Float() * attack
		+ effect["defense"].Float() * defense;
	if(!std::isfinite(value))
		return exactBoundedCoefficient({effect["base"].Float(), effect["attack"].Float(), effect["defense"].Float()},
			{1, attack, defense});
	// Safety bounds avoid overflow and negative speed/damage; these are not balance targets.
	return static_cast<int>(std::lround(std::clamp(value,
		static_cast<double>(MIN_EFFECT_PERCENT), static_cast<double>(MAX_EFFECT_PERCENT))));
}

std::vector<Bonus> bonuses(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero)
{
	std::vector<Bonus> result;
	// The canonical eight Orders are evaluated from HeroOrderState at the exact
	// attack/target moment.  Emitting broad unit bonuses here would make Charge
	// unconditional, let Hold the Line follow moved stacks, and lose one-shot
	// triggers.  Keep the old generic path solely for readable three-Order v3
	// snapshots.
	if(isCanonicalRules(rules))
		return result;
	// Focus Fire is contextual side state, never an unconditional unit bonus.
	if(!isActive(command) || command == HeroCommand::FOCUS_FIRE)
		return result;
	for(const auto & [effect, formula] : rules["commands"][key(command)]["effects"].Struct())
	{
		Bonus bonus;
		bonus.source = BonusSource::HERO_COMMAND;
		bonus.duration = BonusDuration::N_TURNS;
		bonus.turnsRemain = 1;
		bonus.val = coefficient(formula, hero.getPrimSkillLevel(PrimarySkill::ATTACK),
			hero.getPrimSkillLevel(PrimarySkill::DEFENSE));
		bonus.description.appendRawString("New Horizons: " + key(command));
		if(effect == "speedPercent")
		{
			bonus.type = BonusType::STACKS_SPEED;
			bonus.valType = BonusValueType::PERCENT_TO_BASE;
		}
		else if(effect == "damageReductionPercent")
		{
			bonus.type = BonusType::GENERAL_DAMAGE_REDUCTION;
			bonus.subtype = BonusCustomSubtype::damageTypeAll;
			bonus.val = std::min(bonus.val, 90);
		}
		else
		{
			bonus.type = BonusType::PERCENTAGE_DAMAGE_BOOST;
			bonus.subtype = effect == "rangedDamagePercent"
				? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee;
		}
		result.push_back(bonus);
	}
	return result;
}
}

const JsonNode & IGameInfoCallback::getHeroCommandRules() const
{
	static const JsonNode legacy;
	return legacy;
}

const JsonNode & IBattleInfo::getHeroCommandRules() const
{
	static const JsonNode legacy;
	return legacy;
}
