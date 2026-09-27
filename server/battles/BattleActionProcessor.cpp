/*
 * BattleActionProcessor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsShroud.h"

#include "BattleProcessor.h"

#include "../CGameHandler.h"

#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/AbilityCaster.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"

#include <vstd/RNG.h>

/// What a script reacting before an attack learns about a unit that is about to be hit. The damage
/// is not rolled yet, so only the identity of the unit and the health it has left are known.
static AttackedTarget unitAboutToBeAttacked(const battle::Unit * unit)
{
	AttackedTarget target;
	target.unit = unit;
	target.healthBeforeAttack = unit->getAvailableHealth();
	return target;
}

static bool canonicalLandMineHexIsEmpty(const CBattleInfoCallback & battle,
	const AccessibilityInfo & accessibility, const BattleHex & hex)
{
	if(!hex.isAvailable()
		|| accessibility[hex.toInt()] != EAccessibility::ACCESSIBLE
		|| battle.battleGetUnitByPos(hex, true)
		|| !battle.battleGetAllObstaclesOnPos(hex, false).empty())
		return false;

	if(!battle.hasFortifications())
		return true;

	const auto wallPart = battle.battleHexToWallPart(hex);
	if(wallPart == EWallPart::INVALID)
		return true;
	if(wallPart == EWallPart::INDESTRUCTIBLE_PART
		|| wallPart == EWallPart::INDESTRUCTIBLE_PART_OF_GATE
		|| wallPart == EWallPart::BOTTOM_TOWER
		|| wallPart == EWallPart::UPPER_TOWER)
		return false;

	const auto wallState = battle.battleGetWallState(wallPart);
	return wallState == EWallState::NONE || wallState == EWallState::DESTROYED;
}

static bool isTimeStopHeroAction(const BattleAction & action)
{
	if(action.actionType != EActionType::HERO_SPELL || !action.spell.hasValue())
		return false;
	const auto * spell = action.spell.toSpell();
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

static bool shouldActivateGrandMetamagic(const CBattleInfoCallback & battle, BattleSide side,
	const CGHeroInstance * hero, bool metamagicFollowup)
{
	return HeroSpellAllowanceTransition::activatesGrand(
		metamagicFollowup,
		static_cast<uint8_t>(battle.battleMetamagicPendingCount(side)),
		battle.battleMetamagicSequenceSpells(side).size(),
		static_cast<uint8_t>(battle.battleMetamagicUsesConsumed(side)),
		static_cast<uint8_t>(hero ? newHorizonsMagic::metamagicRank(hero) : 0),
		hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
		battle.battleMetamagicGrandUsed(side));
}

static const char * heroOrderDisplayName(HeroCommand command)
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
		default: return "Order";
	}
}

static void appendHeroOrderCauseName(MetaString & line, const CBattleInfoCallback & battle,
	const battle::Unit * unit, HeroCommand command)
{
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(const auto * hero = battle.battleGetFightingHero(side))
	{
		line.appendTextID(hero->getNameTextID());
		line.appendRawString("'s ");
	}
	else
		line.appendRawString("A hero's ");

	line.appendRawString(heroOrderDisplayName(command));
}

static MetaString orderDamageLogLine(const CBattleInfoCallback & battle, const CStack * attacker,
	const battle::Unit * target, const BattleStackAttacked & hit,
	HeroCommand attackerOrderCause, HeroCommand defenderOrderCause)
{
	MetaString line;
	if(attackerOrderCause != HeroCommand::NONE)
	{
		appendHeroOrderCauseName(line, battle, attacker, attackerOrderCause);
		if(attackerOrderCause == HeroCommand::SECOND_WIND)
		{
			line.appendRawString(" gave %s a reduced-strength follow-up against %s: ");
			attacker->addNameReplacement(line, attacker->getCount());
			target->addNameReplacement(line, target->getCount());
		}
		else
		{
			line.appendRawString(": %s struck %s for ");
			attacker->addNameReplacement(line, attacker->getCount());
			target->addNameReplacement(line, target->getCount());
		}
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" damage (");
		line.appendNumber(hit.killedAmount);
		line.appendRawString(" killed)");

		if(defenderOrderCause != HeroCommand::NONE)
		{
			line.appendRawString("; ");
			appendHeroOrderCauseName(line, battle, target, defenderOrderCause);
			line.appendRawString(" reduced the damage to %s");
			target->addNameReplacement(line, target->getCount());
		}
		line.appendRawString(".");
	}
	else if(defenderOrderCause != HeroCommand::NONE)
	{
		appendHeroOrderCauseName(line, battle, target, defenderOrderCause);
		line.appendRawString(" reduced the damage to %s: ");
		target->addNameReplacement(line, target->getCount());
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" damage (");
		line.appendNumber(hit.killedAmount);
		line.appendRawString(" killed) from %s.");
		attacker->addNameReplacement(line, attacker->getCount());
	}
	return line;
}

static void appendHeroOrderTarget(MetaString & line, const CBattleInfoCallback & battle, uint32_t unitId)
{
	const auto * target = battle.battleGetUnitByID(unitId);
	if(!target)
		return;
	line.appendRawString(" %s");
	target->addNameReplacement(line);
}

static MetaString heroOrderLogLine(const CBattleInfoCallback & battle, BattleSide side,
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

	switch(state.command)
	{
		case HeroCommand::CHARGE:
			line.appendRawString(" Each allied stack's first melee attack after moving at least 3 hexes gains +");
			line.appendNumber(hero ? heroCommands::coefficient(
				battle.getBattle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"],
				*hero, state.warcastingBonusPercent) : 0);
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

static bool validateCanonicalLandMineTargets(const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, const battle::Target & target)
{
	const int required = newHorizonsMagic::landMineHexCount(mechanics.getEffectPower());
	if(static_cast<int>(target.size()) != required)
		return false;

	const auto accessibility = battle.getAccessibility();
	std::set<int> selected;
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr || !destination.hexValue.isValid()
			|| !selected.insert(destination.hexValue.toInt()).second
			|| !canonicalLandMineHexIsEmpty(battle, accessibility, destination.hexValue))
			return false;
	}
	return true;
}

static bool canonicalFireWallDirection(BattleHex::EDir direction)
{
	return direction >= BattleHex::TOP_LEFT && direction <= BattleHex::LEFT;
}

static bool chainGateKillQualifies(const CBattleInfoCallback & battle,
	const CStack * attacker, const std::vector<BattleStackAttacked> & hits)
{
	if(!attacker)
		return false;
	const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
	if(!concrete)
		return false;
	const auto rewardSide = concrete->gatedDemonicStackSide(attacker->unitId());
	if((rewardSide != BattleSide::ATTACKER && rewardSide != BattleSide::DEFENDER)
		|| attacker->unitId() == std::numeric_limits<uint32_t>::max())
		return false;
	const auto * hero = battle.battleGetFightingHero(rewardSide);
	if(!hero || !hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.chainGate"))
		return false;

	return std::ranges::any_of(hits, [&battle, attacker, rewardSide](const auto & hit)
	{
		if(hit.attackerID != attacker->unitId() || !hit.killed() || hit.cloneKilled() || hit.willRebirth())
			return false;
		const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
		return victim && victim != attacker && victim->unitSide() != rewardSide;
	});
}

static bool demonicGateFootprintHasObstacle(const CBattleInfoCallback & battle,
	const BattleHex & position, bool doubleWide, BattleSide side)
{
	for(const auto & hex : battle::Unit::getHexes(position, doubleWide, side))
	{
		if(hex.isAvailable() && !battle.battleGetAllObstaclesOnPos(hex, false).empty())
			return true;
	}
	return false;
}

static bool validateDemonicGatingAction(const CBattleInfoCallback & battle, const BattleAction & action)
{
	if(action.actionType != EActionType::DEMONIC_GATING || !action.gatingCreature.hasValue()
		|| (action.target.size() != 1 && action.target.size() != 2)
		|| std::ranges::any_of(action.target, [](const auto & destination)
		{
			return destination.unitValue != -1000 || !destination.hexValue.isValid();
		}))
		return false;
	const auto * source = battle.battleGetStackByID(action.stackNumber, false);
	const auto * hero = battle.battleGetFightingHero(action.side);
	const auto * creature = action.gatingCreature.toCreature();
	if(!source || !source->alive() || !hero || !creature
		|| source->unitSide() != action.side || source->creatureId().toCreature()->getFactionID() != FactionID::INFERNO
		|| creature->getFactionID() != FactionID::INFERNO)
		return false;
	const int rank = hero->getPerkSkillRank("new-horizons:demonicGating");
	const auto category = battle.battleGetCreatureCategory(action.gatingCreature);
	if(rank <= 0 || !category || static_cast<int>(category->category) >= rank)
		return false;
	const auto & demonicReserve = battle.getBattle()->getDemonicReserve(action.side);
	const auto reserve = demonicReserve.find(action.gatingCreature);
	if(reserve == demonicReserve.end() || reserve->second <= 0)
		return false;
	BattleHex sourcePosition = source->getPosition();
	if(action.target.size() == 2)
	{
		if(!hero->hasActivePerk("new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate")
			|| source != battle.battleActiveUnit())
			return false;
		const BattleHex movementDestination = action.target.front().hexValue;
		const auto [path, distance] = battle.getPath(sourcePosition, movementDestination, source);
		const int movementLimit = static_cast<int>(source->getMovementRange(0) / 2);
		if(path.empty() || distance < 0 || distance > movementLimit)
			return false;
		sourcePosition = movementDestination;
	}
	const int placementRange = hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
	const BattleHex target = action.target.back().hexValue;
	const BattleHex occupiedTail = source->doubleWide()
		? source->occupiedHex(sourcePosition) : BattleHex::INVALID;
	const BattleHex gatedTail = battle::Unit::occupiedHex(target, creature->isDoubleWide(), action.side);
	if(!target.isAvailable() || target == sourcePosition || target == occupiedTail
		|| (gatedTail.isValid() && (gatedTail == sourcePosition || gatedTail == occupiedTail))
		|| BattleHex::getDistance(sourcePosition, target) > placementRange
		|| battle.battleGetUnitByPos(target, true)
		|| demonicGateFootprintHasObstacle(battle, target, creature->isDoubleWide(), action.side))
		return false;
	const auto accessibility = battle.getAccessibility();
	return accessibility.accessible(target, creature->isDoubleWide(), action.side);
}

static bool validateCanonicalFireWallAction(const CBattleInfoCallback & battle,
	const BattleAction & action, battle::Target & expandedTarget)
{
	// The wire contract is deliberately compact: one empty start hex plus a
	// direction. The server, rather than the client, derives the footprint.
	constexpr int32_t INVALID_UNIT_ID = -1000;
	if(action.target.size() != 1
		|| action.target.front().unitValue != INVALID_UNIT_ID
		|| !canonicalFireWallDirection(action.spellFireWallDirection))
		return false;

	const auto start = action.target.front().hexValue;
	const auto accessibility = battle.getAccessibility();
	BattleHex current = start;
	for(int index = 0; index < 3; ++index)
	{
		if(!canonicalLandMineHexIsEmpty(battle, accessibility, current))
			return false;
		expandedTarget.emplace_back(current);
		if(index != 2)
			current = current.cloneInDirection(action.spellFireWallDirection, false);
	}
	return true;
}

BattleActionProcessor::BattleActionProcessor(BattleProcessor * owner, CGameHandler * newGameHandler)
	: owner(owner)
	, gameHandler(newGameHandler)
{
}

void BattleActionProcessor::publishHeroOrderState(const CBattleInfoCallback & battle, BattleSide side) const
{
	if(!heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	BattleHeroOrderStateChanged update;
	update.battleID = battle.getBattle()->getBattleID();
	update.side = side;
	update.state = battle.battleGetHeroOrderState(side);
	gameHandler->sendAndApply(update);
}

bool BattleActionProcessor::doEmptyAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	if(const auto * stack = battle.battleGetStackByID(ba.stackNumber, false);
		stack && stack->pursuitMovementRemaining > 0)
	{
		setPursuitMovementRemaining(battle, stack, 0);
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString("%s forgo Pursuit movement.");
		stack->addNameReplacement(line, stack->getCount());
		message.lines.push_back(std::move(line));
		gameHandler->sendAndApply(message);
	}
	return true;
}

bool BattleActionProcessor::doEndTacticsAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	return true;
}

bool BattleActionProcessor::doWaitAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);

	if (!canStackAct(battle, stack))
		return false;

	processBattleEventTriggers(battle, CombatEventType::WAIT, stack, nullptr);
	return true;
}

bool BattleActionProcessor::doRetreatAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	if (!battle.battleCanFlee(battle.sideToPlayer(ba.side)))
	{
		gameHandler->complain("Cannot retreat!");
		return false;
	}

	owner->setBattleResult(battle, EBattleResult::ESCAPE, battle.otherSide(ba.side));
	return true;
}

bool BattleActionProcessor::doSurrenderAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	PlayerColor player = battle.sideToPlayer(ba.side);
	int cost = battle.battleGetSurrenderCost(player);
	if (cost < 0)
	{
		gameHandler->complain("Cannot surrender!");
		return false;
	}

	if (gameHandler->gameInfo().getResource(player, EGameResID::GOLD) < cost)
	{
		gameHandler->complain("Not enough gold to surrender!");
		return false;
	}

	gameHandler->giveResource(player, EGameResID::GOLD, -cost);
	owner->setBattleResult(battle, EBattleResult::SURRENDER, battle.otherSide(ba.side));
	return true;
}

bool BattleActionProcessor::validateHeroSpellAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	if(ba.metamagicGrand)
		return false;
	if(ba.metamagicFollowup != battle.battleCanUseMetamagicFollowup(ba.side))
		return false;

	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(!hero || !ba.spell.hasValue())
		return false;
	const auto * spell = ba.spell.toSpell();
	if(!spell)
		return false;

	spells::BattleCast parameters(&battle, hero, spells::Mode::HERO, spell);
	parameters.setOvercharge(ba.spellOvercharge);
	parameters.setSelectiveDispel(ba.spellSelectiveDispel);
	parameters.setCureAffliction(ba.spellCureAffliction);
	parameters.setMassSlow(ba.spellMassSlow);
	parameters.setMetamagicFollowup(ba.metamagicFollowup);
	parameters.setMetamagicGrand(shouldActivateGrandMetamagic(battle, ba.side, hero, ba.metamagicFollowup));
	if(ba.metamagicFollowup && !ba.target.empty() && ba.target.front().unitValue >= 0)
		parameters.setMetamagicTargetUnitId(static_cast<uint32_t>(ba.target.front().unitValue));

	spells::detail::ProblemImpl problem;
	auto mechanics = spell->battleMechanics(&parameters);
	auto target = ba.getTarget(&battle);
	if(newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(spell->getId()))
	{
		battle::Target expandedTarget;
		if(!validateCanonicalFireWallAction(battle, ba, expandedTarget))
			return false;
		target = std::move(expandedTarget);
	}
	if(newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(spell->getId())
		&& !validateCanonicalLandMineTargets(battle, *mechanics, target))
		return false;

	return mechanics->canBeCast(problem) && !target.empty() && mechanics->canBeCastAt(target, problem);
}

bool BattleActionProcessor::doHeroSpellAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	if(ba.metamagicFollowup != battle.battleCanUseMetamagicFollowup(ba.side))
	{
		gameHandler->complain("Metamagic follow-up is not available in the authoritative battle state");
		return false;
	}
	const CGHeroInstance *h = battle.battleGetFightingHero(ba.side);
	if (!h)
	{
		logGlobal->error("Wrong caster!");
		return false;
	}

	if (!ba.spell.hasValue())
	{
		logGlobal->error("Wrong spell id (%d)!", ba.spell.getNum());
		return false;
	}
	const CSpell * s = ba.spell.toSpell();
	spells::BattleCast parameters(&battle, h, spells::Mode::HERO, s);
	parameters.setOvercharge(ba.spellOvercharge);
	parameters.setSelectiveDispel(ba.spellSelectiveDispel);
	parameters.setCureAffliction(ba.spellCureAffliction);
	parameters.setMassSlow(ba.spellMassSlow);
	parameters.setMetamagicFollowup(ba.metamagicFollowup);
	const bool grandActivation = shouldActivateGrandMetamagic(battle, ba.side, h, ba.metamagicFollowup);
	parameters.setMetamagicGrand(grandActivation);
	// BaseMechanics snapshots the cast metadata in its constructor.  Seed the
	// first targeted unit before creating it so Split Focus and Focused Pairing
	// see the authoritative first target during effect evaluation.  The target
	// is validated and rebuilt below; this early value is only a read-only
	// modifier input and never replaces server target validation.
	if(ba.metamagicFollowup && !ba.target.empty() && ba.target.front().unitValue >= 0)
		parameters.setMetamagicTargetUnitId(static_cast<uint32_t>(ba.target.front().unitValue));

	spells::detail::ProblemImpl problem;

	auto m = s->battleMechanics(&parameters);
	auto target = ba.getTarget(&battle);
	if(newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(s->getId()))
	{
		battle::Target expandedTarget;
		if(!validateCanonicalFireWallAction(battle, ba, expandedTarget))
		{
			gameHandler->complain("New Horizons Fire Wall requires one empty start hex and a valid direction");
			return false;
		}
		target = std::move(expandedTarget);
	}
	if(newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(s->getId())
		&& !validateCanonicalLandMineTargets(battle, *m, target))
	{
		gameHandler->complain("New Horizons Land Mine requires the exact number of unique empty hexes");
		return false;
	}

	if(!m->canBeCast(problem))
	{
		logGlobal->warn("Spell cannot be cast!");
		std::vector<std::string> texts;
		problem.getAll(texts);
		for(const auto & text : texts)
			logGlobal->warn(text);
		return false;
	}

	if(target.empty() || !m->canBeCastAt(target, problem))
	{
		logGlobal->warn("Spell cannot be cast at the requested target!");
		std::vector<std::string> texts;
		problem.getAll(texts);
		for(const auto & text : texts)
			logGlobal->warn(text);
		return false;
	}
	if(ba.metamagicFollowup && !target.empty() && target.front().unitValue)
		parameters.setMetamagicTargetUnitId(target.front().unitValue->unitId());
	if(ba.metamagicFollowup && !grandActivation
		&& battle.battleMetamagicPendingCount(ba.side) == 1
		&& newHorizonsMagic::spellPointRulesActive(h->getMagicRules())
		&& newHorizonsMagic::hasMetamagicPerk(h, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE))
		parameters.setMetamagicManaRefund(newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);

	// Counterspell is resolved after the enemy hero's ordinary cast checks. A
	// valid hero spell therefore still consumes its action and listed mana even
	// when the ward suppresses every effect. A costly spell that the warding
	// hero cannot afford instead collapses the ward and is cast normally.
	const auto counteringSide = battle.otherSide(ba.side);
	const auto * counteringHero = battle.battleGetFightingHero(counteringSide);
	int counterspellCost = 0;
	bool counterspellNegated = false;
	if(counteringHero && battle.battleWasCounterspellArmed(counteringSide))
	{
		const int listedCost = h->getListedSpellCost(s);
		counterspellCost = newHorizonsMagic::counterspellCost(listedCost,
			counteringHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage"),
			battle.battleMetamagicCountersequenceArmed(counteringSide));
		counterspellNegated = counteringHero->getManaAvailable() >= counterspellCost;
		parameters.setCounterspell(counteringSide, counterspellNegated);
	}

	parameters.cast(gameHandler->spellcastEnvironment(), target);
	if(counterspellNegated)
		counteringHero->spendMana(gameHandler->spellcastEnvironment(), counterspellCost);
	gameHandler->useChargeBasedSpell(h->id, ba.spell);

	return true;
}

bool BattleActionProcessor::doWalkAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);
	const bool pursuitContinuation = stack && stack->pursuitMovementRemaining > 0;

	if (!canStackAct(battle, stack))
		return false;

	if(target.empty())
	{
		gameHandler->complain("Destination required for move action.");
		return false;
	}

	processBattleEventTriggers(battle, CombatEventType::BEFORE_MOVE, stack, nullptr);

	auto movementResult = moveStack(battle, ba.stackNumber, target.at(0).hexValue); //move
	if (movementResult.invalidRequest)
	{
		gameHandler->complain("Stack failed movement!");
		return false;
	}
	if(pursuitContinuation)
	{
		setPursuitMovementRemaining(battle, stack, 0);
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString("%s use Pursuit to move ");
		stack->addNameReplacement(line, stack->getCount());
		line.appendNumber(movementResult.distance);
		line.appendRawString(movementResult.distance == 1 ? " hex." : " hexes.");
		message.lines.push_back(std::move(line));
		gameHandler->sendAndApply(message);
	}
	processBattleEventTriggers(battle, CombatEventType::AFTER_MOVE, stack, nullptr);
	return true;
}

bool BattleActionProcessor::doDefendAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);

	if (!canStackAct(battle, stack))
		return false;

	//defensive stance, TODO: filter out spell boosts from bonus (stone skin etc.)
	SetStackEffect sse;
	sse.battleID = battle.getBattle()->getBattleID();

	Bonus defenseBonusToAdd(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::PERCENT_TO_ALL);
	Bonus bonus2(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL, BonusSource::OTHER, stack->valOfBonuses(BonusType::DEFENSIVE_STANCE), BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE);
	Bonus alternativeWeakCreatureBonus(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE);
	Bonus tagBonus(BonusDuration::STACK_GETS_TURN, BonusType::UNIT_DEFENDING, BonusSource::OTHER, 0, BonusSourceID());

	BonusList defence = *stack->getBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)));
	int oldDefenceValue = defence.totalValue();

	defence.push_back(std::make_shared<Bonus>(defenseBonusToAdd));
	defence.push_back(std::make_shared<Bonus>(bonus2));

	int difference = defence.totalValue() - oldDefenceValue;
	const bool weakCreatureFallback = difference == 0;
	std::vector<Bonus> buffer;
	if(weakCreatureFallback) //give replacement bonus for creatures not reaching 5 defense points (20% of def becomes 0)
	{
		difference = 1;
		buffer.push_back(alternativeWeakCreatureBonus);
	}
	else
	{
		buffer.push_back(defenseBonusToAdd);
	}

	// Keep the provenance of this exact Defend contribution in battle state.
	// STACK_GETS_TURN is a lifetime, not an identity: treating every bonus with
	// that duration as Defend would let Breakthrough pierce unrelated temporary
	// effects.  Calculate both combat ranges before publishing the effect so the
	// damage callback can use the authoritative state instead.
	const auto stanceBonus = [&](bool ranged)
	{
		const auto range = ranged
			? Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
				.Or(Selector::effectRange()(BonusLimitEffect::ONLY_DISTANCE_FIGHT))
			: Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
				.Or(Selector::effectRange()(BonusLimitEffect::ONLY_MELEE_FIGHT));
		BonusList projected = *stack->getBonuses(
			Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)).And(range));
		const int oldValue = projected.totalValue();
		projected.push_back(std::make_shared<Bonus>(defenseBonusToAdd));
		projected.push_back(std::make_shared<Bonus>(bonus2));
		if(weakCreatureFallback)
			projected.push_back(std::make_shared<Bonus>(alternativeWeakCreatureBonus));
		return std::max(0, projected.totalValue() - oldValue);
	};
	const int stanceMeleeBonus = stanceBonus(false);
	const int stanceRangedBonus = stanceBonus(true);

	buffer.push_back(bonus2);
	buffer.push_back(tagBonus);

	sse.toUpdate.emplace_back(ba.stackNumber, buffer);
	gameHandler->sendAndApply(sse);

	// Publish the explicit provenance alongside the bonuses.  This is a state
	// update, not a client-side mutation, and therefore survives save/load and
	// hypothetical battle copies just like the rest of CUnitState.
	auto state = stack->acquireState();
	state->defensiveStanceMeleeBonus = stanceMeleeBonus;
	state->defensiveStanceRangedBonus = stanceRangedBonus;
	BattleUnitsChanged stateChanged;
	stateChanged.battleID = battle.getBattle()->getBattleID();
	UnitChanges stateUpdate(stack->unitId(), UnitChanges::EOperation::UPDATE);
	stateUpdate.data = state->save();
	stateChanged.changedStacks.push_back(std::move(stateUpdate));
	gameHandler->sendAndApply(stateChanged);

	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();

	MetaString text;
	stack->addText(text, EMetaText::GENERAL_TXT, 120);
	stack->addNameReplacement(text);
	text.replaceNumber(difference);

	message.lines.push_back(text);
	if(newHorizonsCombatSkills::paviseReductionPercent(battle.battleGetOwnerHero(stack)) > 0)
	{
		MetaString paviseText;
		paviseText.appendRawString("%s braces behind a Pavise, reducing ranged physical creature damage by 25% while Defending.");
		stack->addNameReplacement(paviseText);
		message.lines.push_back(std::move(paviseText));
	}

	gameHandler->sendAndApply(message);

	processBattleEventTriggers(battle, CombatEventType::DEFEND, stack, nullptr);
	return true;
}

bool BattleActionProcessor::doAttackAction(const CBattleInfoCallback & battle, const BattleAction & ba,
	bool allowPursuitContinuation)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	const auto perfectMomentSide = ba.perfectMoment ? battle.playerToSide(battle.battleGetOwner(stack)) : BattleSide::NONE;
	battle::Target target = ba.getTarget(&battle);

	if (!canStackAct(battle, stack))
		return false;
	if(stack->pursuitMovementRemaining > 0)
	{
		gameHandler->complain("Pursuit allows movement only; it does not grant another attack");
		return false;
	}

	if(target.size() < 2)
	{
		gameHandler->complain("Two destinations required for attack action.");
		return false;
	}

	BattleHex attackPos = target.at(0).hexValue;
	BattleHex destinationTile = target.at(1).hexValue;
	const CStack * destinationStack = battle.battleGetStackByPos(destinationTile, true);
	if(ba.archerySkirmisherAttack && target.size() != 2)
	{
		gameHandler->complain("Skirmisher move-and-shoot requires exactly one movement and one target hex.");
		return false;
	}

	if(!destinationStack)
	{
		gameHandler->complain("Invalid target to attack");
		return false;
	}

	BattleHex startingPos = stack->getPosition();
	int beforeAttackSpeed = stack->getMovementRange(0);
	const auto * skirmisherHero = battle.battleGetFightingHero(stack->unitSide());
	const bool skirmisherAvailable = newHorizonsArchery::canUseSkirmisher(skirmisherHero, stack);
	const bool requestedSkirmisherPosition = ba.archerySkirmisherAttack
		&& battle.battleCanSkirmisherAttackFromHex(stack, destinationTile, attackPos);
	if(ba.archerySkirmisherAttack && (!skirmisherAvailable || !requestedSkirmisherPosition))
	{
		gameHandler->complain("Invalid Skirmisher firing destination.");
		return false;
	}
	if(skirmisherAvailable && !ba.archerySkirmisherAttack)
	{
		auto projectedAttacker = stack->acquireState();
		projectedAttacker->setPosition(attackPos);
		const bool ordinaryAttackFromPosition = battle.isMeleeAttackPossible(projectedAttacker.get(), destinationStack)
			|| battle.isLongWeaponAttack(projectedAttacker.get(), destinationStack);
		if(!ordinaryAttackFromPosition)
		{
			gameHandler->complain("Attack position is not a legal melee hex. Use the explicit Skirmisher action to move and fire.");
			return false;
		}
	}
	const auto movementResult = moveStack(battle, ba.stackNumber, attackPos);
	int movementSpent = movementResult.distance;

	logGlobal->trace("%s will attack %s", stack->nodeName(), destinationStack->nodeName());

	if (movementResult.invalidRequest)
	{
		gameHandler->complain("Stack failed attack - unable to reach target!");
		return false;
	}

	if(movementResult.obstacleHit)
	{
		// we were not able to reach destination tile, nor occupy specified hex
		// abort attack attempt, but treat this case as legal - we have stepped onto a quicksands/mine
		return true;
	}

	if(destinationStack && stack->unitId() == destinationStack->unitId()) //we should just move, it will be handled by following check
	{
		destinationStack = nullptr;
	}

	if(!destinationStack)
	{
		gameHandler->complain("Unit can not attack itself");
		return false;
	}

	const bool regularMeleeAttack = battle.isMeleeAttackPossible(stack, destinationStack);
	const bool longWeaponAttack = battle.isLongWeaponAttack(stack, destinationStack);
	const bool skirmisherShot = requestedSkirmisherPosition
		&& newHorizonsArchery::canUseSkirmisher(skirmisherHero, stack)
		&& battle.battleCanShoot(stack, destinationTile);

	if(!regularMeleeAttack && !longWeaponAttack && !skirmisherShot)
	{
		gameHandler->complain("Attack cannot be performed!");
		return false;
	}
	if(skirmisherShot)
	{
		auto rainOfArrows = beginRainOfArrows(battle, stack, destinationStack);
		BonusList attackerBonusesToRemove = *stack->getAllBonuses(Bonus::untilAfterAttackSequence);
		BonusList defenderBonusesToRemove = *destinationStack->getAllBonuses(Bonus::untilAfterAttackSequence);
		static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE,
			BonusCustomSubtype::damageTypeAll).Or(Selector::typeSubtype(BonusType::FIRST_STRIKE,
			BonusCustomSubtype::damageTypeRanged));
		const bool firstStrike = destinationStack->hasBonus(firstStrikeSelector)
			&& !destinationStack->hasBonusOfType(BonusType::NOT_ACTIVE);
		if(!firstStrike)
			makeAttack(battle, stack, destinationStack, {.targetHex = destinationTile, .first = true, .ranged = true,
				.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT,
				.perfectMomentSide = perfectMomentSide}, nullptr, nullptr, &rainOfArrows);

		if(destinationStack->alive()
			&& destinationStack->hasBonusOfType(BonusType::RANGED_RETALIATION)
			&& !stack->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
			&& destinationStack->ableToRetaliate()
			&& battle.battleCanShoot(destinationStack, stack->getPosition())
			&& stack->alive())
			makeAttack(battle, destinationStack, stack, {.targetHex = stack->getPosition(), .first = true,
				.ranged = true, .counter = true});

		int totalRangedAttacks = stack->getTotalAttacks(true);
		const auto * attackingHero = battle.battleGetFightingHero(ba.side);
		if(attackingHero)
			totalRangedAttacks += attackingHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
				BonusSubtypeID(stack->creatureId()));
		for(int i = firstStrike ? 0 : 1; i < totalRangedAttacks; ++i)
		{
			if(stack->alive() && destinationStack->alive() && stack->shots.canUse())
				makeAttack(battle, stack, destinationStack, {.targetHex = destinationTile, .attackIndex = i,
					.first = i == 0, .ranged = true,
					.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT,
					.perfectMomentSide = i == 0 ? perfectMomentSide : BattleSide::NONE}, nullptr, nullptr, &rainOfArrows);
		}

		removeBonuses(battle, stack, attackerBonusesToRemove);
		removeBonuses(battle, destinationStack, defenderBonusesToRemove);
		resolveRainOfArrows(battle, stack, rainOfArrows);
		return true;
	}

	//attack
	int totalAttacks = stack->getTotalAttacks(false);

	//TODO: move to CUnitState
	const auto * attackingHero = battle.battleGetFightingHero(ba.side);
	if(attackingHero)
	{
		totalAttacks += attackingHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(stack->creatureId()));
	}

	bool ferocityApplied = false;
	bool destroyedEnemy = false;
	RelentlessAssaultActionContext relentlessAssault;
	int32_t defenderInitialQuantity = destinationStack->getCount();

	BonusList attackerBonusesToRemove = *stack->getAllBonuses(Bonus::untilAfterAttackSequence);	//they need to be gathered here since bonuses with this duration added during attack (like blind) should not be removed
	BonusList defenderBonusesToRemove = *destinationStack->getAllBonuses(Bonus::untilAfterAttackSequence);
	const auto resolveAttackTarget = [&]() -> const CStack *
	{
		return dynamic_cast<const CStack *>(battle.battleResolveHeroOrderTarget(stack, destinationStack, false));
	};
	const CStack * openingAttackTarget = resolveAttackTarget();
	if(!openingAttackTarget)
		return false;
	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	const bool firstStrike = openingAttackTarget->hasBonus(firstStrikeSelector) && !openingAttackTarget->hasBonusOfType(BonusType::NOT_ACTIVE);

	for (int i = 0; i < totalAttacks; ++i)
	{
		const CStack * attackTarget = resolveAttackTarget();
		if(!attackTarget)
			return false;
		//first strike
		if(i == 0 && firstStrike && openingAttackTarget->ableToRetaliate() && !stack->hasBonusOfType(BonusType::BLOCKS_RETALIATION) && !stack->isInvincible() && !longWeaponAttack)
		{
			makeAttack(battle, openingAttackTarget, stack, {.targetHex = stack->getPosition(), .first = true, .counter = true});
		}

		//move can cause death, eg. by walking into the moat, first strike can cause death or paralysis/petrification
		if(stack->alive() && !stack->hasBonusOfType(BonusType::NOT_ACTIVE) && attackTarget->alive())
		{
			//no distance travelled on second attack
			// Pass the originally selected Ward to makeAttack so Protect can
			// consume its first interception atomically; attackTarget is only the
			// resolved recipient used for local retaliation checks below.
			makeAttack(battle, stack, destinationStack, {.targetHex = destinationTile, .distance = (i ? 0 : movementResult.distance), .attackIndex = i, .first = i == 0, .perfectMomentSide = i == 0 ? perfectMomentSide : BattleSide::NONE}, &destroyedEnemy, &relentlessAssault);

			if(!ferocityApplied && stack->hasBonusOfType(BonusType::FEROCITY))
			{
				auto ferocityBonus = stack->getBonus(Selector::type()(BonusType::FEROCITY));
				int32_t requiredCreaturesToKill = ferocityBonus->parameters ? ferocityBonus->parameters->toNumber() : 1;
				if(defenderInitialQuantity - destinationStack->getCount() >= requiredCreaturesToKill)
				{
					ferocityApplied = true;
					int additionalAttacksCount = stack->valOfBonuses(BonusType::FEROCITY);
					totalAttacks += additionalAttacksCount;
				}
			}
		}

		//counterattack
		//we check retaliation twice, so if it unblocked during attack it will work only on next attack
		if(stack->alive()
			&& !stack->hasBonusOfType(BonusType::BLOCKS_RETALIATION)
			&& !stack->isInvincible()
			&& !longWeaponAttack
			&& (i == 0 && !firstStrike)
			&& !battle.battleShroudDeniesRetaliation(BattleAttackInfo(stack, attackTarget, movementResult.distance, false))
			&& attackTarget->ableToRetaliate())
		{
			makeAttack(battle, attackTarget, stack, {.targetHex = stack->getPosition(), .first = true, .counter = true});
		}
	}

	//return
	if(stack->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		&& !stack->hasBonusOfType(BonusType::NOT_ACTIVE)
		&& !stack->hasBonusOfType(BonusType::BIND_EFFECT)
		&& target.size() == 3
		&& startingPos != stack->getPosition()
		&& startingPos == target.at(2).hexValue
		&& stack->alive())
	{
		assert(stack->unitId() == ba.stackNumber);
		int afterAttackSpeed = stack->getMovementRange(0);
		std::pair<BattleHexArray, int> path = battle.getPath(stack->getPosition(), startingPos, stack);
		size_t maxReachbleIndex = std::max(0, beforeAttackSpeed - afterAttackSpeed);
		if(maxReachbleIndex < path.first.size())
		{
			const auto returnResult = moveStack(battle, ba.stackNumber, path.first[maxReachbleIndex]);
			if(!returnResult.invalidRequest)
				movementSpent += returnResult.distance;
		}
	}

	removeBonuses(battle, stack, attackerBonusesToRemove);
	removeBonuses(battle, destinationStack, defenderBonusesToRemove);

	// attacking without moving still triggers the obstacle the unit stands on (e.g. moat damage);
	// units that moved into the obstacle were already charged during the movement above
	if(movementResult.distance == 0)
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);

	const auto * resolvedAttacker = battle.battleGetStackByID(ba.stackNumber, false);
	const auto * ownerHero = resolvedAttacker ? battle.battleGetOwnerHero(resolvedAttacker) : nullptr;
	const int remainingMovement = std::max(0, beforeAttackSpeed - movementSpent);
	if(allowPursuitContinuation && destroyedEnemy && remainingMovement > 0
		&& resolvedAttacker && resolvedAttacker->alive()
		&& resolvedAttacker->canMove() && !resolvedAttacker->isTimeStopped() && ownerHero
		&& ownerHero->hasActivePerk("new-horizons:offense", "new-horizons:offense.pursuit"))
	{
		setPursuitMovementRemaining(battle, resolvedAttacker, remainingMovement);

		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString("%s trigger Pursuit and may move up to ");
		resolvedAttacker->addNameReplacement(line, resolvedAttacker->getCount());
		line.appendNumber(remainingMovement);
		line.appendRawString(remainingMovement == 1 ? " hex." : " hexes.");
		message.lines.push_back(std::move(line));
		gameHandler->sendAndApply(message);
	}

	return true;
}

void BattleActionProcessor::setPursuitMovementRemaining(const CBattleInfoCallback & battle,
	const CStack * stack, int32_t remaining) const
{
	if(!stack)
		return;
	auto state = stack->acquireState();
	state->pursuitMovementRemaining = std::max(0, remaining);
	BattleUnitsChanged changed;
	changed.battleID = battle.getBattle()->getBattleID();
	UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	changed.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(changed);
}

void BattleActionProcessor::setCleaveUsed(const CBattleInfoCallback & battle, const CStack * stack) const
{
	if(!stack)
		return;
	auto state = stack->acquireState();
	state->cleaveUsedThisActivation = true;
	BattleUnitsChanged changed;
	changed.battleID = battle.getBattle()->getBattleID();
	UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	changed.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(changed);
}

void BattleActionProcessor::removeBonuses(const CBattleInfoCallback & battle, const battle::Unit * stack, BonusList bonuses)
{
	if (!stack)
	{
		logGlobal->error("Attempt at removing bonuses from nullptr!");
		return;
	}	

	if (bonuses.empty())
		return;

	SetStackEffect sse;
	sse.battleID = battle.getBattle()->getBattleID();
	std::vector<Bonus> buffer;
	for (const auto & bonus : bonuses)
		buffer.push_back(*bonus);
	sse.toRemove.emplace_back(stack->unitId(), buffer);

	gameHandler->sendAndApply(sse);
}

BattleActionProcessor::RainOfArrowsAction BattleActionProcessor::beginRainOfArrows(
	const CBattleInfoCallback & battle, const CStack * attacker, const CStack * primaryTarget) const
{
	RainOfArrowsAction action;
	if(!attacker || !primaryTarget || battle.battleMatchOwner(attacker, primaryTarget, true)
		|| !newHorizonsArchery::isOrdinaryPhysicalShooter(attacker))
		return action;

	const auto * hero = battle.battleGetOwnerHero(attacker);
	if(!newHorizonsArchery::hasRainOfArrows(hero))
		return action;

	const auto state = attacker->acquireState();
	if(state->archeryRainOfArrowsActivationSerial == battle.getBattle()->getActivationSerial())
		return action;

	action.enabled = true;
	action.primaryTargetUnitId = primaryTarget->unitId();
	for(const auto & hex : primaryTarget->getHexes())
		if(hex.isValid())
			action.primaryFootprint.push_back(hex);
	return action;
}

void BattleActionProcessor::resolveRainOfArrows(const CBattleInfoCallback & battle,
	const CStack * attacker, RainOfArrowsAction & action)
{
	if(!action.enabled || !attacker)
		return;

	const auto adjacentToOriginalPrimary = [&action](const battle::Unit * candidate)
	{
		for(const auto & targetHex : candidate->getHexes())
		{
			if(!targetHex.isValid())
				continue;
			for(const auto & primaryHex : action.primaryFootprint)
				if(BattleHex::getDistance(targetHex, primaryHex) == 1)
					return true;
		}
		return false;
	};
	const auto lowestOccupiedHex = [](const battle::Unit * unit)
	{
		int result = GameConstants::BFIELD_SIZE;
		for(const auto & hex : unit->getHexes())
			if(hex.isValid())
				result = std::min(result, static_cast<int>(hex.toInt()));
		return result;
	};
	const battle::Unit * secondary = nullptr;
	for(const auto * candidate : battle.battleGetUnitsIf([](const battle::Unit * unit)
		{ return unit->alive(); }))
	{
		if(candidate->unitId() == action.primaryTargetUnitId
			|| battle.battleMatchOwner(attacker, candidate, true)
			|| !adjacentToOriginalPrimary(candidate))
			continue;
		if(!secondary || candidate->getAvailableHealth() > secondary->getAvailableHealth()
			|| (candidate->getAvailableHealth() == secondary->getAvailableHealth()
				&& std::pair{lowestOccupiedHex(candidate), candidate->unitId()}
					< std::pair{lowestOccupiedHex(secondary), secondary->unitId()}))
			secondary = candidate;
	}

	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	if(action.actualPrimaryDamage <= 0)
		line = MetaString::createFromRawString(
			"Rain of Arrows deals no secondary damage because the primary target took no actual damage.");
	else if(!secondary)
		line = MetaString::createFromRawString(
			"Rain of Arrows finds no enemy stack adjacent to the primary target; no secondary damage is dealt.");
	else
	{
		const int64_t proposedDamage = action.actualPrimaryDamage * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100;
		if(proposedDamage <= 0)
			line = MetaString::createFromRawString(
				"Rain of Arrows deals no secondary damage: 35% of the primary damage rounds down to zero.");
		else
		{
			BattleStackAttacked hit;
			hit.attackerID = attacker->unitId();
			hit.stackAttacked = secondary->unitId();
			hit.damageAmount = proposedDamage;
			CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), secondary->acquireState());
			StacksInjured injury;
			injury.battleID = battle.getBattle()->getBattleID();
			injury.stacks.push_back(hit);
			gameHandler->sendAndApply(injury);

			line.appendRawString("Rain of Arrows deals ");
			line.appendNumber(hit.damageAmount);
			line.appendRawString(" damage to %s (based on ");
			secondary->addNameReplacement(line, secondary->getCount());
			line.appendNumber(action.actualPrimaryDamage);
			line.appendRawString(" actual damage to the primary target).");
			if(hit.killedAmount > 0)
			{
				line.appendRawString(" ");
				line.appendNumber(hit.killedAmount);
				line.appendRawString(hit.killedAmount == 1 ? " creature perishes." : " creatures perish.");
			}
		}
	}
	message.lines.push_back(std::move(line));
	gameHandler->sendAndApply(message);
}

bool BattleActionProcessor::doShootAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	const auto perfectMomentSide = ba.perfectMoment ? battle.playerToSide(battle.battleGetOwner(stack)) : BattleSide::NONE;
	battle::Target target = ba.getTarget(&battle);

	if (!canStackAct(battle, stack))
		return false;

	if(target.empty())
	{
		gameHandler->complain("Destination required for shot action.");
		return false;
	}

	auto destination = target.at(0).hexValue;

	const CStack * destinationStack = battle.battleGetStackByPos(destination);

	if (!battle.battleCanShoot(stack, destination))
	{
		gameHandler->complain("Cannot shoot!");
		return false;
	}

	const bool emptyTileAreaAttack = battle.battleCanTargetEmptyHex(stack);

	if (!destinationStack && !emptyTileAreaAttack)
	{
		gameHandler->complain("No target to shoot!");
		return false;
	}
	auto rainOfArrows = beginRainOfArrows(battle, stack, destinationStack);
	RelentlessAssaultActionContext relentlessAssault;

	bool firstStrike = false;
	if(!emptyTileAreaAttack)
	{
		static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
		firstStrike = destinationStack->hasBonus(firstStrikeSelector) && !destinationStack->hasBonusOfType(BonusType::NOT_ACTIVE);
	}

	if (!firstStrike)
		makeAttack(battle, stack, destinationStack, {.targetHex = destination, .first = true, .ranged = true, .perfectMomentSide = perfectMomentSide}, nullptr, &relentlessAssault, &rainOfArrows);

	BonusList attackerBonusesToRemove = *stack->getAllBonuses(Bonus::untilAfterAttackSequence);	//they need to be gathered here since bonuses with this duration added during attack (like blind) should not be removed
	BonusList defenderBonusesToRemove;
	if (destinationStack)
		defenderBonusesToRemove = *destinationStack->getAllBonuses(Bonus::untilAfterAttackSequence);

	//ranged counterattack
	if (!emptyTileAreaAttack
		&& destinationStack->hasBonusOfType(BonusType::RANGED_RETALIATION)
		&& !stack->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
		&& destinationStack->ableToRetaliate()
		&& battle.battleCanShoot(destinationStack, stack->getPosition())
		&& stack->alive()) //attacker may have died (fire shield)
	{
		makeAttack(battle, destinationStack, stack, {.targetHex = stack->getPosition(), .first = true, .ranged = true, .counter = true});
	}
	//allow more than one additional attack

	int totalRangedAttacks = stack->getTotalAttacks(true);

	//TODO: move to CUnitState
	const auto * attackingHero = battle.battleGetFightingHero(ba.side);
	if(attackingHero)
	{
		totalRangedAttacks += attackingHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(stack->creatureId()));
	}

	for(int i = firstStrike ? 0:1; i < totalRangedAttacks; ++i)
	{
		if(stack->alive()
			&& (emptyTileAreaAttack || destinationStack->alive())
			&& stack->shots.canUse())
		{
			// when the defender strikes first the opening shot above is skipped and this loop makes
			// it instead, so the shot that abilities fire on is the first one this loop makes
			makeAttack(battle, stack, destinationStack, {.targetHex = destination, .attackIndex = i, .first = i == 0, .ranged = true, .perfectMomentSide = i == 0 ? perfectMomentSide : BattleSide::NONE}, nullptr, &relentlessAssault, &rainOfArrows);
		}
	}

	removeBonuses(battle, stack, attackerBonusesToRemove);
	removeBonuses(battle, destinationStack, defenderBonusesToRemove);
	resolveRainOfArrows(battle, stack, rainOfArrows);

	return true;
}

bool BattleActionProcessor::doCatapultAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);

	if (!canStackAct(battle, stack))
		return false;

	std::shared_ptr<const Bonus> catapultAbility = stack->getFirstBonus(Selector::type()(BonusType::CATAPULT));
	if(!catapultAbility || catapultAbility->subtype == BonusSubtypeID())
	{
		gameHandler->complain("We do not know how to shoot :P");
	}
	else
	{
		const CSpell * spell = catapultAbility->subtype.as<SpellID>().toSpell();
		spells::BattleCast parameters(&battle, stack, spells::Mode::SPELL_LIKE_ATTACK, spell); //We can shot infinitely by catapult
		auto shotLevel = stack->valOfBonuses(Selector::typeSubtype(BonusType::CATAPULT_EXTRA_SHOTS, catapultAbility->subtype));
		parameters.setSpellLevel(shotLevel);
		parameters.cast(gameHandler->spellcastEnvironment(), target);
	}
	return true;
}

bool BattleActionProcessor::doUnitSpellAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);
	SpellID spellID = ba.spell;

	if (!canStackAct(battle, stack))
		return false;

	std::shared_ptr<const Bonus> randSpellcaster = stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	std::shared_ptr<const Bonus> spellcaster = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spellID)));

	if (!spellcaster && !randSpellcaster)
	{
		gameHandler->complain("That stack can't cast spells!");
		return false;
	}

	if (randSpellcaster)
	{
		if (target.size() != 1)
		{
			gameHandler->complain("Invalid target for random spellcaster!");
			return false;
		}

		const battle::Unit * subject = target[0].unitValue;
		if (target[0].unitValue == nullptr)
			subject = battle.battleGetStackByPos(target[0].hexValue, true);

		if (subject == nullptr)
		{
			gameHandler->complain("Invalid target for random spellcaster!");
			return false;
		}

		spellID = battle.getRandomBeneficialSpell(gameHandler->getRandomGenerator(), stack, subject);

		if (spellID == SpellID::NONE)
		{
			gameHandler->complain("That stack can't cast spells!");
			return false;
		}
	}

	const CSpell * spell = SpellID(spellID).toSpell();
	spells::BattleCast parameters(&battle, stack, spells::Mode::CREATURE_ACTIVE, spell);
	int32_t spellLvl = 0;
	if(spellcaster)
		vstd::amax(spellLvl, spellcaster->val);
	if(randSpellcaster)
		vstd::amax(spellLvl, randSpellcaster->val);
	//Magic Plains raises level of spells cast by creatures; must match the client-side preview in BattleActionsController
	if(spell->getLevel() > 0)
		vstd::amax(spellLvl, stack->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	parameters.setSpellLevel(spellLvl);
	parameters.cast(gameHandler->spellcastEnvironment(), target);

	processBattleEventTriggers(battle, CombatEventType::UNIT_SPELLCAST, stack, nullptr);
	return true;
}

bool BattleActionProcessor::doHealAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);

	if (!canStackAct(battle, stack))
		return false;

	if(target.empty())
	{
		gameHandler->complain("Destination required for heal action.");
		return false;
	}

	const battle::Unit * destStack = nullptr;
	std::shared_ptr<const Bonus> healerAbility = stack->getFirstBonus(Selector::type()(BonusType::HEALER));

	if(target.at(0).unitValue)
		destStack = target.at(0).unitValue;
	else
		destStack = battle.battleGetUnitByPos(target.at(0).hexValue);
	const auto * destCreatureStack = dynamic_cast<const CStack *>(destStack);

	if(stack == nullptr || destStack == nullptr || !healerAbility || !healerAbility->subtype.hasValue())
	{
		gameHandler->complain("There is either no healer, no destination, or healer cannot heal :P");
	}
	else
	{
		const CSpell * spell = healerAbility->subtype.as<SpellID>().toSpell();
		spells::BattleCast parameters(&battle, stack, spells::Mode::SPELL_LIKE_ATTACK, spell); //We can heal infinitely by first aid tent
		if(stack->isFirstAidTent())
		{
			const auto * owner = battle.battleGetOwnerHero(stack);
			if(owner && owner->getCapabilityRules()["rulesetVersion"].Integer() >= 3
				&& battle.battleMatchOwner(stack, destStack, true)
				&& destCreatureStack && destCreatureStack->canBeHealed())
				if(const auto siege = owner->getSiegeCapabilities())
					parameters.setEffectValue(siege->firstAidHealing);
		}
		auto dest = battle::Destination(destStack, target.at(0).hexValue);
		parameters.setSpellLevel(0);
		parameters.cast(gameHandler->spellcastEnvironment(), {dest});
	}
	return true;
}

bool BattleActionProcessor::doWalkAndSpellcastAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);
	SpellID spellID = ba.spell;

	if (!canStackAct(battle, stack))
		return false;

	if(target.size() < 2)
	{
		gameHandler->complain("Two destinations required for walk and spellcast action.");
		return false;
	}

	BattleHex movementDestinationTile = target.at(0).hexValue;
	BattleHex targetUnitTile = target.at(1).hexValue;
	const CStack * destinationStack = battle.battleGetStackByPos(targetUnitTile, false);

	if(!destinationStack)
	{
		gameHandler->complain("Invalid target for walk and spellcast");
		return false;
	}

	auto bonus = stack->getBonus(Selector::typeSubtype(BonusType::ADJACENT_SPELLCASTER, BonusSubtypeID(spellID)));
	if (!bonus)
	{
		gameHandler->complain("Creature cannot walk and spellcast.");
		return false;
	}

	const auto movementResult = moveStack(battle, ba.stackNumber, movementDestinationTile);

	if (movementResult.invalidRequest)
	{
		gameHandler->complain("Stack failed walk and spellcast - unable to reach target!");
		return false;
	}

	if(movementResult.obstacleHit)
	{
		return true;
	}

	const CSpell * spell = spellID.toSpell();
	spells::BattleCast parameters(&battle, stack, spells::Mode::CREATURE_ACTIVE, spell);
	battle::Target spellTarget;
	spellTarget.emplace_back(destinationStack);
	int32_t spellLvl = std::max(0, bonus->val);
	//Magic Plains raises level of spells cast by creatures; must match the client-side preview in BattleActionsController
	if(spell->getLevel() > 0)
		vstd::amax(spellLvl, stack->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	parameters.setSpellLevel(spellLvl);
	parameters.cast(gameHandler->spellcastEnvironment(), spellTarget);

	processBattleEventTriggers(battle, CombatEventType::UNIT_SPELLCAST, stack, nullptr);

	return true;
}

bool BattleActionProcessor::canStackAct(const CBattleInfoCallback & battle, const CStack * stack)
{
	if (!stack)
	{
		gameHandler->complain("No such stack!");
		return false;
	}
	if (!stack->alive())
	{
		gameHandler->complain("This stack is dead: " + stack->nodeName());
		return false;
	}
	if(stack->isTimeStopped())
	{
		gameHandler->complain("This stack is in Time Stop stasis!");
		return false;
	}

	if (battle.battleTacticDist())
	{
		if (stack && stack->unitSide() != battle.battleGetTacticsSide())
		{
			gameHandler->complain("This is not a stack of side that has tactics!");
			return false;
		}
	}
	else
	{
		if (stack != battle.battleActiveUnit())
		{
			gameHandler->complain("Action has to be about active stack!");
			return false;
		}
	}
	return true;
}

bool BattleActionProcessor::dispatchBattleAction(const CBattleInfoCallback & battle, const BattleAction & ba,
	bool allowPursuitContinuation)
{
	if(ba.archerySkirmisherAttack && ba.actionType != EActionType::WALK_AND_ATTACK)
	{
		gameHandler->complain("Skirmisher metadata is only valid for a move-and-attack action.");
		return false;
	}
	switch(ba.actionType)
	{
		case EActionType::BAD_MORALE:
		case EActionType::NO_ACTION:
			return doEmptyAction(battle, ba);
		case EActionType::END_TACTIC_PHASE:
			return doEndTacticsAction(battle, ba);
		case EActionType::RETREAT:
			return doRetreatAction(battle, ba);
		case EActionType::SURRENDER:
			return doSurrenderAction(battle, ba);
		case EActionType::HERO_SPELL:
			return doHeroSpellAction(battle, ba);
		case EActionType::HERO_COMMAND:
			return doHeroCommandAction(battle, ba);
		case EActionType::WALK:
			return doWalkAction(battle, ba);
		case EActionType::WAIT:
			return doWaitAction(battle, ba);
		case EActionType::DEFEND:
			return doDefendAction(battle, ba);
		case EActionType::WALK_AND_ATTACK:
			return doAttackAction(battle, ba, allowPursuitContinuation);
		case EActionType::WALK_AND_CAST:
			return doWalkAndSpellcastAction(battle, ba);
		case EActionType::SHOOT:
			return doShootAction(battle, ba);
		case EActionType::CATAPULT:
			return doCatapultAction(battle, ba);
		case EActionType::MONSTER_SPELL:
			return doUnitSpellAction(battle, ba);
		case EActionType::STACK_HEAL:
			return doHealAction(battle, ba);
		case EActionType::DEMONIC_GATING:
			return doDemonicGatingAction(battle, ba);
	}
	gameHandler->complain("Unrecognized action type received!!");
	return false;
}

bool BattleActionProcessor::doDemonicGatingAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
	if(!concrete)
		return false;
	const auto * actingStack = battle.battleGetStackByID(ba.stackNumber, false);
	if(!canStackAct(battle, actingStack))
		return false;
	if(ba.target.size() == 2)
	{
		processBattleEventTriggers(battle, CombatEventType::BEFORE_MOVE, actingStack, nullptr);
		const auto movement = moveStack(battle, ba.stackNumber, ba.target.front().hexValue);
		if(movement.invalidRequest)
			return false;
		const auto * source = battle.battleGetStackByID(ba.stackNumber, false);
		if(source && source->alive())
			processBattleEventTriggers(battle, CombatEventType::AFTER_MOVE, source, nullptr);

		const auto * hero = battle.battleGetFightingHero(ba.side);
		const auto * creature = ba.gatingCreature.toCreature();
		const BattleHex gateHex = ba.target.back().hexValue;
		const int placementRange = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
		const auto accessibility = battle.getAccessibility();
		const bool gateStillLegal = source && source->alive() && hero && creature
			&& BattleHex::getDistance(source->getPosition(), gateHex) <= placementRange
			&& !battle.battleGetUnitByPos(gateHex, true)
			&& !demonicGateFootprintHasObstacle(battle, gateHex, creature->isDoubleWide(), ba.side)
			&& accessibility.accessible(gateHex, creature->isDoubleWide(), ba.side);
		if(!gateStillLegal)
			return true; // authoritative movement or an obstacle may still have consumed the activation

		const auto & side = concrete->getSide(ba.side);
		const auto reserve = side.demonicReserve.find(ba.gatingCreature);
		if(reserve == side.demonicReserve.end() || reserve->second <= 0)
			return true;
		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = ba.side;
		update.reserve = side.demonicReserve;
		update.pending = side.pendingDemonicGates;
		update.gated = side.gatedDemonicStacks;
		update.chainGateArmed = side.chainGateArmed;
		update.masterGateUsed = side.masterGateUsed;
		const bool masterGateContinuation = !side.masterGateUsed && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.masterGate");
		SideInBattle::PendingDemonicGate gate;
		gate.creature = reserve->first;
		gate.count = reserve->second;
		gate.position = gateHex;
		gate.arrivalRound = concrete->getRound() + 1;
		gate.sourceUnitId = ba.stackNumber;
		update.reserve.erase(gate.creature);
		update.pending.push_back(gate);
		if(masterGateContinuation)
		{
			update.masterGateUsed = true;
			update.masterGateContinuationUnitId = ba.stackNumber;
		}
		gameHandler->sendAndApply(update);
	}
	const auto & gates = concrete->getSide(ba.side).pendingDemonicGates;
	const auto found = std::ranges::find_if(gates, [&ba](const auto & gate)
	{
		return gate.sourceUnitId == ba.stackNumber && gate.creature == ba.gatingCreature;
	});
	if(found == gates.end())
		return false;
	const auto acceptedGate = *found;

	// A valid Gate is the only point at which the token is spent.  Validation
	// happened before StartAction, so rejected requests never reach this block;
	// Swift Gate still consumes Chain Gate but retains its already-end-of-round
	// timing instead of advancing it any further.
	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(hero && hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.chainGate")
		&& concrete->getChainGateArmed(ba.side))
	{
		const bool swiftGate = hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.swiftGate");
		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = ba.side;
		update.reserve = concrete->getSide(ba.side).demonicReserve;
		update.pending = concrete->getSide(ba.side).pendingDemonicGates;
		update.gated = concrete->getSide(ba.side).gatedDemonicStacks;
		update.chainGateArmed = false;
		update.masterGateUsed = concrete->getSide(ba.side).masterGateUsed;
		if(!swiftGate)
		{
			const auto pending = std::ranges::find_if(update.pending, [&ba](const auto & gate)
			{
				return gate.sourceUnitId == ba.stackNumber && gate.creature == ba.gatingCreature;
			});
			if(pending != update.pending.end())
			{
				pending->arrivalRound = concrete->getRound();
				pending->chainGateAccelerated = true;
			}
		}
		gameHandler->sendAndApply(update);
	}
	BattleLogMessage message;
	message.battleID = concrete->getBattleID();
	MetaString line = MetaString::createFromRawString("A Gate opens for ");
	line.appendNumber(acceptedGate.count);
	line.appendRawString(" ");
	line.appendName(acceptedGate.creature, acceptedGate.count);
	line.appendRawString(".");
	message.lines.push_back(std::move(line));
	gameHandler->sendAndApply(message);
	return true;
}

bool BattleActionProcessor::doHeroCommandAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	// Canonical Orders are represented by an immutable StartAction snapshot and
	// evaluated from that snapshot by the battle callback.  There is no broad
	// SetStackEffect to emit here: doing so would turn conditional Orders into
	// unconditional bonuses and would lose their one-shot trigger state.
	if(heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
	{
		const auto state = battle.battleGetHeroOrderState(ba.side);
		if(!state || state->command != ba.command)
			return false;
		if(ba.command == HeroCommand::RIPOSTE)
		{
			const auto * hero = battle.battleGetFightingHero(ba.side);
			if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE))
			{
				SetStackEffect update;
				update.battleID = battle.getBattle()->getBattleID();
				for(const auto * unit : battle.battleGetAllStacks(true))
				{
					if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
						|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
						|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
						|| battle.battleGetOwner(unit) != battle.sideToPlayer(ba.side)
						|| newHorizonsOffense::hasVengeanceRetaliationBonus(unit))
						continue;
					update.toAdd.emplace_back(unit->unitId(),
						std::vector<Bonus>{newHorizonsOffense::vengeanceRetaliationBonus()});
				}
				if(!update.toAdd.empty())
					gameHandler->sendAndApply(update);
			}
		}
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		message.lines.push_back(heroOrderLogLine(battle, ba.side, *state));
		gameHandler->sendAndApply(message);
		return true;
	}
	if(ba.command == HeroCommand::FOCUS_FIRE)
		return true; // Validated contextual state was published atomically by StartAction.
	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(!hero || heroCommands::isDoctrine(ba.command)
		|| !heroCommands::supportedByRules(battle.getBattle()->getHeroCommandRules(), ba.command))
		return false;
	const auto effects = heroCommands::bonuses(battle.getBattle()->getHeroCommandRules(), ba.command, *hero);
	SetStackEffect update;
	update.battleID = battle.getBattle()->getBattleID();
	for(const auto * unit : battle.battleGetAllStacks(true))
	{
		if(battle.battleGetOwner(unit) != battle.sideToPlayer(ba.side))
			continue;
		if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			update.toAdd.emplace_back(unit->unitId(), effects);
	}
	gameHandler->sendAndApply(update);
	return true;
}

bool BattleActionProcessor::makeBattleActionImpl(const CBattleInfoCallback & battle, const BattleAction &ba,
	bool * masterGateActivationContinuationOut)
{
	if(masterGateActivationContinuationOut)
		*masterGateActivationContinuationOut = false;
	const auto * demonicBattle = dynamic_cast<const BattleInfo *>(battle.getBattle());
	const bool validMasterGateSide = ba.side == BattleSide::ATTACKER || ba.side == BattleSide::DEFENDER;
	const auto * masterGateHero = ba.actionType == EActionType::DEMONIC_GATING && validMasterGateSide
		? battle.battleGetFightingHero(ba.side) : nullptr;
	const bool masterGateWasUnused = demonicBattle && masterGateHero
		&& !demonicBattle->getSide(ba.side).masterGateUsed
		&& masterGateHero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.masterGate");
	if(ba.perfectMoment)
	{
		const auto * unit = battle.battleGetStackByID(ba.stackNumber, false);
		const bool skirmisherShot = ba.archerySkirmisherAttack
			&& ba.actionType == EActionType::WALK_AND_ATTACK;
		const bool melee = ba.actionType == EActionType::WALK_AND_ATTACK && !skirmisherShot;
		const bool shooting = ba.actionType == EActionType::SHOOT;
		const auto targets = ba.getTarget(&battle);
		const auto * target = targets.size() == ((melee || skirmisherShot) ? 2u : 1u)
			? battle.battleGetStackByPos(targets.back().hexValue) : nullptr;
		bool legal = (melee || shooting || skirmisherShot) && battle.battleCanUsePerfectMoment(unit)
			&& target && target->alive() && battle.battleMatchOwner(unit, target);
		if(legal && shooting)
			legal = battle.battleCanShoot(unit, target->getPosition());
		if(legal && skirmisherShot)
			legal = battle.battleCanSkirmisherAttackFromHex(unit, targets.back().hexValue, targets.front().hexValue);
		if(legal && melee)
		{
			const auto position = targets.front().hexValue;
			auto moved = unit->acquireState();
			moved->setPosition(position);
			legal = (position == unit->getPosition() || battle.battleGetAvailableHexes(unit, false).contains(position))
				&& (battle.isMeleeAttackPossible(moved.get(), target) || battle.isLongWeaponAttack(moved.get(), target));
		}
		if(!legal)
		{
			gameHandler->complain("Perfect Moment declaration is unavailable or not an eligible attack");
			return false;
		}
	}
	if((ba.metamagicFollowup || ba.metamagicGrand || ba.metamagicDecline)
		&& ba.side != BattleSide::ATTACKER && ba.side != BattleSide::DEFENDER)
	{
		gameHandler->complain("Metamagic action has an invalid battle side");
		return false;
	}
	// Typed Spell allowances last through the round. Their presence does not
	// block creature actions, Orders, Wait, or Defend; only an accepted HERO_SPELL
	// may consume the selected spell grant.
	if(ba.actionType == EActionType::HERO_SPELL
		&& ba.metamagicFollowup != battle.battleCanUseMetamagicFollowup(ba.side))
	{
		gameHandler->complain("Forged or stale Metamagic follow-up request");
		return false;
	}
	if(ba.actionType != EActionType::HERO_SPELL && ba.metamagicFollowup)
	{
		gameHandler->complain("Metamagic follow-up flag is only valid for Hero Spell actions");
		return false;
	}
	if(ba.actionType != EActionType::HERO_SPELL && ba.metamagicGrand)
	{
		gameHandler->complain("Grand Metamagic flag is only valid for Hero Spell actions");
		return false;
	}
	if(ba.actionType == EActionType::HERO_SPELL && ba.metamagicGrand)
	{
		gameHandler->complain("Grand Metamagic activation is derived by the server");
		return false;
	}
	if(ba.metamagicDecline)
	{
		gameHandler->complain("Metamagic decline is retired; unused opportunities expire at the round boundary");
		return false;
	}
	if(ba.metamagicManaRefund != 0)
	{
		gameHandler->complain("Metamagic mana refund is server-derived");
		return false;
	}
	// Reject before StartAction can reserve the action budget or publish state.
	if(ba.actionType == EActionType::HERO_SPELL && !ba.metamagicFollowup && battle.battleUsesHeroCommands()
		&& battle.battleCanCastSpell(battle.battleGetFightingHero(ba.side), spells::Mode::HERO) != ESpellCastProblem::OK)
	{
		gameHandler->complain("Hero spell unavailable under the shared round action budget");
		return false;
	}
	BattleAction effectiveAction = ba;
	std::optional<FocusFireState> preparedFocusFire;
	std::optional<HeroOrderState> preparedOrderState;
	if(ba.actionType == EActionType::HERO_COMMAND)
	{
		const bool canonical = heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		if(canonical)
		{
			std::vector<uint32_t> targetUnitIds;
			bool validTargetShape = ba.stackNumber == static_cast<uint32_t>(ba.side == BattleSide::ATTACKER ? -1 : -2);
			for(const auto & target : ba.target)
			{
				if(target.unitValue < 0 || target.hexValue != BattleHex::INVALID)
				{
					validTargetShape = false;
					break;
				}
				targetUnitIds.push_back(static_cast<uint32_t>(target.unitValue));
			}
			if(validTargetShape && !ba.spell.hasValue())
				preparedOrderState = battle.battlePrepareHeroOrderState(ba.side, ba.command, targetUnitIds);
			// Focus Fire keeps its dedicated snapshot for the ranged damage path;
			// the canonical Order snapshot is additionally required for every
			// command and is what makes the command state serializable.
			if(preparedOrderState && ba.command == HeroCommand::FOCUS_FIRE)
				preparedFocusFire = battle.battlePrepareFocusFireState(ba.side, preparedOrderState->primaryTargetUnitId);
		}
		else
		{
			const bool targeted = ba.command == HeroCommand::FOCUS_FIRE;
			if(targeted && !ba.spell.hasValue() && ba.target.size() == 1
				&& ba.target.front().unitValue >= 0 && ba.target.front().hexValue == BattleHex::INVALID
				&& ba.stackNumber == static_cast<uint32_t>(ba.side == BattleSide::ATTACKER ? -1 : -2))
				preparedFocusFire = battle.battlePrepareFocusFireState(ba.side, ba.target.front().unitValue);
		}
		const bool available = !ba.spell.hasValue()
			&& (canonical ? preparedOrderState.has_value()
				: (ba.command == HeroCommand::FOCUS_FIRE ? preparedFocusFire.has_value()
					: ba.target.empty() && battle.battleCanUseHeroCommand(ba.side, ba.command)));
		if(!available)
		{
			gameHandler->complain("Hero command unavailable: ruleset, ownership, target, or round action budget");
			return false;
		}
	}
	// Hero spell mechanics validate their final target inside the dispatcher.
	// Run the same pure validation before StartAction so an invalid request
	// cannot release Time Stop (or advance the flow) merely by reaching the
	// shared action visitor. The dispatcher repeats this check immediately
	// before applying effects as the authoritative final guard.
	if(ba.actionType == EActionType::HERO_SPELL && !validateHeroSpellAction(battle, ba))
	{
		gameHandler->complain("Hero spell unavailable under authoritative target validation");
		return false;
	}
	if(ba.actionType == EActionType::DEMONIC_GATING && !validateDemonicGatingAction(battle, ba))
	{
		gameHandler->complain("Demonic Gate placement or reserve selection is invalid");
		return false;
	}
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	// WAIT provenance is published by StartAction. Validate a repeated request
	// against the pre-action snapshot, before that packet marks the first legal
	// Wait as completed.
	if(ba.actionType == EActionType::WAIT && stack && stack->waitedThisTurn)
	{
		gameHandler->complain("This stack has already waited this round!");
		return false;
	}
	logGlobal->trace("Making action: %s", ba.toString());

	// for these events client does not expects StartAction/EndAction wrapper
	if (!ba.isBattleEndAction())
	{
		StartAction startAction(effectiveAction);
		startAction.battleID = battle.getBattle()->getBattleID();
		startAction.focusFire = preparedFocusFire;
		startAction.orderState = preparedOrderState;
		gameHandler->sendAndApply(startAction);
	}

	bool result = dispatchBattleAction(battle, effectiveAction, masterGateActivationContinuationOut != nullptr);
	if(masterGateActivationContinuationOut && result && masterGateWasUnused)
	{
		const auto * updatedBattle = gameHandler->gs->getBattle(battle.getBattle()->getBattleID());
		*masterGateActivationContinuationOut = updatedBattle
			&& updatedBattle->getSide(ba.side).masterGateUsed;
	}
	if(result && isTimeStopHeroAction(ba))
	{
		if(auto * state = gameHandler->gs->getBattle(battle.getBattle()->getBattleID()))
			state->notePendingTimeStopHeroAction(ba.side);
	}

	if (!ba.isBattleEndAction())
	{
		const auto * updatedBattle = gameHandler->gs->getBattle(battle.getBattle()->getBattleID());
		const auto * updatedStack = updatedBattle && effectiveAction.stackNumber >= 0
			? updatedBattle->battleGetStackByID(effectiveAction.stackNumber, false) : nullptr;
		const bool pursuitContinuation = masterGateActivationContinuationOut && result
			&& effectiveAction.actionType == EActionType::WALK_AND_ATTACK
			&& updatedStack && updatedStack->pursuitMovementRemaining > 0;
		EndAction endAction;
		endAction.battleID = battle.getBattle()->getBattleID();
		endAction.endsFortuneActivation = result && effectiveAction.isUnitAction() && !battle.battleTacticDist()
			&& stack && !stack->isTimeStopped() && !effectiveAction.timeStopHeroActionPass
			&& !(masterGateActivationContinuationOut && *masterGateActivationContinuationOut)
			&& !pursuitContinuation
			&& !(effectiveAction.actionType == EActionType::MONSTER_SPELL && effectiveAction.spell.hasValue()
				&& effectiveAction.spell.toSpell()->canCastWithoutSkip());
		gameHandler->sendAndApply(endAction);
	}

	if(effectiveAction.actionType == EActionType::WAIT || effectiveAction.actionType == EActionType::DEFEND
		|| effectiveAction.actionType == EActionType::SHOOT || effectiveAction.actionType == EActionType::MONSTER_SPELL)
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);

	return result;
}

BattleActionProcessor::MovementResult BattleActionProcessor::moveStack(const CBattleInfoCallback & battle, int stack, BattleHex dest)
{
	const CStack *currentUnit = battle.battleGetStackByID(stack);
	const CStack *stackAtEnd = battle.battleGetStackByPos(dest);

	assert(currentUnit);
	assert(dest < GameConstants::BFIELD_SIZE);

	if (battle.battleGetTacticDist())
	{
		assert(battle.isInTacticRange(dest));
	}

	auto start = currentUnit->getPosition();
	if (start == dest)
		return { 0, false, false };
	const auto orderStateBeforeAttacker = battle.battleGetHeroOrderState(BattleSide::ATTACKER);
	const auto orderStateBeforeDefender = battle.battleGetHeroOrderState(BattleSide::DEFENDER);

	//initing necessary tables
	auto accessibility = battle.getAccessibility(currentUnit);
	const bool ghostWalk = newHorizonsShroud::rank(battle.battleGetOwnerHero(currentUnit)) > 0;
	BattleHexArray passed;
	//Ignore obstacles on starting position
	passed.insert(currentUnit->getPosition());
	if(currentUnit->doubleWide())
		passed.insert(currentUnit->occupiedHex());

	//shifting destination (if we have double wide stack and we can occupy dest but not be exactly there)
	if(!stackAtEnd && currentUnit->doubleWide() && !accessibility.accessible(dest, currentUnit))
	{
		BattleHex shifted = dest.cloneInDirection(currentUnit->headDirection(), false);

		if(accessibility.accessible(shifted, currentUnit))
			dest = shifted;
	}

	if((stackAtEnd && stackAtEnd!=currentUnit && stackAtEnd->alive()) || !accessibility.accessible(dest, currentUnit))
	{
		gameHandler->complain("Given destination is not accessible!");
		return { 0, false, true };
	}

	bool canUseGate = false;
	auto dbState = battle.battleGetGateState();
	if(battle.battleGetFortifications().wallsHealth > 0 && currentUnit->unitSide() == BattleSide::DEFENDER &&
		dbState != EGateState::DESTROYED &&
		dbState != EGateState::BLOCKED)
	{
		canUseGate = true;
	}

	auto [unitPath, pathDistance] = battle.getPath(start, dest, currentUnit);
	bool movementSuccess = true;

	int unitMovementRange = currentUnit->getMovementRange(0);
	if(currentUnit->pursuitMovementRemaining > 0)
		unitMovementRange = std::min(unitMovementRange, currentUnit->pursuitMovementRemaining);

	if (battle.battleGetTacticDist() > 0 && unitMovementRange > 0)
		unitMovementRange = GameConstants::BFIELD_SIZE;

	if (pathDistance > unitMovementRange)
	{
		gameHandler->complain("Given destination is not reachable!");
		return { 0, false, true };
	}

	bool hasWideMoat = vstd::contains_if(battle.battleGetAllObstaclesOnPos(BattleHex(BattleHex::GATE_BRIDGE), false), [](const std::shared_ptr<const CObstacleInstance> & obst)
	{
		return obst->obstacleType == CObstacleInstance::MOAT;
	});

	auto isGateDrawbridgeHex = [&](const BattleHex & hex) -> bool
	{
		if (hasWideMoat && hex == BattleHex::GATE_BRIDGE)
			return true;
		if (hex == BattleHex::GATE_OUTER)
			return true;
		if (hex == BattleHex::GATE_INNER)
			return true;

		return false;
	};

	auto occupyGateDrawbridgeHex = [&](const BattleHex & hex) -> bool
	{
		if (isGateDrawbridgeHex(hex))
			return true;

		if (currentUnit->doubleWide())
		{
			BattleHex otherHex = currentUnit->occupiedHex(hex);
			if (otherHex.isValid() && isGateDrawbridgeHex(otherHex))
				return true;
		}

		return false;
	};

	if (currentUnit->hasBonusOfType(BonusType::FLYING))
	{
		if (pathDistance <= unitMovementRange && !unitPath.empty())
		{
			if (canUseGate && dbState != EGateState::OPENED &&
				occupyGateDrawbridgeHex(dest))
			{
				BattleUpdateGateState db;
				db.battleID = battle.getBattle()->getBattleID();
				db.state = EGateState::OPENED;
				gameHandler->sendAndApply(db);
			}

			//inform clients about move
			BattleStackMoved sm;
			sm.battleID = battle.getBattle()->getBattleID();
			sm.stack = currentUnit->unitId();
			BattleHexArray tiles;
			tiles.insert(unitPath[0]);
			sm.tilesToMove = tiles;
			sm.distance = pathDistance;
			sm.teleporting = false;
			gameHandler->sendAndApply(sm);
		}
	}
	else //for non-flying creatures
	{
		BattleHexArray tiles;
		const int tilesToMove = std::max<int>(unitPath.size() - unitMovementRange, 0);
		int movementsLeft = static_cast<int>(unitPath.size())-1;
		unitPath.insert(start);

		// check if gate need to be open or closed at some point
		BattleHex openGateAtHex;
		BattleHex gateMayCloseAtHex;
		if (canUseGate)
		{
			for (int i = static_cast<int>(unitPath.size())-1; i >= 0; i--)
			{
				auto needOpenGates = [hasWideMoat, i, unitPath = unitPath](const BattleHex & hex) -> bool
				{
					if (hasWideMoat && hex == BattleHex::GATE_BRIDGE)
						return true;
					if (hex == BattleHex::GATE_BRIDGE && i-1 >= 0 && unitPath[i-1] == BattleHex::GATE_OUTER)
						return true;
					if (hex == BattleHex::GATE_OUTER || hex == BattleHex::GATE_INNER)
						return true;

					return false;
				};

				auto hex = unitPath[i];
				if (!openGateAtHex.isValid() && dbState != EGateState::OPENED)
				{
					if (needOpenGates(hex) || needOpenGates(currentUnit->occupiedHex(hex)))
						openGateAtHex = unitPath[i+1];

					//gate may be opened and then closed during stack movement, but not other way around
					if (openGateAtHex.isValid())
						dbState = EGateState::OPENED;
				}

				if (!gateMayCloseAtHex.isValid() && dbState != EGateState::CLOSED)
				{
					if (hex == BattleHex::GATE_INNER && i-1 >= 0 && unitPath[i-1] != BattleHex::GATE_OUTER)
					{
						gateMayCloseAtHex = unitPath[i-1];
					}
					if (hasWideMoat)
					{
						if (hex == BattleHex::GATE_BRIDGE && i-1 >= 0 && unitPath[i-1] != BattleHex::GATE_OUTER)
						{
							gateMayCloseAtHex = unitPath[i-1];
						}
						else if (hex == BattleHex::GATE_OUTER && i-1 >= 0 &&
							unitPath[i-1] != BattleHex::GATE_INNER &&
							unitPath[i-1] != BattleHex::GATE_BRIDGE)
						{
							gateMayCloseAtHex = unitPath[i-1];
						}
					}
					else if (hex == BattleHex::GATE_OUTER && i-1 >= 0 && unitPath[i-1] != BattleHex::GATE_INNER)
					{
						gateMayCloseAtHex = unitPath[i-1];
					}
				}
			}
		}

		while(movementSuccess)
		{
			if (movementsLeft<tilesToMove)
				throw std::runtime_error("Movement terminated abnormally");

			bool gateStateChanging = false;
			//special handling for opening gate on from starting hex
			if (openGateAtHex.isValid() && openGateAtHex == start)
				gateStateChanging = true;
			else
			{
				for (bool obstacleHit = false; (!obstacleHit) && (!gateStateChanging) && (movementsLeft >= tilesToMove); --movementsLeft)
				{
					BattleHex hex = unitPath[movementsLeft];
					tiles.insert(hex);
					const auto footprint = currentUnit->getHexes(hex);
					const bool crossingOccupiedStack = ghostWalk && std::ranges::any_of(footprint, [&](const BattleHex & occupiedHex)
					{
						return occupiedHex.isValid()
							&& accessibility[occupiedHex.toInt()] == EAccessibility::ALIVE_STACK;
					});

					if ((openGateAtHex.isValid() && openGateAtHex == hex) ||
						(gateMayCloseAtHex.isValid() && gateMayCloseAtHex == hex))
					{
						gateStateChanging = true;
					}

					//if we walked onto something, finalize this portion of stack movement check into obstacle
					if(!crossingOccupiedStack && !battle.battleGetAllObstaclesOnPos(hex, false).empty())
						obstacleHit = true;

					if (currentUnit->doubleWide())
					{
						BattleHex otherHex = currentUnit->occupiedHex(hex);
						//two hex creature hit obstacle by backside
						auto obstacle2 = battle.battleGetAllObstaclesOnPos(otherHex, false);
						if(!crossingOccupiedStack && otherHex.isValid() && !obstacle2.empty())
							obstacleHit = true;
					}
					if(!obstacleHit)
						passed.insert(hex);
				}
			}

			if (!tiles.empty())
			{
				//commit movement
				BattleStackMoved sm;
				sm.battleID = battle.getBattle()->getBattleID();
				sm.stack = currentUnit->unitId();
				sm.distance = pathDistance;
				sm.teleporting = false;
				sm.tilesToMove = tiles;
				gameHandler->sendAndApply(sm);
				tiles.clear();
			}

			//we don't handle obstacle at the destination tile -> it's handled separately in the if at the end
			if (currentUnit->getPosition() != dest)
			{
				if(movementSuccess && start != currentUnit->getPosition())
				{
					movementSuccess &= battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *currentUnit, passed);
					passed.insert(currentUnit->getPosition());
					if(currentUnit->doubleWide())
						passed.insert(currentUnit->occupiedHex());
				}
				if (gateStateChanging)
				{
					if (currentUnit->getPosition() == openGateAtHex)
					{
						openGateAtHex = BattleHex();
						//only open gate if stack is still alive
						if (currentUnit->alive())
						{
							BattleUpdateGateState db;
							db.battleID = battle.getBattle()->getBattleID();
							db.state = EGateState::OPENED;
							gameHandler->sendAndApply(db);
						}
					}
					else if (currentUnit->getPosition() == gateMayCloseAtHex)
					{
						gateMayCloseAtHex = BattleHex();
						owner->updateGateState(battle);
					}
				}
			}
			else
			{
				//movement finished normally: we reached destination
				break;
			}
		}
	}
	//handle last hex separately for deviation
	if (gameHandler->gameInfo().getSettings().getBoolean(EGameSettings::COMBAT_ONE_HEX_TRIGGERS_OBSTACLES))
	{
		if (dest == battle::Unit::occupiedHex(start, currentUnit->doubleWide(), currentUnit->unitSide())
			|| start == battle::Unit::occupiedHex(dest, currentUnit->doubleWide(), currentUnit->unitSide()))
			passed.clear(); //Just empty passed, obstacles will handled automatically
	}
	if(dest == start) 	//If dest is equal to start, then we should handle obstacles for it anyway
		passed.clear();	//Just empty passed, obstacles will handled automatically
	//handling obstacle on the final field (separate, because it affects both flying and walking stacks)
	movementSuccess &= battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *currentUnit, passed);
	if(orderStateBeforeAttacker != battle.battleGetHeroOrderState(BattleSide::ATTACKER))
		publishHeroOrderState(battle, BattleSide::ATTACKER);
	if(orderStateBeforeDefender != battle.battleGetHeroOrderState(BattleSide::DEFENDER))
		publishHeroOrderState(battle, BattleSide::DEFENDER);

	return { static_cast<int16_t>(pathDistance), !movementSuccess, false };
}

void BattleActionProcessor::rollAttackFlags(const CBattleInfoCallback & battle, const CStack * attacker, const CStack * defender, BattleAttack & bat, bool perfectMoment) const
{
	const int attackerLuck = battle.battleGetAttackLuck(attacker, defender, bat.shot());
	ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(attacker->unitSide())->id;

	if(perfectMoment || (attackerLuck > 0 && gameHandler->randomizer->rollGoodLuck(ownerArmy, attackerLuck)))
		bat.flags |= BattleAttack::LUCKY;

	if(!perfectMoment && attackerLuck < 0 && gameHandler->randomizer->rollBadLuck(ownerArmy, -attackerLuck))
		bat.flags |= BattleAttack::UNLUCKY;

	const auto side = battle.playerToSide(battle.battleGetOwner(attacker));
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		auto fortune = battle.getBattle()->getSylvanLuckState(side);
		if(fortune.active())
		{
			if(perfectMoment)
				fortune.consumePerfectMoment();
			if(fortune.recordStrike(attacker->unitId(), bat.lucky(), bat.unlucky()))
				bat.flags &= ~BattleAttack::UNLUCKY;
			bat.fortuneSide = side;
			bat.fortuneState = std::move(fortune);
		}
	}

	if (gameHandler->randomizer->rollCombatAbility(ownerArmy, attacker->valOfBonuses(BonusType::DOUBLE_DAMAGE_CHANCE)))
		bat.flags |= BattleAttack::DEATH_BLOW;

	const auto * ownerHero = battle.battleGetFightingHero(attacker->unitSide());
	if(ownerHero)
	{
		int chance = ownerHero->valOfBonuses(BonusType::BONUS_DAMAGE_CHANCE, BonusSubtypeID(attacker->creatureId()));
		if (gameHandler->randomizer->rollCombatAbility(ownerArmy, chance))
			bat.flags |= BattleAttack::BALLISTA_DOUBLE_DMG;
	}
}

void BattleActionProcessor::describeUpcomingAttack(CombatEventPayload & payload, const CStack * defender, const battle::Units & secondaryTargets) const
{
	if(defender && defender->alive())
		payload.targets.push_back(unitAboutToBeAttacked(defender));

	for(const auto * unit : secondaryTargets)
		payload.targets.push_back(unitAboutToBeAttacked(unit));
}

battle::Units BattleActionProcessor::collectSecondaryTargets(const CBattleInfoCallback & battle, const CStack * attacker, const CStack * defender, const AttackDescriptor & attack, BattleAttack & bat) const
{
	battle::Units result;

	const auto & addOnce = [&result, defender](const battle::Unit * unit)
	{
		if(unit != defender && unit->alive() && !vstd::contains(result, unit)) //do not hit same unit twice
			result.push_back(unit);
	};

	//multiple-hex normal attack
	const auto & [attackedCreatures, useCustomAnimation] = battle.getAttackedCreatures(attacker, attack.targetHex, attack.ranged);

	for(const auto * unit : attackedCreatures)
		addOnce(unit);

	if (useCustomAnimation)
		bat.flags |= BattleAttack::CUSTOM_ANIMATION;

	std::shared_ptr<const Bonus> bonus = attacker->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));

	if(!bonus || !attack.ranged || !bonus->subtype.hasValue()) //TODO: make it work in melee?
		return result;

	//this is need for displaying hit animation
	bat.flags |= BattleAttack::SPELL_LIKE;
	bat.spellID = bonus->subtype.as<SpellID>();

	//TODO: should spell override creature`s projectile?

	const auto * spell = bat.spellID.toSpell();

	battle::Target target;
	target.emplace_back(defender, attack.targetHex);

	spells::BattleCast event(&battle, attacker, spells::Mode::SPELL_LIKE_ATTACK, spell);
	event.setSpellLevel(bonus->val);

	//TODO: get exact attacked hex for defender

	for(const CStack * stack : spell->battleMechanics(&event)->getAffectedStacks(target))
		addOnce(stack);

	return result;
}

void BattleActionProcessor::markSpellLikeAttack(const CStack * attacker, BattleAttack & bat) const
{
	if(!bat.spellLike())
		return;

	//now add effect info for all attacked stacks
	for (BattleStackAttacked & bsa : bat.bsa)
	{
		if (bsa.attackerID == attacker->unitId()) //this is our attack and not f.e. fire shield
		{
			//this is need for displaying affect animation
			bsa.flags |= BattleStackAttacked::SPELL_EFFECT;
			bsa.spellID = bat.spellID;
		}
	}
}

void BattleActionProcessor::makeAttack(const CBattleInfoCallback & battle, const CStack * attacker,
	const CStack * defender, const AttackDescriptor & attack, bool * destroyedEnemyOut,
	RelentlessAssaultActionContext * relentlessAssault, RainOfArrowsAction * rainOfArrows)
{
	const int bulwarkReflectionPercent = defender && !attack.ranged && defender->defended()
		? newHorizonsBulwark::reflectionPercent(newHorizonsBulwark::rank(battle.battleGetOwnerHero(defender)))
		: 0;
	std::optional<HeroOrderState> orderStateBeforeAttacker = battle.battleGetHeroOrderState(BattleSide::ATTACKER);
	std::optional<HeroOrderState> orderStateBeforeDefender = battle.battleGetHeroOrderState(BattleSide::DEFENDER);
	bool protectIntercepted = attack.protectIntercepted;
	// Protect redirects each qualifying melee blow until this saved Order's
	// snapshot-aware interception allowance is consumed. Resolve and consume the
	// destination on the authoritative battle snapshot before reactions or damage.
	if(defender && !attack.ranged)
	{
		const auto * redirected = battle.battleResolveHeroOrderTarget(attacker, defender, false);
		if(redirected != defender)
		{
			if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
			{
				const auto side = battle.playerToSide(battle.battleGetOwner(defender));
				protectIntercepted = const_cast<BattleInfo *>(state)->interceptHeroOrderProtect(side);
				if(protectIntercepted)
				{
					publishHeroOrderState(battle, side);
					if(side == BattleSide::ATTACKER)
						orderStateBeforeAttacker = battle.battleGetHeroOrderState(side);
					else
						orderStateBeforeDefender = battle.battleGetHeroOrderState(side);
				}
			}
			if(const auto * redirectedStack = dynamic_cast<const CStack *>(redirected))
				defender = redirectedStack;
		}
	}

	// spell-casting abilities keep their own rule - once per attack action and never on a counter.
	// Combat event reactions are notified before every attack instead, further below
	if(defender && attack.first && !attack.counter)
		attackCasting(battle, attack.ranged, BonusType::SPELL_BEFORE_ATTACK, attacker, defender);

	// If the attacker or defender is not alive before the attack action, the action should be skipped.
	if((!attacker->alive()) || (defender && !defender->alive()))
		return;

	// Brace answers every qualifying incoming melee attack after the enemy has
	// voluntarily crossed three or more hexes. The recursive pre-emptive strike
	// is marked as a counter so it cannot recursively trigger Brace itself.
	if(defender && !attack.ranged && !attack.counter
		&& battle.battleCanTriggerHeroOrderBrace(attacker, defender, attack.distance, false, false))
	{
		makeAttack(battle, defender, attacker, {.targetHex = attacker->getPosition(), .first = true, .counter = true, .brace = true});
		if(!attacker->alive() || (defender && !defender->alive()))
			return;
	}

	if(defender && !attack.ranged && !attack.counter && defender->defended()
		&& !defender->acquireState()->bulwarkPreemptiveUsed)
	{
		const int percent = newHorizonsBulwark::preemptivePercent(
			newHorizonsBulwark::rank(battle.battleGetOwnerHero(defender)));
		if(percent > 0)
		{
			auto state = defender->acquireState();
			state->bulwarkPreemptiveUsed = true;
			BattleUnitsChanged changed;
			changed.battleID = battle.getBattle()->getBattleID();
			UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			changed.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(changed);
			makeAttack(battle, defender, attacker, {.targetHex = attacker->getPosition(), .first = true,
				.counter = true, .preemptiveDamagePercent = percent});
			if(!attacker->alive() || !defender->alive())
				return;
		}
	}

	BattleAttack bat;
	BattleLogMessage blm;
	struct ResolvedOrderCauses
	{
		uint32_t targetUnitId;
		HeroCommand attacker = HeroCommand::NONE;
		HeroCommand defender = HeroCommand::NONE;
	};
	std::vector<ResolvedOrderCauses> resolvedOrderCauses;
	std::vector<MetaString> archeryFeedbackLogLines;
	const auto appendArcheryFeedback = [&](const DamageEstimation & estimation, const battle::Unit * target)
	{
		if(!target || !attack.ranged || bat.spellLike())
			return;
		if(estimation.archeryDeadeye)
		{
			MetaString line;
			line.appendRawString("Deadeye rolls maximum creature damage and ignores 25% Creature Defense against %s.");
			target->addNameReplacement(line, target->getCount());
			archeryFeedbackLogLines.push_back(std::move(line));
		}
		if(estimation.archeryDefenseIgnorePercent > (estimation.archeryDeadeye
			? newHorizonsArchery::DEADEYE_DEFENSE_IGNORE_PERCENT : 0))
		{
			MetaString line;
			line.appendRawString("Armor-Piercing Shot ignores 20% of Creature Defense against %s.");
			target->addNameReplacement(line, target->getCount());
			archeryFeedbackLogLines.push_back(std::move(line));
		}
		if(estimation.archeryCrossfireDamagePercent > 0)
		{
			MetaString line;
			line.appendRawString("Crossfire adds +15% ranged damage against %s.");
			target->addNameReplacement(line, target->getCount());
			archeryFeedbackLogLines.push_back(std::move(line));
		}
		if(estimation.archeryHighArc)
			archeryFeedbackLogLines.push_back(MetaString::createFromRawString(
				"High Arc ignores obstacle penalties and halves distance penalties for this ranged attack."));
	};
	// Brace's pre-emptive strike is dispatched through the counterattack path so
	// that it happens before the incoming blow, but it must not consume the
	// defender's normal retaliation. Keep the two notions separate here.
	const bool counterAttack = attack.counter && !attack.brace && attack.preemptiveDamagePercent <= 0;
	const bool normalCounter = counterAttack && !attack.archeryCounterfire;
	blm.battleID = battle.getBattle()->getBattleID();
	bat.battleID = battle.getBattle()->getBattleID();
	bat.attackerChanges.battleID = battle.getBattle()->getBattleID();
	bat.stackAttacking = attacker->unitId();
	bat.tile = attack.targetHex;

	if(attack.ranged)
		bat.flags |= BattleAttack::SHOT;
	if(counterAttack)
		bat.flags |= BattleAttack::COUNTER;

	// the same units feed the notification below and the damage further down
	const battle::Units secondaryTargets = attack.cleaveFollowup
		? battle::Units{} : collectSecondaryTargets(battle, attacker, defender, attack, bat);

	CombatEventPayload payload;
	payload.ranged = attack.ranged;
	payload.isCounter = counterAttack;
	payload.attackIndex = attack.attackIndex;

	CombatEventPayload upcoming = payload;
	describeUpcomingAttack(upcoming, defender, secondaryTargets);

	processAttackTriggers(battle, CombatEventType::BEFORE_ATTACK, CombatEventType::BEFORE_ATTACKED, attacker, defender, upcoming);

	// a reaction to the upcoming attack may have killed either side of it - a unit that died before
	// striking does not strike, and one that died before being hit is not hit again
	if((!attacker->alive()) || (defender && !defender->alive()))
		return;

	if(relentlessAssault && defender && !attack.counter && !attack.brace && !attack.cleaveFollowup
		&& attack.preemptiveDamagePercent <= 0 && !attacker->isGhost() && !attacker->isTurret()
		&& !attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& attacker->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& !attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		&& !defender->isGhost() && !defender->isTurret()
		&& !defender->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& defender->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& battle.battleMatchOwner(attacker, defender))
	{
		const auto side = battle.playerToSide(battle.battleGetOwner(attacker));
		const auto * hero = (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			? battle.battleGetFightingHero(side) : nullptr;
		if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		{
			relentlessAssault->eligible = true;
			relentlessAssault->side = side;
			if(relentlessAssault->primaryTargetUnitId != defender->unitId())
			{
				relentlessAssault->damagePercent = relentlessAssault->primaryTargetUnitId
					== RelentlessAssaultState::INVALID_TARGET
					? battle.battleGetRelentlessAssaultDamagePercent(attacker, defender)
					: 0;
				relentlessAssault->primaryTargetUnitId = defender->unitId();
			}
		}
	}

	std::shared_ptr<battle::CUnitState> attackerState = attacker->acquireState();

	const bool perfectMoment = attack.perfectMomentSide != BattleSide::NONE && !attack.counter && !attack.brace
		&& attack.attackIndex == 0 && battle.playerToSide(battle.battleGetOwner(attacker)) == attack.perfectMomentSide
		&& defender && battle.battleMatchOwner(attacker, defender) && battle.battleCanUsePerfectMoment(attacker);
	rollAttackFlags(battle, attacker, defender, bat, perfectMoment);

	// only primary target
	if(defender && defender->alive())
	{
		const auto estimation = applyBattleEffects(battle, bat, attackerState, payload, defender,
			attack.distance, false, attack.brace, attack.preemptiveDamagePercent,
			attack.cleaveDamagePercent, protectIntercepted,
			relentlessAssault && relentlessAssault->eligible ? relentlessAssault->damagePercent : 0,
			attack.archeryRangedDamageMultiplierPercent);
		appendArcheryFeedback(estimation, defender);
		if(relentlessAssault && relentlessAssault->eligible
			&& relentlessAssault->lastRecordedTargetUnitId != defender->unitId()
			&& std::ranges::any_of(bat.bsa, [defender](const auto & hit)
			{
				return hit.stackAttacked == defender->unitId();
			}))
		{
			if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
			{
				auto * mutableState = const_cast<BattleInfo *>(state);
				mutableState->recordRelentlessAssaultAttack(relentlessAssault->side, defender->unitId());
				bat.relentlessAssaultSide = relentlessAssault->side;
				bat.relentlessAssaultState = state->getRelentlessAssaultState(relentlessAssault->side);
				relentlessAssault->lastRecordedTargetUnitId = defender->unitId();
			}
		}
		if(estimation.attackerOrderCause != HeroCommand::NONE || estimation.defenderOrderCause != HeroCommand::NONE)
			resolvedOrderCauses.push_back({defender->unitId(), estimation.attackerOrderCause, estimation.defenderOrderCause});
		if(!attack.ranged && !attack.counter)
		{
			if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
			{
				auto * mutableState = const_cast<BattleInfo *>(state);
				const auto side = battle.playerToSide(battle.battleGetOwner(attacker));
				const auto order = battle.battleGetHeroOrderState(side);
				const bool eligibleOrderUnit = !attacker->isGhost() && !attacker->isTurret()
					&& !attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
					&& attacker->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
				if(order && order->command == HeroCommand::CHARGE && eligibleOrderUnit && attack.distance >= 3)
					mutableState->consumeHeroOrderUnit(side, attacker->unitId());
				if(order && order->command == HeroCommand::FLANK && eligibleOrderUnit
					&& order->primaryTargetUnitId == defender->unitId())
					mutableState->recordHeroOrderFlankSide(side, defender->unitId(), battle.battleHeroOrderFlankSide(attacker, defender));
			}
		}
	}

	for(const auto * unit : secondaryTargets)
	{
		// a reaction to the upcoming attack may have killed it before the blow landed
		if(!unit->alive())
			continue;

		const auto estimation = applyBattleEffects(battle, bat, attackerState, payload, unit,
			attack.distance, true, attack.brace, attack.preemptiveDamagePercent,
			attack.cleaveDamagePercent, false,
			relentlessAssault && relentlessAssault->eligible ? relentlessAssault->damagePercent : 0,
			attack.archeryRangedDamageMultiplierPercent);
		appendArcheryFeedback(estimation, unit);
		if(estimation.attackerOrderCause != HeroCommand::NONE || estimation.defenderOrderCause != HeroCommand::NONE)
			resolvedOrderCauses.push_back({unit->unitId(), estimation.attackerOrderCause, estimation.defenderOrderCause});
		if(!unit->isTimeStopped())
			removeBonuses(battle, unit, *unit->getAllBonuses(Bonus::UntilTakingIndirectDamage));
	}

	const auto currentActivationSerial = battle.getBattle()->getActivationSerial();
	const int currentRound = battle.battleGetRound();
	const auto * attackerHero = battle.battleGetOwnerHero(attacker);
	const bool ordinaryPhysicalShot = attack.ranged && !attack.brace
		&& !attack.cleaveFollowup && !bat.spellLike()
		&& newHorizonsArchery::isOrdinaryPhysicalShooter(attacker);
	if(ordinaryPhysicalShot && attackerHero && newHorizonsArchery::hasDeadeye(attackerHero)
		&& attackerState->archeryDeadeyeRound != currentRound)
		attackerState->archeryDeadeyeRound = currentRound;
	if(ordinaryPhysicalShot && !attack.counter && rainOfArrows && rainOfArrows->enabled
		&& attackerState->archeryRainOfArrowsActivationSerial != currentActivationSerial)
		attackerState->archeryRainOfArrowsActivationSerial = currentActivationSerial;
	if(ordinaryPhysicalShot && !attack.counter && rainOfArrows && rainOfArrows->enabled
		&& rainOfArrows->primaryTargetUnitId == (defender ? defender->unitId() : 0))
	{
		for(const auto & hit : bat.bsa)
			if(hit.stackAttacked == rainOfArrows->primaryTargetUnitId)
				rainOfArrows->actualPrimaryDamage += hit.damageAmount;
	}
	std::optional<uint32_t> suppressedTargetId;
	if(ordinaryPhysicalShot && !attack.counter && attackerHero && newHorizonsArchery::hasSuppression(attackerHero)
		&& attackerState->archerySuppressionActivationSerial != currentActivationSerial)
	{
		const auto firstDamaged = std::ranges::find_if(bat.bsa, [](const BattleStackAttacked & hit)
		{
			return hit.damageAmount > 0;
		});
		if(firstDamaged != bat.bsa.end())
		{
			suppressedTargetId = firstDamaged->stackAttacked;
			attackerState->archerySuppressionActivationSerial = currentActivationSerial;
		}
	}

	markSpellLikeAttack(attacker, bat);
	if(bat.lucky() && bat.fortuneState)
	{
		int64_t actualDamage = 0;
		bool enemyStackKilled = false;
		auto adjacentFriends = battle.battleFortuneAdjacentFriends(attacker);
		for(const auto & target : payload.targets)
		{
			const auto victim = std::find_if(bat.bsa.begin(), bat.bsa.end(), [&target](const auto & hit) { return hit.newState.id == target.unit->unitId(); });
			const bool destroyed = victim != bat.bsa.end() && victim->killed() && !victim->willRebirth();
			if(destroyed)
				vstd::erase(adjacentFriends, target.unit->unitId());
			// In classic single-target Luck mode, collateral victims did not
			// receive a Lucky Strike and cannot fuel Recovery or Cascading.
			if(target.unit != defender && !gameHandler->gameInfo().getSettings().getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS))
				continue;
			actualDamage += std::min(target.damage, target.healthBeforeAttack);
			if(destroyed && battle.battleMatchOwner(attacker, target.unit))
				enemyStackKilled = true;
		}
		bat.fortuneState->finishPositiveStrike(adjacentFriends, enemyStackKilled);
		if(!attack.ranged && bat.fortuneState->luckyRecovery && attackerState->alive())
		{
			auto healing = SylvanLuckState::recoveryAmount(actualDamage);
			attackerState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
		}
	}

	// The one-shot Battlecraft Wait bonus is consumed only by a physical blow;
	// spell-like attacks still spend ordinary attack resources but do not spend
	// this physical-damage state.  Retaliations use this same path, so their
	// consumption is authoritative and is included in attackerChanges below.
	attackerState->afterAttack(attack.ranged, normalCounter, !bat.spellLike());

	{
		UnitChanges info(attackerState->unitId(), UnitChanges::EOperation::UPDATE);
		info.data = attackerState->save();
		bat.attackerChanges.changedStacks.push_back(info);
	}

	// collected before the blow lands: a stack that dies to it loses its spell effects, and a shield
	// that was up when the stack was struck answers the strike that killed it
	std::vector<PendingTrigger> reactions;
	collectEventTriggers(battle, reactions, CombatEventType::AFTER_ATTACK, attacker, defender);

	for(const AttackedTarget & target : payload.targets)
		collectEventTriggers(battle, reactions, CombatEventType::AFTER_ATTACKED, target.unit, attacker);

	// The packet carries the server-authored trigger so receivers can converge
	// on the same capped token without inferring gameplay state from animation
	// or client-side damage estimates.
	bat.chainGateTriggered = chainGateKillQualifies(battle, attacker, bat.bsa);
	MetaString braceLogLine;
	if(attack.brace)
	{
		bool wroteTarget = false;
		for(const BattleStackAttacked & hit : bat.bsa)
		{
			const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
			if(!target)
				continue;
			if(!wroteTarget)
				braceLogLine.appendRawString("Brace preemptive strike: ");
			else
				braceLogLine.appendRawString("; ");
			braceLogLine.appendRawString("%s hit ");
			attacker->addNameReplacement(braceLogLine, attacker->getCount());
			braceLogLine.appendRawString("%s for ");
			target->addNameReplacement(braceLogLine, target->getCount());
			braceLogLine.appendNumber(hit.damageAmount);
			braceLogLine.appendRawString(" damage (");
			braceLogLine.appendNumber(hit.killedAmount);
			braceLogLine.appendRawString(" killed)");
			const auto cause = std::ranges::find(resolvedOrderCauses, hit.stackAttacked,
				&ResolvedOrderCauses::targetUnitId);
			if(cause != resolvedOrderCauses.end() && cause->defender != HeroCommand::NONE)
			{
				braceLogLine.appendRawString(" despite ");
				appendHeroOrderCauseName(braceLogLine, battle, target, cause->defender);
				braceLogLine.appendRawString(" reducing the damage");
			}
			wroteTarget = true;
		}
		if(wroteTarget)
			braceLogLine.appendRawString(" before the incoming melee attack.");
		else
			braceLogLine = MetaString::createFromRawString("Brace triggers, but its preemptive strike deals no damage.");
	}
	// Format provenance while Order state and hero names are still available.
	// The outgoing BattleAttack may consume the source Order.
	std::vector<MetaString> orderDamageLogLines;
	if(!attack.brace)
	{
		for(const auto & cause : resolvedOrderCauses)
		{
			const auto hit = std::ranges::find(bat.bsa, cause.targetUnitId, &BattleStackAttacked::stackAttacked);
			const auto * target = battle.battleGetUnitByID(cause.targetUnitId);
			if(hit == bat.bsa.end() || !target)
				continue;
			auto line = orderDamageLogLine(battle, attacker, target, *hit, cause.attacker, cause.defender);
			const auto * hero = battle.battleGetOwnerHero(attacker);
			if(cause.attacker == HeroCommand::FOCUS_FIRE && attack.ranged && !bat.spellLike()
				&& newHorizonsArchery::hasTargetCaller(hero))
				line.appendRawString(" Target Caller adds +5 percentage points and ignores all obstacle penalties.");
			orderDamageLogLines.push_back(std::move(line));
		}
	}
	if(destroyedEnemyOut)
	{
		for(const auto & hit : bat.bsa)
		{
			const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
			if(victim && hit.killed() && !hit.willRebirth() && battle.battleMatchOwner(attacker, victim))
			{
				*destroyedEnemyOut = true;
				break;
			}
		}
	}
	gameHandler->sendAndApply(bat);
	std::optional<MetaString> suppressionLogLine;
	if(suppressedTargetId)
	{
		const auto * target = battle.battleGetUnitByID(*suppressedTargetId);
		if(target && target->alive())
		{
			SetStackEffect suppression;
			suppression.battleID = battle.getBattle()->getBattleID();
			const Bonus slow(BonusDuration::STACK_GETS_TURN, BonusType::STACKS_SPEED,
				BonusSource::OTHER, -1, BonusSourceID());
			suppression.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{slow});
			gameHandler->sendAndApply(suppression);

			MetaString line;
			line.appendRawString("Suppression reduces %s's Speed by 1 until its next activation.");
			target->addNameReplacement(line, target->getCount());
			suppressionLogLine = std::move(line);
		}
	}

	BattleAttackInfo noQuarterAttack(attacker, defender, attack.distance, attack.ranged);
	noQuarterAttack.retaliation = normalCounter;
	noQuarterAttack.bracePreemptive = attack.brace;
	noQuarterAttack.preemptiveDamagePercent = attack.preemptiveDamagePercent;
	noQuarterAttack.cleaveDamagePercent = attack.cleaveDamagePercent;
	noQuarterAttack.physicalDamage = !bat.spellLike();
	if(battle.battleCanTriggerNoQuarter(noQuarterAttack))
	{
		std::set<uint32_t> checkedTargets;
		for(const auto & hit : bat.bsa)
		{
			if(!checkedTargets.insert(hit.stackAttacked).second)
				continue;
			const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
			if(!target || !target->alive() || target->isTimeStopped()
				|| !battle.battleMatchOwner(attacker, target)
				|| !newHorizonsOffense::belowNoQuarterThreshold(
					target->getAvailableHealth(), battle::getMaximumHealth(*target)))
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
				newHorizonsOffense::noQuarterMoralePenalty()});
			gameHandler->sendAndApply(effects);

			auto state = target->acquireState();
			state->noQuarterMoraleActivationsRemaining = battle.getBattle()->getActiveStackID()
				== static_cast<int32_t>(target->unitId()) ? 2 : 1;
			BattleUnitsChanged stateChange;
			stateChange.battleID = battle.getBattle()->getBattleID();
			UnitChanges update(target->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			stateChange.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(stateChange);

			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("No Quarter affects %s: all remaining retaliations are lost this round, and Morale is reduced by 2 until the end of the stack's next activation.");
			target->addNameReplacement(line, target->getCount());
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}

	// Bulwark reflects a share of the physical health loss that actually landed,
	// after all reductions. It is direct retaliation damage, not another attack,
	// so it cannot recursively trigger attack reactions or consume retaliation.
	if(bulwarkReflectionPercent > 0 && !bat.spellLike() && attacker->alive() && defender)
	{
		const auto reflectedFrom = std::find_if(payload.targets.begin(), payload.targets.end(), [&](const auto & target)
		{
			return target.unit == defender;
		});
		if(reflectedFrom != payload.targets.end())
		{
			const int64_t received = std::min(reflectedFrom->damage, reflectedFrom->healthBeforeAttack);
			const int64_t reflected = received * bulwarkReflectionPercent / 100;
			if(reflected > 0)
			{
				StacksInjured injury;
				injury.battleID = battle.getBattle()->getBattleID();
				BattleStackAttacked hit;
				hit.attackerID = defender->unitId();
				hit.stackAttacked = attacker->unitId();
				hit.damageAmount = reflected;
				CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), attacker->acquireState());
				injury.stacks.push_back(hit);
				gameHandler->sendAndApply(injury);
			}
		}
	}

	// A lethal BattleAttack can invalidate a Protect pair while the authoritative
	// BattleStackAttacked updates are applied (for example, killing the protector).
	// Publish every state transition made by the complete attack, including those
	// updates, so remote battle snapshots cannot retain an armed pair.
	if(orderStateBeforeAttacker != battle.battleGetHeroOrderState(BattleSide::ATTACKER))
		publishHeroOrderState(battle, BattleSide::ATTACKER);
	if(orderStateBeforeDefender != battle.battleGetHeroOrderState(BattleSide::DEFENDER))
		publishHeroOrderState(battle, BattleSide::DEFENDER);

	{
		const bool multipleTargets = bat.bsa.size() > 1;

		int64_t totalDamage = 0;
		int32_t totalKills = 0;

		for(const BattleStackAttacked & bsa : bat.bsa)
		{
			totalDamage += bsa.damageAmount;
			totalKills += bsa.killedAmount;
		}

		if(attack.brace)
			blm.lines.push_back(std::move(braceLogLine));
		else
		{
			addGenericDamageLog(blm, attackerState, totalDamage);
			if(attack.ranged && attack.archeryRangedDamageMultiplierPercent == newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT)
			{
				MetaString line;
				line.appendRawString("Skirmisher lets the shooter move and fire at 75% normal damage.");
				blm.lines.push_back(std::move(line));
			}
			if(attack.ranged && !bat.spellLike() && defender
				&& newHorizonsArchery::hasPointBlankShot(battle.battleGetOwnerHero(attacker)))
			{
				const auto adjacent = battle.battleAdjacentUnits(attacker);
				if(vstd::contains_if(adjacent, [defender](const battle::Unit * unit)
					{ return unit->unitId() == defender->unitId(); }))
				{
					MetaString line;
					line.appendRawString("Point-Blank Shot ignores the ordinary adjacent-target ranged penalty.");
					blm.lines.push_back(std::move(line));
				}
			}

			if(defender)
				addGenericKilledLog(blm, defender, totalKills, multipleTargets);

			for(auto & line : orderDamageLogLines)
				blm.lines.push_back(std::move(line));

			if(relentlessAssault && relentlessAssault->eligible
				&& relentlessAssault->damagePercent > 0 && defender
				&& relentlessAssault->primaryTargetUnitId == defender->unitId())
			{
				MetaString line;
				line.appendRawString("Relentless Assault increases this attack's damage by +");
				line.appendNumber(relentlessAssault->damagePercent);
				line.appendRawString("%.");
				blm.lines.push_back(std::move(line));
			}
			for(auto & line : archeryFeedbackLogLines)
				blm.lines.push_back(std::move(line));
			if(suppressionLogLine)
				blm.lines.push_back(std::move(*suppressionLogLine));
		}
	}

	// sent before the triggers below so that anything they log lands after the attack description
	gameHandler->sendAndApply(blm);

	if(defender && !attack.cleaveFollowup)
		handleAfterAttackCasting(battle, attacker, defender, payload);

	// priority alone decides what runs first, which is how life drain heals before a fire shield can
	// burn the attacker down and how a death stare only lands after it. Not gated on anyone being
	// alive: a reflecting ability answers a lethal blow while dying, so each reaction decides for itself
	runEventTriggers(battle, reactions, payload);

	// Counterfire is a once-per-round ranged reaction to physical creature damage. Process
	// every actually damaged stack (including secondary targets) only after the original
	// attack's primary damage and event reactions have resolved. Stamp before each response;
	// the counter flag is the recursion guard, so a Counterfire shot cannot provoke another.
	if(attacker && attack.ranged && !attack.counter && !attack.brace && !attack.cleaveFollowup
		&& !bat.spellLike() && attacker->alive()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker))
	{
		std::set<uint32_t> counterfireTargets;
		for(const auto & hit : bat.bsa)
		{
			if(hit.damageAmount <= 0 || !counterfireTargets.insert(hit.stackAttacked).second || !attacker->alive())
				continue;

			const auto * reactionUnit = battle.battleGetUnitByID(hit.stackAttacked);
			const auto * reactionShooter = dynamic_cast<const CStack *>(reactionUnit);
			if(!reactionShooter
				|| !newHorizonsArchery::canUseCounterfire(battle.battleGetOwnerHero(reactionShooter), reactionShooter)
				|| reactionShooter->acquireState()->archeryCounterfireRound == battle.battleGetRound()
				|| !battle.battleCanShoot(reactionShooter, attacker->getPosition()))
				continue;

			auto state = reactionShooter->acquireState();
			state->archeryCounterfireRound = battle.battleGetRound();
			BattleUnitsChanged stateChange;
			stateChange.battleID = battle.getBattle()->getBattleID();
			UnitChanges update(reactionShooter->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			stateChange.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(stateChange);

			BattleLogMessage counterfireLog;
			counterfireLog.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Counterfire: %s answers the ranged attack with a shot at 50% normal damage.");
			reactionShooter->addNameReplacement(line, reactionShooter->getCount());
			counterfireLog.lines.push_back(std::move(line));
			gameHandler->sendAndApply(counterfireLog);

			makeAttack(battle, reactionShooter, attacker, {.targetHex = attacker->getPosition(), .first = true,
				.ranged = true, .archeryRangedDamageMultiplierPercent = newHorizonsArchery::COUNTERFIRE_DAMAGE_PERCENT,
				.counter = true, .archeryCounterfire = true});
		}
	}

	if(!attack.cleaveFollowup && !attack.ranged && !attack.counter && !attack.brace
		&& attack.preemptiveDamagePercent <= 0 && !bat.spellLike())
	{
		std::vector<const battle::Unit *> destroyedEnemies;
		for(const auto & hit : bat.bsa)
		{
			const auto * destroyed = battle.battleGetUnitByID(hit.stackAttacked);
			if(destroyed && hit.killed() && !hit.willRebirth() && battle.battleMatchOwner(attacker, destroyed))
				destroyedEnemies.push_back(destroyed);
		}
		const auto lowestOccupiedHex = [](const battle::Unit * unit)
		{
			int result = GameConstants::BFIELD_SIZE;
			for(const auto hex : unit->getHexes())
				result = std::min<int>(result, hex.toInt());
			return result;
		};
		std::sort(destroyedEnemies.begin(), destroyedEnemies.end(), [&](const battle::Unit * left,
			const battle::Unit * right)
		{
			const int leftHex = lowestOccupiedHex(left);
			const int rightHex = lowestOccupiedHex(right);
			return leftHex != rightHex ? leftHex < rightHex : left->unitId() < right->unitId();
		});
		const auto * resolvedAttacker = battle.battleGetStackByID(attacker->unitId(), false);
		if(resolvedAttacker && battle.battleCanTriggerCleave(resolvedAttacker))
		{
			for(const auto * destroyed : destroyedEnemies)
			{
				const auto * selected = battle.battleSelectCleaveTarget(resolvedAttacker, destroyed);
				const auto * cleaveTarget = dynamic_cast<const CStack *>(selected);
				if(!cleaveTarget)
					continue;

				setCleaveUsed(battle, resolvedAttacker);
				BattleLogMessage cleaveLog;
				cleaveLog.battleID = battle.getBattle()->getBattleID();
				MetaString line;
				line.appendRawString("Cleave: %s automatically strike %s for 50% normal damage.");
				resolvedAttacker->addNameReplacement(line, resolvedAttacker->getCount());
				cleaveTarget->addNameReplacement(line, cleaveTarget->getCount());
				cleaveLog.lines.push_back(std::move(line));
				gameHandler->sendAndApply(cleaveLog);

				makeAttack(battle, resolvedAttacker, cleaveTarget,
					{.targetHex = cleaveTarget->getPosition(), .cleaveFollowup = true,
						.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT});
				break;
			}
		}
	}
}

void BattleActionProcessor::attackCasting(const CBattleInfoCallback & battle, bool ranged, BonusType attackMode, const battle::Unit * attacker, const CStack * defender)
{
	ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(attacker->unitSide())->id;

	if(attacker->hasBonusOfType(attackMode))
	{
		TConstBonusListPtr spells = attacker->getBonuses(Selector::type()(attackMode));
		std::set<SpellID> spellsToCast = getSpellsForAttackCasting(spells, defender);

		for(SpellID spellID : spellsToCast)
		{
			bool castMe = false;
			if(!defender->alive())
			{
				logGlobal->debug("attackCasting: all attacked creatures have been killed");
				return;
			}
			int32_t spellLevel = 0;
			TConstBonusListPtr spellsByType = attacker->getBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
			for(const auto & sf : *spellsByType)
			{
				int meleeRanged = -1;
				if (sf->parameters)
				{
					vstd::amax(spellLevel, sf->parameters->toVector()[0]);
					meleeRanged = sf->parameters->toVector()[1];
				}

				if (meleeRanged == -1 || meleeRanged == 0 || (meleeRanged == 1 && ranged) || (meleeRanged == 2 && !ranged))
					castMe = true;
			}
			int chance = attacker->valOfBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
			vstd::amin(chance, 100);

			const CSpell * spell = SpellID(spellID).toSpell();
			spells::AbilityCaster caster(attacker, spellLevel);

			spells::Target target;
			target.emplace_back(defender);

			spells::BattleCast parameters(&battle, &caster, spells::Mode::PASSIVE, spell);

			auto m = spell->battleMechanics(&parameters);

			if(!m->canBeCastAt(target))
				continue;

			//check if spell should be cast (probability handling)
			if (!gameHandler->randomizer->rollCombatAbility(ownerArmy, chance))
				continue;

			//casting
			if(castMe)
			{
				parameters.cast(gameHandler->spellcastEnvironment(), target);
			}
		}
	}
}

std::set<SpellID> BattleActionProcessor::getSpellsForAttackCasting(const TConstBonusListPtr & spells, const CStack *defender)
{
	std::set<SpellID> spellsToCast;
	constexpr int unlayeredItemsInternalLayer = -1;

	std::map<int, std::vector<std::shared_ptr<Bonus>>> spellsWithBackupLayers;

	for(int i = 0; i < spells->size(); i++)
	{
		std::shared_ptr<Bonus> bonus = spells->operator[](i);
		int layer = bonus->parameters ? bonus->parameters->toVector()[2] : -1;
		vstd::amax(layer, -1);
		spellsWithBackupLayers[layer].push_back(bonus);
	}

	auto addSpellsFromLayer = [&](int layer) -> void
	{
		assert(spellsWithBackupLayers.find(layer) != spellsWithBackupLayers.end());

		for(const auto & spell : spellsWithBackupLayers[layer])
		{
			if (spell->subtype.as<SpellID>() != SpellID())
				spellsToCast.insert(spell->subtype.as<SpellID>());
			else
				logGlobal->error("Invalid spell to cast during attack!");
		}
	};

	if(spellsWithBackupLayers.find(unlayeredItemsInternalLayer) != spellsWithBackupLayers.end())
	{
		addSpellsFromLayer(unlayeredItemsInternalLayer);
		spellsWithBackupLayers.erase(unlayeredItemsInternalLayer);
	}

	for(auto item : spellsWithBackupLayers)
	{
		bool areCurrentLayerSpellsApplied = std::all_of(item.second.begin(), item.second.end(),
			[&](const std::shared_ptr<Bonus> spell)
			{
				std::vector<SpellID> activeSpells = defender->activeSpells();
				return vstd::find(activeSpells, spell->subtype.as<SpellID>()) != activeSpells.end();
			});

		if(!areCurrentLayerSpellsApplied || item.first == spellsWithBackupLayers.rbegin()->first)
		{
			addSpellsFromLayer(item.first);
			break;
		}
	}

	return spellsToCast;
}

/// Legacy spell-casting abilities only - combat event reactions run from makeAttack instead, and
/// deliberately without these gates.
void BattleActionProcessor::handleAfterAttackCasting(const CBattleInfoCallback & battle, const CStack * attacker, const CStack * defender, const CombatEventPayload & payload)
{
	if(!attacker->alive()) // can be already dead, e.g. from retaliation
		return;

	if(defender->alive())
		attackCasting(battle, payload.ranged, BonusType::SPELL_AFTER_ATTACK, attacker, defender);
}

DamageEstimation BattleActionProcessor::applyBattleEffects(const CBattleInfoCallback & battle, BattleAttack & bat,
	std::shared_ptr<battle::CUnitState> attackerState, CombatEventPayload & payload,
	const battle::Unit * def, int distance, bool secondary, bool bracePreemptive,
	int preemptiveDamagePercent, int cleaveDamagePercent, bool protectIntercepted,
	int relentlessAssaultDamagePercent, int archeryRangedDamageMultiplierPercent) const
{
	BattleStackAttacked bsa;
	if(secondary)
		bsa.flags |= BattleStackAttacked::SECONDARY; //all other targets do not suffer from spells & spell-like abilities

	bsa.attackerID = attackerState->unitId();
	bsa.stackAttacked = def->unitId();

	BattleAttackInfo bai(attackerState.get(), def, distance, bat.shot());
	bai.secondaryAttack = secondary;
	// Brace travels through the counterattack path for ordering, but its
	// pre-emptive blow is not a normal retaliation for Riposte or retaliation
	// consumption purposes.
	bai.retaliation = bat.counter() && !bracePreemptive;
	bai.bracePreemptive = bracePreemptive;
	bai.preemptiveDamagePercent = preemptiveDamagePercent;
	bai.cleaveDamagePercent = cleaveDamagePercent;
	bai.relentlessAssaultDamagePercent = relentlessAssaultDamagePercent;
	bai.archeryRangedDamageMultiplierPercent = archeryRangedDamageMultiplierPercent;
	bai.protectIntercepted = protectIntercepted;
	bai.physicalDamage = !bat.spellLike();
	bai.deathBlow = bat.deathBlow();
	bai.doubleDamage = bat.ballistaDoubleDmg();
	// SoD: lucky strike only affects creature that was directly attacked; HotA: affects every target of a multi-target attack
	bai.luckyStrike  = bat.lucky() && (!secondary || gameHandler->gameInfo().getSettings().getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS));
	bai.unluckyStrike  = bat.unlucky();

	auto range = battle.calculateDmgRange(bai);
	{
		bsa.damageAmount = battle.getBattle()->getActualDamage(range.damage, attackerState->getCount(), gameHandler->getRandomGenerator());
		auto defenderState = bai.defender->acquireState();
		CStack::prepareAttacked(bsa, gameHandler->getRandomGenerator(), defenderState); //calculate casualties
		if(bsa.damageAmount > 0 && !bat.spellLike()
			&& newHorizonsArchery::isOrdinaryPhysicalShooter(attackerState.get()))
		{
			const auto shooterSide = battle.playerToSide(battle.battleGetOwner(attackerState.get()));
			defenderState->archeryRecordCrossfireDamage(shooterSide, attackerState->unitId(), battle.battleGetRound());
			bsa.newState.data = defenderState->save();
		}
	}

	bat.bsa.push_back(bsa); //add this stack to the list of victims after drain life has been calculated

	AttackedTarget target;
	target.unit = def;
	target.damage = bsa.damageAmount;
	target.killed = bsa.killedAmount;
	// scripts that reflect a strike, such as fire shield, work from the blow that actually landed,
	// so the roll is scaled back up by what the defences took off it rather than rolled again
	target.damageBeforeDefense = range.damage.max > 0
		? bsa.damageAmount * range.damageBeforeDefense.max / range.damage.max
		: 0;
	target.healthBeforeAttack = def->getAvailableHealth();
	payload.targets.push_back(target);
	return range;
}

void BattleActionProcessor::addGenericKilledLog(BattleLogMessage & blm, const CStack * defender, int32_t killed, bool multiple) const
{
	if(killed > 0)
	{
		MetaString line;

		if (killed > 1)
		{
			line.appendTextID("core.genrltxt.379"); // %d %s perished
			line.replaceNumber(killed);
		}
		else
			line.appendTextID("core.genrltxt.378"); // One %s perishes

		if (multiple)
		{
			if (killed > 1)
				line.replaceTextID("core.genrltxt.43"); // creatures
			else
				line.replaceTextID("core.genrltxt.42"); // creature
		}
		else
			line.replaceName(defender->unitType()->getId(), killed);

		blm.lines.push_back(line);
	}
}

void BattleActionProcessor::addGenericDamageLog(BattleLogMessage& blm, const std::shared_ptr<battle::CUnitState> &attackerState, int64_t damageDealt) const
{
	MetaString text;
	attackerState->addText(text, EMetaText::GENERAL_TXT, 376);
	attackerState->addNameReplacement(text);
	text.replaceNumber(damageDealt);
	blm.lines.push_back(std::move(text));
}

bool BattleActionProcessor::makeAutomaticBattleAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	return makeBattleActionImpl(battle, ba);
}

bool BattleActionProcessor::makePlayerBattleAction(const CBattleInfoCallback & battle, PlayerColor player,
	const BattleAction &ba, BattleAction * effectiveActionOut, bool * masterGateActivationContinuationOut)
{
	if(masterGateActivationContinuationOut)
		*masterGateActivationContinuationOut = false;
	if(ba.timeStopHeroActionPass)
	{
		gameHandler->complain("Time Stop Hero Action pass is server-derived");
		return false;
	}
	if (ba.side != BattleSide::ATTACKER && ba.side != BattleSide::DEFENDER && gameHandler->complain("Can not make action - invalid battle side!"))
		return false;

	if(battle.battleGetTacticDist() != 0)
	{
		if(!ba.isTacticsAction())
		{
			gameHandler->complain("Can not make actions while in tactics mode!");
			return false;
		}

		if(player != battle.sideToPlayer(ba.side))
		{
			gameHandler->complain("Can not make actions in battles you are not part of!");
			return false;
		}
	}
	else
	{
		const auto * active = battle.battleActiveUnit();
		if(!active)
		{
			gameHandler->complain("No active unit in battle!");
			return false;
		}

		if (ba.isUnitAction() && ba.stackNumber != active->unitId())
		{
			gameHandler->complain("Can not make actions - stack is not active!");
			return false;
		}

		// The client-side player check authenticates the active unit's owner, but
		// hero actions carry their side separately and therefore have no stack ID
		// to bind that identity to.  Validate both forms before the Metamagic
		// pending guard (and before StartAction) so a forged opposite-side action
		// cannot consume or bypass the pending sequence.
		auto unitOwner = battle.battleGetOwner(active);
		const auto controllingSide = battle.playerToSide(unitOwner);
		const bool stoppedPassRequest = active->isTimeStopped()
			&& (ba.actionType == EActionType::NO_ACTION || ba.actionType == EActionType::DEFEND);
		if(ba.isUnitAction() && ba.side != (stoppedPassRequest ? controllingSide : active->unitSide()))
		{
			gameHandler->complain("Can not make actions for the other battle side!");
			return false;
		}
		if(!ba.isUnitAction()
			&& ((ba.side != BattleSide::ATTACKER && ba.side != BattleSide::DEFENDER)
				|| player != battle.sideToPlayer(ba.side)))
		{
			gameHandler->complain("Can not make hero actions for the other battle side!");
			return false;
		}

		if(player != unitOwner)
		{
			gameHandler->complain("Can not make actions in battles you are not part of!");
			return false;
		}

		if(active->isTimeStopped() && ba.actionType == EActionType::NO_ACTION)
		{
			// This is the explicit pass for a control-visible stopped activation.
			// The identity/side/owner checks above are deliberately completed before
			// allowing it; no WAIT/DEFEND or other creature action can use this path.
			BattleAction pass = BattleAction::makeNoAction(active);
			pass.side = battle.playerToSide(unitOwner);
			pass.timeStopHeroActionPass = true;
			if(effectiveActionOut)
				*effectiveActionOut = pass;
			return makeBattleActionImpl(battle, pass, masterGateActivationContinuationOut);
		}

		if(active->isTimeStopped() && ba.actionType == EActionType::DEFEND)
		{
			// Existing human clients expose DEFEND as the visible close-turn
			// control. Canonicalize only that owner-authenticated request to the
			// explicit no-op pass so stasis never gains a defending bonus. WAIT and
			// every other creature action remain rejected by canStackAct.
			BattleAction pass = BattleAction::makeNoAction(active);
			pass.side = battle.playerToSide(unitOwner);
			pass.timeStopHeroActionPass = true;
			if(effectiveActionOut)
				*effectiveActionOut = pass;
			return makeBattleActionImpl(battle, pass, masterGateActivationContinuationOut);
		}

		const auto * activeStack = battle.battleGetStackByID(active->unitId(), false);
		if(activeStack && activeStack->pursuitMovementRemaining > 0)
		{
			if(ba.actionType == EActionType::DEFEND || ba.actionType == EActionType::NO_ACTION)
			{
				// Defend is the existing visible close-turn control. During Pursuit it
				// declines the optional movement without granting a defensive stance.
				BattleAction pass = BattleAction::makeNoAction(active);
				pass.side = controllingSide;
				if(effectiveActionOut)
					*effectiveActionOut = pass;
				return makeBattleActionImpl(battle, pass, masterGateActivationContinuationOut);
			}
			if(ba.isUnitAction() && ba.actionType != EActionType::WALK)
			{
				gameHandler->complain("Pursuit continuation permits only movement or ending the creature activation");
				return false;
			}
		}
	}

	if(effectiveActionOut)
		*effectiveActionOut = ba;
	return makeBattleActionImpl(battle, ba, masterGateActivationContinuationOut);
}

void BattleActionProcessor::runPredefinedReaction(const CBattleInfoCallback & battle, const Bonus & bonus, const battle::Unit * self, const battle::Unit * other)
{
	const auto parameters = bonus.parameters->toCustom<BonusParametersOnCombatEvent>();

	for (const auto & effect : parameters.effects)
	{
		const auto * bonusEffect = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect);
		const auto * spellEffect = std::get_if<BonusParametersOnCombatEvent::CombatEffectSpell>(&effect);

		if (bonusEffect)
		{
			SetStackEffect sse;
			sse.battleID = battle.getBattle()->getBattleID();
			std::vector<Bonus> bonuses{*bonusEffect->bonus};
			if (bonusEffect->targetEnemy && other)
				sse.toAdd.emplace_back(other->unitId(), bonuses);
			if (!bonusEffect->targetEnemy)
				sse.toAdd.emplace_back(self->unitId(), bonuses);
			gameHandler->sendAndApply(sse);
		}
		if (spellEffect)
		{
			const CSpell * spell = spellEffect->spell.toSpell();
			spells::AbilityCaster spellCaster(self, spellEffect->masteryLevel);

			spells::Target spellTarget;
			if (spellEffect->targetEnemy && other)
				spellTarget.emplace_back(other);
			if (!spellEffect->targetEnemy)
				spellTarget.emplace_back(self);

			spells::BattleCast castParameters(&battle, &spellCaster, spells::Mode::PASSIVE, spell);

			auto m = spell->battleMechanics(&castParameters);

			if(m->canBeCastAt(spellTarget))
				castParameters.cast(gameHandler->spellcastEnvironment(), spellTarget);
		}
	}
}

void BattleActionProcessor::collectEventTriggers(const CBattleInfoCallback & battle, std::vector<PendingTrigger> & pending, CombatEventType event, const battle::Unit * self, const battle::Unit * other) const
{
	auto add = [&pending, event, self, other](const std::shared_ptr<const Bonus> & bonus, int priority, const ICombatEventScript * script)
	{
		PendingTrigger trigger;
		trigger.event = event;
		trigger.self = self->unitId();
		trigger.other = other ? other->unitId() : -1;
		trigger.priority = priority;
		trigger.bonus = bonus;
		trigger.script = script;
		pending.push_back(trigger);
	};

	// predefined reactions name the event they react to in their subtype, and declare no priority
	for (const auto & bonus : *self->getBonusesOfType(BonusType::ON_COMBAT_EVENT, BonusCustomSubtype(static_cast<int>(event))))
	{
		// what to do is the entire content of such a bonus, and the schema requires it, so one
		// without it is content that already failed validation and reacts to nothing
		if (bonus->parameters)
			add(bonus, 0, nullptr);
	}

	// a script instead reacts to every event it implements, so which script to run - the subtype -
	// is not part of the selector here
	for (const auto & bonus : *self->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER))
	{
		ScriptID scriptID = bonus->subtype.as<ScriptID>();

		// content declaring a script that does not exist, or one of another kind - reported on load,
		// and there is nothing to run for it here
		if (!scriptID.hasValue())
		{
			logMod->warn("Unit '%s' carries a combat event trigger that names no script!", self->getDescription());
			continue;
		}

		const auto & script = LIBRARY->scriptTypes()->getById(scriptID);

		if (!script.combatEventScript)
		{
			logMod->warn("Unit '%s' carries a combat event trigger running script '%s', which is not a combat event script!", self->getDescription(), script.scriptId);
			continue;
		}

		if (script.combatEventScript->handlesEvent(battle, event))
			add(bonus, script.priority, script.combatEventScript.get());
	}
}

void BattleActionProcessor::runEventTriggers(const CBattleInfoCallback & battle, std::vector<PendingTrigger> & pending, const CombatEventPayload & payload)
{
	std::ranges::stable_sort(pending, {}, &PendingTrigger::priority);

	// Combat events are only fired from battle actions, and neither the script API nor the spell
	// casts below can start one - both only emit netpacks. Adding a binding that re-enters this
	// class (making a unit attack or move) would make this recursive and need a depth guard.
	for (const auto & trigger : pending)
	{
		// an earlier reaction may have removed either unit - transmutation replaces the stack it
		// hits - so both are looked up again rather than kept as pointers. A removed unit is left
		// behind as a ghost with no bonuses and no health, which is why every script that acts on
		// one has to check that it is still alive
		const battle::Unit * self = battle.battleGetUnitByID(trigger.self);
		if (!self)
			continue;

		const battle::Unit * other = trigger.other == -1 ? nullptr : battle.battleGetUnitByID(trigger.other);

		if (!trigger.script)
		{
			runPredefinedReaction(battle, *trigger.bonus, self, other);
			continue;
		}

		JsonNode parameters;
		if (trigger.bonus->parameters)
			parameters = trigger.bonus->parameters->toCustom<JsonNode>();

		// value of this bonus alone - another bonus running the same script is a reaction of its own,
		// with its own value, rather than something to sum into this one
		parameters["val"].Integer() = trigger.bonus->val;

		trigger.script->run(gameHandler->spellcastEnvironment(), battle, trigger.event, self, other, parameters, payload);
	}
}

void BattleActionProcessor::processBattleEventTriggers(const CBattleInfoCallback & battle, CombatEventType event, const battle::Unit * target, const battle::Unit * secondary, const CombatEventPayload & payload)
{
	std::vector<PendingTrigger> pending;
	collectEventTriggers(battle, pending, event, target, secondary);
	runEventTriggers(battle, pending, payload);
}

void BattleActionProcessor::processAttackTriggers(const CBattleInfoCallback & battle, CombatEventType attackerEvent, CombatEventType targetEvent, const CStack * attacker, const CStack * defender, const CombatEventPayload & payload)
{
	std::vector<PendingTrigger> pending;
	collectEventTriggers(battle, pending, attackerEvent, attacker, defender);

	for(const AttackedTarget & target : payload.targets)
		collectEventTriggers(battle, pending, targetEvent, target.unit, attacker);

	runEventTriggers(battle, pending, payload);
}
