/*
 * NewHorizonsOverwhelmingFormula.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../networkPacks/PacksForClientBattle.h"

#include <optional>

class CBattleInfoCallback;
class CSpell;
class JsonNode;

namespace spells
{
/// Build, but do not apply, the side's first qualifying actual-damage claim.
/// Call before publishing injuries, so nested echoes observe the selected cast.
DLL_LINKAGE std::optional<SetOverwhelmingFormulaState> overwhelmingFormulaClaim(
	const CBattleInfoCallback & battle, const CSpell * spell,
	const JsonNode & captured, const std::vector<BattleStackAttacked> & hits);
}
