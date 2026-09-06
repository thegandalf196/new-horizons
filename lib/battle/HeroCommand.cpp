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

namespace heroCommands
{
std::string key(HeroCommand command)
{
	switch(command)
	{
	case HeroCommand::CHARGE: return "charge";
	case HeroCommand::HOLD_THE_LINE: return "holdTheLine";
	case HeroCommand::ADVANCE: return "advance";
	case HeroCommand::AGGRESSIVE: return "aggressive";
	case HeroCommand::DEFENSIVE: return "defensive";
	default: return {};
	}
}

bool valid(HeroCommand command)
{
	return command >= HeroCommand::CHARGE && command <= HeroCommand::DEFENSIVE;
}

bool isDoctrine(HeroCommand command)
{
	return command == HeroCommand::AGGRESSIVE || command == HeroCommand::DEFENSIVE;
}

void validateRules(const JsonNode & rules)
{
	if(rules.isNull() || (rules.isStruct() && rules.Struct().empty()))
		return;
	if(rules["schemaVersion"].Integer() != 1 || rules["rulesetVersion"].Integer() != RULESET_VERSION)
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
	// Safety bounds avoid overflow and negative speed/damage; these are not balance targets.
	return static_cast<int>(std::lround(std::clamp(value, -90.0, 200.0)));
}

std::vector<Bonus> bonuses(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero)
{
	std::vector<Bonus> result;
	if(!valid(command))
		return result;
	for(const auto & [effect, formula] : rules["commands"][key(command)]["effects"].Struct())
	{
		Bonus bonus;
		bonus.source = BonusSource::HERO_COMMAND;
		bonus.duration = isDoctrine(command) ? BonusDuration::ONE_BATTLE : BonusDuration::N_TURNS;
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
