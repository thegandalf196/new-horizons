/*
 * HeroScholar.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsMagic.h"
#include <algorithm>
#include <stdexcept>
std::vector<SpellID> CGHeroInstance::getNewHorizonsScholarSpellsFor(
	const CGHeroInstance & recipient, size_t maximumSpells) const
{
	if(maximumSpells > 2)
		throw std::invalid_argument("Unsupported Scholar spell budget");
	if(this == &recipient || !hasSpellbook() || !recipient.hasSpellbook() || maximumSpells == 0)
		return {};
	std::vector<SpellID> selected;
	selected.reserve(maximumSpells + 1);
	const auto higherPriority = [&recipient](SpellID left, SpellID right)
	{
		const auto leftLevel = recipient.getSpellLevel(left.toSpell());
		const auto rightLevel = recipient.getSpellLevel(right.toSpell());
		return leftLevel != rightLevel ? leftLevel > rightLevel
			: left.toSpell()->getJsonKey() < right.toSpell()->getJsonKey();
	};
	for(const auto spell : getSpellsInSpellbook())
	{
		if(!spells.contains(spell) || !newHorizonsMagic::spellAllowedByHeroRoster(getMagicRules(), spell)
			|| !recipient.canLearnSpell(spell.toSpell()))
			continue;
		const auto position = std::ranges::lower_bound(selected, spell, higherPriority);
		selected.insert(position, spell);
		if(selected.size() > maximumSpells)
			selected.pop_back();
	}
	return selected;
}
std::optional<SpellID> CGHeroInstance::getNewHorizonsScholarSpellFor(const CGHeroInstance & recipient) const
{
	const auto selected = getNewHorizonsScholarSpellsFor(recipient, 1);
	return selected.empty() ? std::nullopt : std::optional<SpellID>(selected.front());
}
