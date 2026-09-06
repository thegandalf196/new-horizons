/*
 * PossibleSpellcast.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <vcmi/spells/Magic.h>

#include "../../lib/battle/Destination.h"
#include "../../lib/battle/HeroCommand.h"

class CSpell;

class PossibleSpellcast
{
public:
	using ValueMap = std::map<uint32_t, int64_t>;

	HeroCommand command = HeroCommand::NONE;
	std::string name() const;
	const CSpell * spell;
	spells::Target dest;
	float value;

	PossibleSpellcast();
	virtual ~PossibleSpellcast();
};
