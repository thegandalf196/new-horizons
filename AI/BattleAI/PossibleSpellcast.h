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
#include "../../lib/battle/FocusFireState.h"

class CSpell;

class PossibleSpellcast
{
public:
	using ValueMap = std::map<uint32_t, int64_t>;

	HeroCommand command = HeroCommand::NONE;
	std::optional<FocusFireState> focusFire;
	/// Unit identities carried by a targeted Order.  Focus Fire keeps its
	/// validated value object above for compatibility with its existing
	/// hypothetical-battle path; the remaining Orders use this value-only
	/// payload so the AI can rank one-target and pair-target choices without
	/// mutating the authoritative battle.
	std::vector<uint32_t> commandTargets;
	/// Orders whose effects are contextual (for example Protect and Second
	/// Wind) are scored by a deterministic read-only heuristic.  A non-zero
	/// value marks that the generic spell projection must not overwrite it.
	float commandHeuristicValue = 0.0f;
	std::string name() const;
	const CSpell * spell;
	int32_t spellOvercharge = 0;
	bool spellSelectiveDispel = false;
	/// Requests the once-per-combat Sorcery Temporal Field variant of Slow.
	bool spellMassSlow = false;
	spells::Target dest;
	float value;

	PossibleSpellcast();
	virtual ~PossibleSpellcast();
};
