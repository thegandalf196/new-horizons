/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "tbb/parallel_for.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"

// TODO: remove
// Eventually only IBattleInfoCallback and battle::Unit should be used,
// CUnitState should be private and CStack should be removed completely
#include "../../lib/CStack.h"

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

enum class SpellTypes
{
	ADVENTURE, BATTLE, OTHER
};

SpellTypes spellType(const CSpell * spell)
{
	if(!spell->isCombat() || spell->isCreatureAbility())
		return SpellTypes::OTHER;

	if(spell->isOffensive() || spell->hasEffects() || spell->hasBattleEffects())
		return SpellTypes::BATTLE;

	return SpellTypes::OTHER;
}

bool isTransfigureMatter(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:transfigureMatter";
}

bool isCounterspell(const CSpell * spell)
{
	return newHorizonsMagic::isCounterspell(spell);
}

bool isCanonicalLandMine(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(spell->getId());
}

bool isCanonicalFireWall(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(spell->getId());
}

bool isCanonicalTimeStop(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

BattleHex::EDir fireWallDirection(const spells::Target & target)
{
	if(target.size() < 2 || target.front().unitValue != nullptr || target.at(1).unitValue != nullptr)
		return BattleHex::NONE;

	for(const auto direction : BattleHex::hexagonalDirections())
		if(target.front().hexValue.cloneInDirection(direction, false) == target.at(1).hexValue)
			return direction;

	return BattleHex::NONE;
}

/// Estimate the deterministic value of arming Counterspell without mutating a
/// battle preview.  The authoritative battle snapshot is used for the enemy
/// hero, mana costs, and current ward state; the enemy spellbook is only used
/// as a read-only threat list.  A ward is useful only when the AI can afford
/// both the arming cast and at least one likely enemy spell's listed ward cost.
float counterspellThreatValue(const CBattleInfoCallback & battle, BattleSide side,
	const CGHeroInstance * caster, const CSpell * counterspell)
{
	if(!caster || !counterspell || battle.battleWasCounterspellArmed(side))
		return 0.0f;

	const auto enemySide = CBattleInfoEssentials::otherSide(side);
	const auto * enemy = battle.getBattle()->getSideHero(enemySide);
	if(!enemy || !enemy->hasSpellbook())
		return 0.0f;

	// Player callbacks intentionally hide enemy hero details.  Spell-level
	// blockers must nevertheless be evaluated from the all-knowing authoritative
	// battle callback, rather than from the caller's perspective-limited view.
	const auto * authoritativeBattle = dynamic_cast<const CBattleInfoCallback *>(battle.getBattle());
	if(!authoritativeBattle)
		return 0.0f;
	const auto minEnemySpellLevel = authoritativeBattle->battleMinSpellLevel(enemySide);
	const auto maxEnemySpellLevel = authoritativeBattle->battleMaxSpellLevel(enemySide);

	const int armCost = battle.battleGetSpellCost(counterspell, caster);
	const int remainingMana = caster->mana - armCost;
	if(remainingMana < 0)
		return 0.0f;

	const bool countermage = caster->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage");
	float bestValue = 0.0f;
	for(const auto spellID : enemy->getSpellsInSpellbook())
	{
		const auto * spell = spellID.toSpell();
		if(!spell || !spell->isCombat() || spell->isCreatureAbility() || isCounterspell(spell))
			continue;
		const int spellLevel = battle.battleGetSpellLevel(spell->getId());
		if(spellLevel < minEnemySpellLevel || spellLevel > maxEnemySpellLevel)
			continue;
		if(!enemy->canCastThisSpell(spell))
			continue;

		const int listedCost = enemy->getSpellCost(spell);
		const int enemyManaCost = battle.battleGetSpellCost(spell, enemy);
		if(listedCost < 0 || enemy->mana < enemyManaCost)
			continue;

		const int wardCost = newHorizonsMagic::counterspellCost(listedCost, countermage);
		if(remainingMana < wardCost)
			continue;

		// A high-cost offensive spell represents the largest deterministic
		// threat.  The small non-offensive component still lets the ward cover
		// decisive disables, buffs, and summons without making it automatic.
		const float value = static_cast<float>(listedCost) * 100.0f
			+ (spell->isOffensive() ? 250.0f : 0.0f)
			- static_cast<float>(armCost) * 10.0f;
		bestValue = std::max(bestValue, value);
	}

	return bestValue;
}

namespace
{
constexpr HeroCommand riposteCommand()
{
	return HeroCommand::RIPOSTE;
}

constexpr HeroCommand braceCommand()
{
	return HeroCommand::BRACE;
}

constexpr HeroCommand protectCommand()
{
	return HeroCommand::PROTECT;
}

constexpr HeroCommand flankCommand()
{
	return HeroCommand::FLANK;
}

constexpr HeroCommand secondWindCommand()
{
	return HeroCommand::SECOND_WIND;
}

bool isEligibleOrderUnit(const CBattleInfoCallback & battle, BattleSide side,
	const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && unit->isValidTarget()
		&& battle.battleGetOwner(unit) == battle.sideToPlayer(side)
		&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool adjacentForProtect(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second || !first->getPosition().isValid() || !second->getPosition().isValid())
		return false;
	for(const auto & firstHex : first->getHexes())
		for(const auto & secondHex : second->getHexes())
			if(BattleHex::getDistance(firstHex, secondHex) <= 1)
				return true;
	return false;
}

/// A value-only fallback for older snapshots and for runtime APIs that expose
/// only the ordinary one-target query.  It is intentionally conservative: the
/// authoritative callback remains the final arbiter before an action is sent.
template <typename Battle>
std::vector<std::vector<uint32_t>> orderTargetOptions(const Battle & battle,
	BattleSide side, HeroCommand command)
{
	std::vector<std::vector<uint32_t>> result;

	// Canonical runtime builds validate the complete target tuple (including the
	// Protector/Ward ordering and round eligibility) through this snapshot API.
	// Enumerate only the small candidate identity set exposed by the callback;
	// never infer authority from local geometry or activation flags.
	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, std::vector<uint32_t>{}); })
	{
		// The method is present on every current callback, including legacy
		// targeted-command snapshots.  Only the canonical ruleset gives it
		// authority; legacy Focus Fire still uses its scalar validator below.
		const bool canonicalRules = battle.getBattle()
			&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		if(canonicalRules)
		{
			const auto candidates = battle.battleGetHeroCommandTargets(side, command);
			if(command == protectCommand())
			{
				for(const auto protector : candidates)
					for(const auto ward : candidates)
						if(protector != ward && battle.battlePrepareHeroOrderState(side, command, {protector, ward}))
							result.push_back({protector, ward});
			}
			else
			{
				for(const auto targetId : candidates)
					if(battle.battlePrepareHeroOrderState(side, command, {targetId}))
						result.push_back({targetId});
			}
			return result;
		}
	}

	if(command == protectCommand())
	{
		const auto units = battle.battleGetAllUnits(false);
		for(size_t i = 0; i < units.size(); ++i)
		{
			if(!isEligibleOrderUnit(battle, side, units[i]))
				continue;
			for(size_t j = 0; j < units.size(); ++j)
			{
				if(i != j && isEligibleOrderUnit(battle, side, units[j]) && adjacentForProtect(units[i], units[j]))
					result.push_back({units[i]->unitId(), units[j]->unitId()});
			}
		}
		return result;
	}

	// The existing callback already returns authoritative Focus Fire targets;
	// the runtime extension uses the same shape for Flank and Second Wind.
	for(const auto targetId : battle.battleGetHeroCommandTargets(side, command))
		result.push_back({targetId});
	return result;
}

