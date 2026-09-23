/*
 * PossibleSpellcast.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "PossibleSpellcast.h"
#include "../../lib/spells/CSpellHandler.h"

PossibleSpellcast::PossibleSpellcast()
	: spell(nullptr),
	dest(),
	value(0)
{
}

PossibleSpellcast::~PossibleSpellcast() = default;

std::string PossibleSpellcast::name() const
{
	if(command != HeroCommand::NONE)
		return heroCommands::key(command);
	if(spellSelectiveDispel)
		return spell->getNameTranslated() + " (Selective)";
	if(spellCureAffliction != SpellID::NONE)
	{
		const auto * affliction = spellCureAffliction.toSpell();
		return spell->getNameTranslated() + " (Cure: "
			+ (affliction ? affliction->getNameTranslated() : std::to_string(spellCureAffliction.getNum())) + ")";
	}
	if(spellMassSlow)
		return spell->getNameTranslated() + " (Mass)";
	if(spellOvercharge == 0)
		return spell->getNameTranslated();
	return spell->getNameTranslated() + " (Overcharge +" + std::to_string(spellOvercharge) + ")";
}
