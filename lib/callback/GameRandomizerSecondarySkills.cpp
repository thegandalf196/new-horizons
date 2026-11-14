/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameRandomizer.h"
#include "SkillOfferLayout.h"
#include "IGameInfoCallback.h"
#include "../GameLibrary.h"
#include "../CSkillHandler.h"
#include "../IGameSettings.h"
#include "../entities/hero/CHeroClass.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include <functional>

namespace
{
struct SkillOfferPools
{
	std::set<SecondarySkill> upgrades;
	std::set<SecondarySkill> newSkills;
};

SkillOfferPools eligibleSkillOffers(const CGHeroInstance * hero, bool canonical, const IGameInfoCallback & gameInfo)
{
	SkillOfferPools result;
	const auto & rules = hero->getPrimaryGrowthRules();
	const auto ownFaction = newHorizonsHeroes::factionSkill(rules, hero->getFactionID());
	const auto ownEntry = ownFaction ? std::ranges::find_if(hero->secSkills, [&](const auto & entry)
	{
		return newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), entry.first);
	}) : hero->secSkills.end();
	const auto eligible = [&](SecondarySkill skill)
	{
		if(canonical && (newHorizonsHeroes::isExcludedSkill(rules, skill)
			|| newHorizonsHeroes::skillOfferWeight(rules, skill).value_or(0) <= 0)) return false;
		if(newHorizonsHeroes::isFactionSkill(rules, skill)
			&& (!ownFaction || !newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), skill))) return false;
		if(!newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules)) return true;
		const auto * definition = LIBRARY->skillh->getById(skill);
		return definition && hero->getPerkState().canAdvanceSkillNormally(
			definition->getJsonKey(), hero->getSecSkillLevel(skill));
	};
	if(hero->canLearnSkill())
		for(int i = 0; i < LIBRARY->skillh->size(); ++i)
		{
			const SecondarySkill skill(i);
			if(eligible(skill) && hero->canLearnSkill(skill)) result.newSkills.insert(skill);
		}
	for(const auto & [skill, rank] : hero->secSkills)
	{
		const bool duplicateFaction = ownEntry != hero->secSkills.end()
			&& newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), skill)
			&& skill != ownEntry->first;
		if(rank < MasteryLevel::EXPERT && eligible(skill) && !duplicateFaction) result.upgrades.insert(skill);
		result.newSkills.erase(skill);
	}
	// Historical faction creation may omit its own zero-legacy-weight Skill.
	// The captured canonical table already handles this in the ordinary pool.
	if(!canonical && ownFaction && ownEntry == hero->secSkills.end()
		&& hero->getSecSkillLevel(*ownFaction) == MasteryLevel::NONE && eligible(*ownFaction)
		&& hero->canLearnSkill() && gameInfo.isAllowed(*ownFaction)) result.newSkills.insert(*ownFaction);
	return result;
}

}

namespace newHorizonsHeroes
{
std::vector<SecondarySkill> splitSkillOffers(std::set<SecondarySkill> upgrades,
	std::set<SecondarySkill> newSkills, int maximum,
	const std::function<SecondarySkill(const std::set<SecondarySkill> &)> & choose)
{
	std::vector<SecondarySkill> result;
	while(result.size() < static_cast<size_t>(std::max(0, maximum)) && (!upgrades.empty() || !newSkills.empty()))
	{
		auto options = upgrades;
		options.insert(newSkills.begin(), newSkills.end());
		if(result.empty() && !upgrades.empty()) options = upgrades;
		else if(result.size() == 1 && !newSkills.empty()) options = newSkills;
		const auto selected = choose(options);
		if(!options.contains(selected)) throw std::runtime_error("Skill chooser returned an ineligible offer");
		result.push_back(selected);
		upgrades.erase(selected);
		newSkills.erase(selected);
	}
	return result;
}
}