template <typename Battle>
bool commandTargetIsLegal(const Battle & battle, BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targetIds)
{
	if(targetIds.empty())
		return battle.battleCanUseHeroCommand(side, command);

	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, targetIds); })
	{
		if(battle.getBattle() && heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
			return battle.battlePrepareHeroOrderState(side, command, targetIds).has_value();
	}
	if constexpr(requires { battle.battleGetHeroCommandTargetOptions(side, command); })
	{
		for(const auto & option : battle.battleGetHeroCommandTargetOptions(side, command))
			if(std::vector<uint32_t>(option.begin(), option.end()) == targetIds)
				return true;
		return false;
	}
	else if constexpr(requires { battle.battleCanConfirmHeroCommand(side, command, targetIds); })
		return battle.battleCanConfirmHeroCommand(side, command, targetIds);
	else if(targetIds.size() == 1)
		return battle.battleCanConfirmHeroCommand(side, command, targetIds.front());
	else
		// Protect's pair legality is checked atomically by the new server API.
		// No pair must be sent from a legacy callback that cannot validate it.
		return false;
}

float averageOrderDamage(const DamageEstimation & damage)
{
	return static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
}

float canonicalOrderHeuristic(const CBattleInfoCallback & battle, BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetIds)
{
	const auto * hero = battle.battleGetFightingHero(side);
	const auto units = battle.battleGetAllUnits(false);
	const auto ownPlayer = battle.sideToPlayer(side);
	std::vector<const battle::Unit *> ownUnits;
	std::vector<const battle::Unit *> enemyUnits;
	for(const auto * unit : units)
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
			continue;
		if(battle.battleGetOwner(unit) == ownPlayer)
			ownUnits.push_back(unit);
		else
			enemyUnits.push_back(unit);
	}

	const auto & commandRules = battle.getBattle()->getHeroCommandRules()["commands"];
	const auto coefficient = [&](const char * commandKey, const char * effectKey)
	{
		const auto & formula = commandRules[commandKey]["effects"][effectKey];
		return static_cast<float>(heroCommands::coefficient(formula,
			hero ? hero->getPrimSkillLevel(PrimarySkill::ATTACK) : 0,
			hero ? hero->getPrimSkillLevel(PrimarySkill::DEFENSE) : 0));
	};
	const auto meleeDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit || !attackerUnit->isMeleeAttacker())
			return 0.0f;
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, false)));
	};
	const auto anyDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit)
			return 0.0f;
		const bool shooting = battle.battleCanShoot(attackerUnit, defenderUnit->getPosition());
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, shooting)));
	};
	const auto bestOwnMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : ownUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto bestEnemyMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : enemyUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto sumOwnMeleeDamage = [&]
	{
		float total = 0.0f;
		for(const auto * own : ownUnits)
		{
			float best = 0.0f;
			for(const auto * enemy : enemyUnits)
				best = std::max(best, meleeDamage(own, enemy));
			total += best;
		}
		return total;
	};
	const auto ownMeleePotential = sumOwnMeleeDamage();
	if(command == HeroCommand::FOCUS_FIRE && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		const auto rangedPercent = coefficient("focusFire", "rangedDamagePercent");
		float rangedPotential = 0.0f;
		for(const auto * unit : ownUnits)
		{
			if(!unit->isShooter() || unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				|| !unit->willMove(0) || !battle.battleCanShoot(unit, target->getPosition()))
				continue;
			rangedPotential += anyDamage(unit, target);
		}
		return rangedPotential * rangedPercent / 100.0f;
	}
	const auto chargePercent = coefficient("charge", "meleeDamagePercent");
	const auto holdPercent = coefficient("holdTheLine", "damageReductionPercent");
	const auto riposteReduction = coefficient("riposte", "meleeDamageReductionPercent");
	const auto riposteDamage = coefficient("riposte", "retaliationDamagePercent");
	const auto braceDamage = coefficient("brace", "preemptiveDamagePercent");
	const auto protectReduction = coefficient("protect", "interceptedDamageReductionPercent");
	const auto flankDamage = coefficient("flank", "meleeDamagePercent");

	if(command == HeroCommand::CHARGE)
	{
		const bool canCharge = std::any_of(ownUnits.begin(), ownUnits.end(), [](const auto * unit)
		{
			return unit->isMeleeAttacker() && unit->getMovementRange(0) >= 3;
		});
		return canCharge ? ownMeleePotential * chargePercent / 100.0f : 0.0f;
	}

	if(command == HeroCommand::HOLD_THE_LINE)
	{
		float incoming = 0.0f;
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
				if(enemy->isMeleeAttacker())
					incoming = std::max(incoming, meleeDamage(enemy, own));
		return incoming * holdPercent / 100.0f;
	}

	if(command == riposteCommand())
	{
		float incoming = 0.0f;
		float retaliation = 0.0f;
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
			{
				if(!enemy->isMeleeAttacker())
					continue;
				DamageEstimation retaliationEstimate;
				const auto incomingEstimate = battle.battleEstimateDamage(
					BattleAttackInfo(enemy, own, 0, false), &retaliationEstimate);
				incoming = std::max(incoming, averageOrderDamage(incomingEstimate));
				retaliation = std::max(retaliation, averageOrderDamage(retaliationEstimate));
			}
		return incoming * riposteReduction / 100.0f + retaliation * riposteDamage / 100.0f;
	}

	if(command == braceCommand())
	{
		float advancingDamage = 0.0f;
		for(const auto * enemy : enemyUnits)
			if(enemy->isMeleeAttacker() && enemy->getMovementRange(0) >= 3)
				for(const auto * own : ownUnits)
					advancingDamage = std::max(advancingDamage, meleeDamage(enemy, own));
		return advancingDamage * braceDamage / 100.0f;
	}

	if(command == protectCommand() && targetIds.size() == 2)
	{
		const auto * protector = battle.battleGetUnitByID(targetIds[0]);
		const auto * ward = battle.battleGetUnitByID(targetIds[1]);
		if(!isEligibleOrderUnit(battle, side, protector) || !isEligibleOrderUnit(battle, side, ward))
			return 0.0f;
		return bestEnemyMeleeDamage(ward) * protectReduction / 100.0f;
	}

	if(command == flankCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		// The first distinct side gets the base bonus; additional side bonuses are
		// earned only after the authoritative combat path records another approach.
		return bestOwnMeleeDamage(target) * flankDamage / 100.0f;
	}

	if(command == secondWindCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!isEligibleOrderUnit(battle, side, target) || !target->moved(0))
			return 0.0f;
		float extraAttack = 0.0f;
		for(const auto * enemy : enemyUnits)
			extraAttack = std::max(extraAttack, anyDamage(target, enemy));
		const auto leadership = hero && hero->getLeadershipCapacity()
			? static_cast<float>(hero->getLeadershipCapacity()->capacity) : 0.0f;
		const auto directDamagePercent = std::clamp(50.0f + 0.015f * leadership, 0.0f, 100.0f);
		return extraAttack * directDamagePercent / 100.0f;
	}

	return 0.0f;
}
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env),
	cb(cb), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	hb = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	damageCache.buildDamageCache(hb, side);

	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env), cb(cb), hb(hb),
	damageCache(damageCache), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

