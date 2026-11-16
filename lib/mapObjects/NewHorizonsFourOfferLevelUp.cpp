/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsLevelUpOffer.h"
#include "CGHeroInstance.h"
#include "../entities/hero/NewHorizonsFourOfferProgression.h"
#include "../networkPacks/PacksForClient.h"
#include <stdexcept>

void newHorizonsHeroes::applyFourOfferLevelUp(CGHeroInstance & hero, const HeroLevelUp & pack)
{
	if(!usesFourOfferProgression(hero.getPerkState().rules) || !hero.cb
		|| pack.heroId != hero.id || pack.player != hero.getOwner()
		|| !pack.skills.empty() || !pack.perks.empty() || pack.perkOfferSeed != 0 || pack.perkOnly)
		throw std::runtime_error("Invalid four-offer level-up envelope");
	validateFourOfferSnapshot(pack.fourOffers, hero.getPerkState());
	if(pack.replacesQueryID != QueryID::NONE)
	{
		const bool confirmation = pack.queryID == pack.replacesQueryID;
		if(!quickStudyRerollInterval(hero.getPerkState().rules) || pack.queryID < 0
			|| pack.reachedLevel != hero.level || (!confirmation && pack.optionalRerollAvailable))
			throw std::runtime_error("Invalid four-offer Connoisseur replacement");
		return;
	}
	if(pack.restoredOffer)
	{
		if(pack.reachedLevel != hero.level)
			throw std::runtime_error("Saved four-offer level does not match the hero");
		return;
	}
	if(pack.reachedLevel != hero.level + 1)
		throw std::runtime_error("Invalid four-offer reached level");
	hero.captureMasteryEligibility(hero.level + 1, pack.artilleryExpertBeforeGain, pack.logisticsExpertBeforeGain);
	hero.levelUp(pack.primaryGains);
}

// True means a captured follow-up/restoration already handled the packet.
bool newHorizonsHeroes::applyCapturedLevelUpOffer(CGHeroInstance & hero, const HeroLevelUp & pack)
{
	if(usesFourOfferProgression(hero.getPerkState().rules))
	{
		applyFourOfferLevelUp(hero, pack);
		return true;
	}
	if(!pack.fourOffers.empty())
		throw std::runtime_error("Four-offer payload without captured policy");
	const bool unified = usesUnifiedSkillProgression(hero.getPerkState().rules);
	if(unified)
	{
		if(pack.heroId != hero.id || pack.player != hero.getOwner() || pack.perkOfferSeed != 0)
			throw std::runtime_error("Invalid unified level-up offer identity");
		if(pack.perkOnly)
		{
			const auto expected = hero.getPerkState().nextRequiredPerkOffer([&hero](const std::string & skill)
				{ return hero.getPerkSkillRank(skill); });
			if(!pack.skills.empty() || expected.empty() || pack.perks != expected
				|| pack.reachedLevel != hero.level || pack.optionalRerollAvailable)
				throw std::runtime_error("Invalid mandatory perk follow-up");
			return true;
		}
		if(!pack.perks.empty() || pack.skills.size() > PERK_MAX_SKILL_CHOICES)
			throw std::runtime_error("Unified level-up must offer only Skills");
		if(pack.restoredOffer)
		{
			if(pack.reachedLevel != hero.level)
				throw std::runtime_error("Saved level-up offer does not match the hero level");
			return true;
		}
	}
	else if(pack.perkOnly || pack.restoredOffer)
		throw std::runtime_error("Unified level-up flags without captured policy");
	if(!unified && pack.skills.empty() && !pack.perks.empty())
	{
		// Major System Rework Part VII: Dedicated follow-up perk offer for an acquired skill rank.
		if(!pack.perks.empty())
		{
			if(pack.perkOfferSeed < 0 || !newHorizonsHeroes::usesPerkRules(hero.getPerkState().rules))
				throw std::runtime_error("Invalid New Horizons perk offer envelope");
			for(const auto & candidate : pack.perks)
			{
				if(candidate.selection.perkId.empty() || candidate.selection.skillId.empty())
					throw std::runtime_error("Malformed New Horizons perk offer candidate");
			}
		}
		return true;
	}
	return false;
}
