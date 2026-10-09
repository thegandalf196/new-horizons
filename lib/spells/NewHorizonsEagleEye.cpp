/*
 * NewHorizonsEagleEye.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsEagleEye.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../GameLibrary.h"
#include "../mapObjects/CGHeroInstance.h"
#include <vcmi/spells/Spell.h>

namespace newHorizonsEagleEye
{
bool enabled(const CGHeroInstance * hero)
{
	return hero && newHorizonsMagic::rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION
		&& hero->hasActivePerk(std::string(SKILL_ID), std::string(PERK_ID));
}

std::optional<SpellID> selectSpell(const CGHeroInstance * hero,
	const std::vector<SpellID> & enemyHeroCasts)
{
	if(!enabled(hero))
		return std::nullopt;
	std::optional<SpellID> selected;
	int highestLevel = 0;
	for(const auto spellId : enemyHeroCasts)
	{
		if(!newHorizonsMagic::spellAllowedByHeroRoster(hero->getMagicRules(), spellId))
			continue;
		const auto * spell = spellId.toEntity(LIBRARY->spells());
		if(!spell)
			continue;
		const int level = hero->getSpellLevel(spell);
		if(level < 1 || level > 3 || level <= highestLevel || !hero->canLearnSpell(spell))
			continue;
		selected = spellId;
		highestLevel = level;
	}
	return selected;
}
}
