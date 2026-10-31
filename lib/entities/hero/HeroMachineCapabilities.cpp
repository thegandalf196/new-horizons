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
	result.ballistaDamageMultiplier = newHorizonsHeroes::capabilityBallistaMultiplier(capabilityRules, artillery);
	result.ballistaControlChance = control(CreatureID::BALLISTA);
	result.catapultControlChance = control(CreatureID::CATAPULT);
	result.firstAidControlChance = control(CreatureID::FIRST_AID_TENT);

	// Ruleset v3 deliberately switches identity at this boundary. A saved
	// canonical War Machines rank controls both the derived Siege values and
	// deterministic machine control; legacy Artillery/Ballistics/First Aid
	// skills and their MANUAL_CONTROL bonuses remain a read-only projection.
	if(capabilityRules["rulesetVersion"].Integer() >= 3)
	{
		const int canonicalId = SecondarySkill::decode("new-horizons:warMachines");
		const bool canonicalIdentity = canonicalId >= 0
			&& SecondarySkill::encode(canonicalId) == "new-horizons:warMachines";
		const int rank = canonicalIdentity
			? std::clamp<int>(getSecSkillLevel(SecondarySkill(canonicalId)), 0, 3)
			: 0;
		result.warMachinesRank = rank;
		result.siegeRating = newHorizonsHeroes::capabilitySiegeRating(capabilityRules, rank);
		if(capabilityRules["rulesetVersion"].Integer() >= 4)
			result.siegeRating = std::max(0, result.siegeRating + valOfBonuses(BonusType::SIEGE_RATING));
		result.ballistaDamage = newHorizonsMachineSpecialties::siegeOutput(*this, capabilityRules, result.siegeRating, "ballistaDamage");
		result.catapultStructuralDamage = newHorizonsMachineSpecialties::siegeOutput(*this, capabilityRules, result.siegeRating, "catapultStructuralDamage");
		result.firstAidHealing = newHorizonsHeroes::capabilitySiegeOutput(capabilityRules, result.siegeRating, "firstAidHealing");
		result.defensiveTowerDamage = newHorizonsHeroes::capabilitySiegeOutput(capabilityRules, result.siegeRating, "defensiveTowerDamage");
		const int directControl = newHorizonsHeroes::capabilityDirectControlChance(capabilityRules, rank);
		result.ballistaControlChance = directControl;
		result.catapultControlChance = directControl;
		result.firstAidControlChance = directControl;
	}
	return result;
}
