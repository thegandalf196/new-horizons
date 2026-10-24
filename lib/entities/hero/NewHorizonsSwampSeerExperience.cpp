/*
 * NewHorizonsSwampSeerExperience.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSwampSeer.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../rewardable/Reward.h"
#include <algorithm>
#include <limits>
namespace newHorizonsHeroes
{
namespace
{
bool activeLearning(const CGHeroInstance & hero)
{
	return hero.getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning"))) > MasteryLevel::NONE;
}
int32_t baseField(const CGHeroInstance & hero, PrimarySkill skill)
{
	return hero.projectPrimarySkillChange(skill, 0, ChangeValueMode::RELATIVE).before;
}
}
bool isSwampSeerStudyObject(std::optional<Obj> source)
{
	return source && (*source == Obj::GARDEN_OF_REVELATION || *source == Obj::STAR_AXIS
		|| *source == Obj::LIBRARY_OF_ENLIGHTENMENT || *source == Obj::SCHOOL_OF_MAGIC);
}
std::optional<StudyPrimarySnapshot> captureStudyPrimarySnapshot(const CGHeroInstance & hero, std::optional<Obj> source)
{
	if(!swampSeerRules(&hero) || !isSwampSeerStudyObject(source) || !activeLearning(hero))
		return std::nullopt;
	return StudyPrimarySnapshot{baseField(hero, PrimarySkill::KNOWLEDGE), baseField(hero, PrimarySkill::SPELL_POWER)};
}
int64_t acceptedStudyPrimaryPoints(const CGHeroInstance & hero, const std::optional<StudyPrimarySnapshot> & before)
{
	if(!before)
		return 0;
	return std::max<int64_t>(0, int64_t(baseField(hero, PrimarySkill::KNOWLEDGE)) - before->knowledge)
		+ std::max<int64_t>(0, int64_t(baseField(hero, PrimarySkill::SPELL_POWER)) - before->spellPower);
}
int64_t projectedStudyPrimaryPoints(const CGHeroInstance & hero, const Rewardable::Reward & reward, std::optional<Obj> source)
{
	if(!captureStudyPrimarySnapshot(hero, source))
		return 0;
	int64_t points = 0;
	for(const auto skill : {PrimarySkill::KNOWLEDGE, PrimarySkill::SPELL_POWER})
	{
		const auto index = static_cast<size_t>(skill.getNum());
		if(index >= reward.primary.size())
			continue;
		const auto change = hero.projectPrimarySkillChange(skill, reward.primary[index], ChangeValueMode::RELATIVE);
		points += std::max<int64_t>(0, int64_t(change.after) - change.before);
	}
	return points;
}
int64_t swampSeerStudyExperience(const CGHeroInstance * hero, std::optional<Obj> source, int64_t acceptedPrimaryPoints)
{
	if(acceptedPrimaryPoints < 0)
		throw std::invalid_argument("Negative accepted study primary points");
	const auto rules = swampSeerRules(hero);
	if(!rules || !hero || !isSwampSeerStudyObject(source) || !activeLearning(*hero)
		|| acceptedPrimaryPoints == 0 || rules->studyExperiencePerMagicPrimaryPoint == 0)
		return 0;
	const auto maximum = std::numeric_limits<int64_t>::max();
	const auto raw = acceptedPrimaryPoints > maximum / rules->studyExperiencePerMagicPrimaryPoint
		? maximum : acceptedPrimaryPoints * rules->studyExperiencePerMagicPrimaryPoint;
	return hero->calculateXp(raw); // Separate component: no Historian source classification.
}
int64_t projectedSwampSeerStudyExperience(const CGHeroInstance * hero, const Rewardable::Reward & reward, std::optional<Obj> source)
{
	return hero ? swampSeerStudyExperience(hero, source, projectedStudyPrimaryPoints(*hero, reward, source)) : 0;
}
int64_t addStudyExperience(int64_t ordinary, int64_t study)
{
	if(study > 0 && ordinary > std::numeric_limits<int64_t>::max() - study)
		return std::numeric_limits<int64_t>::max();
	if(study < 0 && ordinary < std::numeric_limits<int64_t>::min() - study)
		return std::numeric_limits<int64_t>::min();
	return ordinary + study;
}
}
