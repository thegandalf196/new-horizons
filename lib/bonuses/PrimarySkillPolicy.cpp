/*
 * PrimarySkillPolicy.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PrimarySkillPolicy.h"
#include "BonusList.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
#include "../json/JsonNode.h"

namespace primarySkillPolicy
{
LegacyBounds legacyBounds()
{
	std::array<int, 4> minValues = {
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MINIMAL_PRIMARY_SKILLS, PrimarySkill::ATTACK),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MINIMAL_PRIMARY_SKILLS, PrimarySkill::DEFENSE),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MINIMAL_PRIMARY_SKILLS, PrimarySkill::SPELL_POWER),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MINIMAL_PRIMARY_SKILLS, PrimarySkill::KNOWLEDGE)
	};

	std::array<int, 4> maxValues = {
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MAXIMAL_PRIMARY_SKILLS, PrimarySkill::ATTACK),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MAXIMAL_PRIMARY_SKILLS, PrimarySkill::DEFENSE),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MAXIMAL_PRIMARY_SKILLS, PrimarySkill::SPELL_POWER),
		LIBRARY->engineSettings()->getVectorValue(EGameSettings::HEROES_MAXIMAL_PRIMARY_SKILLS, PrimarySkill::KNOWLEDGE)
	};
	return {minValues, maxValues};
}

int LegacyBounds::value(PrimarySkill skill, int rawValue) const
{
	return std::clamp(rawValue, minimum[skill.getNum()], std::max(minimum[skill.getNum()], maximum[skill.getNum()]));
}

int growthValue(int rawValue, const JsonNode & rules)
{
	return std::clamp(rawValue, 0, static_cast<int>(rules["maxPrimary"].Integer()));
}

int projectedValue(PrimarySkill skill, const BonusList & primaryBonuses, const JsonNode * growthRules)
{
	if(growthRules)
		return growthValue(primaryBonuses.valOfBonuses(Selector::subtype()(skill)), *growthRules);
	const auto bounds = legacyBounds();
	return bounds.value(skill, primaryBonuses.valOfBonuses(Selector::subtype()(skill)));
}
}
