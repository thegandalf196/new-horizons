/*
 * CreatureContinuationUI.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CreatureContinuationUI.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/texts/MetaString.h"
#include "../../lib/texts/ITranslator.h"

namespace creatureContinuationUI
{
State read(const CBattleInfoCallback & battle, const CStack * stack)
{
	const auto state = stack ? stack->acquireState() : nullptr;
	if(!state)
		return {};
	if(state->rangedFollowUpDamagePercent > 0)
		return {Kind::MASTER_GUNNER, battle.battleHasPendingRangedFollowUp(stack), 100, 0};
	if(state->dreadCavalryState.pending)
		return {Kind::DREAD_CAVALRY, battle.battleHasPendingDreadCavalry(stack),
			battle.battleGetDreadCavalryDamagePercent(stack), battle.battleGetDreadCavalryMovementRemaining(stack)};
	return {};
}

std::string closeTitle(const State & state, const ITranslator * translator)
{
	return state.kind == Kind::DREAD_CAVALRY
		? translator->translate("new-horizons.combat.dreadCavalry.skip") : "End activation";
}

std::string closeHelp(const State & state, const ITranslator * translator)
{
	return state.kind == Kind::DREAD_CAVALRY
		? translator->translate("new-horizons.combat.dreadCavalry.skip.help")
		: "Decline the pending Master Gunner second shot and end this activation.";
}

std::string attackPreview(const State & state, const ITranslator * translator)
{
	if(state.kind != Kind::DREAD_CAVALRY || !state.attackAvailable)
		return {};
	auto text = MetaString::createFromTextID("new-horizons.combat.dreadCavalry.preview");
	text.replaceNumber(state.damagePercent);
	text.replaceNumber(state.remainingMovement);
	return "\n" + text.toString(translator);
}
}
