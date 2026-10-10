/*
 * NewHorizonsSage.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsSage.h"
#include "CGHeroInstance.h"
#include "CGTownInstance.h"
#include "../callback/IGameInfoCallback.h"
#include "../GameLibrary.h"
#include "../GameConstants.h"
#include "../spells/CSpellHandler.h"
#include "../spells/NewHorizonsMagic.h"

namespace newHorizonsSage
{
bool firstGuildVisit(const CGHeroInstance & hero, const CGTownInstance & town)
{
	return hero.cb && hero.id.hasValue() && town.id.hasValue()
		&& newHorizonsMagic::rulesActive(hero.getMagicRules())
		&& newHorizonsMagic::mageGuildGenerationActive(hero.getMagicRules())
		&& town.mageGuildLevel() > 0
		&& hero.cb->getPlayerRelations(hero.getOwner(), town.getOwner()) != PlayerRelations::ENEMIES
		&& !hero.getNewHorizonsSageGuildVisits().contains(town.id);
}

std::vector<SpellID> candidates(const CGHeroInstance & hero, const CGTownInstance & town)
{
	std::vector<SpellID> result;
	const auto & rules = hero.getMagicRules();
	if(!hero.cb || !newHorizonsMagic::rulesActive(rules)
		|| !newHorizonsMagic::mageGuildGenerationActive(rules)
		|| town.newHorizonsMageGuildVisibleSpellSchools.size() != GameConstants::SPELL_LEVELS)
		return result;
	for(const auto & definition : LIBRARY->spellh->objects)
	{
		if(!definition || !hero.cb->isAllowed(definition->getId()))
			continue;
		const auto spell = definition->getId();
		const int level = newHorizonsMagic::spellLevel(rules, spell);
		if(!definition->isCommonHeroSpell() || definition->isAdventure()
			|| level < 1 || level > town.mageGuildLevel()
			|| hero.spellbookContainsSpell(spell) || !hero.canLearnSpell(definition.get()))
			continue;
		bool displayed = false;
		for(int row = 0; row < GameConstants::SPELL_LEVELS; ++row)
			for(int index = 0; index < town.spellsAtLevel(row + 1, false) && index < town.spells.at(row).size(); ++index)
				displayed |= town.spells.at(row).at(index) == spell;
		if(displayed)
			continue;
		const auto schools = newHorizonsMagic::spellSchools(rules, spell);
		const auto & available = town.newHorizonsMageGuildVisibleSpellSchools.at(level - 1);
		if(std::any_of(available.begin(), available.end(), [&](SpellSchool school){ return vstd::contains(schools, school); }))
			result.push_back(spell);
	}
	std::sort(result.begin(), result.end(), [&](SpellID left, SpellID right)
	{
		const auto leftLevel = newHorizonsMagic::spellLevel(rules, left);
		const auto rightLevel = newHorizonsMagic::spellLevel(rules, right);
		return leftLevel != rightLevel ? leftLevel > rightLevel : left.toSpell()->getJsonKey() < right.toSpell()->getJsonKey();
	});
	return result;
}

std::optional<SpellID> selectSpell(const CGHeroInstance & hero, const CGTownInstance & town)
{
	const auto pool = candidates(hero, town);
	return pool.empty() ? std::nullopt : std::optional<SpellID>(pool.front());
}

std::optional<SpellID> wisdomReveal(const CGHeroInstance & hero, const CGTownInstance & town)
{
	if(!hero.hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage"))
		return std::nullopt;
	return selectSpell(hero, town);
}

bool learningSelected(const CGHeroInstance & hero)
{
	return hero.hasActivePerk("new-horizons:learning", "new-horizons:learning.sage");
}

bool hasVisitReward(const CGHeroInstance & hero, const CGTownInstance & town)
{
	return firstGuildVisit(hero, town)
		&& (wisdomReveal(hero, town).has_value()
			|| (learningSelected(hero) && selectSpell(hero, town).has_value()));
}
}