std::vector<BattleHex> BattleEvaluator::getBrokenWallMoatHexes() const
{
	std::vector<BattleHex> result;

	for(EWallPart wallPart : { EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL })
	{
		auto state = cb->getBattle(battleID)->battleGetWallState(wallPart);

		if(state != EWallState::DESTROYED)
			continue;

		auto wallHex = cb->getBattle(battleID)->wallPartToBattleHex(wallPart);
		auto moatHex = wallHex.cloneInDirection(BattleHex::LEFT);

		result.push_back(moatHex);

		moatHex = moatHex.cloneInDirection(BattleHex::LEFT);
		auto obstaclesSecondRow = cb->getBattle(battleID)->battleGetAllObstaclesOnPos(moatHex, false);

		for(auto obstacle : obstaclesSecondRow)
		{
			if(obstacle->obstacleType == CObstacleInstance::EObstacleType::MOAT)
			{
				result.push_back(moatHex);
				break;
			}
		}
	}

	return result;
}

bool BattleEvaluator::hasWorkingTowers() const
{
	bool keepIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::DESTROYED;
	bool upperIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::DESTROYED;
	bool bottomIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::DESTROYED;
	return keepIntact || upperIntact || bottomIntact;
}

std::optional<PossibleSpellcast> BattleEvaluator::findBestCreatureSpell(const CStack * stack)
{
	if(!stack->canCast())
		return std::nullopt;

	std::vector<SpellID> spellsToCast;
	TConstBonusListPtr bl = stack->getBonusesOfType(BonusType::SPELLCASTER);

	//TODO: faerie dragon type spell should be selected by server
	SpellID creatureSpellToCast = cb->getBattle(battleID)->getRandomCastedSpell(CRandomGenerator::getDefault(), stack);

	for(const auto & bonus : *bl)
		if(!bonus->parameters && bonus->subtype.as<SpellID>().hasValue())
			spellsToCast.push_back(bonus->subtype.as<SpellID>());

	if(creatureSpellToCast.hasValue())
		spellsToCast.push_back(creatureSpellToCast);

	std::vector<PossibleSpellcast> possibleCasts;

	for(const auto spellID : spellsToCast)
	{
		const CSpell * spell = spellID.toSpell();

		if(!spell->canBeCast(cb->getBattle(battleID).get(), spells::Mode::CREATURE_ACTIVE, stack))
			continue;

		spells::BattleCast temp(cb->getBattle(battleID).get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
		{
			PossibleSpellcast ps;
			ps.dest = target;
			ps.spell = spell;
			evaluateCreatureSpellcast(stack, ps);
			possibleCasts.push_back(ps);
		}
	}

	std::sort(
		possibleCasts.begin(), possibleCasts.end(),
		[&](const PossibleSpellcast & lhs, const PossibleSpellcast & rhs)
		{
			return lhs.value > rhs.value;
		}
	);

	if(!possibleCasts.empty() && possibleCasts.front().value > 0)
		return possibleCasts.front();

	return std::nullopt;
}

BattleAction BattleEvaluator::selectStackAction(const CStack * stack)
{
#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace("Select stack action");
#endif
	//evaluate casting spell for spellcasting stack
	std::optional<PossibleSpellcast> bestSpellcast = findBestCreatureSpell(stack);

	auto moveTarget = scoreEvaluator.findMoveTowardsUnreachable(stack, *targets, damageCache, hb);
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});
	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& !stack->canShoot()
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if(targets->possibleAttacks.empty() && bestSpellcast.has_value())
	{
		activeActionMade = true;
		return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
	}

	if(!targets->possibleAttacks.empty())
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating attack for %s", stack->getDescription());
#endif

		auto evaluationResult = scoreEvaluator.findBestTarget(stack, *targets, damageCache, hb, siegeDefense);
		auto & bestAttack = evaluationResult.bestAttack;

		cachedAttack.ap = bestAttack;
		cachedAttack.score = evaluationResult.score;
		cachedAttack.turn = 0;
		cachedAttack.waited = evaluationResult.wait;

		//TODO: consider more complex spellcast evaluation, f.e. because "re-retaliation" during enemy move in same turn for melee attack etc.
		if(bestSpellcast.has_value() && bestSpellcast->value > bestAttack.damageDiff())
		{
			// return because spellcast value is damage dealt and score is dps reduce
			activeActionMade = true;
			return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
		}

		if(evaluationResult.score > score)
		{
			score = evaluationResult.score;

			logAi->debug("BattleAI: %s -> %s x %d, from %d curpos %d dist %d speed %d: +%2f -%2f = %2f",
				bestAttack.attackerState->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->getCount(),
				bestAttack.from.toInt(),
				bestAttack.attack.attacker->getPosition().toInt(),
				bestAttack.attack.chargeDistance,
				bestAttack.attack.attacker->getMovementRange(0),
				bestAttack.defenderDamageReduce,
				bestAttack.attackerDamageReduce,
				score
			);

			if (moveTarget.score <= score)
			{
				if(evaluationResult.wait)
				{
					return BattleAction::makeWait(stack);
				}
				else if(bestAttack.attack.shooting)
				{
					activeActionMade = true;
					return BattleAction::makeShotAttack(stack, bestAttack.attack.defender);
				}
				else
				{
					if(bestAttack.collateralDamageReduce
						&& bestAttack.collateralDamageReduce >= bestAttack.defenderDamageReduce / 2
						&& score < 0)
					{
						return BattleAction::makeDefend(stack);
					}

					bool isTargetOutsideFort = !hb->battleIsInsideWalls(bestAttack.from);
					bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
						&& !bestAttack.attack.shooting
						&& hasWorkingTowers()
						&& !enemyMellee.empty()
						&& isTargetOutsideFort;

					if(siegeDefense)
					{
						logAi->trace("Evaluating exchange at %d self-defense", stack->getPosition());

						BattleAttackInfo bai(stack, stack, 0, false);
						AttackPossibility apDefend(stack->getPosition(), stack->getPosition(), bai);

						float defenseValue = scoreEvaluator.evaluateExchange(apDefend, 0, *targets, damageCache, hb);

						if((defenseValue > score && score <= 0) || (defenseValue > 2 * score && score > 0))
						{
							return BattleAction::makeDefend(stack);
						}
					}
					
					activeActionMade = true;
					return BattleAction::makeMeleeAttack(stack, bestAttack.attack.defenderPos, bestAttack.from);
				}
			}
		}
	}

	//ThreatMap threatsToUs(stack); // These lines may be useful but they are't used in the code.
	if(moveTarget.score > score)
	{
		score = moveTarget.score;
		cachedAttack.ap = moveTarget.cachedAttack;
		cachedAttack.score = score;
		cachedAttack.turn = moveTarget.turnsToReach;

		if(stack->waited())
		{
			logAi->debug(
				"Moving %s towards hex %s[%d], score: %2f",
				stack->getDescription(),
				moveTarget.cachedAttack->attack.defender->getDescription(),
				moveTarget.cachedAttack->attack.defender->getPosition(),
				moveTarget.score);

			return goTowardsNearest(stack, moveTarget.positions, *targets);
		}
				else
		{
			cachedAttack.waited = true;

			return BattleAction::makeWait(stack);
		}
	}

	if(score <= EvaluationResult::INEFFECTIVE_SCORE
		&& !stack->hasBonusOfType(BonusType::FLYING)
		&& stack->getMovementRange(0) != 0
		&& stack->unitSide() == BattleSide::ATTACKER
	   && cb->getBattle(battleID)->battleGetFortifications().hasMoat)
	{
		auto brokenWallMoat = getBrokenWallMoatHexes();

		if(brokenWallMoat.size())
		{
			activeActionMade = true;

			if(stack->doubleWide() && vstd::contains(brokenWallMoat, stack->getPosition()))
				return BattleAction::makeMove(stack, stack->getPosition().cloneInDirection(BattleHex::RIGHT));
			else
				return goTowardsNearest(stack, brokenWallMoat, *targets);
		}
	}

	return stack->waited() ?  BattleAction::makeDefend(stack) : BattleAction::makeWait(stack);
}

