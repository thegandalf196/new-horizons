/*
 * NewHorizonsLevelUpOffer.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsLevelUpOffer.h"
#include "CGHeroInstance.h"
#include "../networkPacks/PacksForClient.h"

void newHorizonsHeroes::applyLevelUpOffer(CGHeroInstance * hero, const HeroLevelUp & pack)
{
	if(!hero)
		throw std::runtime_error("Level-up offer for a missing hero");
	if(!pack.perks.empty() || pack.perkOfferSeed != 0)
	{
		if(pack.perkOfferSeed < 0 || !newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
			throw std::runtime_error("Invalid New Horizons perk offer envelope");
		const size_t maxSkills = static_cast<size_t>(hero->getPerkState().rules["maxSkillChoices"].Integer());
		const size_t maxPerks = static_cast<size_t>(hero->getPerkState().rules["maxPerkChoices"].Integer());
		if(pack.skills.size() > maxSkills || pack.perks.size() > maxPerks)
			throw std::runtime_error("Oversized New Horizons level-up offer");
		const auto expected = hero->getPerkState().prepareOffer([hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		}, static_cast<uint64_t>(pack.perkOfferSeed), hero->getPerkOfferWeightContext());
		if(expected != pack.perks)
			throw std::runtime_error("Forged New Horizons level-up perk offer");
	}
	if(pack.replacesQueryID != QueryID::NONE)
	{
		const bool confirmation = pack.queryID == pack.replacesQueryID;
		if(!newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules)
			|| pack.heroId != hero->id || pack.player != hero->getOwner() || pack.queryID < 0
			|| pack.reachedLevel != hero->level || (!confirmation && pack.optionalRerollAvailable))
			throw std::runtime_error("Invalid optional Quick Study replacement");
		// Same-ID confirmation and fresh-ID replacement both retain the existing gain.
		return;
	}
	if(pack.reachedLevel != 0 && pack.reachedLevel != hero->level + 1)
		throw std::runtime_error("Invalid optional Quick Study reached level");
	hero->captureMasteryEligibility(hero->level + 1, pack.artilleryExpertBeforeGain, pack.logisticsExpertBeforeGain);
	hero->levelUp(pack.primaryGains);
}
