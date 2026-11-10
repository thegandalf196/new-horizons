/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandArmyBonuses.h"
#include "Unit.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/Limiters.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace commandArmyBonuses
{
namespace
{
constexpr auto PREFIX = "new-horizons:commandArmy.";
constexpr auto DIRECT = "new-horizons:commandArmy.directAttack";
bool ours(const Bonus * bonus)
{
	return bonus && bonus->source == BonusSource::OTHER && bonus->duration == BonusDuration::ONE_BATTLE
		&& bonus->stacking.starts_with(PREFIX);
}
void append(std::vector<Bonus> & result, BonusType type, int64_t value, const char * key,
	BonusValueType mode = BonusValueType::ADDITIVE_VALUE, BonusSubtypeID subtype = BonusSubtypeID())
{
	if(value <= 0) return;
	result.emplace_back(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER,
		static_cast<int>(std::min<int64_t>(value, std::numeric_limits<int>::max())), BonusSourceID(), subtype, mode);
	result.back().stacking = std::string(PREFIX) + key;
}
int parameter(const CGHeroInstance & hero, const char * skill, const char * id, const char * key)
{
	if(!hero.hasActivePerk(skill, id)) return 0;
	for(const auto & perk : hero.getPerkState().rules["skills"][skill]["perks"].Vector())
		if(perk["id"].String() == id)
		{
			const auto & value = perk["effect"]["parameters"][key];
			if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > 100)
				throw std::runtime_error("Missing or invalid captured army passive parameter");
			return value.Integer();
		}
	throw std::runtime_error("Missing captured army passive definition");
}
void appendFaction(std::vector<Bonus> & result, const CGHeroInstance & hero)
{
	// The new meanings are admitted by the same new-game policy as Command.
	if(!hero.getArmyCommandRules()) return;
	const auto familyBonus = [&](BonusType type, int value, const char * key,
		BonusValueType mode, std::initializer_list<const char *> creatures)
	{
		if(value == 0) return;
		append(result, type, value, key, mode);
		auto family = std::make_shared<AnyOfLimiter>();
		for(const auto * name : creatures)
		{
			const CreatureID creature(CreatureID::decode(name));
			if(!creature.hasValue()) throw std::runtime_error("Missing army passive creature family");
			family->add(std::make_shared<CCreatureTypeLimiter>(*creature.toCreature(), false));
		}
		result.back().limiter = std::move(family);
	};
	familyBonus(BonusType::STACK_HEALTH, newHorizonsNecromancy::lordOfTheDeadMaximumHealthPercent(
		&hero, CreatureID(CreatureID::SKELETON)), "lordOfTheDead",
		BonusValueType::PERCENT_TO_BASE, {"core:skeleton", "core:skeletonWarrior"});
	familyBonus(BonusType::NONE, parameter(hero, "new-horizons:elementalRebirth",
		"new-horizons:elementalRebirth.phoenixSpark", "directAttackDamagePercent"), "primordialHost",
		BonusValueType::ADDITIVE_VALUE, {"core:airElemental", "core:stormElemental", "core:fireElemental", "core:energyElemental",
			"core:waterElemental", "core:iceElemental", "core:earthElemental", "core:magmaElemental", "core:firebird", "core:phoenix"});
}
}

