/*
 * NewHorizonsLearningExperience.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningSpecialties.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsSwampSeer.h"
#include "../../GameLibrary.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
namespace newHorizonsHeroes
{
int64_t calculateLearningSpecialtyExperience(const CGHeroInstance & hero, int64_t exp, int32_t additionalPercent)
{
	const auto specialty = hero.getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::NATURAL_PHILOSOPHER);
	const auto learned = learningSpecialtyRules(&hero);
	const auto exchange = learningExchangeSpecialtyRules(&hero);
	const auto seer = swampSeerRules(&hero);
	if((!specialty && !learned && !exchange && !seer) || exp <= 0)
		return static_cast<TExpType>(
			exp * (hero.valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT) + additionalPercent) / 100.0);
	const auto scaledExperience = [](TExpType base, int64_t percent) -> TExpType
	{
		const bool negative = percent < 0;
		const auto magnitude = static_cast<uint64_t>(negative ? -percent : percent);
		if(magnitude == 0)
			return 0;
		const auto maximum = std::numeric_limits<TExpType>::max();
		const uint64_t limit = static_cast<uint64_t>(maximum) + (negative ? 1 : 0);
		const auto whole = static_cast<uint64_t>(base) / 100;
		const auto remainder = (static_cast<uint64_t>(base) % 100) * magnitude / 100;
		const auto saturated = negative ? std::numeric_limits<TExpType>::min() : maximum;
		if(whole > limit / magnitude)
			return saturated;
		const auto product = whole * magnitude;
		if(remainder > limit - product)
			return saturated;
		const auto value = product + remainder;
		if(negative && value == limit)
			return saturated;
		return negative ? -static_cast<TExpType>(value) : static_cast<TExpType>(value);
	};
	// Only the captured specialty path uses checked integer scaling. The old
	// policy-absent calculation retains its established floating-point behavior.
	const auto ordinary = scaledExperience(exp,
		static_cast<int64_t>(hero.valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT)) + additionalPercent);
	const auto learning = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), SecondarySkill::entityType(), "new-horizons:learning", true);
	if(!learning || (!specialty && hero.getSecSkillLevel(SecondarySkill(*learning)) == MasteryLevel::NONE))
		return ordinary;
	const auto coreBonuses = hero.getAllBonuses(Selector::type()(BonusType::HERO_EXPERIENCE_GAIN_PERCENT)
		.And(Selector::source(BonusSource::SECONDARY_SKILL, BonusSourceID(SecondarySkill(*learning)))));
	int64_t corePercent = 0;
	for(const auto & bonus : *coreBonuses)
		if(bonus->valType == BonusValueType::PERCENT_TO_BASE)
			corePercent += bonus->val;
	const auto coreExperience = scaledExperience(exp,
		static_cast<int>(std::clamp<int64_t>(corePercent, 0, std::numeric_limits<int>::max())));
	const LevelScaledSpecialty scaling = specialty ? specialty->naturalPhilosopher
		: learned ? learned->learning
		: exchange ? LevelScaledSpecialty{exchange->learningPercentPerLevel, exchange->learningMaximumPercent}
		: LevelScaledSpecialty{seer->learningPercentPerLevel, seer->learningMaximumPercent};
	const auto percent = specialty ? druidSpecialtyPercent(hero.level, scaling)
		: static_cast<int>(std::min<int64_t>(int64_t(hero.level) * scaling.percentPerLevel, scaling.maximumPercent));
	const auto extra = druidSpecialtyExtra(coreExperience, percent);
	if(ordinary > 0 && extra > std::numeric_limits<TExpType>::max() - ordinary)
		return std::numeric_limits<TExpType>::max();
	return ordinary + extra;
}

}