SecondarySkill GameRandomizer::rollSecondarySkillForLevelup(const CGHeroInstance * hero, const std::set<SecondarySkill> & options)
{
	if(!heroSkillSeed.count(hero->getHeroTypeID()))
		heroSkillSeed.try_emplace(hero->getHeroTypeID(), getDefault().nextInt());

	auto & heroRng = heroSkillSeed.at(hero->getHeroTypeID());

	const bool canonicalSkillOffers = newHorizonsHeroes::usesSkillOfferWeights(hero->getPrimaryGrowthRules());
	const SecondarySkill canonicalWisdom(SecondarySkill::decode("new-horizons:wisdom"));
	auto getObligatorySkills = [&options, canonicalWisdom](bool magicSchools)
	{
		std::set<SecondarySkill> obligatory;
		for(const auto option : options)
			if(magicSchools ? option.toSkill()->isSpellSchool()
				: (option.toSkill()->isWisdom() || (canonicalWisdom != SecondarySkill::NONE && option == canonicalWisdom)))
				obligatory.insert(option); //Always return all obligatory skills

		return obligatory;
	};

	std::set<SecondarySkill> wisdomList = getObligatorySkills(false);
	std::set<SecondarySkill> schoolList = getObligatorySkills(true);

	std::set<SecondarySkill> actualCandidates;
	if(canonicalSkillOffers)
	{
		// Canonical New Horizons rows are already the complete policy. Do not
		// apply the legacy periodic Wisdom/school forcing on top of them.
		actualCandidates = options;
		// Keep this boundary defensive as well as the caller-side filtering:
		// level-up offer generation must never be able to reintroduce a retired
		// skill (or a zero-weight table entry) through a hand-built option set.
		vstd::erase_if(actualCandidates, [hero](SecondarySkill skill)
		{
			return newHorizonsHeroes::isExcludedSkill(hero->getPrimaryGrowthRules(), skill)
				|| newHorizonsHeroes::skillOfferWeight(hero->getPrimaryGrowthRules(), skill).value_or(0) <= 0;
		});
	}
	else
	{
		bool wantsWisdom = heroRng.wisdomCounter >= hero->maxlevelsToWisdom();
		bool wantsSchool = heroRng.magicSchoolCounter >= hero->maxlevelsToMagicSchool();
		bool selectWisdom = wantsWisdom && !wisdomList.empty();
		bool selectSchool = !selectWisdom && wantsSchool && !schoolList.empty();

		if(selectWisdom)
			actualCandidates = wisdomList;
		else if(selectSchool)
			actualCandidates = schoolList;
		else
			actualCandidates = options;
	}

	if(actualCandidates.empty())
		return SecondarySkill::NONE;

	std::vector<int> weights;
	std::vector<SecondarySkill> skills;

	for(const auto & possible : actualCandidates)
	{
		skills.push_back(possible);
		if(canonicalSkillOffers)
		{
			// Canonical tables are authoritative. A missing entry remains zero
			// instead of reviving a retired skill through legacy probabilities.
			weights.push_back(newHorizonsHeroes::skillOfferWeight(
				hero->getPrimaryGrowthRules(), possible).value_or(0));
		}
		else if(hero->getHeroClass()->secSkillProbability.count(possible) != 0)
		{
			int weight = hero->getHeroClass()->secSkillProbability.at(possible);
			weights.push_back(std::max(1, weight));
		}
		else
			weights.push_back(1); // H3 behavior - banned skills have minimal (1) chance to be picked
	}

	int selectedIndex = RandomGeneratorUtil::nextItemWeighted(weights, heroRng.seed);
	SecondarySkill selectedSkill = skills.at(selectedIndex);

	if((*LIBRARY->skillh)[selectedSkill]->isWisdom()
		|| (canonicalWisdom != SecondarySkill::NONE && selectedSkill == canonicalWisdom))
		heroRng.wisdomCounter = 0;
	if((*LIBRARY->skillh)[selectedSkill]->isSpellSchool())
		heroRng.magicSchoolCounter = 0;

	return selectedSkill;
}

std::vector<SecondarySkill> GameRandomizer::rollSecondarySkills(const CGHeroInstance * hero)
{
	// Primary growth does not initialize the independent secondary-Skill stream.
	if(!heroSkillSeed.count(hero->getHeroTypeID()))
		heroSkillSeed.try_emplace(hero->getHeroTypeID(), getDefault().nextInt());
	auto & heroRng = heroSkillSeed.at(hero->getHeroTypeID());
	const bool canonical = newHorizonsHeroes::usesSkillOfferWeights(hero->getPrimaryGrowthRules());
	if(!canonical)
	{
		++heroRng.magicSchoolCounter;
		++heroRng.wisdomCounter;
	}
	auto pools = eligibleSkillOffers(hero, canonical, gameInfo);
	const int maximum = hero->cb->getSettings().getInteger(EGameSettings::LEVEL_UP_TOTAL_SKILLS_AMOUNT);
	if(newHorizonsHeroes::usesSplitSkillOffers(hero->getPrimaryGrowthRules()))
		return newHorizonsHeroes::splitSkillOffers(std::move(pools.upgrades), std::move(pools.newSkills), maximum,
			[this, hero](const auto & options) { return rollSecondarySkillForLevelup(hero, options); });
	std::vector<SecondarySkill> result;
	if(canonical)
	{
		// Captures without the split marker retain their combined weighted pool.
		auto options = std::move(pools.upgrades);
		options.insert(pools.newSkills.begin(), pools.newSkills.end());
		while(result.size() < static_cast<size_t>(std::max(0, maximum)) && !options.empty())
		{
			result.push_back(rollSecondarySkillForLevelup(hero, options));
			options.erase(result.back());
		}
		return result;
	}
	const int upgraded = hero->cb->getSettings().getInteger(EGameSettings::LEVEL_UP_UPGRADED_SKILLS_AMOUNT);
	const int upgradeCount = std::max(upgraded, maximum - static_cast<int>(pools.newSkills.size()));
	while(result.size() < upgradeCount && !pools.upgrades.empty())
	{
		result.push_back(rollSecondarySkillForLevelup(hero, pools.upgrades));
		pools.upgrades.erase(result.back());
	}
	while(result.size() < maximum && !pools.newSkills.empty())
	{
		result.push_back(rollSecondarySkillForLevelup(hero, pools.newSkills));
		pools.newSkills.erase(result.back());
	}
	return result;
}