uint64_t timeElapsed(std::chrono::time_point<std::chrono::steady_clock> start)
{
	auto end = std::chrono::steady_clock::now();

	return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

BattleAction BattleEvaluator::moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets)
{
	auto additionalScore = 0;
	std::optional<AttackPossibility> attackOnTheWay;

	for(auto & target : targets.possibleAttacks)
	{
		if(!target.attack.shooting && target.from == hex && target.attackValue() > additionalScore)
		{
			additionalScore = target.attackValue();
			attackOnTheWay = target;
		}
	}

	if(attackOnTheWay)
	{
		activeActionMade = true;
		return BattleAction::makeMeleeAttack(stack, attackOnTheWay->attack.defender->getPosition(), attackOnTheWay->from);
	}
	else
	{
		if(stack->position == hex)
			return BattleAction::makeDefend(stack);
		else
			return BattleAction::makeMove(stack, hex);
	}
}

BattleAction BattleEvaluator::goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets)
{
	auto reachability = cb->getBattle(battleID)->getReachability(stack);
	auto avHexes = cb->getBattle(battleID)->battleGetAvailableHexes(reachability, stack, false);

	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});

	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if (siegeDefense)
	{
		avHexes.eraseIf([&](const BattleHex & hex)
		{
			return !cb->getBattle(battleID)->battleIsInsideWalls(hex);
		});
	}

	if(avHexes.empty() || hexes.empty()) //we are blocked or dest is blocked
	{
		return BattleAction::makeDefend(stack);
	}

	BattleHexArray targetHexes = hexes;

	targetHexes.sort([&reachability](const BattleHex & h1, const BattleHex & h2) -> bool
		{
			return reachability.distances[h1.toInt()] < reachability.distances[h2.toInt()];
		});

	BattleHex bestNeighbour = targetHexes.front();

	if(reachability.distances[bestNeighbour.toInt()] > GameConstants::BFIELD_SIZE)
	{
		logAi->trace("No reachable hexes.");
		return BattleAction::makeDefend(stack);
	}

	// this turn
	for(const auto & hex : targetHexes)
	{
		if(avHexes.contains(hex))
		{
			return moveOrAttack(stack, hex, targets);
		}

		if(stack->coversPos(hex))
		{
			logAi->warn("Warning: already standing on neighbouring hex!");
			//We shouldn't even be here...
			return BattleAction::makeDefend(stack);
		}
	}

	// not this turn
	scoreEvaluator.updateReachabilityMap(hb);

	if(stack->hasBonusOfType(BonusType::FLYING))
	{
		BattleHexArray obstacleHexes;

		const auto & obstacles = hb->battleGetAllObstacles();

		for (const auto & obst : obstacles) 
		{
			if(obst->triggersEffects())
			{
				auto triggerAbility =  LIBRARY->spells()->getById(obst->getTrigger());
				auto triggerIsNegative = triggerAbility->isNegative() || triggerAbility->isDamage();

				if(triggerIsNegative)
					obstacleHexes.insert(obst->getAffectedTiles());
			}
		}
		// Flying stack doesn't go hex by hex, so we can't backtrack using predecessors.
		// We just check all available hexes and pick the one closest to the target.
		auto nearestAvailableHex = vstd::minElementByFun(avHexes, [this, &bestNeighbour, &stack, &obstacleHexes](const BattleHex & hex) -> int
		{
			const int NEGATIVE_OBSTACLE_PENALTY = 100; // avoid landing on negative obstacle (moat, fire wall, etc)
			const int BLOCKED_STACK_PENALTY = 100; // avoid landing on moat

			auto distance = BattleHex::getDistance(bestNeighbour, hex);

			if(obstacleHexes.contains(hex))
				distance += NEGATIVE_OBSTACLE_PENALTY;

			return scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, hex) ? BLOCKED_STACK_PENALTY + distance : distance;
		});

		return moveOrAttack(stack, *nearestAvailableHex, targets);
	}
	else
	{
		BattleHex currentDest = bestNeighbour;

		while(true)
		{
			if(!currentDest.isValid())
			{
				return BattleAction::makeDefend(stack);
			}

			if(avHexes.contains(currentDest)
				&& !scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, currentDest))
			{
				return moveOrAttack(stack, currentDest, targets);
			}

			currentDest = reachability.predecessors[currentDest.toInt()];
		}
	}
	
	logAi->error("We should either detect that hexes are unreachable or make a move!");
	return BattleAction::makeDefend(stack);
}

bool BattleEvaluator::canCastSpell()
{
	auto hero = cb->getBattle(battleID)->battleGetMyHero();
	if(!hero)
		return false;

	if(cb->getBattle(battleID)->battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK)
		return true;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		if(cb->getBattle(battleID)->battleCanUseHeroCommand(side, command))
			return true;
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
		if(cb->getBattle(battleID)->battleCanBeginHeroCommand(side, command))
			return true;
	return false;
}

