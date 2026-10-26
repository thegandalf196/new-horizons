/*
 * HeroOrderLog.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroOrderLog.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/MetaString.h"

const char * heroOrderDisplayName(HeroCommand command)
{
	switch(command)
	{
		case HeroCommand::CHARGE: return "Charge";
		case HeroCommand::HOLD_THE_LINE: return "Hold the Line";
		case HeroCommand::FOCUS_FIRE: return "Focus Fire";
		case HeroCommand::RIPOSTE: return "Riposte";
		case HeroCommand::BRACE: return "Brace";
		case HeroCommand::PROTECT: return "Protect";
		case HeroCommand::FLANK: return "Flank";
		case HeroCommand::SECOND_WIND: return "Second Wind";
		case HeroCommand::FEINT: return "Feint";
		case HeroCommand::VANGUARD: return "Vanguard";
		default: return "Order";
	}
}

namespace
{
static void appendHeroOrderTarget(MetaString & line, const CBattleInfoCallback & battle, uint32_t unitId)
{
	const auto * target = battle.battleGetUnitByID(unitId);
	if(!target)
		return;
	line.appendRawString(" %s");
	target->addNameReplacement(line);
}

void appendVanguardNumbers(MetaString & line, int statBonus, int initiativePercent)
{
	line.appendRawString("+");
	line.appendNumber(statBonus);
	line.appendRawString(" Creature Attack, Defense and Morale, and +");
	line.appendNumber(initiativePercent);
	line.appendRawString("% Initiative");
}

void appendRevisedOrderSnapshot(MetaString & line, const HeroOrderState & state)
{
	if(!state.revision || !state.revision->potencyFinalized)
		return;
	const auto & snapshot = *state.revision;
	if(state.command == HeroCommand::FEINT)
	{
		line.appendRawString(" Until the end of this round, allied stacks take ");
		line.appendNumber(snapshot.feintReductionPercent);
		line.appendRawString("% less physical retaliation damage.");
		if(snapshot.crown)
		{
			line.appendRawString(" Crown and Altar recipients take ");
			line.appendNumber(snapshot.crown->feintReductionPercent);
			line.appendRawString("% less physical retaliation damage.");
		}
	}
	else if(state.command == HeroCommand::VANGUARD)
	{
		line.appendRawString(" Allied recipients gain ");
		appendVanguardNumbers(line, snapshot.vanguardStatBonus, snapshot.vanguardInitiativePercent);
		const int rounds = snapshot.expiresAfterRound - state.issuedRound + 1;
		line.appendRawString(" for ");
		line.appendNumber(rounds);
		line.appendRawString(rounds == 1 ? " round, including this round (through round " : " rounds, including this round (through round ");
		line.appendNumber(snapshot.expiresAfterRound);
		line.appendRawString(").");
		if(snapshot.crown)
		{
			line.appendRawString(" Crown and Altar recipients instead gain ");
			appendVanguardNumbers(line, snapshot.crown->vanguardStatBonus, snapshot.crown->vanguardInitiativePercent);
			line.appendRawString(".");
		}
	}
}

}

MetaString heroOrderLogLine(const CBattleInfoCallback & battle, BattleSide side,
	const HeroOrderState & state)
{
	const auto * hero = battle.battleGetFightingHero(side);
	MetaString line = hero
		? MetaString::createFromTextID(hero->getNameTextID())
		: MetaString::createFromRawString("Hero");
	line.appendRawString(": ");
	line.appendRawString(heroOrderDisplayName(state.command));
	line.appendRawString("!");
	if(state.warcastingBonusPercent > 0)
	{
		line.appendRawString(" Warcasting adds +");
		line.appendNumber(state.warcastingBonusPercent);
		line.appendRawString(" percentage points to attribute-derived efficiency.");
	}
	if(state.sacredCommandEfficiencyBonusPercent > 0)
	{
		line.appendRawString(" Sacred Command adds +");
		line.appendNumber(state.sacredCommandEfficiencyBonusPercent);
		line.appendRawString(" percentage points to attribute-derived efficiency.");
	}
	if(state.knightlySequenceEfficiencyBonusPercent > 0)
	{
		line.appendRawString(" Knightly Sequence adds +");
		line.appendNumber(state.knightlySequenceEfficiencyBonusPercent);
		line.appendRawString(" additional percentage points to attribute-derived efficiency.");
	}

	switch(state.command)
	{
		case HeroCommand::FEINT:
		case HeroCommand::VANGUARD:
			appendRevisedOrderSnapshot(line, state);
			break;
		case HeroCommand::CHARGE:
			line.appendRawString(" Each allied stack's first melee attack after moving at least 3 hexes gains +");
			line.appendNumber(hero ? heroCommands::coefficient(
				battle.getBattle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"],
				*hero, state.warcastingBonusPercent, state.divineMandateEfficiencyBonusPercent()) : 0);
			line.appendRawString("% damage, plus 2 percentage points per additional hex, this round.");
			break;
		case HeroCommand::HOLD_THE_LINE:
			line.appendRawString(" Allied stacks that hold position resist physical damage this round.");
			if(state.holdMagicalReductionBasisPoints > 0)
			{
				line.appendRawString(" Iron Discipline adds ");
				line.appendNumber(state.holdMagicalReductionBasisPoints / 100);
				const int remainder = state.holdMagicalReductionBasisPoints % 100;
				if(remainder != 0)
				{
					line.appendRawString(".");
					if(remainder < 10)
						line.appendRawString("0");
					line.appendNumber(remainder % 10 == 0 ? remainder / 10 : remainder);
				}
				line.appendRawString("% magical damage reduction while they hold position.");
			}
			break;
		case HeroCommand::FOCUS_FIRE:
			line.appendRawString(" Target:");
			appendHeroOrderTarget(line, battle, state.primaryTargetUnitId);
			if(heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
				&& heroCommands::hasCombinedArms(hero))
				line.appendRawString(". Allied shooters concentrate fire; melee stacks included when issuing gain half this damage bonus against this target this round.");
			else
				line.appendRawString(". Allied shooters concentrate fire this round.");
			break;
		case HeroCommand::RIPOSTE:
			line.appendRawString(" Allied stacks take less melee damage and retaliate more fiercely this round.");
			if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE))
				line.appendRawString(" Vengeance grants each affected stack one additional retaliation this round.");
			break;
		case HeroCommand::BRACE:
			line.appendRawString(" Allied stacks strike first when an enemy moves at least 3 hexes before a melee attack this round.");
			break;
		case HeroCommand::PROTECT:
			line.appendRawString(" Protector:");
			appendHeroOrderTarget(line, battle, state.primaryTargetUnitId);
			line.appendRawString(". Ward:");
			appendHeroOrderTarget(line, battle, state.secondaryTargetUnitId);
			line.appendRawString(battle.battleHeroOrderProtectInterceptionLimit(side) > 1
				? ". The first two qualifying melee attacks are intercepted this round."
				: ". The first qualifying melee attack is intercepted this round.");
			break;
		case HeroCommand::FLANK:
			line.appendRawString(" Target:");
			appendHeroOrderTarget(line, battle, state.primaryTargetUnitId);
			if(heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
				&& heroCommands::hasCombinedArms(hero))
				line.appendRawString(". Allied melee attackers exploit new sides; ranged attacks gain half the Attack-derived damage bonus without adding sides this round.");
			else
				line.appendRawString(". Allied melee attackers exploit new sides this round.");
			break;
		case HeroCommand::SECOND_WIND:
			line.appendRawString(" Target:");
			appendHeroOrderTarget(line, battle, state.primaryTargetUnitId);
			line.appendRawString(". One reduced-strength activation is granted this round.");
			break;
		default:
			break;
	}
	return line;
}
