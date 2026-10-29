/*
 * CreatureContinuationUI.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/constants/Enumerations.h"
#include <string>

class CBattleInfoCallback;
class CStack;
class ITranslator;

namespace creatureContinuationUI
{
enum class Kind { NONE, MASTER_GUNNER, DREAD_CAVALRY };

/// Read-only control affordances. Gameplay legality remains in the callback.
struct State
{
	Kind kind = Kind::NONE;
	bool attackAvailable = false;
	int damagePercent = 100;
	int remainingMovement = 0;

	bool pending() const { return kind != Kind::NONE; }
	bool allows(PossiblePlayerBattleAction::Actions action) const
	{
		if(!pending() || action == PossiblePlayerBattleAction::HERO_INFO
			|| action == PossiblePlayerBattleAction::CREATURE_INFO)
			return true;
		if(!attackAvailable)
			return false;
		return kind == Kind::MASTER_GUNNER ? action == PossiblePlayerBattleAction::SHOOT
			: action == PossiblePlayerBattleAction::ATTACK || action == PossiblePlayerBattleAction::WALK_AND_ATTACK;
	}

	bool normalizeCommand(EActionType & action) const
	{
		if(!pending())
			return true;
		if(action == EActionType::DEFEND)
			action = EActionType::NO_ACTION;
		if(action == EActionType::NO_ACTION || action == EActionType::HERO_SPELL
			|| action == EActionType::HERO_COMMAND || action == EActionType::RETREAT || action == EActionType::SURRENDER)
			return true;
		return attackAvailable && action == (kind == Kind::MASTER_GUNNER ? EActionType::SHOOT : EActionType::WALK_AND_ATTACK);
	}
};

State read(const CBattleInfoCallback & battle, const CStack * stack);
std::string closeTitle(const State & state, const ITranslator * translator);
std::string closeHelp(const State & state, const ITranslator * translator);
std::string attackPreview(const State & state, const ITranslator * translator);
}