bool BattleEvaluator::attemptCastingSpell(const CStack * activeStack, bool allowSpells)
{
	auto hero = cb->getBattle(battleID)->battleGetMyHero();
	if(!hero)
		return false;

	LOGL("Casting spells sounds like fun. Let's see...");
	const bool metamagicFollowup = cb->getBattle(battleID)->battleCanUseMetamagicFollowup(side);
	const bool metamagicGrandAvailable = metamagicFollowup
		&& cb->getBattle(battleID)->battleMetamagicPendingCount(side) == 1
		&& cb->getBattle(battleID)->battleMetamagicSequenceSpells(side).size() == 1
		&& !cb->getBattle(battleID)->battleMetamagicGrandUsed(side)
		&& newHorizonsMagic::metamagicRank(hero) >= 3
		&& newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND);
	// Grand is an optional variant of the immediate offer.  Keep both choices
	// in the candidate set so the ordinary one-extra spell can win when it is
	// better (and so a repeated first spell remains available as an ordinary
	// cast even though the Grand version is rejected by Perfect Sequence).
	const std::vector<bool> metamagicGrandChoices = metamagicGrandAvailable
		? std::vector<bool>{false, true}
		: std::vector<bool>{false};
	// A pending sequence is an authoritative immediate window.  Do not let a
	// player preference that disables ordinary spell use strand the AI in that
	// window: it still has to search for a legal follow-up (or decline below).
	if(metamagicFollowup)
		allowSpells = true;
	//Get all spells we can cast
	struct SpellOption
	{
		const CSpell * spell = nullptr;
		bool metamagicGrand = false;
	};
	std::vector<SpellOption> possibleSpells;

	for(const auto metamagicGrand : metamagicGrandChoices)
		for(auto const & s : LIBRARY->spellh->objects)
			if(allowSpells && s->canBeCast(cb->getBattle(battleID).get(), spells::Mode::HERO, hero, metamagicGrand))
				possibleSpells.push_back({s.get(), metamagicGrand});

	LOGFL("I can cast %d spells.", possibleSpells.size());

	vstd::erase_if(possibleSpells, [](const SpellOption & option)
	{
		return spellType(option.spell) != SpellTypes::BATTLE && !isCounterspell(option.spell);
	});

	LOGFL("I know how %d of them works.", possibleSpells.size());

	//Get viable spell-target pairs
	std::vector<PossibleSpellcast> possibleCasts;
	for(const auto & spellOption : possibleSpells)
	{
		const auto * spell = spellOption.spell;
		const bool metamagicGrandChoice = spellOption.metamagicGrand;
		if(isCounterspell(spell))
		{
			// Counterspell has a no-target cast and no ordinary spell effect to
			// project.  Give it a dedicated threat score instead of allowing the
			// generic effect evaluator to treat the ward as a zero-value cast.
			const auto value = counterspellThreatValue(*cb->getBattle(battleID), side, hero, spell);
			if(value <= 0.0f)
				continue;

			PossibleSpellcast ps;
			ps.spell = spell;
			ps.dest = {spells::Destination()};
			ps.metamagicFollowup = metamagicFollowup;
			ps.metamagicGrand = metamagicGrandChoice;
			ps.value = value;
			possibleCasts.push_back(std::move(ps));
			continue;
		}

		const int maxOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
			cb->getBattle(battleID)->getBattle()->getMagicRules(), spell->getId(), hero->getEffectPower(spell),
			newHorizonsMagic::magicArrowOverchargeModifiers(hero));
		const bool canUseSelectiveDispel = spell->getId() == SpellID::DISPEL
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel");
		const bool canUseTemporalField = spell->getId() == SpellID::SLOW
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField")
			&& !cb->getBattle(battleID)->battleWasTemporalFieldUsed(side);

		for(const bool massSlow : {false, true})
		{
			if(massSlow && !canUseTemporalField)
				continue;
			for(const bool selectiveDispel : {false, true})
			{
				if(selectiveDispel && !canUseSelectiveDispel)
					continue;
				spells::BattleCast temp(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
				temp.setMetamagicFollowup(metamagicFollowup);
				temp.setMetamagicGrand(metamagicGrandChoice);
				temp.setMassSlow(massSlow);
				temp.setSelectiveDispel(selectiveDispel);
				for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
				{
					for(int overcharge = 0; overcharge <= maxOvercharge; ++overcharge)
					{
						spells::BattleCast candidateCast(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
						candidateCast.setMetamagicFollowup(metamagicFollowup);
						candidateCast.setMetamagicGrand(metamagicGrandChoice);
						if(!target.empty() && target.front().unitValue)
							candidateCast.setMetamagicTargetUnitId(target.front().unitValue->unitId());
						candidateCast.setOvercharge(overcharge);
						candidateCast.setMassSlow(massSlow);
						candidateCast.setSelectiveDispel(selectiveDispel);
						auto candidateMechanics = spell->battleMechanics(&candidateCast);
						spells::detail::ProblemImpl problem;
						if(!candidateMechanics->canBeCast(problem))
							continue;

						PossibleSpellcast ps;
						ps.dest = target;
						// NO_LOCATION is represented on the wire by one invalid
						// destination.  Keep a concrete sentinel in the hypothetical
						// cast too: BattleSpellMechanics::castEval intentionally rejects
						// an entirely empty aim, while mass effects use the invalid
						// destination to collect every eligible unit.
						if(massSlow && ps.dest.empty())
							ps.dest.emplace_back(BattleHex::INVALID);
						ps.spell = spell;
						ps.metamagicFollowup = metamagicFollowup;
						ps.metamagicGrand = metamagicGrandChoice;
						ps.spellOvercharge = overcharge;
						ps.spellSelectiveDispel = selectiveDispel;
						ps.spellMassSlow = massSlow;
						if(isCanonicalLandMine(*cb->getBattle(battleID), spell))
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::landMinePlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
						if(isCanonicalFireWall(*cb->getBattle(battleID), spell))
						{
							ps.spellFireWallDirection = fireWallDirection(ps.dest);
							if(ps.spellFireWallDirection == BattleHex::NONE)
								continue;
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::fireWallPlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							// A zero-value line is either exposed to friendly ground or has no
							// reachable hostile pressure. Do not let the generic hypothetical
							// cast evaluator turn such a delayed placement into an accidental
							// positive action.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalTimeStop(spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::timeStopPlacementValue(
								candidateMechanics.get(), ps.dest);
							// Time Stop is neutral by content definition, so the generic
							// target comparer cannot tell a helpful enemy footprint from
							// a harmful healthy-ally footprint.  Require a strictly
							// beneficial placement before exposing it to action ranking.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						possibleCasts.push_back(ps);
					}
				}
			}
		}
	}
	// Commands compete in the same exchange evaluation as legal spell/target pairs
	// only for an ordinary hero action.  While a Metamagic sequence is pending,
	// the server rejects every other action until a follow-up or explicit decline
	// resolves it.
	if(!metamagicFollowup)
	{
	// Side-wide Orders use the authoritative availability query.  Targeted Orders
	// are enumerated through the callback's legal target-set query, with no local
	// guess about action budget, ownership, or current-round state.
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		if(cb->getBattle(battleID)->battleCanUseHeroCommand(side, command))
		{
			PossibleSpellcast candidate;
			candidate.command = command;
			if(heroCommands::isCanonicalRules(cb->getBattle(battleID)->getBattle()->getHeroCommandRules()))
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, {});
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(candidate);
		}
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
	{
		for(const auto & targetIds : orderTargetOptions(*cb->getBattle(battleID), side, command))
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, command, targetIds))
				continue;

			PossibleSpellcast candidate;
			candidate.command = command;
			candidate.commandTargets = targetIds;
			if(command == HeroCommand::FOCUS_FIRE)
			{
				if(targetIds.size() != 1)
					continue;
				candidate.focusFire = cb->getBattle(battleID)->battlePrepareFocusFireState(side, targetIds.front());
				if(!candidate.focusFire)
					continue;
				const auto & recipients = candidate.focusFire->recipientUnitIds;
				const bool hasRemainingShooter = std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
				{
					const auto * unit = cb->getBattle(battleID)->battleGetUnitByID(id);
					return unit && unit->willMove(0) && unit->canShoot();
				});
				if(!hasRemainingShooter)
					continue;
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, targetIds);
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			else
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, targetIds);
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(std::move(candidate));
		}
	}
	}
	LOGFL("Found %d spell-target combinations.", possibleCasts.size());
	if(possibleCasts.empty())
	{
		if(metamagicFollowup)
		{
			LOGL("No legal Metamagic follow-up remains; declining the sequence.");
			cb->battleMakeSpellAction(battleID, BattleAction::makeMetamagicDecline(side));
			activeActionMade = true;
			return true;
		}
		return false;
	}

	using ValueMap = PossibleSpellcast::ValueMap;

	auto evaluateQueue = [&](ValueMap & values, const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state, size_t minTurnSpan, bool * enemyHadTurnOut) -> bool
	{
		bool firstRound = true;
		bool enemyHadTurn = false;
		size_t ourTurnSpan = 0;

		bool stop = false;

		for(auto & round : queue)
		{
			if(!firstRound)
				state->nextRound();
			for(auto queuedUnit : round)
			{
				const auto * unit = state->battleGetUnitByID(queuedUnit->unitId());
				if(!unit)
					continue;
				if(!vstd::contains(values, unit->unitId()))
					values[unit->unitId()] = 0;

				if(!unit->alive())
					continue;

				if(state->battleGetOwner(unit) != playerID)
				{
					enemyHadTurn = true;

					if(!firstRound || state->battleCastSpells(unit->unitSide()) == 0)
					{
						//enemy could counter our spell at this point
						//anyway, we do not know what enemy will do
						//just stop evaluation
						stop = true;
						break;
					}
				}
				else if(!enemyHadTurn)
				{
					ourTurnSpan++;
				}

				state->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);

				PotentialTargets potentialTargets(unit, damageCache, state);

				if(!potentialTargets.possibleAttacks.empty())
				{
					AttackPossibility attackPossibility = potentialTargets.bestAction();

					auto stackWithBonuses = state->getForUpdate(unit->unitId());
					const bool attackerWasAlive = stackWithBonuses->alive();
					*stackWithBonuses = *attackPossibility.attackerState;
					state->recordBloodrageTransition(stackWithBonuses, attackerWasAlive);

					if(attackPossibility.defenderDamageReduce > 0)
					{
						stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
						stackWithBonuses->removeUnitBonus(Bonus::UntilOwnAttack);
					}
					if(attackPossibility.attackerDamageReduce > 0)
						stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);

					for(auto affected : attackPossibility.affectedUnits)
					{
						stackWithBonuses = state->getForUpdate(affected->unitId());
						const bool affectedWasAlive = stackWithBonuses->alive();
						*stackWithBonuses = *affected;
						state->recordBloodrageTransition(stackWithBonuses, affectedWasAlive);

						if(attackPossibility.defenderDamageReduce > 0)
							stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);
						if(attackPossibility.attackerDamageReduce > 0 && attackPossibility.attack.defender->unitId() == affected->unitId())
							stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
					}
				}

				auto bav = potentialTargets.bestActionValue();

				//best action is from effective owner`s point if view, we need to convert to our point if view
				if(state->battleGetOwner(unit) != playerID)
					bav = -bav;
				values[unit->unitId()] += bav;
				state->getForUpdate(unit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			}

			firstRound = false;

			if(stop)
				break;
		}

		if(enemyHadTurnOut)
			*enemyHadTurnOut = enemyHadTurn;

		return ourTurnSpan >= minTurnSpan;
	};

	ValueMap valueOfStack;
	ValueMap healthOfStack;

	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	size_t ourRemainingTurns = 0;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
		valueOfStack[unit->unitId()] = 0;

		if(cb->getBattle(battleID)->battleGetOwner(unit) == playerID && unit->canMove() && !unit->moved())
			ourRemainingTurns++;
	}

	LOGFL("I have %d turns left in this round", ourRemainingTurns);

	const bool castNow = ourRemainingTurns <= 1;

	if(castNow)
		print("I should try to cast a spell now");
	else
		print("I could wait better moment to cast a spell");

	auto amount = all.size();

	std::vector<battle::Units> turnOrder;

	cb->getBattle(battleID)->battleGetTurnOrder(turnOrder, amount, 2); //no more than 1 turn after current, each unit at least once

	{
		bool enemyHadTurn = false;

		auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));

		evaluateQueue(valueOfStack, turnOrder, state, 0, &enemyHadTurn);

		if(!enemyHadTurn)
		{
			auto battleIsFinishedOpt = state->battleIsFinished();

			if(battleIsFinishedOpt && !metamagicFollowup)
			{
				print("No need to cast a spell. Battle will finish soon.");
				return false;
			}
		}
	}

	CStopWatch timer;

