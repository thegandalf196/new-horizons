/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "NewHorizonsSkillPresentation.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/CCallback.h"

namespace newHorizonsSkillPresentation
{
/// Generic components have world context but no owning hero. Explicit hero
/// panels instead pass that hero's captured rules, including hero overrides.
inline const JsonNode & currentRules()
{
	return GAME->interface() && GAME->interface()->cb
		? GAME->interface()->cb->getHeroPerkRules()
		: LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
}

inline std::string currentName(SecondarySkill skill)
{
	const auto * definition = skill.toSkill();
	return skillName(currentRules(), definition->getJsonKey(), definition->getNameTranslated());
}

inline std::string currentDescription(SecondarySkill skill, int rank)
{
	const auto * definition = skill.toSkill();
	return rankDescription(currentRules(), definition->getJsonKey(), rank,
		definition->getDescriptionTranslated(rank));
}
}
