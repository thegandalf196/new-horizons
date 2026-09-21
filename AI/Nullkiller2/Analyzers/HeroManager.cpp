/*
* HeroManager.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "../StdInc.h"

#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/MapObjects.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/adventure/TownPortalEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../Engine/Nullkiller.h"
#include "mapping/CMapHeader.h"

namespace NK2AI
{

float evaluateMainHeroRoleScore(float heroProfileScore, uint64_t heroTotalStrength, uint64_t strongestHeroTotalStrength)
{
	if(strongestHeroTotalStrength == 0)
		return heroProfileScore;

	const float armyShare = heroTotalStrength / static_cast<float>(strongestHeroTotalStrength);
	return heroProfileScore + 50.0f * armyShare;
}

std::optional<float> evaluateNewHorizonsStrategicSkillRoleScore(const std::string & skillId, HeroRole role)
{
	if(skillId == "new-horizons:estates")
		return role == HeroRole::MAIN ? 0.5f : 2.0f;
	if(skillId == "new-horizons:learning")
		return role == HeroRole::MAIN ? 1.0f : 0.5f;
	if(skillId == "new-horizons:luck")
		return role == HeroRole::MAIN ? 1.5f : 0.5f;
	if(skillId == "new-horizons:wisdom")
		return role == HeroRole::MAIN ? 1.5f : 0.25f;
	return std::nullopt;
}

const SecondarySkillEvaluator HeroManager::mainSkillsEvaluator = SecondarySkillEvaluator(
	{
		std::make_shared<SecondarySkillScoreMap>(
			std::map<SecondarySkill, float>
			{
				{SecondarySkill::DIPLOMACY, 2},
				{SecondarySkill::LOGISTICS, 2},
				{SecondarySkill::EARTH_MAGIC, 2},
				{SecondarySkill::ARMORER, 2},
				{SecondarySkill::OFFENCE, 2},
				{SecondarySkill::AIR_MAGIC, 1},
				{SecondarySkill::WISDOM, 1},
				{SecondarySkill::LEADERSHIP, 1},
				{SecondarySkill::INTELLIGENCE, 1},
				{SecondarySkill::RESISTANCE, 1},
				{SecondarySkill::MYSTICISM, -1},
				{SecondarySkill::SORCERY, -1},
				{SecondarySkill::ESTATES, -1},
				{SecondarySkill::FIRST_AID, -1},
				{SecondarySkill::LEARNING, -1},
				{SecondarySkill::SCHOLAR, -1},
				{SecondarySkill::EAGLE_EYE, -1},
				{SecondarySkill::NAVIGATION, -1}
			}),
		std::make_shared<ExistingSkillRule>(),
		std::make_shared<WisdomRule>(),
		std::make_shared<AtLeastOneMagicRule>()
	});

const SecondarySkillEvaluator HeroManager::scoutSkillsEvaluator = SecondarySkillEvaluator(
	{
		std::make_shared<SecondarySkillScoreMap>(
			std::map<SecondarySkill, float>
			{
				{SecondarySkill::LOGISTICS, 2},
				{SecondarySkill::ESTATES, 2},
				{SecondarySkill::PATHFINDING, 1},
				{SecondarySkill::SCHOLAR, 1}
			}),
		std::make_shared<ExistingSkillRule>()
	});

float HeroManager::evaluateSecSkill(SecondarySkill skill, const CGHeroInstance * hero) const
{
	auto role = getHeroRoleOrDefaultInefficient(hero);
	if(const auto strategicScore = evaluateNewHorizonsStrategicSkillRoleScore(
		SecondarySkill::encode(skill.getNum()), role))
	{
		float score = *strategicScore;
		ExistingSkillRule().evaluateScore(hero, skill, score);
		return score;
	}

	if(role == HeroRole::MAIN)
	{
		float score = mainSkillsEvaluator.evaluateSecSkill(hero, skill);
		const int armorer = SecondarySkill::decode("new-horizons:armorer");
		const int archery = SecondarySkill::decode("new-horizons:archery");
		if((armorer >= 0 && skill == SecondarySkill(armorer))
			|| (archery >= 0 && skill == SecondarySkill(archery)))
		{
			score = 2.0f;
			ExistingSkillRule().evaluateScore(hero, skill, score);
		}
		return score;
	}

	return scoutSkillsEvaluator.evaluateSecSkill(hero, skill);
}

float HeroManager::evaluateSpeciality(const CGHeroInstance * hero) const
{
	auto heroSpecial = Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(hero->getHeroTypeID()));
	auto secondarySkillBonus = Selector::targetSourceType()(BonusSource::SECONDARY_SKILL);
	auto specialSecondarySkillBonuses = hero->getBonuses(heroSpecial.And(secondarySkillBonus), "HeroManager::evaluateSpeciality");
	auto secondarySkillBonuses = hero->getBonusesFrom(BonusSource::SECONDARY_SKILL);
	float specialityScore = 0.0f;

	for(auto bonus : *secondarySkillBonuses)
	{
		auto hasBonus = !!specialSecondarySkillBonuses->getFirst(Selector::typeSubtype(bonus->type, bonus->subtype));

		if(hasBonus)
		{
			SecondarySkill bonusSkill = bonus->sid.as<SecondarySkill>();
			float bonusScore = mainSkillsEvaluator.evaluateSecSkill(hero, bonusSkill);

			if(bonusScore > 0)
				specialityScore += bonusScore * bonusScore * bonusScore;
		}
	}

	return specialityScore;
}

float HeroManager::evaluateFightingStrength(const CGHeroInstance * hero) const
{
	// TODO: Mircea: Shouldn't we count bonuses from artifacts when generating the fighting strength? That could make a huge difference
	return evaluateSpeciality(hero) + mainSkillsEvaluator.evaluateSecSkills(hero) + hero->getBasePrimarySkillValue(PrimarySkill::ATTACK)
		 + hero->getBasePrimarySkillValue(PrimarySkill::DEFENSE) + hero->getBasePrimarySkillValue(PrimarySkill::SPELL_POWER)
		 + hero->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE);
}

void HeroManager::update()
{
	logAi->trace("Start analysing our heroes");

	HeroMap<float> scores;
	auto myHeroes = cc->getHeroesInfo();
	uint64_t strongestHeroTotalStrength = 0;

	for(auto & hero : myHeroes)
		vstd::amax(strongestHeroTotalStrength, hero->getTotalStrength());

	for(auto & hero : myHeroes)
	{
		scores[hero] = evaluateMainHeroRoleScore(evaluateFightingStrength(hero), hero->getTotalStrength(), strongestHeroTotalStrength);
		knownFightingStrength[hero->id] = normalizeHeroStrength(hero->getHeroStrength());
	}

	auto scoreSort = [&](const CGHeroInstance * h1, const CGHeroInstance * h2) -> bool
	{
		return scores.at(h1) > scores.at(h2);
	};

	const int biggerMapFactor = cc->getCalendar().getCurrentDay() > 21 ? cc->getMapSize().x / CMapHeader::MAP_SIZE_LARGE : 0;
	// One per town + static bonus on bigger maps after some weeks
	int globalMainCount = std::max(static_cast<int>(cc->getTownsInfo().size()) + biggerMapFactor, 1);
	// If 1 town but big map, limit a bit to don't spread the army too much
	globalMainCount = std::min(globalMainCount, static_cast<int>(cc->getTownsInfo().size() * 2));

	// TODO: Mircea: Should spread them on map min 1 per town, avoiding all within the same town and the other towns just with dummy scouts
	logAi->trace("HeroManager::update Max number of main heroes (globalMainCount) is %d", globalMainCount);

	std::sort(myHeroes.begin(), myHeroes.end(), scoreSort);
	heroToRoleMap.clear();

	for(const auto hero : myHeroes)
	{
		HeroPtr heroPtr(hero, aiNk->cc.get());
		HeroRole role;
		if(hero->patrol.patrolling)
		{
			// Patrolling seems to be a special feature on some maps
			role = MAIN;
		}
		else
		{
			role = globalMainCount-- > 0 ? MAIN : SCOUT;
		}

		heroToRoleMap[heroPtr] = role;
		logAi->trace("HeroManager::update Hero %s has role %s", heroPtr.nameOrDefault(), role == MAIN ? "main" : "scout");
	}
}

HeroRole HeroManager::getHeroRoleOrDefaultInefficient(const  CGHeroInstance * hero) const
{
	return getHeroRoleOrDefault(HeroPtr(hero, aiNk->cc.get()));
}

// TODO: Mircea: Do we need this map on HeroPtr or is enough just on hero?
HeroRole HeroManager::getHeroRoleOrDefault(const HeroPtr & heroPtr) const
{
	if(heroToRoleMap.find(heroPtr) != heroToRoleMap.end())
		return heroToRoleMap.at(heroPtr);
	return HeroRole::SCOUT;
}

int HeroManager::selectBestSkillIndex(const HeroPtr & heroPtr, const std::vector<SecondarySkill> & skills) const
{
	const auto role = getHeroRoleOrDefault(heroPtr);
	const auto & evaluator = role == MAIN ? mainSkillsEvaluator : scoutSkillsEvaluator;
	const auto * hero = heroPtr.getUnverified();
	if(!hero)
		return 0;
	const auto & rules = hero->getPrimaryGrowthRules();
	const auto ownFactionSkill = newHorizonsHeroes::factionSkill(rules, hero->getFactionID());
	int result = 0;
	float resultScore = -100;

	for(int i = 0; i < skills.size(); i++)
	{
		auto score = evaluator.evaluateSecSkill(hero, skills[i]);
		// New Horizons heroes already begin with their faction skill. Keep the
		// identity meaningful when it is offered for a later rank: the generic
		// evaluator has no legacy probability entry for these skills and would
		// otherwise routinely prefer an unrelated skill. The authoritative query
		// still validates the final choice; this is only the AI's preference.
		if(ownFactionSkill && newHorizonsHeroes::isFactionSkillForFaction(
			rules, hero->getFactionID(), skills[i]))
			score += 1000.0f;
		else if(newHorizonsHeroes::isFactionSkill(rules, skills[i]))
			score -= 1000.0f;
		if(score > resultScore)
		{
			resultScore = score;
			result = i;
		}

		logAi->trace(
			"Hero %s is offered to learn %d with score %f",
			heroPtr.nameOrDefault(),
			skills[i].toEnum(),
			score);
	}

	return result;
}

float HeroManager::evaluateHero(const CGHeroInstance * hero) const
{
	return evaluateFightingStrength(hero);
}

bool HeroManager::heroCapReached(bool includeGarrisoned) const
{
	int heroCount = cc->getHeroCount(aiNk->playerID, includeGarrisoned);
	int maxAllowed = aiNk->settings->getMaxRoamingHeroes();
	if (aiNk->settings->getMaxRoamingHeroesPerTown() > 0)
	{
		maxAllowed += cc->howManyTowns() * aiNk->settings->getMaxRoamingHeroesPerTown();
	}

	return heroCount >= maxAllowed
		|| heroCount >= cc->getSettings().getInteger(EGameSettings::HEROES_PER_PLAYER_ON_MAP_CAP)
		|| heroCount >= cc->getSettings().getInteger(EGameSettings::HEROES_PER_PLAYER_TOTAL_CAP);
}

float HeroManager::getFightingStrengthCached(const CGHeroInstance * hero) const
{
	auto cached = knownFightingStrength.find(hero->id);

	//FIXME: fallback to hero->getFightingStrength() is VERY slow on higher difficulties (no object graph? map reveal?)
	return normalizeHeroStrength(cached != knownFightingStrength.end() ? cached->second : hero->getHeroStrength());
}

float HeroManager::getMagicStrength(const CGHeroInstance * hero) const
{
	auto manaLimit = hero->manaLimit();
	auto spellPower = static_cast<float>(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)) / hero->getEffectPowerDivisor(nullptr);

	auto score = 0.0f;

	// FIXME: this will not cover spells give by scrolls / tomes. Intended?
	for(auto spellId : hero->getSpellsInSpellbook())
	{
		if(!newHorizonsMagic::spellAllowedBySavedRoster(hero->getMagicRules(), spellId))
			continue;
		auto spell = spellId.toSpell();

		if (!spell->isAdventure())
			continue;

		auto schoolLevel = hero->getSpellSchoolLevel(spell);
		auto townPortalEffect = spell->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);

		score += (hero->getSpellLevel(spell) + 1) * (schoolLevel + 1) * 0.05f;

		if (spell->getAdventureMechanics().givesBonus(hero, BonusType::FLYING_MOVEMENT))
			score += 0.3;

		if(townPortalEffect != nullptr && schoolLevel != 0)
			score += 0.6f;
	}

	vstd::amin(score, 1);

	score *= std::min(1.0f, spellPower / 10.0f);

	vstd::amin(score, 1);

	score *= std::min(1.0f, manaLimit / 100.0f);

	return std::min(score, 1.0f);
}

bool HeroManager::canRecruitHero(const CGTownInstance * town) const
{
	if(!town)
		town = findTownWithTavern();

	if(!town || !townHasFreeTavern(town))
		return false;

	if(cc->getResourceAmount(EGameResID::GOLD) < GameConstants::HERO_GOLD_COST)
		return false;

	if(heroCapReached())
		return false;

	if(!cc->getAvailableHeroes(town).size())
		return false;

	return true;
}

const CGTownInstance * HeroManager::findTownWithTavern() const
{
	for(const CGTownInstance * t : cc->getTownsInfo())
		if(townHasFreeTavern(t))
			return t;

	return nullptr;
}

const CGHeroInstance * HeroManager::findHeroWithGrail() const
{
	for(const CGHeroInstance * h : cc->getHeroesInfo())
	{
		if(h->hasArt(ArtifactID::GRAIL))
			return h;
	}
	return nullptr;
}

const CGHeroInstance * HeroManager::findWeakHeroToDismiss(uint64_t armyLimit, const CGTownInstance* townToSpare) const
{
	const CGHeroInstance * weakestHero = nullptr;
	auto myHeroes = aiNk->cc->getHeroesInfo();

	for(auto existingHero : myHeroes)
	{
		if(aiNk->getHeroLockedReason(existingHero) == HeroLockedReason::DEFENCE
			|| existingHero->getArmyStrength() >armyLimit
			|| getHeroRoleOrDefaultInefficient(existingHero) == HeroRole::MAIN
			|| existingHero->movementPointsRemaining()
			|| (townToSpare != nullptr && existingHero->getVisitedTown() == townToSpare)
			|| existingHero->artifactsWorn.size() > (existingHero->hasSpellbook() ? 2 : 1))
		{
			continue;
		}

		if(!weakestHero || weakestHero->getHeroStrength() > existingHero->getHeroStrength())
		{
			weakestHero = existingHero;
		}
	}

	return weakestHero;
}

SecondarySkillScoreMap::SecondarySkillScoreMap(std::map<SecondarySkill, float> scoreMap)
	:scoreMap(scoreMap)
{
}

void SecondarySkillScoreMap::evaluateScore(const CGHeroInstance * hero, SecondarySkill skill, float & score) const
{
	auto it = scoreMap.find(skill);

	if(it != scoreMap.end())
	{
		score = it->second;
	}
}

void ExistingSkillRule::evaluateScore(const CGHeroInstance * hero, SecondarySkill skill, float & score) const
{
	int upgradesLeft = 0;

	for(auto & heroSkill : hero->secSkills)
	{
		if(heroSkill.first == skill)
			return;

		upgradesLeft += MasteryLevel::EXPERT - heroSkill.second;
	}

	if(score >= 2 || (score >= 1 && upgradesLeft <= 1))
		score += 1.5;
}

void WisdomRule::evaluateScore(const CGHeroInstance * hero, SecondarySkill skill, float & score) const
{
	if(skill != SecondarySkill::WISDOM)
		return;

	auto wisdomLevel = hero->getSecSkillLevel(SecondarySkill::WISDOM);

	if(hero->level > 10 && wisdomLevel == MasteryLevel::NONE)
		score += 1.5;
}

const std::vector<SecondarySkill> AtLeastOneMagicRule::magicSchools = {
	SecondarySkill::AIR_MAGIC,
	SecondarySkill::EARTH_MAGIC,
	SecondarySkill::FIRE_MAGIC,
	SecondarySkill::WATER_MAGIC
};

void AtLeastOneMagicRule::evaluateScore(const CGHeroInstance * hero, SecondarySkill skill, float & score) const
{
	auto activeSkills = magicSchools;
	std::optional<SpellSchool> selectedSchool;
	const auto & rules = hero->getMagicRules();
	if(!rules.isNull() && !rules.Struct().empty())
	{
		activeSkills.clear();
		for(const auto & [school, skillName] : rules["schoolSkills"].Struct())
		{
			const SecondarySkill schoolSkill(SecondarySkill::decode(skillName.String()));
			activeSkills.push_back(schoolSkill);
			if(schoolSkill == skill)
				selectedSchool = SpellSchool::fromSerializationKey(school);
		}
	}
	if(!vstd::contains(activeSkills, skill))
		return;

	bool heroHasAnyMagic = vstd::contains_if(activeSkills, [&](SecondarySkill skill) -> bool
	{
		return hero->getSecSkillLevel(skill) > MasteryLevel::NONE;
	});

	if(!heroHasAnyMagic)
		score += 1;
	if(selectedSchool)
	{
		// Value actual known spells in this saved school, not a fixed preference
		// for one of the old four IDs. Numerical tuning is intentionally provisional.
		float knownSpellValue = 0;
		for(const auto spellID : hero->getSpellsInSpellbook())
			if(newHorizonsMagic::spellAllowedBySavedRoster(hero->getMagicRules(), spellID)
				&& vstd::contains(hero->getSpellSchools(spellID.toSpell()), *selectedSchool))
				knownSpellValue += 0.5f;
		score += std::min(knownSpellValue, 2.0f);
	}
}

SecondarySkillEvaluator::SecondarySkillEvaluator(std::vector<std::shared_ptr<ISecondarySkillRule>> evaluationRules)
	: evaluationRules(evaluationRules)
{
}

float SecondarySkillEvaluator::evaluateSecSkills(const CGHeroInstance * hero) const
{
	float totalScore = 0;

	for(auto skill : hero->secSkills)
	{
		totalScore += skill.second * evaluateSecSkill(hero, skill.first);
	}

	return totalScore;
}

float SecondarySkillEvaluator::evaluateSecSkill(const CGHeroInstance * hero, SecondarySkill skill) const
{
	float score = 0;

	for(auto rule : evaluationRules)
		rule->evaluateScore(hero, skill, score);

	return score;
}

}
