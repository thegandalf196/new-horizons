/*
 * NewHorizonsCreatureAbilityHelp.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCreatureAbilityHelp.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace newHorizonsCreatureAbilityHelp
{
std::vector<InnateAbilityHelp> translatedInnateAbilities(std::string_view creatureKey,
	const JsonNode * capturedRules, int32_t summonRounds)
{
	return innateAbilities(creatureKey, capturedRules,
		LIBRARY->generaltexth->translate("new-horizons.creature.freezingTouch.compact"),
		LIBRARY->generaltexth->translate("new-horizons.creature.freezingTouch.help"), summonRounds,
		LIBRARY->generaltexth->translate("new-horizons.creature.soulHarvest.compact"),
		LIBRARY->generaltexth->translate("new-horizons.creature.soulHarvest.help"));
}
}
