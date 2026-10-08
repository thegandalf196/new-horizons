/*
 * NewHorizonsOverwhelmingFormula.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsOverwhelmingFormula.h"
#include "MagicalDamageReduction.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"

namespace spells
{
std::optional<SetOverwhelmingFormulaState> overwhelmingFormulaClaim(
	const CBattleInfoCallback & battle, const CSpell * spell,
	const JsonNode & captured, const std::vector<BattleStackAttacked> & hits)
{
	const auto identity = capturedOverwhelmingFormula(captured);
	const auto * state = battle.getBattle();
	if(!identity || !state || !spell || !spell->isMagical())
		return std::nullopt;
	const auto & previous = state->getOverwhelmingFormulaState(identity->side);
	if(previous.winningCastToken != 0 || !previous.canPenetrate(identity->token))
		return std::nullopt;
	auto claimed = previous;
	for(const auto & hit : hits)
	{
		const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
		// The accepted magical cast/marker supplies provenance. Lua damageUnit
		// injuries do not carry SPELL_EFFECT even though their health state records
		// magical casualties, so packet flags alone cannot classify this injury.
		if(!target || hit.damageAmount <= 0
			|| battle.battleGetOwner(target) == battle.sideToPlayer(identity->side))
			continue;
		const bool protectedTarget = spell->hasApplicableMagicalDamageReduction(target,
			battle.battleGetHoldTheLineMagicalReductionBasisPoints(target),
			battle.battleGetPerkMagicalReductionBasisPoints(target), true);
		if(!claimed.claimActualDamage(identity->token, true, hit.damageAmount, protectedTarget))
			continue;
		SetOverwhelmingFormulaState result;
		result.battleID = state->getBattleID();
		result.side = identity->side;
		result.state = claimed;
		return result;
	}
	return std::nullopt;
}
}
