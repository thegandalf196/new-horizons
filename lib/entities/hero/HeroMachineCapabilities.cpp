/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "NewHorizonsCapabilityRules.h"
#include "../../battle/NewHorizonsMachineSpecialties.h"
#include <algorithm>
std::optional<newHorizonsHeroes::SiegeCapabilities> CGHeroInstance::getSiegeCapabilities() const
{
	if(!newHorizonsHeroes::usesRules(capabilityRules))
		return std::nullopt;
	const int artillery = getSecSkillLevel(SecondarySkill::ARTILLERY);
	const auto control = [this](CreatureID machine)
	{
		return std::clamp(valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(machine)), 0, 100);
	};
	newHorizonsHeroes::SiegeCapabilities result;
	result.artilleryRank = artillery;
	result.ballisticsRank = getSecSkillLevel(SecondarySkill::BALLISTICS);
	result.firstAidRank = getSecSkillLevel(SecondarySkill::FIRST_AID);
	const auto * compiled = compiledCapabilities();
	result.ballistaDamageMultiplier = compiled ? compiled->ballistaDamageMultiplier(artillery)
		: newHorizonsHeroes::capabilityBallistaMultiplier(capabilityRules, artillery);
	result.ballistaControlChance = control(CreatureID::BALLISTA);
	result.catapultControlChance = control(CreatureID::CATAPULT);
	result.firstAidControlChance = control(CreatureID::FIRST_AID_TENT);

	// Ruleset v3 deliberately switches identity at this boundary. A saved
	// canonical War Machines rank controls both the derived Siege values and
	// deterministic machine control; legacy Artillery/Ballistics/First Aid
	// skills and their MANUAL_CONTROL bonuses remain a read-only projection.
	if(capabilityRules["rulesetVersion"].Integer() >= 3)
	{
		const auto skill = compiled ? compiled->warMachinesSkill() : std::nullopt;
		const int canonicalId = compiled ? -1 : SecondarySkill::decode("new-horizons:warMachines");
		const bool canonicalIdentity = canonicalId >= 0
			&& SecondarySkill::encode(canonicalId) == "new-horizons:warMachines";
		const int rank = skill ? std::clamp<int>(getSecSkillLevel(*skill), 0, 3)
			: canonicalIdentity ? std::clamp<int>(getSecSkillLevel(SecondarySkill(canonicalId)), 0, 3) : 0;
		result.warMachinesRank = rank;
		result.siegeRating = compiled ? compiled->rating(rank)
			: newHorizonsHeroes::capabilitySiegeRating(capabilityRules, rank);
		if(capabilityRules["rulesetVersion"].Integer() >= 4)
			result.siegeRating = std::max(0, result.siegeRating + valOfBonuses(BonusType::SIEGE_RATING));
		result.ballistaDamage = newHorizonsMachineSpecialties::siegeOutput(*this, capabilityRules, result.siegeRating, "ballistaDamage");
		result.catapultStructuralDamage = newHorizonsMachineSpecialties::siegeOutput(*this, capabilityRules, result.siegeRating, "catapultStructuralDamage");
		result.firstAidHealing = getCapturedSiegeOutput(result.siegeRating, "firstAidHealing");
		result.defensiveTowerDamage = getCapturedSiegeOutput(result.siegeRating, "defensiveTowerDamage");
		const int directControl = compiled ? compiled->directControlChance(rank)
			: newHorizonsHeroes::capabilityDirectControlChance(capabilityRules, rank);
		result.ballistaControlChance = directControl;
		result.catapultControlChance = directControl;
		result.firstAidControlChance = directControl;
	}
	return result;
}

int CGHeroInstance::getCapturedSiegeOutput(int rating, const std::string & output) const
{
	const auto * compiled = compiledCapabilities();
	if(!compiled) return newHorizonsHeroes::capabilitySiegeOutput(capabilityRules, rating, output);
	nativeRules::SiegeOutput kind;
	if(output == "ballistaDamage") kind = nativeRules::SiegeOutput::BallistaDamage;
	else if(output == "catapultStructuralDamage") kind = nativeRules::SiegeOutput::CatapultStructuralDamage;
	else if(output == "firstAidHealing") kind = nativeRules::SiegeOutput::FirstAidHealing;
	else if(output == "defensiveTowerDamage") kind = nativeRules::SiegeOutput::DefensiveTowerDamage;
	else throw std::runtime_error("Unknown captured Siege output " + output);
	return compiled->siegeOutput(rating, kind);
}
