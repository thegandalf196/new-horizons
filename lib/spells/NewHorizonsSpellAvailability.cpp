/*
 * NewHorizonsSpellAvailability.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsSpellAvailability.h"
#include "../constants/StringConstants.h"
#include "../GameLibrary.h"
#include "../callback/IGameInfoCallback.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "CSpell.h"
#include "CSpellHandler.h"

namespace newHorizonsMagic
{
bool spellBelongsToRules(const JsonNode & rules, const std::string & scopedIdentity, bool commonHeroSpell)
{
	if(!commonHeroSpell)
		return true; // Existing creature/special-ability rules remain responsible.
	const auto separator = scopedIdentity.find(':');
	if(separator == std::string::npos || separator == 0 || separator + 1 == scopedIdentity.size())
		throw std::runtime_error("Spell availability requires a scoped identity");
	if(rules.isNull() || (rules.isStruct() && rules.Struct().empty()))
		return !scopedIdentity.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':');
	if(!rules.isStruct() || !rules["spells"].isStruct())
		throw std::runtime_error("Spell availability requires a saved spell roster");
	return rules["spells"].Struct().contains(scopedIdentity);
}

bool spellAllowedBySavedRoster(const JsonNode & rules, SpellID spell)
{
	const auto id = spell.getNum();
	if(id < 0 || !LIBRARY || !LIBRARY->spellh || static_cast<size_t>(id) >= LIBRARY->spellh->objects.size())
		return false;
	const auto & definition = LIBRARY->spellh->objects.at(id);
	return definition && spellBelongsToRules(rules, definition->getJsonKey(), definition->isCommonHeroSpell());
}

bool spellAllowedByWorldRoster(const IGameInfoCallback & world, SpellID spell)
{
	return spellAllowedBySavedRoster(world.getMagicRules(), spell);
}

bool spellAllowedByBattleRoster(const CBattleInfoCallback & battle, SpellID spell)
{
	const auto * state = battle.getBattle();
	return state && spellAllowedBySavedRoster(state->getMagicRules(), spell);
}
}
