/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroOrderRecipients.h"
#include "CBattleInfoCallback.h"
#include "HeroCommand.h"
#include "IBattleState.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include <algorithm>
namespace heroCommands
{
std::vector<uint32_t> recipientIds(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	std::vector<uint32_t> result;
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !battle.getBattle()
		|| !heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		|| !heroCommands::isActive(order.command)
		|| order.issuedRound != battle.battleGetRound())
		return result;
	const auto owner = battle.sideToPlayer(side);
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost()
			|| battle.battleGetOwner(unit) != owner
			|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit))
			continue;
		bool covered = false;
		switch(order.command)
		{
			case HeroCommand::PROTECT:
				covered = unit->unitId() == order.primaryTargetUnitId
					|| unit->unitId() == order.secondaryTargetUnitId;
				break;
			case HeroCommand::SECOND_WIND:
				covered = unit->unitId() == order.primaryTargetUnitId;
				break;
			case HeroCommand::FOCUS_FIRE:
				covered = unit->isShooter() || (unit->isMeleeAttacker()
					&& heroCommands::hasCombinedArms(battle.battleGetFightingHero(side)));
				break;
			case HeroCommand::HOLD_THE_LINE:
				covered = std::any_of(order.anchors.begin(), order.anchors.end(),
					[unit](const HeroOrderAnchor & anchor) { return anchor.unitId == unit->unitId(); });
				break;
			case HeroCommand::VANGUARD:
				covered = order.revision && order.revision->contains(unit->unitId());
				break;
			case HeroCommand::FEINT:
			case HeroCommand::CHARGE:
			case HeroCommand::FLANK:
			case HeroCommand::RIPOSTE:
			case HeroCommand::BRACE:
				// Shooters can make ordinary melee attacks too. Do not restrict
				// coverage by future charge travel or an attack specialization.
				covered = true;
				break;
			default:
				break;
		}
		if(covered)
			result.push_back(unit->unitId());
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

}