#if BATTLE_TRACE_LEVEL >= 1
	tbb::blocked_range<size_t> r(0, possibleCasts.size());
#else
	tbb::parallel_for(tbb::blocked_range<size_t>(0, possibleCasts.size()), [&](const tbb::blocked_range<size_t> & r)
		{
#endif
			for(auto i = r.begin(); i != r.end(); i++)
			{
				auto & ps = possibleCasts[i];
				if(isCounterspell(ps.spell))
					continue;
				// Canonical Land Mine deliberately has no immediate unit-health delta:
				// its value is the pressure it places on hostile ground approaches.
				// Keep that deterministic live-snapshot score instead of allowing the
				// generic hypothetical cast path to collapse every legal placement to
				// zero merely because the mine has not triggered yet.
				if(ps.command == HeroCommand::NONE && ps.spellPlacementHeuristicValue > 0.0f)
				{
					// A delayed mine still consumes the hero exchange; preserve the
					// same valid best-attack baseline used by contextual Orders so a
					// placement heuristic is compared against an ordinary action on
					// the shared BattleAI scale.
					const auto baseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
						? cachedAttack.score : 0.0f;
					ps.value = baseline + ps.spellPlacementHeuristicValue;
					continue;
				}
				// Contextual Orders have no faithful projection in the old
				// hypothetical-battle model: targeted Orders and trigger/relationship
				// state carry more information than ordinary unit bonuses.  Their
				// deterministic read-only value was computed while enumerating the
				// authoritative legal target set.  Keep that score intact so they
				// still compete with spells and normal attacks.
				if(ps.command != HeroCommand::NONE && ps.commandHeuristicValue > 0.0f)
				{
					// An Order consumes the hero's exchange but does not replace the
					// unit action that follows it.  Keep the normal best-action score
					// in the candidate value so contextual Orders compete on the same
					// scale as spells and ordinary attacks rather than being treated as
					// a small, standalone bonus.
					const auto baseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
						? cachedAttack.score : 0.0f;
					ps.value = baseline + ps.commandHeuristicValue;
					continue;
				}

#if BATTLE_TRACE_LEVEL >= 1
				if(ps.dest.empty())
					logAi->trace("Evaluating %s", ps.name());
				else
				{
					auto psFirst = ps.dest.front();
					auto strWhere = psFirst.unitValue ? psFirst.unitValue->getDescription() : std::to_string(psFirst.hexValue.toInt());

					logAi->trace("Evaluating %s at %s", ps.name(), strWhere);
				}
#endif

				auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));

				if(ps.command == HeroCommand::NONE)
				{
					spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, ps.spell);
					cast.setMetamagicFollowup(ps.metamagicFollowup);
					cast.setMetamagicGrand(ps.metamagicGrand);
					if(!ps.dest.empty() && ps.dest.front().unitValue)
						cast.setMetamagicTargetUnitId(ps.dest.front().unitValue->unitId());
					cast.setOvercharge(ps.spellOvercharge);
					cast.setSelectiveDispel(ps.spellSelectiveDispel);
					cast.setMassSlow(ps.spellMassSlow);
					cast.castEval(state->getServerCallback(), ps.dest);
				}
				else if(ps.command == HeroCommand::FOCUS_FIRE)
				{
					state->setFocusFireState(side, ps.focusFire.value());
				}
				else
				{
					const auto effects = heroCommands::bonuses(state->getHeroCommandRules(), ps.command, *hero);
					for(const auto * unit : state->battleGetAllStacks(true))
					{
						if(state->battleGetOwner(unit) != playerID)
							continue;
						if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
							state->addUnitBonus(unit->unitId(), effects);
					}
				}

				// Removed sacrifice victims must remain in the health accounting below.
				auto allUnits = state->battleGetUnitsIf([](const battle::Unit * u) -> bool { return !u->isTurret(); });
				const bool transfigureMatter = isTransfigureMatter(ps.spell);

				auto needFullEval = ps.command == HeroCommand::FOCUS_FIRE
					|| state->hasObstacleChanges() || state->hasWallChanges()
					|| vstd::contains_if(allUnits, [&](const battle::Unit * u) -> bool
					{
						auto original = cb->getBattle(battleID)->battleGetUnitByID(u->unitId());
						return !original || u->getMovementRange() != original->getMovementRange()
							|| (ps.spell && ps.spell->getId() == SpellID::SLOW
								&& u->getInitiative() != original->getInitiative())
							|| u->getPosition() != original->getPosition()
							|| u->alive() != original->alive() || u->isGhost() != original->isGhost();
					});

				DamageCache safeCopy = damageCache;
				DamageCache innerCache(&safeCopy);

				innerCache.buildDamageCache(state, side);

				if(cachedAttack.ap && cachedAttack.waited)
				{
					state->makeWait(activeStack);
				}

				float stackActionScore = 0;
				float damageToHostilesScore = 0;
				float damageToFriendliesScore = 0;
				float initiativeEffectScore = 0;

				const auto modelActive = state->getForUpdate(activeStack->unitId());
				if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
				{
#if BATTLE_TRACE_LEVEL >= 1
					logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

					PotentialTargets innerTargets(modelActive.get(), innerCache, state);
					BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

					innerEvaluator.updateReachabilityMap(state);

					auto moveTarget = innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state);

					if(!innerTargets.possibleAttacks.empty())
					{
						auto newStackAction = innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state);

						stackActionScore = std::max(moveTarget.score, newStackAction.score);
					}
					else
					{
						stackActionScore = moveTarget.score;
					}
				}
				else if(modelActive->alive())
				{
					auto updatedAttacker = state->getForUpdate(cachedAttack.ap->attack.attacker->unitId());
					auto updatedDefender = state->getForUpdate(cachedAttack.ap->attack.defender->unitId());
					auto updatedBai = BattleAttackInfo(
						updatedAttacker.get(),
						updatedDefender.get(),
						cachedAttack.ap->attack.chargeDistance,
						cachedAttack.ap->attack.shooting);

					auto updatedAttack = AttackPossibility::evaluate(updatedBai, cachedAttack.ap->from, innerCache, state);

					BattleExchangeEvaluator innerEvaluator(scoreEvaluator);

					stackActionScore = innerEvaluator.evaluateExchange(updatedAttack, cachedAttack.turn, *targets, innerCache, state);
				}
				for(const auto & unit : allUnits)
				{
					if(!unit->isValidTarget(true) && !vstd::contains(healthOfStack, unit->unitId()))
						continue;

					auto newHealth = unit->getAvailableHealth();
					auto oldHealth = vstd::find_or(healthOfStack, unit->unitId(), 0); // old health value may not exist for newly summoned units
					auto original = cb->getBattle(battleID)->battleGetUnitByID(unit->unitId());
					if(transfigureMatter && !original && unit->unitType()
						&& unit->unitType()->getJsonKey() == "core:diamondGolem"
						&& state->battleGetOwner(unit) == playerID && newHealth > 0)
					{
						// A Transfigure Matter cast creates temporary Diamond Golems. Use
						// their projected partial-stack health and normal creature AI
						// value, rather than dropping all magical summons from scoring.
						const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
						damageToHostilesScore += static_cast<float>(newHealth)
							* static_cast<float>(unit->unitType()->getAIValue())
							/ static_cast<float>(maxHealth);
					}
					if(ps.spell && ps.spell->getId() == SpellID::SLOW
						&& original && original->alive() && unit->alive())
					{
						const int oldInitiative = std::max(1, original->getInitiative());
						const int initiativeDelta = unit->getInitiative() - original->getInitiative();
						const int signedDelta = state->battleGetOwner(unit) == playerID
							? initiativeDelta : -initiativeDelta;
						const float stackValue = static_cast<float>(unit->getCount()) * unit->unitType()->getAIValue();
						initiativeEffectScore += stackValue * static_cast<float>(signedDelta)
							/ static_cast<float>(oldInitiative) * 0.01f;
					}

					if(oldHealth != newHealth)
					{
						auto damage = std::abs(oldHealth - newHealth);
						auto originalDefender = original;

						auto dpsReduce = AttackPossibility::calculateDamageReduce(
							nullptr,
							originalDefender && originalDefender->alive() ? originalDefender : unit,
							damage,
							innerCache,
							state);

						const bool ourUnit = state->battleGetOwner(unit) == playerID;
						const bool goodEffect = newHealth > oldHealth;

						if(ourUnit == goodEffect)
						{
							auto isMagical = state->getForUpdate(unit->unitId())->summoned
								|| unit->isClone()
								|| unit->isGhost();

							if(ourUnit && goodEffect && isMagical)
								continue;

							damageToHostilesScore += dpsReduce * scoreEvaluator.getPositiveEffectMultiplier();
						}
						else
							// discourage AI making collateral damage with spells
							damageToFriendliesScore -= 4 * dpsReduce * scoreEvaluator.getNegativeEffectMultiplier();

#if BATTLE_TRACE_LEVEL >= 1
						// Ensure ps.dest is not empty before accessing the first element
						if (!ps.dest.empty()) 
						{
							logAi->trace(
								"Spell %s to %d affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								ps.dest.at(0).hexValue.toInt(),  // Safe to access .at(0) now
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
						else 
						{
							// Handle the case where ps.dest is empty
							logAi->trace(
								"Spell %s has no destination, affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
#endif
					}
				}

				if (vstd::isAlmostEqual(stackActionScore, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)))
				{
					ps.value = damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}
				else
				{
					ps.value = stackActionScore + damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("Total score for %s: %2f (action: %2f, friedly damage: %2f, hostile damage: %2f)", ps.name(), ps.value, stackActionScore, damageToFriendliesScore, damageToHostilesScore);
#endif
			}
#if BATTLE_TRACE_LEVEL == 0
		});
#endif

	// Grand Metamagic buys a second follow-up, so its first-cast candidate must
	// include the value of a legal second spell.  The ordinary hypothetical
	// evaluation above already gives us the best current-state value for every
	// spell/target pair; use the best positive marginal as a conservative
	// continuation estimate.  Repeated spells remain legal here; Perfect
	// Sequence changes their power, not their availability.  A distinct second
	// spell is the opportunity signal for spending Grand: an ordinary repeat is
	// already available without consuming that once-per-battle reserve.
	if(metamagicFollowup && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND))
	{
		const auto & sequence = cb->getBattle(battleID)->battleMetamagicSequenceSpells(side);
		const auto firstSpell = sequence.size() == 1 ? sequence.front() : SpellID();
		for(auto & first : possibleCasts)
		{
			if(!first.metamagicGrand || !first.spell)
				continue;

			float bestContinuation = 0.0f;
			for(const auto & second : possibleCasts)
			{
				if(second.metamagicGrand || !second.spell)
					continue;
				// Keep Grand in reserve when its only projected continuation is
				// the same spell that ordinary Metamagic can already repeat.  A
				// distinct legal continuation is the opportunity that makes
				// spending the once-per-battle Grand charge worthwhile.
				if(firstSpell.hasValue() && second.spell->getId() == firstSpell)
					continue;
				bestContinuation = std::max(bestContinuation, second.value - cachedAttack.score);
			}
			if(bestContinuation > 0.0f)
				first.value += bestContinuation;
		}
	}

	LOGFL("Evaluation took %d ms", timer.getDiff());

	auto castToPerform = *vstd::maxElementByFun(possibleCasts, [](const PossibleSpellcast & ps) -> float
		{
			return ps.value;
		});
	if(metamagicFollowup
		&& (castToPerform.value < cachedAttack.score
			|| vstd::isAlmostEqual(castToPerform.value, cachedAttack.score)))
	{
		// Decline is the no-cast baseline, not literal score zero: hypothetical
		// spell values include the active exchange's projected attack. A legal
		// follow-up can therefore be positive in absolute terms while still
		// being harmful relative to leaving the active exchange untouched.
		LOGL("All Metamagic follow-ups are no better than declining; ending sequence.");
		cb->battleMakeSpellAction(battleID, BattleAction::makeMetamagicDecline(side));
		activeActionMade = true;
		return true;
	}

	if(metamagicFollowup || (castToPerform.value > cachedAttack.score && !vstd::isAlmostEqual(castToPerform.value, cachedAttack.score)))
	{
		LOGFL("Best hero action is %s (value %d). Will perform.", castToPerform.name() % castToPerform.value);
		if(castToPerform.command != HeroCommand::NONE)
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, castToPerform.command,
				castToPerform.commandTargets))
				return false;

			if(castToPerform.command == HeroCommand::FOCUS_FIRE)
			{
				const auto targetId = castToPerform.focusFire.value().targetUnitId;
				if(!cb->getBattle(battleID)->battleCanConfirmHeroCommand(side, castToPerform.command, targetId))
					return false;
				cb->battleMakeSpellAction(battleID,
					BattleAction::makeTargetedHeroCommand(side, castToPerform.command, targetId));
			}
			else
			{
				BattleAction action;
				if(castToPerform.commandTargets.size() == 1)
					action = BattleAction::makeTargetedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front());
				else if(castToPerform.commandTargets.size() == 2)
					action = BattleAction::makePairedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front(), castToPerform.commandTargets.back());
				else
					action = BattleAction::makeHeroCommand(side, castToPerform.command);
				cb->battleMakeSpellAction(battleID, action);
			}
			activeActionMade = true;
			return true;
		}
		BattleAction spellcast;
		spellcast.actionType = EActionType::HERO_SPELL;
		spellcast.spell = castToPerform.spell->id;
		spellcast.spellOvercharge = castToPerform.spellOvercharge;
		spellcast.spellSelectiveDispel = castToPerform.spellSelectiveDispel;
		spellcast.spellMassSlow = castToPerform.spellMassSlow;
		spellcast.metamagicFollowup = castToPerform.metamagicFollowup;
		spellcast.metamagicGrand = castToPerform.metamagicGrand;
		if(isCanonicalFireWall(*cb->getBattle(battleID), castToPerform.spell)
			&& castToPerform.spellFireWallDirection != BattleHex::NONE
			&& !castToPerform.dest.empty())
		{
			// Evaluation keeps the complete footprint so delayed damage and
			// friendly exposure can be scored.  The wire action intentionally
			// carries only the start hex plus the direction; the server expands
			// and validates the line authoritatively.
			spellcast.aimToHex(castToPerform.dest.front().hexValue);
			spellcast.spellFireWallDirection = castToPerform.spellFireWallDirection;
		}
		else
			spellcast.setTarget(castToPerform.dest);
		spellcast.side = side;
		spellcast.stackNumber = -1;
		cb->battleMakeSpellAction(battleID, spellcast);
		activeActionMade = true;

		return true;
	}

	LOGFL("Best hero action is %s. But it is actually useless (value %d).", castToPerform.name() % castToPerform.value);

	return false;
}

