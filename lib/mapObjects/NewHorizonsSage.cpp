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

std::vector<SpellID> candidates(const CGHeroInstance &, const CGTownInstance &)
{
	return {}; // Retired reveal/hidden-learning effects have no candidate pool.
}

std::optional<SpellID> selectSpell(const CGHeroInstance & hero, const CGTownInstance & town)
{
	const auto pool = candidates(hero, town);
	return pool.empty() ? std::nullopt : std::optional<SpellID>(pool.front());
}

std::optional<SpellID> wisdomReveal(const CGHeroInstance &, const CGTownInstance &)
{
	return std::nullopt; // Wisdom Sage modifies effective Knowledge and reveals no spells.
}

bool learningSelected(const CGHeroInstance &)
{
	return false; // Both old Sage reveal mechanics are retired.
}

bool hasVisitReward(const CGHeroInstance & hero, const CGTownInstance & town)
{
	return firstGuildVisit(hero, town)
		&& (wisdomReveal(hero, town).has_value()
			|| (learningSelected(hero) && selectSpell(hero, town).has_value()));
}
}