std::vector<Bonus> project(const newHorizonsHeroes::CommandSlotModifiers & modifiers)
{
	std::vector<Bonus> result;
	const auto all = modifiers.additiveReadback();
	append(result, BonusType::PRIMARY_SKILL, all.attack, "attack", BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK));
	append(result, BonusType::PRIMARY_SKILL, all.defense, "defense", BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE));
	append(result, BonusType::STACKS_INITIATIVE_FLAT, all.initiative, "initiative");
	// STACKS_SPEED can also be an Initiative fallback: use battlefield movement only.
	append(result, BonusType::STACKS_MOVEMENT_RANGE, all.speed, "speed");
	append(result, BonusType::ADDITIONAL_RETALIATION, all.retaliations, "retaliations");
	append(result, BonusType::LUCK, all.luck, "luck");
	append(result, BonusType::MORALE, all.morale, "morale");
	append(result, BonusType::STACK_HEALTH, all.maximumHealthPercent, "health", BonusValueType::PERCENT_TO_BASE);
	append(result, BonusType::SPELL_DAMAGE_REDUCTION, modifiers.boundedEnhancementWardingPercent(),
		"warding", BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(SpellSchool::ANY));
	append(result, BonusType::SPELL_DAMAGE_REDUCTION, std::clamp<int64_t>(modifiers.authority.magicalReductionPercent, 0, 100),
		"protectiveTraining", BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(SpellSchool::ANY));
	append(result, BonusType::NONE, all.directAttackDamagePercent, "directAttack");
	return result;
}

const CGHeroInstance * originHero(const CStack & stack)
{
	if(!stack.base || stack.isSummoned() || stack.isClone()) return nullptr;
	const auto * hero = dynamic_cast<const CGHeroInstance *>(stack.base->getArmy());
	if(!hero || !stack.unitSlot().validSlot() || hero->getStackPtr(stack.unitSlot()) != stack.base) return nullptr;
	return hero;
}

void initialize(CStack & stack)
{
	const auto * hero = originHero(stack);
	std::vector<Bonus> bonuses;
	if(hero)
	{
		// Exactly one authoritative slot query per initialization, never per damage/stat query.
		if(const auto modifiers = hero->getCommandSlotModifiers(stack.unitSlot())) bonuses = project(*modifiers);
	}
	// Faction passives follow the established original hero source, including
	// its summons; this grants no slot or Command enhancement to a summon/clone.
	if(const auto * factionHero = stack.getMyHero()) appendFaction(bonuses, *factionHero);
	stack.removeBonuses(CSelector(ours));
	for(auto & bonus : bonuses) stack.addNewBonus(std::make_shared<Bonus>(std::move(bonus)));
}

int directAttackPercent(const battle::Unit & unit)
{
	return unit.getBonuses(CSelector([](const Bonus * bonus)
	{
		return ours(bonus) && bonus->type == BonusType::NONE
			&& (bonus->stacking == DIRECT || bonus->stacking == "new-horizons:commandArmy.primordialHost");
	}))->totalValue();
}

std::vector<int> directAttackMultipliers(const battle::Unit & unit)
{
	std::vector<int> result;
	for(const auto & bonus : *unit.getBonuses(CSelector([](const Bonus * bonus)
	{
		return ours(bonus) && bonus->type == BonusType::NONE
			&& (bonus->stacking == DIRECT || bonus->stacking == "new-horizons:commandArmy.primordialHost");
	}))) result.push_back(100 + bonus->val);
	return result;
}

std::vector<int> magicalReductionBasisPoints(const battle::Unit & unit)
{
	std::vector<int> result;
	for(const auto & bonus : *unit.getBonuses(CSelector([](const Bonus * bonus)
	{
		return ours(bonus) && bonus->type == BonusType::SPELL_DAMAGE_REDUCTION
			&& bonus->subtype == BonusSubtypeID(SpellSchool::ANY)
			&& (bonus->stacking == "new-horizons:commandArmy.warding"
				|| bonus->stacking == "new-horizons:commandArmy.protectiveTraining");
	}))) result.push_back(std::clamp(bonus->val, 0, 100) * 100);
	return result;
}

bool isProjectedBonus(const Bonus & bonus)
{
	return ours(&bonus);
}

void validateSerialization(const CStack & stack, bool supported)
{
	if(!supported)
		for(const auto & bonus : stack.getExportedBonusList())
			if(ours(bonus.get())) throw std::runtime_error("Army Command bonuses require the Command/Authority format");
}
}
