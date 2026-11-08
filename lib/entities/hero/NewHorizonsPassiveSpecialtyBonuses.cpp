/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPassiveSpecialtyBonuses.h"
#include "NewHorizonsCreatureSpecialtyCapabilities.h"
#include "../../spells/NewHorizonsRunicReflection.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../CCreatureHandler.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/Limiters.h"
#include "../../bonuses/Propagators.h"
#include "../../bonuses/Updaters.h"
#include "../../nativeRules/NativeRulePilots.h"
#include <string_view>
#include <array>
#include <limits>

namespace newHorizonsHeroes
{
namespace
{
using Bundle = std::vector<std::shared_ptr<Bonus>>;
class Builder
{
	const CGHeroInstance & hero;
	CreatureID family;
	std::string prefix;
public:
	Builder(const CGHeroInstance & hero, const char * creature)
		: hero(hero), family(CreatureID::decode(creature)), prefix("new-horizons:ordinary-passive:" + hero.getHeroType()->getJsonKey())
	{
		if(!family.hasValue() || !family.toCreature())
			throw std::runtime_error("Native passive creature family is not registered");
	}
	std::shared_ptr<Bonus> familyBonus(BonusType type, int value, const char * operation) const
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::HERO_SPECIAL,
			value, BonusSourceID(hero.getHeroTypeID()));
		bonus->stacking = prefix + ':' + operation;
		bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*family.toCreature(), true);
		return nativeRules::prepareFamilySpecialtyBonus(*bonus);
	}
	std::shared_ptr<UnitAdjacentLimiter> adjacent(AdjacentUnitFilter::Relationship relationship) const
	{
		auto limiter = std::make_shared<UnitAdjacentLimiter>();
		limiter->targetUnit = family;
		limiter->filter.includeUpgrades = true;
		limiter->filter.relationship = relationship;
		return limiter;
	}
	std::shared_ptr<Bonus> opposingAura(BonusType type, int value, const char * operation, bool spatial) const
	{
		auto bonus = familyBonus(type, value, operation);
		auto all = std::make_shared<AllOfLimiter>();
		if(spatial)
			all->add(adjacent(AdjacentUnitFilter::Relationship::OPPOSING_ARMY));
		all->add(std::make_shared<OppositeSideLimiter>());
		bonus->limiter = all;
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		bonus->propagationUpdater = std::make_shared<OwnerUpdater>();
		return spatial ? nativeRules::prepareAdjacentSpecialtyBonus(*bonus) : bonus;
	}
	std::shared_ptr<Bonus> treantmoot() const
	{
		auto bonus = familyBonus(BonusType::CREATURE_ATTACK_TARGET_PROTECTION, 1, "targetProtection");
		auto excludeFamily = std::make_shared<NoneOfLimiter>();
		excludeFamily->add(bonus->limiter);
		auto all = std::make_shared<AllOfLimiter>();
		all->add(excludeFamily);
		all->add(adjacent(AdjacentUnitFilter::Relationship::SAME_ARMY));
		bonus->limiter = all;
		return nativeRules::prepareAdjacentSpecialtyBonus(*bonus);
	}
};
Bundle boneFormation(const Builder & builder)
{
	Bundle result;
	for(const auto & band : {std::array{0, 10, 10}, std::array{5, 15, 20},
		std::array{6, std::numeric_limits<int>::max(), 50}})
	{
		auto bonus = builder.familyBonus(BonusType::PHYSICAL_DEFENSE, 0,
			band[2] == 10 ? "firstBand" : band[2] == 20 ? "secondBand" : "remainingBand");
		bonus->updater = std::make_shared<TimesStackSizeUpdater>(band[0], band[1], band[2], 1);
		result.push_back(std::move(bonus));
	}
	auto offset = builder.familyBonus(BonusType::PHYSICAL_DEFENSE, -11, "bandOffsets");
	result.push_back(std::move(offset));
	result.push_back(builder.familyBonus(BonusType::DYNAMIC_CREATURE_DAMAGE, 1, "dynamicCount"));
	return result;
}
Bundle chillingPresence(const Builder & builder)
{
	Bundle result;
	auto melee = builder.opposingAura(BonusType::GENERAL_ATTACK_REDUCTION, 10, "meleeReduction", true);
	melee->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
	result.push_back(std::move(melee));
	result.push_back(builder.opposingAura(BonusType::MORALE, -1, "morale", true));
	result.push_back(builder.opposingAura(BonusType::LUCK, -1, "luck", true));
	result.push_back(builder.opposingAura(BonusType::DYNAMIC_CREATURE_DAMAGE, 1, "dynamicAura", false));
	return result;
}
Bundle furyOfTheHorde(const Builder & builder)
{
	Bundle result;
	for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
	{
		auto bonus = builder.familyBonus(BonusType::PRIMARY_SKILL, 2,
			skill == PrimarySkill::ATTACK ? "attack" : "defense");
		bonus->subtype = BonusSubtypeID(skill);
		result.push_back(std::move(bonus));
	}
	auto damage = builder.familyBonus(BonusType::CREATURE_DAMAGE, 1, "damage");
	damage->subtype = BonusSubtypeID(BonusCustomSubtype(0));
	result.push_back(std::move(damage));
	result.push_back(builder.familyBonus(BonusType::CANNOT_SHOOT, 1, "meleeCreature"));
	auto rage = builder.familyBonus(BonusType::CASUALTY_DAMAGE_PERCENTAGE, 100, "ownCasualties");
	rage->subtype = BonusSubtypeID(BonusCustomSubtype(0));
	result.push_back(std::move(rage));
	return result;
}
}
std::vector<std::shared_ptr<Bonus>> passiveCreatureSpecialtyBonuses(const CGHeroInstance & hero)
{
	if(!hero.getHeroType())
		return {};
	const auto & root = hero.getPrimaryGrowthRules();
	const auto & policy = root["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	if(!policy.isStruct())
		return {};
	const auto has = [&](const char * key) { return policy.Struct().contains(key); };
	const std::string_view identity = hero.getHeroType()->getJsonKey();
	if(identity == "core:vokial" && has("bloodScentMaximumDamagePercent"))
	{
		auto bonus = Builder(hero, "core:vampire").familyBonus(BonusType::CASUALTY_DAMAGE_PERCENTAGE, 100, "targetCasualties");
		bonus->subtype = BonusSubtypeID(BonusCustomSubtype(1));
		return {bonus};
	}
	if(identity == "core:ryland" && has("treantmootProtection"))
		return {Builder(hero, "core:dendroidGuard").treantmoot()};
	if(identity == "core:charna" && has("chillingPresenceMoraleReduction"))
		return chillingPresence(Builder(hero, "core:wight"));
	if(identity == "core:galthran" && has("boneFormationFirstBandSize"))
		return boneFormation(Builder(hero, "core:skeleton"));
	if(identity == "core:aeris" && has("perfectMountMagicalDamageImmunity"))
		return {Builder(hero, "core:pegasus").familyBonus(BonusType::MAGICAL_DAMAGE_IMMUNITY, 1, "magicalImmunity")};
	if(identity == "core:jabarkas" && has("furyOfTheHordeAttack"))
		return furyOfTheHorde(Builder(hero, "core:orc"));
	auto capability = creatureSpecialtyCapabilityBonuses(hero);
	return capability.empty() ? newHorizonsRunicReflection::preparedBonuses(hero) : capability;
}
}
