/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsPariahLearning.h"
#include "NewHorizonsCastleSpecialties.h"
#include "CHero.h"
#include "../../GameLibrary.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/CSpellHandler.h"
#include "../../spells/CSpell.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
#include <vstd/RNG.h>

namespace newHorizonsHeroes
{
namespace
{
bool inSchool(const JsonNode & rules, SpellID spell, const SpellSchool & school)
{
	const auto schools = newHorizonsMagic::spellSchools(rules, spell);
	return std::ranges::find(schools, school) != schools.end();
}

bool learnableMirror(const CGHeroInstance & hero, const CSpell & spell)
{
	const auto & rules = hero.getMagicRules();
	const auto id = spell.getId();
	return spell.isCommonHeroSpell() && !spell.isSpecial() && !spell.isCreatureAbility()
		&& newHorizonsMagic::spellAllowedByHeroRoster(rules, id)
		&& newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, id)
		&& !newHorizonsMagic::isAdventureSpell(rules, id)
		&& !(hero.getHeroType() && vstd::contains(hero.getHeroType()->excludedSpells, id))
		&& (!hero.cb || hero.cb->isAllowed(id));
}
}

std::vector<SpellID> pariahMirrorSpells(const CGHeroInstance & hero, const std::set<SpellID> & learned, vstd::RNG & rng,
	bool learnedAlreadyKnown)
{
	std::vector<SpellID> result;
	if(!pariahRule(&hero) || learned.empty() || !newHorizonsMagic::rulesActive(hero.getMagicRules()))
		return result;
	const auto & rules = hero.getMagicRules();
	const SpellSchool light(SpellSchool::decode("new-horizons:light"));
	const SpellSchool shadow(SpellSchool::decode("new-horizons:shadow"));
	if(!light.hasValue() || !shadow.hasValue())
		return result;
	for(const auto & spell : learned)
	{
		if((!learnedAlreadyKnown && hero.spellbookContainsSpell(spell)) || !inSchool(rules, spell, light))
			continue;
		const int level = newHorizonsMagic::spellLevel(rules, spell);
		std::vector<SpellID> pool;
		for(const auto & candidate : LIBRARY->spellh->objects)
		{
			if(!candidate)
				continue;
			const auto id = candidate->getId();
			if(newHorizonsMagic::spellLevel(rules, id) != level || !inSchool(rules, id, shadow)
				|| hero.spellbookContainsSpell(id) || learned.contains(id)
				|| std::ranges::find(result, id) != result.end() || !learnableMirror(hero, *candidate))
				continue;
			pool.push_back(id);
		}
		if(pool.empty())
			continue;
		const size_t index = pool.size() == 1 ? 0 : static_cast<size_t>(rng.nextInt64(0, pool.size() - 1));
		result.push_back(pool.at(index));
	}
	return result;
}
}