//Below method works only for offensive spells
void BattleEvaluator::evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps)
{
	using ValueMap = PossibleSpellcast::ValueMap;

	RNGStub rngStub;
	HypotheticBattle state(env.get(), cb->getBattle(battleID));
	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	ValueMap healthOfStack;
	ValueMap newHealthOfStack;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
	}


	spells::BattleCast cast(&state, stack, spells::Mode::CREATURE_ACTIVE, ps.spell);
	cast.castEval(state.getServerCallback(), ps.dest);

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);
		newHealthOfStack[unitId] = localUnit->getAvailableHealth();
	}

	int64_t totalGain = 0;

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);

		auto healthDiff = newHealthOfStack[unitId] - healthOfStack[unitId];

		if(state.battleGetOwner(localUnit) != playerID)
			healthDiff = -healthDiff;

		if(healthDiff < 0)
		{
			ps.value = -1;
			return; //do not damage own units at all
		}

		totalGain += healthDiff;
	}

	// consider the case in which spell summons units
	auto newUnits = state.getUnitsIf([&](const battle::Unit * u) -> bool
		{
			return !u->isGhost() && !u->isTurret() && !vstd::contains(healthOfStack, u->unitId());
		});

	for(auto unit : newUnits)
	{
		const auto health = unit->getAvailableHealth();
		totalGain += state.battleGetOwner(unit) == playerID ? health : -health;
	}

	ps.value = totalGain;
}

void BattleEvaluator::print(const std::string & text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}
