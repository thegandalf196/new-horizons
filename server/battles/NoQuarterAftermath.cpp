/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NoQuarterAftermath.h"
#include "DefiantDenial.h"
#include "../CGameHandler.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <set>
namespace noQuarterAftermath
{
void apply(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const BattleAttackInfo & noQuarterAttack, const std::vector<BattleStackAttacked> & hits)
{
	const auto * attacker = noQuarterAttack.attacker;
	if(battle.battleCanTriggerNoQuarter(noQuarterAttack))
	{
		std::set<uint32_t> checkedTargets;
		for(const auto & hit : hits)
		{
			if(!checkedTargets.insert(hit.stackAttacked).second)
				continue;
			const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
			if(!target || !target->alive() || target->isTimeStopped()
				|| !battle.battleMatchOwner(attacker, target)
				|| !newHorizonsOffense::belowNoQuarterThreshold(
					target->getAvailableHealth(), battle::getMaximumHealth(*target)))
				continue;

			// Ignore this complete application, not an earlier package or an unrelated denial.
			auto defiantApplication = noQuarterAttack;
			defiantApplication.defender = target;
			if(battleDefiant::consume(gameHandler, battle, defiantApplication,
				newHorizonsArmorer::DefiantDenialCause::NO_QUARTER))
				continue;

			SetStackEffect effects;
			effects.battleID = battle.getBattle()->getBattleID();
			const auto existing = target->getAllBonuses(CSelector([](const Bonus * bonus)
			{
				return newHorizonsOffense::isNoQuarterBonus(bonus);
			}));
			if(existing && !existing->empty())
			{
				std::vector<Bonus> toReplace;
				toReplace.reserve(existing->size());
				for(const auto & bonus : *existing)
					toReplace.push_back(*bonus);
				effects.toRemove.emplace_back(target->unitId(), std::move(toReplace));
			}
			effects.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{
				newHorizonsOffense::noQuarterRetaliationBonus(),
				newHorizonsOffense::noQuarterMoralePenalty(true)});
			gameHandler.sendAndApply(effects);

			auto state = target->acquireState();
			state->noQuarterMoraleActivationsRemaining = battle.getBattle()->getActiveStackID()
				== static_cast<int32_t>(target->unitId()) ? 2 : 1;
			BattleUnitsChanged stateChange;
			stateChange.battleID = battle.getBattle()->getBattleID();
			UnitChanges update(target->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			stateChange.changedStacks.push_back(std::move(update));
			gameHandler.sendAndApply(stateChange);

			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("No Quarter affects %s: all remaining retaliations are lost this round, and Morale is reduced by 2 until the end of the stack's next activation.");
			target->addNameReplacement(line, target->getCount());
			message.lines.push_back(std::move(line));
			gameHandler.sendAndApply(message);
		}
	}
}
}
