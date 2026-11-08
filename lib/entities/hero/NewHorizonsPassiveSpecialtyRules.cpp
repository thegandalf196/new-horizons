/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPassiveSpecialtyRules.h"
#include "NewHorizonsRuleValidation.h"
#include <array>

namespace newHorizonsHeroes
{
namespace
{
constexpr std::array fixed{
	std::pair{"bloodScentMaximumDamagePercent", 100}, std::pair{"treantmootProtection", 1},
	std::pair{"chillingPresenceMoraleReduction", 1}, std::pair{"chillingPresenceLuckReduction", 1},
	std::pair{"boneFormationFirstBandSize", 10}, std::pair{"boneFormationFirstBandLimit", 100},
	std::pair{"boneFormationSecondBandSize", 20}, std::pair{"boneFormationSecondBandLimit", 300},
	std::pair{"boneFormationRemainingBandSize", 50}, std::pair{"perfectMountMagicalDamageImmunity", 1},
	std::pair{"furyOfTheHordeAttack", 2}, std::pair{"furyOfTheHordeDefense", 2},
	std::pair{"furyOfTheHordeDamage", 1}, std::pair{"orcRageMaximumDamagePercent", 100},
	std::pair{"wyvernVenomPolicyVersion", 1}, std::pair{"dispellingShotMageChancePercent", 20},
	std::pair{"dispellingShotArchMageChancePercent", 40}, std::pair{"cyclopeanStunChancePercent", 25},
	std::pair{"stonecallerAmmunition", 12}, std::pair{"archshamanSpellMastery", 2}, std::pair{"goldStandardUpgrade", 1},
	std::pair{"relentlessBarrageDamagePercent", 50}, std::pair{"pointBlankShotDamageBonusPercent", 100},
	std::pair{"cripplingBoltSpeedReduction", 3}, std::pair{"cripplingBoltDuration", 3},
	std::pair{"incendiaryShotPolicyVersion", 1}, std::pair{"ogreBloodlustPolicyVersion", 1},
	std::pair{"runicReflectionPolicyVersion", 1}};
void group(const JsonNode & policy, std::initializer_list<const char *> keys)
{
	const bool present = policy.Struct().contains(*keys.begin());
	for(const auto key : keys)
		detail::require(policy.Struct().contains(key) == present, "Incomplete native passive specialty recipe");
}
}
void validatePassiveSpecialtyPolicy(const JsonNode & policy)
{
	detail::require(policy.isStruct(), "Native passive specialty policy object");
	for(const auto & [key, value] : fixed)
		if(policy.Struct().contains(key))
			detail::require(detail::integer(policy[key], value, value), "Native passive specialty fixed scalar");
	group(policy, {"chillingPresenceReductionPercent", "chillingPresenceMoraleReduction", "chillingPresenceLuckReduction"});
	if(policy.Struct().contains("chillingPresenceReductionPercent"))
		detail::require(detail::integer(policy["chillingPresenceReductionPercent"], 10, 10), "Chilling Presence reduction");
	group(policy, {"boneFormationFirstBandSize", "boneFormationFirstBandLimit", "boneFormationSecondBandSize",
		"boneFormationSecondBandLimit", "boneFormationRemainingBandSize"});
	group(policy, {"furyOfTheHordeAttack", "furyOfTheHordeDefense", "furyOfTheHordeDamage", "orcRageMaximumDamagePercent"});
	group(policy, {"dispellingShotMageChancePercent", "dispellingShotArchMageChancePercent"});
	group(policy, {"cripplingBoltSpeedReduction", "cripplingBoltDuration"});
}
void validatePassiveSpecialtySerialization(const JsonNode & development, bool supported)
{
	const auto & policy = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	if(!policy.isStruct() || supported)
		return;
	for(const auto & [key, value] : fixed)
		if(policy.Struct().contains(key))
			throw std::runtime_error("Captured native passive specialties require format1157");
}
}
