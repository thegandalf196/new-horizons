/*
 * NewHorizonsEagleEye.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsEagleEye.h"
#include "NewHorizonsMagic.h"
#include "../entities/hero/NewHorizonsLearningSpecialties.h"
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

std::vector<SpellID> selectSpells(const CGHeroInstance * hero,
	const std::vector<SpellID> & enemyHeroCasts)
{
	if(!enabled(hero))
		return {};
	std::vector<SpellID> eligible;
	for(const auto spellId : enemyHeroCasts)
	{
		if(vstd::contains(eligible, spellId)
			|| !newHorizonsMagic::spellAllowedByHeroRoster(hero->getMagicRules(), spellId))
			continue;
		const auto * spell = spellId.toEntity(LIBRARY->spells());
		if(!spell)
			continue;
		const int level = hero->getSpellLevel(spell);
		if(level >= 1 && level <= 3 && hero->canLearnSpell(spell))
			eligible.push_back(spellId);
	}
	std::stable_sort(eligible.begin(), eligible.end(), [hero](SpellID lhs, SpellID rhs)
	{
		return hero->getSpellLevel(lhs.toEntity(LIBRARY->spells()))
			> hero->getSpellLevel(rhs.toEntity(LIBRARY->spells()));
	});
	const auto maximum = static_cast<std::size_t>(1 + newHorizonsHeroes::additionalEagleEyeSpells(hero));
	if(eligible.size() > maximum)
		eligible.resize(maximum);
	return eligible;
}
std::optional<SpellID> selectSpell(const CGHeroInstance * hero,
	const std::vector<SpellID> & enemyHeroCasts)
{
	const auto selected = selectSpells(hero, enemyHeroCasts);
	return selected.empty() ? std::nullopt : std::optional(selected.front());
}
}
