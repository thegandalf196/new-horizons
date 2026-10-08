/*
 * CBattleInfoCallback.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"

#include <vcmi/scripting/Service.h>
#include <vstd/RNG.h>
#include <unordered_map>

#include "../CStack.h"
#include "BattleInfo.h"
#include "CObstacleInstance.h"
#include "CUnitState.h"
#include "NewHorizonsBulwark.h"
#include "NewHorizonsBattlecraft.h"
#include "NewHorizonsArchery.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsOffense.h"
#include "NewHorizonsShroud.h"
#include "NewHorizonsWarcasting.h"
#include "NewHorizonsBloodrage.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "NewHorizonsDiscipline.h"
#include "NewHorizonsDivineMandate.h"
#include "NewHorizonsPuppetMaster.h"
#include "IGameSettings.h"
#include "PossiblePlayerBattleAction.h"
#include "../bonuses/BonusParameters.h"
#include "../entities/building/TownFortifications.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../GameLibrary.h"
#include "../combatScripts/IDamageCalculatorScript.h"
#include "../scripting/ScriptService.h"
#include "../spells/ObstacleCasterProxy.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/Problem.h"
#include "../spells/CSpell.h"
#include "../mapObjects/CGTownInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../BattleFieldHandler.h"
#include "../Rect.h"
#include "../spells/effects/Effect.h"

namespace
{
constexpr int ELVEN_PRECISION_DEFENSE_IGNORE_PERCENT = 25;
constexpr int LUCKY_AIM_DEFENSE_IGNORE_PERCENT = 25;
constexpr int SHOCK_ASSAULT_DEFENSE_IGNORE_PERCENT = 25;
constexpr int EXECUTIONER_DAMAGE_PERCENT = 20;
constexpr int ARMOR_PIERCER_DEFENSE_IGNORE_PERCENT = 20;
constexpr int BREAKTHROUGH_DAMAGE_REDUCTION_IGNORE_PERCENT = 50;
constexpr int PIERCING_BOLTS_DEFENSE_IGNORE_PERCENT = 50;
constexpr int FORTIFICATION_ENGINEER_SIEGE_PERCENT = 125;
constexpr auto HOLY_ARMOR_SAVED_ROSTER_KEY = "new-horizons:holyArmor";
constexpr auto WARCASTING_SKILL_ID = "new-horizons:warcasting";
constexpr auto SPELLWARD_PERK_ID = "new-horizons:warcasting.spellward";
constexpr int SPELLWARD_REDUCTION_BASIS_POINTS = 1000;
constexpr int NEW_HORIZONS_MAGIC_RESISTANCE_CAP_PERCENT = 75;

int64_t scaledBattleOutput(int64_t base, int32_t outputPercent)
{
	if(base <= 0 || outputPercent <= 0)
		return 0;
	return (base / 100) * outputPercent + ((base % 100) * outputPercent) / 100;
}

int64_t firstAidHealingBase(const CGHeroInstance * owner)
{
	if(!owner)
		return 0;
	if(const auto siege = owner->getSiegeCapabilities();
		siege && owner->getCapabilityRules()["rulesetVersion"].Integer() >= 3)
		return siege->firstAidHealing;
	return owner->valOfBonuses(BonusType::MANUAL_CONTROL,
		BonusSubtypeID(CreatureID(CreatureID::FIRST_AID_TENT)));
}

LuckRollRules battleLuckRules(const IBattleInfo & battle)
{
	auto rules = battle.getLuckRollRules();
	if(rules.diceSize == 0)
	{
		const auto & settings = *LIBRARY->engineSettings();
		rules.goodChance = settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
		rules.badChance = settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
		rules.diceSize = settings.getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE);
		rules.affectsAllTargets = settings.getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS);
	}
	return rules;
}

/// Order targeting is based on occupied hexes, not a unit's primary position.
/// This matters for double-wide stacks: their rear hex may be the one actually
/// touching the ward or presenting a flank.
bool orderUnitsAdjacent(const battle::Unit * first, const battle::Unit * second,
	const BattleHex & firstPosition = BattleHex::INVALID, const BattleHex & secondPosition = BattleHex::INVALID)
{
	if(!first || !second)
		return false;
	const auto & firstHexes = firstPosition.isValid() ? first->getHexes(firstPosition) : first->getHexes();
	const auto & secondHexes = secondPosition.isValid() ? second->getHexes(secondPosition) : second->getHexes();
	for(const auto & firstHex : firstHexes)
	{
		if(!firstHex.isValid())
			continue;
		for(const auto & secondHex : secondHexes)
			if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	}
	return false;
}

uint8_t orderContactingSideMask(const battle::Unit * attacker, const battle::Unit * defender,
	const BattleHex & attackerPosition = BattleHex::INVALID, const BattleHex & defenderPosition = BattleHex::INVALID)
{
	if(!attacker || !defender)
		return 0;
	uint8_t sideMask = 0;
	const auto & attackerHexes = attackerPosition.isValid() ? attacker->getHexes(attackerPosition) : attacker->getHexes();
	const auto & defenderHexes = defenderPosition.isValid() ? defender->getHexes(defenderPosition) : defender->getHexes();
	for(const auto & attackerHex : attackerHexes)
	{
		if(!attackerHex.isValid())
			continue;
		for(const auto & defenderHex : defenderHexes)
		{
			if(!defenderHex.isValid() || BattleHex::getDistance(defenderHex, attackerHex) != 1)
				continue;
			const auto direction = BattleHex::mutualPosition(defenderHex, attackerHex);
			if(direction >= BattleHex::TOP_LEFT && direction <= BattleHex::LEFT)
				sideMask |= static_cast<uint8_t>(1u << static_cast<unsigned>(direction));
		}
	}
	return sideMask;
}
}

std::optional<newHorizonsCreatures::CreatureCategoryView> CBattleInfoCallback::battleGetCreatureCategory(CreatureID creature) const
{
	const auto * battle = getBattle();
	if(!battle)
		return std::nullopt;
	return newHorizonsCreatures::creatureCategoryView(battle->getCreatureCategoryRules(), creature);
}

bool CBattleInfoCallback::battleCanUseFortificationEngineer(const battle::Unit * turret) const
{
	if(!turret || !getBattle() || !turret->alive() || turret->isGhost() || !turret->isTurret()
		|| turret->unitSlot() != SlotID::ARROW_TOWERS_SLOT || turret->unitSide() != BattleSide::DEFENDER
		|| playerToSide(battleGetOwner(turret)) != BattleSide::DEFENDER)
		return false;

	const auto * town = battleGetDefendedTown();
	if(!town || !town->hasFort() || !hasFortifications())
		return false;

	const auto * hero = battleGetOwnerHero(turret);
	if(!hero || !newHorizonsHeroes::usesRules(hero->getCapabilityRules())
		|| hero->getCapabilityRules()["rulesetVersion"].Integer() < 3)
		return false;

	return hero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.fortificationEngineer");
}

bool CBattleInfoCallback::battleHasPendingRangedFollowUp(const battle::Unit * unit) const
{
	const auto * battle = getBattle();
	if(!unit || !battle || !unit->isBallista() || battle->getActiveStackID() < 0
		|| unit->unitId() != static_cast<uint32_t>(battle->getActiveStackID()))
		return false;

	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	return state && state->rangedFollowUpDamagePercent > 0 && state->rangedFollowUpDamagePercent <= 100;
}

ReducedExtraActivationState CBattleInfoCallback::battleGetReducedExtraActivationState(BattleSide side) const
{
	const auto * battle = getBattle();
	if(!battle || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {};
	return battle->getReducedExtraActivationState(side);
}

int32_t CBattleInfoCallback::battleGetActivationOutputPercent(const battle::Unit * unit) const
{
	const auto * battle = getBattle();
	if(!battle || !unit)
		return 100;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = battle->getReducedExtraActivationState(side);
		if(state.activeUnitId == unit->unitId())
			return state.outputPercent;
	}
	return 100;
}

int64_t CBattleInfoCallback::battleGetFirstAidHealingOutput(const battle::Unit * healer) const
{
	if(!healer || !healer->isFirstAidTent())
		return 0;
	return scaledBattleOutput(firstAidHealingBase(battleGetOwnerHero(healer)),
		battleGetActivationOutputPercent(healer));
}

int32_t CBattleInfoCallback::battleGetCatapultStructuralDamage(
	const battle::Unit * attacker, int32_t hitQuality) const
{
	// This callback is called at the catapult boundary, where the machine type
	// is already known. Some legacy adapters provide a lightweight Unit proxy
	// without creature identity, so do not rediscover its type here.
	if(!attacker || hitQuality <= 0)
		return 0;

	int64_t structuralOutput = std::min<int32_t>(hitQuality, 2);
	if(const auto * hero = battleGetOwnerHero(attacker);
		hero && hero->getCapabilityRules()["rulesetVersion"].Integer() >= 3)
	{
		if(const auto siege = hero->getSiegeCapabilities())
			structuralOutput *= std::max(0, siege->catapultStructuralDamage);
	}
	structuralOutput = scaledBattleOutput(structuralOutput, battleGetActivationOutputPercent(attacker));
	return static_cast<int32_t>(std::clamp<int64_t>(structuralOutput, 0,
		std::numeric_limits<int32_t>::max()));
}

bool CBattleInfoCallback::battleCanTakeRangedFollowUp(const battle::Unit * unit) const
{
	if(!battleHasPendingRangedFollowUp(unit) || !unit->alive() || unit->isGhost()
		|| unit->isTimeStopped() || !unit->canMove() || !battleCanShoot(unit))
		return false;

	for(const auto * target : battleAliveUnits())
	{
		if(!target || !target->alive() || target->isGhost() || battleMatchOwner(unit, target, true))
			continue;
		for(const auto & hex : target->getHexes())
			if(hex.isValid() && battleCanShoot(unit, hex))
				return true;
	}
	return false;
}

int CBattleInfoCallback::battleGetRangedFollowUpDamagePercent(const battle::Unit * unit) const
{
	if(!unit || !unit->isBallista())
		return 100;

	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	if(!state || state->rangedFollowUpDamagePercent < 1 || state->rangedFollowUpDamagePercent > 100)
		return 100;
	return state->rangedFollowUpDamagePercent;
}

static BattleHex lineToWallHex(int line) //returns hex with wall in given line (y coordinate)
{
	static const BattleHex lineToHex[] = {12, 29, 45, 62, 78, 96, 112, 130, 147, 165, 182};

	return lineToHex[line];
}

static std::optional<std::pair<BattleHex, BattleHex>> getLongWeaponLineHexes(const BattleHex & defenderHex, BattleHex::EDir direction)
{
	try
	{
		BattleHex middleHex = defenderHex.cloneInDirection(direction, false);
		BattleHex attackerHex = middleHex.cloneInDirection(direction, false);
		return std::make_pair(middleHex, attackerHex);
	}
	catch(const std::out_of_range &)
	{
		return std::nullopt;
	}
}

/// Checks whether unit with free shooting may target units standing next to it. Unrestricted free
/// shooting, e.g. Bow of the Sharpshooter, takes priority over restricted one, e.g. Steel Elves
static bool canShootAdjacentUnits(const battle::Unit * attacker)
{
	static const auto restricted = Selector::typeSubtype(BonusType::FREE_SHOOTING, BonusCustomSubtype::freeShootingExceptAdjacent);
	static const auto unrestricted = Selector::type()(BonusType::FREE_SHOOTING).And(restricted.Not());

	return !attacker->hasBonus(restricted) || attacker->hasBonus(unrestricted);
}

static bool isLongWeaponMiddleHexClear(const CBattleInfoCallback & callback, const BattleHex & middleHex)
{
	if(!middleHex.isValid())
		return false;

	const auto accessibility = callback.getAccessibility();
	return accessibility[middleHex.toInt()] == EAccessibility::ACCESSIBLE;
}

static bool sameSideOfWall(const BattleHex & pos1, const BattleHex & pos2)
{
	const bool stackLeft = pos1 < lineToWallHex(pos1.getY());
	const bool destLeft = pos2 < lineToWallHex(pos2.getY());

	return stackLeft == destLeft;
}

// parts of wall
static const std::pair<int, EWallPart> wallParts[] =
{
	std::make_pair(50, EWallPart::KEEP),
	std::make_pair(183, EWallPart::BOTTOM_TOWER),
	std::make_pair(182, EWallPart::BOTTOM_WALL),
	std::make_pair(130, EWallPart::BELOW_GATE),
	std::make_pair(78, EWallPart::OVER_GATE),
	std::make_pair(29, EWallPart::UPPER_WALL),
	std::make_pair(12, EWallPart::UPPER_TOWER),
	std::make_pair(95, EWallPart::INDESTRUCTIBLE_PART_OF_GATE),
	std::make_pair(96, EWallPart::GATE),
	std::make_pair(45, EWallPart::INDESTRUCTIBLE_PART),
	std::make_pair(62, EWallPart::INDESTRUCTIBLE_PART),
	std::make_pair(112, EWallPart::INDESTRUCTIBLE_PART),
	std::make_pair(147, EWallPart::INDESTRUCTIBLE_PART),
	std::make_pair(165, EWallPart::INDESTRUCTIBLE_PART)
};

static EWallPart hexToWallPart(const BattleHex & hex)
{
	si16 hexValue = hex.toInt();
	for(const auto & elem : wallParts)
	{
		if(elem.first == hexValue)
			return elem.second;
	}

	return EWallPart::INVALID; //not found!
}

static BattleHex WallPartToHex(EWallPart part)
{
	for(const auto & elem : wallParts)
	{
		if(elem.second == part)
			return elem.first;
	}

	return BattleHex::INVALID; //not found!
}

bool CBattleInfoCallback::battleUsesHeroCommands() const
{
	return getBattle() && heroCommands::supportedByRules(getBattle()->getHeroCommandRules(), HeroCommand::CHARGE);
}

HeroCommand CBattleInfoCallback::battleGetActiveDoctrine(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return HeroCommand::NONE;
	return getBattle()->getActiveDoctrine(side);
}

HeroCommand CBattleInfoCallback::battleGetActiveOrder(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return HeroCommand::NONE;
	return getBattle()->getActiveOrder(side);
}

bool CBattleInfoCallback::battleHeroCommandCommonAvailable(BattleSide side, HeroCommand command) const
{
	if(!getBattle() || !heroCommands::supportedByRules(getBattle()->getHeroCommandRules(), command)
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return false;
	const auto & doubleCommand = getBattle()->getDoubleCommandState(side);
	if((doubleCommand.orderPending()
		&& (!battleHasPendingDoubleCommand(side) || command == doubleCommand.firstOrder))
		|| doubleCommand.secondWindReady())
		return false;
	const auto & preCombatOrder = getBattle()->getPreCombatOrderState(side);
	if(preCombatOrder.orderPending() && !battleHasPendingPreCombatOrder(side))
		return false;
	const bool canonical = heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules());
	const auto sameOrder = canonical ? battleGetHeroOrderState(side, command) : std::nullopt;
	const bool alreadyIssued = canonical
		? sameOrder && sameOrder->issuedRound == battleGetRound()
		: battleGetActiveOrder(side) != HeroCommand::NONE;
	if(battleTacticDist() || !battleGetFightingHero(side)
		|| battleGetActiveDoctrine(side) == command || alreadyIssued)
		return false;
	if(battleUsesHeroCommands())
	{
		const auto selected = battleGetOrderActionAllowance(side);
		if(!selected || (preCombatOrder.orderPending()
			&& selected->source != HeroActionAllowanceState::GrantSource::BATTLE_PLAN))
			return false;
	}
	else if(getBattle()->getHeroCommandUsed(side) || battleCastSpells(side) != 0)
		return false;
	const auto * active = battleActiveUnit();
	return active && battleGetOwner(active) == sideToPlayer(side);
}

bool CBattleInfoCallback::battleCanUseHeroCommand(BattleSide side, HeroCommand command) const
{
	if(command == HeroCommand::FOCUS_FIRE || command == HeroCommand::PROTECT
		|| command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND
		|| !battleHeroCommandCommonAvailable(side, command))
		return false;
	for(const auto * unit : battleGetAllStacks())
	{
		if(unit->alive() && battleGetOwner(unit) == sideToPlayer(side)
			&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			return true;
	}
	return false;
}

bool CBattleInfoCallback::battleUnitHasAmmoCart(const battle::Unit * unit) const
{
	if(!getBattle() || !unit)
		return false;
	const auto carts = battleGetUnitsIf([this, unit](const battle::Unit * candidate)
	{
		return candidate->isAmmoCart() && battleMatchOwner(candidate, unit, true);
	});
	// Preserve the original first-matching-cart policy, including a destroyed cart.
	// Hypothetical enumeration must retain real battle order, not sort by ID.
	if(!carts.empty())
		return carts.front()->alive();
	const auto * hero = battleGetOwnerHero(unit);
	if(!hero)
		return false;
	const auto artifact = hero->artifactsWorn.find(ArtifactPosition::MACH2);
	return artifact != hero->artifactsWorn.end()
		&& artifact->second.getArt()->getTypeId() == ArtifactID::AMMO_CART;
}

int CBattleInfoCallback::battleGetMagicResistance(const battle::Unit * unit) const
{
	if(!unit)
		return 0;

	int resistance = std::clamp(unit->valOfBonuses(BonusType::MAGIC_RESISTANCE), 0, 100);
	int auraResistance = 0;
	for(const auto * adjacent : battleAdjacentUnits(unit))
	{
		if(adjacent->unitOwner() == unit->unitOwner())
			auraResistance = std::max(auraResistance, adjacent->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA));
	}
	auraResistance = std::clamp(auraResistance, 0, 100);

	// Combine independent resistance sources using the same legacy composition,
	// with integer flooring rather than floating-point rounding.
	const int combinedResistance = (10000 - (100 - resistance) * (100 - auraResistance)) / 100;
	const auto * battleState = getBattle();
	const int maximumResistance = battleState
		&& newHorizonsMagic::rulesActive(battleState->getMagicRules())
		? NEW_HORIZONS_MAGIC_RESISTANCE_CAP_PERCENT
		: 100;
	return std::min(combinedResistance, maximumResistance);
}

bool CBattleInfoCallback::battleIsFocusFireRecipient(const battle::Unit * unit, BattleSide side) const
{
	return unit && unit->alive() && !unit->isGhost() && unit->isShooter()
		&& battleGetOwner(unit) == sideToPlayer(side) && !unit->isTurret()
		&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

bool CBattleInfoCallback::battleCanConfirmHeroCommand(BattleSide side, HeroCommand command, uint32_t targetUnitId) const
{
	return battleFocusFireTargetRejection(side, command, targetUnitId) == heroCommands::TargetRejection::NONE;
}

heroCommands::TargetRejection CBattleInfoCallback::battleFocusFireTargetRejection(
	BattleSide side, HeroCommand command, uint32_t targetUnitId) const
{
	using Reason = heroCommands::TargetRejection;
	if(command != HeroCommand::FOCUS_FIRE || !battleHeroCommandCommonAvailable(side, command)
		|| battleGetRound() < 1 || targetUnitId > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
		return Reason::UNAVAILABLE;
	const auto * target = battleGetUnitByID(targetUnitId);
	if(!target)
		return Reason::NO_STACK;
	if(!target->alive() || target->isGhost())
		return Reason::NOT_LIVING;
	if(!target->isValidTarget())
		return Reason::TARGET_UNAVAILABLE;
	if(target->isInvincible())
		return Reason::INVULNERABLE;
	if(battleGetOwner(target) == sideToPlayer(side))
		return Reason::ENEMY_REQUIRED;
	const auto * hero = battleGetFightingHero(side);
	const bool combinedArms = getBattle()
		&& heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		&& heroCommands::hasCombinedArms(hero);
	for(const auto * unit : battleAliveUnits())
	{
		// Shot legality is static: already-acted units do not disable issuing.
		if(battleIsFocusFireRecipient(unit, side) && battleCanShoot(unit, target->getPosition()))
			return Reason::NONE;
		// Combined Arms also lets a melee-only army mark an enemy. Keep the
		// same ordinary-unit exclusions as the saved cohort below.
		if(combinedArms && unit && unit->alive() && !unit->isGhost() && unit->isMeleeAttacker()
			&& battleGetOwner(unit) == sideToPlayer(side) && !unit->isTurret()
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER)
			return Reason::NONE;
	}
	return Reason::NO_FOCUS_RECIPIENT;
}

std::vector<uint32_t> CBattleInfoCallback::battleGetHeroCommandTargets(BattleSide side, HeroCommand command) const
{
	std::vector<uint32_t> result;
	if((command != HeroCommand::FOCUS_FIRE && command != HeroCommand::FLANK
		&& command != HeroCommand::PROTECT && command != HeroCommand::SECOND_WIND)
		|| !battleHeroCommandCommonAvailable(side, command))
		return result;
	for(const auto * unit : battleAliveUnits())
	{
		if(command == HeroCommand::PROTECT)
		{
			// This list is the legal candidate set for the two-step Protector -> Ward
			// selector. Pair validation remains authoritative in battlePrepare...().
			if(battleGetOwner(unit) == sideToPlayer(side) && !unit->isTurret()
				&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
				result.push_back(unit->unitId());
		}
		else if(command == HeroCommand::FOCUS_FIRE
			? battleCanConfirmHeroCommand(side, command, unit->unitId())
			: battlePrepareHeroOrderState(side, command, {unit->unitId()}).has_value())
			result.push_back(unit->unitId());
	}
	std::sort(result.begin(), result.end());
	return result;
}

bool CBattleInfoCallback::battleCanBeginHeroCommand(BattleSide side, HeroCommand command) const
{
	if(command == HeroCommand::FOCUS_FIRE || command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND)
		return !battleGetHeroCommandTargets(side, command).empty();
	if(command == HeroCommand::PROTECT)
	{
		const auto candidates = battleGetHeroCommandTargets(side, command);
		for(const auto protector : candidates)
			for(const auto ward : candidates)
				if(protector != ward && battlePrepareHeroOrderState(side, command, {protector, ward}))
					return true;
		return false;
	}
	return battleCanUseHeroCommand(side, command);
}

std::optional<FocusFireState> CBattleInfoCallback::battlePrepareFocusFireState(BattleSide side,
	uint32_t targetUnitId) const
{
	if(!battleCanConfirmHeroCommand(side, HeroCommand::FOCUS_FIRE, targetUnitId))
		return {};
	FocusFireState result;
	result.targetUnitId = targetUnitId;
	result.issuedRound = battleGetRound();
	const auto * hero = battleGetFightingHero(side);
	const auto & formula = getBattle()->getHeroCommandRules()["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	const auto allowance = battleGetOrderActionAllowance(side);
	const bool spendsHeroAllowance = !heroCommands::supportedByRules(getBattle()->getHeroCommandRules(), HeroCommand::CHARGE)
		|| (allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO);
	const auto warcastingBonus = newHorizonsWarcasting::enabled(getBattle()->getMagicRules())
		&& spendsHeroAllowance
		? newHorizonsWarcasting::orderBonus(hero, getBattle()->getWarcastingState(side), result.issuedRound) : 0;
	const auto sacredCommandBonus = allowance
		&& allowance->allowance == HeroActionAllowanceState::AllowanceKind::ORDER
		&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
		? newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(hero) : 0;
	const auto knightlySequenceBonus = allowance
		&& allowance->allowance == HeroActionAllowanceState::AllowanceKind::ORDER
		&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
		? newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(hero) : 0;
	result.rangedDamagePercent = heroCommands::coefficient(formula, *hero, warcastingBonus,
		sacredCommandBonus + knightlySequenceBonus);
	const bool includeMeleeRecipients = heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		&& heroCommands::hasCombinedArms(hero);
	const auto recipients = battleGetUnitsIf([this, side, includeMeleeRecipients](const battle::Unit * unit)
	{
		if(battleIsFocusFireRecipient(unit, side))
			return true;
		return includeMeleeRecipients && unit && unit->alive() && !unit->isGhost()
			&& unit->isMeleeAttacker() && battleGetOwner(unit) == sideToPlayer(side)
			&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
	});
	for(const auto * unit : recipients)
		result.recipientUnitIds.push_back(unit->unitId());
	std::sort(result.recipientUnitIds.begin(), result.recipientUnitIds.end());
	result.validateShape();
	return result;
}

std::optional<FocusFireState> CBattleInfoCallback::battleGetFocusFireState(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {};
	return getBattle()->getFocusFireState(side);
}

int CBattleInfoCallback::battleFortuneSpeed(const battle::Unit * unit) const
{
	if(!unit || !getBattle())
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? getBattle()->getSylvanLuckState(side).speedBonus(unit->unitId()) : 0;
}

int CBattleInfoCallback::battleBloodrageSpeed(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost())
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = getBattle()->getBloodrageCapPercent(side);
	const int damage = battleGetBloodrageDamagePercent(unit);
	const int bonus = std::max(0, getBattle()->getBloodrageSpeedBonus(side));
	return cap > 0 && static_cast<int64_t>(damage) * 2 >= cap ? bonus : 0;
}

int CBattleInfoCallback::battleBloodrageRetaliations(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost())
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = getBattle()->getBloodrageCapPercent(side);
	const int damage = battleGetBloodrageDamagePercent(unit);
	const int bonus = std::max(0, getBattle()->getBloodrageAdditionalRetaliations(side));
	return cap > 0 && static_cast<int64_t>(damage) * 2 >= cap ? bonus : 0;
}

int CBattleInfoCallback::battleBloodragePainIncrement(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost())
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int rank = getBattle()->getBloodrageRank(side);
	const int increment = getBattle()->getBloodragePainIncrement(side);
	return getBattle()->getBloodrageCapPercent(side) > 0 && rank > 0
		&& increment == newHorizonsBloodrage::incrementForRank(rank) ? increment : 0;
}

bool CBattleInfoCallback::battleBeginsActivation(const battle::Unit * unit, BattleUnitTurnReason reason) const
{
	if(!unit || unit->isTimeStopped() || reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION
		|| reason == BattleUnitTurnReason::HERO_SPELLCAST || reason == BattleUnitTurnReason::UNIT_SPELLCAST)
		return false;
	if(reason != BattleUnitTurnReason::HERO_COMMAND)
		return true;
	const auto controllerSide = playerToSide(battleGetOwner(unit));
	if(controllerSide != BattleSide::ATTACKER && controllerSide != BattleSide::DEFENDER)
		return false;
	const auto order = battleGetHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
	return order && order->secondWindActive
		&& order->primaryTargetUnitId == unit->unitId();
}

std::vector<uint32_t> CBattleInfoCallback::battleFortuneAdjacentFriends(const battle::Unit * unit) const
{
	std::vector<uint32_t> result;
	if(!unit)
		return result;
	for(const auto * candidate : battleGetUnitsIf([](const battle::Unit * candidate) { return candidate->alive(); }))
	{
		if(candidate->unitId() == unit->unitId() || !candidate->alive() || !battleMatchOwner(unit, candidate, true))
			continue;
		for(const auto & hex : unit->getSurroundingHexes())
			if(candidate->coversPos(hex))
			{
				result.push_back(candidate->unitId());
				break;
			}
	}
	return result;
}

bool CBattleInfoCallback::battleCanUsePerfectMoment(const battle::Unit * attacker,
	const battle::Unit * target, bool shooting) const
{
	if(!attacker || !getBattle() || battleTacticDist() || !attacker->alive() || attacker->isGhost()
		|| attacker->isTimeStopped() || attacker->isTurret() || attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->hasBonusOfType(BonusType::NO_LUCK) || attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		|| (attacker->hasBonusOfType(BonusType::MAXIMUM_LUCK)
			&& attacker->valOfBonuses(BonusType::MAXIMUM_LUCK) <= 0)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| getBattle()->getActiveStackID() != attacker->unitId())
		return false;
	const auto side = playerToSide(battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return getBattle()->getSylvanLuckState(side).canUsePerfectMoment()
		&& battleGetAttackLuck(attacker, target, shooting, false) >= 5;
}

bool CBattleInfoCallback::battleCanTriggerCleave(const battle::Unit * attacker) const
{
	if(!attacker || !getBattle() || !attacker->alive() || attacker->isGhost()
		|| attacker->isTurret() || attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return false;
	const auto * state = dynamic_cast<const battle::CUnitState *>(attacker);
	const auto * hero = battleGetOwnerHero(attacker);
	return state && !state->cleaveUsedThisActivation && hero
		&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE);
}

bool CBattleInfoCallback::battleCanTriggerNoQuarter(const BattleAttackInfo & attack) const
{
	if(!attack.attacker || attack.shooting || !attack.physicalDamage)
		return false;
	const auto * hero = battleGetOwnerHero(attack.attacker);
	return hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER);
}

const battle::Unit * CBattleInfoCallback::battleSelectCleaveTarget(const battle::Unit * attacker,
	const battle::Unit * destroyed) const
{
	if(!attacker || !destroyed || !getBattle())
		return nullptr;
	auto candidates = battleAdjacentUnits(destroyed);
	vstd::erase_if(candidates, [this, attacker](const battle::Unit * candidate)
	{
		return !candidate || !candidate->alive() || candidate->isGhost()
			|| candidate->unitId() == attacker->unitId()
			|| !battleMatchOwner(attacker, candidate);
	});
	const auto lowestOccupiedHex = [](const battle::Unit * unit)
	{
		int result = GameConstants::BFIELD_SIZE;
		for(const auto hex : unit->getHexes())
			result = std::min(result, static_cast<int>(hex.toInt()));
		return result;
	};
	std::sort(candidates.begin(), candidates.end(), [&](const battle::Unit * left, const battle::Unit * right)
	{
		if(left->getAvailableHealth() != right->getAvailableHealth())
			return left->getAvailableHealth() > right->getAvailableHealth();
		const int leftHex = lowestOccupiedHex(left);
		const int rightHex = lowestOccupiedHex(right);
		return leftHex != rightHex ? leftHex < rightHex : left->unitId() < right->unitId();
	});
	return candidates.empty() ? nullptr : candidates.front();
}

int CBattleInfoCallback::battleGetAttackLuck(const battle::Unit * attacker, const battle::Unit * target,
	bool shooting, bool includeChanceOnlySerendipity) const
{
	if(!attacker || !getBattle())
		return 0;
	const auto rules = battleLuckRules(*getBattle());
	const int maximum = static_cast<int>(rules.goodChance.size());
	const int minimum = -static_cast<int>(rules.badChance.size());
	const auto cap = [attacker](int luck)
	{
		return attacker->hasBonusOfType(BonusType::MAXIMUM_LUCK)
			? std::min(luck, attacker->valOfBonuses(BonusType::MAXIMUM_LUCK)) : luck;
	};
	if(attacker->hasBonusOfType(BonusType::MAX_LUCK))
		return cap(maximum);
	if(attacker->hasBonusOfType(BonusType::NO_LUCK))
		return 0;
	const int baseLuck = std::clamp(attacker->valOfBonuses(BonusType::LUCK), minimum, maximum);
	const auto side = playerToSide(battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return cap(baseLuck);
	const auto mark = battleGetFocusFireState(side);
	const bool focused = shooting && target && battleIsFocusFireRecipient(attacker, side)
		&& battleIsFocusFireTargetActive(side) && mark && mark->targetUnitId == target->unitId();
	const auto & fortune = getBattle()->getSylvanLuckState(side);
	int luck = fortune.chanceLuck(baseLuck, attacker->unitId(), focused);
	if(!includeChanceOnlySerendipity && fortune.serendipity
		&& !fortune.positiveLuckUnits.contains(attacker->unitId()))
		--luck;
	return cap(std::clamp(luck, minimum, maximum));
}

int64_t CBattleInfoCallback::battleExpectedLuckDamage(const BattleAttackInfo & attack) const
{
	const auto average = [](const DamageRange & range) { return range.min + (range.max - range.min) / 2; };
	const auto normal = average(calculateDmgRange(attack).damage);
	const auto side = playerToSide(battleGetOwner(attack.attacker));
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || attack.luckyStrike || attack.unluckyStrike)
		return normal;
	const auto fortune = getBattle()->getSylvanLuckState(side);
	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return normal;
	if(luck < 0 && fortune.canIgnoreNegativeLuck(
		newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage)))
		return normal;
	const auto rules = battleLuckRules(*getBattle());
	if(luck > 0 && attack.secondaryAttack && !rules.affectsAllTargets)
		return normal;
	const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
	const auto dice = rules.diceSize;
	if(chances.empty() || dice <= 0)
		return normal;
	double chance = std::clamp(static_cast<double>(chances[std::min<size_t>(std::abs(luck), chances.size()) - 1]) / dice, 0.0, 1.0);
	// Forecast this attack's first adverse result. Deterministic outcomes do not
	// spend an allowance; positive Luck is not an adverse result for this army.
	if(luck < 0 && chance > 0.0 && chance < 1.0
		&& getBattle()->getAdverseCombatRerollState(side).available())
		chance *= chance;
	auto rolled = attack;
	rolled.luckyStrike = luck > 0;
	rolled.unluckyStrike = luck < 0;
	return static_cast<int64_t>(normal * (1.0 - chance) + average(calculateDmgRange(rolled).damage) * chance);
}

std::vector<HeroOrderState> CBattleInfoCallback::battleGetHeroOrderStates(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {};
	return getBattle()->getHeroOrderStates(side);
}

std::optional<HeroOrderState> CBattleInfoCallback::battleGetHeroOrderState(BattleSide side,
	HeroCommand command) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {};
	return getBattle()->getHeroOrderState(side, command);
}

std::optional<HeroOrderState> CBattleInfoCallback::battleGetHeroOrderState(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {};
	return getBattle()->getHeroOrderState(side);
}

int CBattleInfoCallback::battleGetBloodrageDamagePercent(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost())
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = std::max(0, getBattle()->getBloodrageCapPercent(side));
	if(cap == 0)
		return 0;
	const int globalDamage = std::max(0, getBattle()->getBloodrageDamagePercent(side));
	const int personalIncrement = std::max(0, unit->getPersonalBloodrageIncrement());
	return static_cast<int>(std::min<int64_t>(cap,
		static_cast<int64_t>(globalDamage) + personalIncrement));
}

int CBattleInfoCallback::battleGetBloodrageDamagePercent(const battle::Unit * attacker,
	const battle::Unit * defender) const
{
	const int currentDamagePercent = battleGetBloodrageDamagePercent(attacker);
	if(!getBattle() || !attacker || !attacker->alive() || attacker->isGhost()
		|| !defender || !defender->alive() || defender->isGhost()
		|| battleGetOwner(attacker) == battleGetOwner(defender))
		return currentDamagePercent;

	const auto side = playerToSide(battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return currentDamagePercent;
	const int increment = std::max(0, getBattle()->getBloodrageLowHealthIncrement(side));
	const int cap = std::max(0, getBattle()->getBloodrageCapPercent(side));
	const int64_t maximumHealth = battle::getMaximumHealth(*defender);
	const int64_t currentHealth = defender->getAvailableHealth();
	if(increment == 0 || cap == 0 || maximumHealth <= 0 || currentHealth <= 0)
		return currentDamagePercent;

	// “Below half” is strict. Comparing with ceil(maximum / 2) avoids overflow
	// from multiplying the current and maximum HP values by two or one hundred.
	const int64_t halfHealthCeiling = maximumHealth / 2 + maximumHealth % 2;
	if(currentHealth >= halfHealthCeiling)
		return currentDamagePercent;

	return static_cast<int>(std::min<int64_t>(cap,
		static_cast<int64_t>(currentDamagePercent) + increment));
}

const RelentlessAssaultState & CBattleInfoCallback::battleGetRelentlessAssaultState(BattleSide side) const
{
	static const RelentlessAssaultState empty;
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return empty;
	return getBattle()->getRelentlessAssaultState(side);
}

int CBattleInfoCallback::battleGetRelentlessAssaultDamagePercent(const battle::Unit * attacker,
	const battle::Unit * primaryTarget) const
{
	if(!getBattle() || !attacker || !primaryTarget || !attacker->alive() || !primaryTarget->alive()
		|| attacker->isGhost() || primaryTarget->isGhost()
		|| attacker->isTurret() || primaryTarget->isTurret()
		|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| primaryTarget->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| primaryTarget->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		|| battleGetOwner(attacker) == battleGetOwner(primaryTarget))
		return 0;
	const auto side = playerToSide(battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const auto * hero = battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		return 0;
	return battleGetRelentlessAssaultState(side).damagePercentForTarget(primaryTarget->unitId());
}

bool CBattleInfoCallback::battleIsShroudFlankingAttack(const BattleAttackInfo & attack) const
{
	if(!getBattle() || !attack.attacker || !attack.defender || attack.shooting || !attack.physicalDamage
		|| attack.secondaryAttack || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTurret() || attack.defender->isTurret()
		|| attack.attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attack.defender->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attack.attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| attack.defender->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| battleGetOwner(attack.attacker) == battleGetOwner(attack.defender))
		return false;
	const auto attackerHex = attack.attackerPos.isValid() ? attack.attackerPos : attack.attacker->getPosition();
	const auto defenderHex = attack.defenderPos.isValid() ? attack.defenderPos : attack.defender->getPosition();
	if(battleHasFormationFightingProtection(attack.defender, defenderHex))
		return false;
	const auto adjacent = std::ranges::any_of(attack.attacker->getHexes(attackerHex), [&](const BattleHex & first)
	{
		return first.isValid() && std::ranges::any_of(attack.defender->getHexes(defenderHex), [&](const BattleHex & second)
		{
			return second.isValid() && BattleHex::getDistance(first, second) == 1;
		});
	});
	if(!adjacent)
		return false;
	return isToReverse(attack.attacker, attack.defender, attackerHex, defenderHex);
}

bool CBattleInfoCallback::battleNightProwlerCrossesEnemy(
	const battle::Unit * mover, const BattleHexArray & committedPath) const
{
	const auto * currentBattle = getBattle();
	if(!mover || !currentBattle || !mover->alive() || mover->isGhost()
		|| mover->hasBonusOfType(BonusType::FLYING) || committedPath.empty())
		return false;

	const auto & deployment = currentBattle->getDeploymentState();
	if(deployment.activeSide() != BattleSide::NONE || currentBattle->getTacticDist() > 0)
		return false;

	const auto controllerSide = playerToSide(battleGetOwner(mover));
	if(controllerSide != BattleSide::ATTACKER && controllerSide != BattleSide::DEFENDER)
		return false;
	const auto moverOwner = battleGetOwner(mover);
	if(moverOwner == PlayerColor::CANNOT_DETERMINE)
		return false;
	const auto * hero = battleGetFightingHero(controllerSide);
	if(!hero || newHorizonsShroud::rank(hero) <= 0 || !newHorizonsShroud::hasNightProwler(hero))
		return false;

	for(const auto & position : committedPath)
	{
		if(!position.isValid())
			continue;
		for(const auto & footprintHex : mover->getHexes(position))
		{
			if(!footprintHex.isValid())
				continue;
			const auto * occupant = battleGetUnitByPos(footprintHex, true);
			if(!occupant || occupant->unitId() == mover->unitId())
				continue;
			const auto occupantOwner = battleGetOwner(occupant);
			if(occupantOwner != PlayerColor::CANNOT_DETERMINE && moverOwner != occupantOwner)
				return true;
		}
	}
	return false;
}

bool CBattleInfoCallback::battleHasFormationFightingProtection(const battle::Unit * defender,
	const BattleHex & assumedPosition) const
{
	if(!getBattle() || !defender || !defender->alive() || defender->isGhost()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender))
		return false;

	const auto owner = battleGetOwner(defender);
	if(playerToSide(owner) == BattleSide::NONE
		|| newHorizonsCombatSkills::formationFightingReductionPercent(battleGetOwnerHero(defender)) == 0)
		return false;

	for(const auto * friendly : battleAliveUnits())
	{
		if(friendly->unitId() == defender->unitId() || !friendly->alive() || friendly->isGhost()
			|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(friendly)
			|| battleGetOwner(friendly) != owner)
			continue;

		if(orderUnitsAdjacent(defender, friendly, assumedPosition))
			return true;
	}
	return false;
}

bool CBattleInfoCallback::battleHasBastionProtection(const battle::Unit * defender) const
{
	if(!getBattle() || !defender || !defender->alive()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender)
		|| defender->unitSlot() == SlotID::WAR_MACHINES_SLOT)
		return false;

	const auto * controllerHero = battleGetOwnerHero(defender);
	if(!controllerHero || !controllerHero->hasActivePerk(
		std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID),
		std::string(newHorizonsCombatSkills::BASTION_PERK_ID)))
		return false;

	const auto defenderState = defender->acquireState();
	if(!defenderState || defenderState->armorerBastionRound == battleGetRound())
		return false;
	if(defender->defended())
		return true;

	const auto side = playerToSide(battleGetOwner(defender));
	if(side == BattleSide::NONE)
		return false;
	const auto orderState = battleGetHeroOrderState(side, HeroCommand::HOLD_THE_LINE);
	return orderState && battleIsHoldTheLineRecipient(*orderState, defender);
}

bool CBattleInfoCallback::battleOrderBenefitAppliesTo(const HeroOrderState & state, BattleSide side,
	const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		|| !heroCommands::supportedByRules(getBattle()->getHeroCommandRules(), state.command)
		|| state.issuedRound != battleGetRound()
		|| battleGetOwner(unit) != sideToPlayer(side)
		|| unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return false;

	switch(state.command)
	{
	case HeroCommand::CHARGE:
		return !state.containsConsumed(unit->unitId());
	case HeroCommand::FOCUS_FIRE:
	{
		const auto mark = battleGetFocusFireState(side);
		const auto * hero = battleGetFightingHero(side);
		const bool eligibleFocusAttacker = battleIsFocusFireRecipient(unit, side)
			|| (heroCommands::hasCombinedArms(hero) && unit->isMeleeAttacker()
				&& !unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		return eligibleFocusAttacker && battleIsFocusFireTargetActive(side) && mark
			&& mark->issuedRound == state.issuedRound
			&& mark->targetUnitId == state.primaryTargetUnitId
			&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), unit->unitId());
	}
	case HeroCommand::RIPOSTE:
	case HeroCommand::BRACE:
		return true;
	case HeroCommand::HOLD_THE_LINE:
		return battleIsHoldTheLineRecipient(state, unit);
	case HeroCommand::PROTECT:
	{
		if(state.protectBroken || state.protectInterceptionsConsumed >= state.protectInterceptionLimit)
			return false;
		const auto * protector = battleGetUnitByID(state.primaryTargetUnitId);
		const auto * ward = battleGetUnitByID(state.secondaryTargetUnitId);
		if(!protector || !ward || !protector->alive() || !ward->alive()
			|| protector->isGhost() || ward->isGhost()
			|| battleGetOwner(protector) != sideToPlayer(side)
			|| battleGetOwner(ward) != sideToPlayer(side)
			|| !orderUnitsAdjacent(protector, ward))
			return false;
		return unit->unitId() == protector->unitId() || unit->unitId() == ward->unitId();
	}
	case HeroCommand::FLANK:
	{
		const auto * target = battleGetUnitByID(state.primaryTargetUnitId);
		if(!target || !target->alive() || target->isGhost() || target->isTurret()
			|| battleGetOwner(target) == sideToPlayer(side) || !state.flankFor(target->unitId()))
			return false;
		if(unit->isMeleeAttacker())
			return true;
		const auto * hero = battleGetFightingHero(side);
		return heroCommands::hasCombinedArms(hero) && newHorizonsArchery::isOrdinaryPhysicalShooter(unit);
	}
	case HeroCommand::SECOND_WIND:
		return state.secondWindActive && state.primaryTargetUnitId == unit->unitId();
	default:
		return false;
	}
}

bool CBattleInfoCallback::battleHasCommandingPresence(const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules()))
		return false;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	const auto * controllerHero = battleGetOwnerHero(unit);
	if(!controllerHero || !controllerHero->hasActivePerk(
		"new-horizons:command", "new-horizons:command.commandingPresence"))
		return false;
	const auto orderStates = battleGetHeroOrderStates(side);
	return std::ranges::any_of(orderStates, [this, side, unit](const HeroOrderState & orderState)
	{
		return battleOrderBenefitAppliesTo(orderState, side, unit);
	});
}

BattleMoraleInfo CBattleInfoCallback::battleGetMoraleInfo(const battle::Unit * unit) const
{
	BattleMoraleInfo result;
	if(!unit)
		return result;

	if(!getBattle() || !unit->alive() || unit->isGhost())
	{
		result.real = unit->moraleVal();
		result.effective = result.real;
		return result;
	}

	const int32_t firstRoundMoraleModifier = getBattle()->getRound() == 1
		? getBattle()->getFirstRoundMoraleModifier(unit->unitSide()) : 0;
	result.firstRoundModifier = firstRoundMoraleModifier;
	const auto * hero = battleGetOwnerHero(unit);
	int32_t additionalMorale = 0;
	if(hero && hero->hasActivePerk("new-horizons:discipline", "new-horizons:discipline.standardBearer"))
	{
		const auto owner = battleGetOwner(unit);
		for(const auto * supporter : battleGetUnitsIf([](const battle::Unit * candidate)
			{
				return candidate->alive() && !candidate->isGhost();
			}))
		{
			if(supporter->unitId() == unit->unitId() || battleGetOwner(supporter) != owner)
				continue;

			if(orderUnitsAdjacent(unit, supporter))
			{
				additionalMorale = 1;
				break;
			}
		}
	}
	result.standardBearerBonus = additionalMorale;

	const auto applyMoraleFloor = [this, unit, hero, &result](int morale)
	{
		result.real = morale;
		if(morale >= 0)
			return morale;
		if(battleHasCommandingPresence(unit))
		{
			result.commandingPresenceFloorApplied = true;
			return 0;
		}
		if(newHorizonsBloodrage::hasFuryUnbound(hero)
			&& battleGetBloodrageDamagePercent(unit) > 0)
		{
			result.furyUnboundFloorApplied = true;
			return 0;
		}
		return morale;
	};
	if(!newHorizonsDiscipline::hasSteadfast(hero))
	{
		if(additionalMorale == 0 && firstRoundMoraleModifier == 0)
			result.effective = applyMoraleFloor(unit->moraleVal());
		else
		{
			const auto totalAdditionalMorale = static_cast<int64_t>(additionalMorale)
				+ static_cast<int64_t>(firstRoundMoraleModifier);
			const auto boundedAdditionalMorale = static_cast<int32_t>(std::clamp<int64_t>(totalAdditionalMorale,
				std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
			result.effective = applyMoraleFloor(unit->moraleValWithBonus(boundedAdditionalMorale));
		}
		return result;
	}

	const auto moraleBonuses = unit->getUnstackedBonuses(Selector::type()(BonusType::MORALE));
	BonusList adjustedMoraleBonuses;
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> adjustedByOriginal;
	const PlayerColor targetAuraOwner = unit->unitOwner() == PlayerColor::UNFLAGGABLE
		? PlayerColor::NEUTRAL : unit->unitOwner();
	for(const auto & bonus : *moraleBonuses)
	{
		const bool hostileCreatureAura = bonus->source == BonusSource::CREATURE_ABILITY
			&& bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
			&& bonus->bonusOwner != targetAuraOwner;
		if(bonus->val < 0 && (bonus->appliedByEnemy || hostileCreatureAura))
		{
			auto & adjusted = adjustedByOriginal[bonus.get()];
			if(!adjusted)
			{
				adjusted = std::make_shared<Bonus>(*bonus);
				++adjusted->val;
			}
			adjustedMoraleBonuses.push_back(adjusted);
		}
		else
			adjustedMoraleBonuses.push_back(bonus);
	}
	adjustedMoraleBonuses.stackBonuses();

	const auto currentMoraleBonuses = unit->getBonusesOfType(BonusType::MORALE);
	const int64_t moraleDelta = static_cast<int64_t>(adjustedMoraleBonuses.totalValue())
		- static_cast<int64_t>(currentMoraleBonuses->totalValue());
	result.steadfastAdjustment = static_cast<int32_t>(std::clamp<int64_t>(moraleDelta,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
	const int64_t totalAdditionalMorale = moraleDelta + additionalMorale + firstRoundMoraleModifier;
	const auto boundedAdditionalMorale = static_cast<int32_t>(std::clamp<int64_t>(totalAdditionalMorale,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
	result.effective = applyMoraleFloor(unit->moraleValWithBonus(boundedAdditionalMorale));
	return result;
}

int CBattleInfoCallback::battleGetMorale(const battle::Unit * unit) const
{
	return battleGetMoraleInfo(unit).effective;
}

int CBattleInfoCallback::battleGetFearChance(const battle::Unit * affected) const
{
	if(!affected)
		return 0;

	const auto * controllerHero = battleGetOwnerHero(affected);
	if(!newHorizonsDiscipline::hasFearless(controllerHero))
		return affected->valOfBonuses(BonusType::FEARFUL);

	// Fearless filters only the current native turn-start fear family. FEARFUL
	// currently has no shared stacking key across source families, so the normal
	// BonusList aggregation preserves spell and unclassified contributions.
	const CSelector survivingFear = CSelector([](const Bonus * bonus)
	{
		return bonus->type == BonusType::FEARFUL
			&& !(bonus->source == BonusSource::CREATURE_ABILITY && bonus->val > 0);
	});
	const auto bonuses = affected->getBonuses(survivingFear);
	return bonuses ? bonuses->totalValue() : 0;
}

bool CBattleInfoCallback::battleShroudDeniesRetaliation(const BattleAttackInfo & attack) const
{
	return battleIsShroudFlankingAttack(attack)
		&& newHorizonsShroud::deniesRetaliation(newHorizonsShroud::rank(battleGetOwnerHero(attack.attacker)));
}

std::optional<HeroOrderState> CBattleInfoCallback::battlePrepareHeroOrderState(BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetUnitIds) const
{
	heroCommands::TargetRejection rejection;
	return battlePrepareHeroOrderStateImpl(side, command, targetUnitIds, rejection);
}

heroCommands::TargetRejection CBattleInfoCallback::battleOwnOrderUnitRejection(
	BattleSide side, const battle::Unit * unit) const
{
	using Reason = heroCommands::TargetRejection;
	if(!unit)
		return Reason::NO_STACK;
	if(!unit->alive() || unit->isGhost())
		return Reason::NOT_LIVING;
	if(battleGetOwner(unit) != sideToPlayer(side))
		return Reason::FRIENDLY_REQUIRED;
	if(unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return Reason::ORDINARY_REQUIRED;
	return Reason::NONE;
}

heroCommands::TargetRejection CBattleInfoCallback::battleGetHeroOrderTargetRejection(
	BattleSide side, HeroCommand command, const std::vector<uint32_t> & targetUnitIds, bool selectingProtector) const
{
	using Reason = heroCommands::TargetRejection;
	if(command == HeroCommand::FOCUS_FIRE)
		return targetUnitIds.size() == 1
			? battleFocusFireTargetRejection(side, command, targetUnitIds.front()) : Reason::TARGET_COUNT;
	if(selectingProtector && command == HeroCommand::PROTECT)
	{
		if(!battleHeroCommandCommonAvailable(side, command))
			return Reason::UNAVAILABLE;
		if(targetUnitIds.size() != 1)
			return Reason::TARGET_COUNT;
		const auto id = targetUnitIds.front();
		const auto ordinary = battleOwnOrderUnitRejection(side, battleGetUnitByID(id));
		if(ordinary != Reason::NONE)
			return ordinary;
		const auto candidates = battleGetHeroCommandTargets(side, command);
		if(std::ranges::find(candidates, id) == candidates.end())
			return Reason::ORDINARY_REQUIRED;
		for(const auto ward : candidates)
			if(id != ward && battlePrepareHeroOrderState(side, command, {id, ward}))
				return Reason::NONE;
		return Reason::NO_ADJACENT_WARD;
	}
	Reason rejection;
	battlePrepareHeroOrderStateImpl(side, command, targetUnitIds, rejection);
	return rejection;
}

std::optional<HeroOrderState> CBattleInfoCallback::battlePrepareHeroOrderStateImpl(BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetUnitIds, heroCommands::TargetRejection & rejection) const
{
	using Reason = heroCommands::TargetRejection;
	rejection = Reason::NONE;
	const auto reject = [&rejection](Reason reason) -> std::optional<HeroOrderState>
	{
		rejection = reason;
		return {};
	};
	if(!getBattle() || !heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		|| !heroCommands::isActive(command) || !battleHeroCommandCommonAvailable(side, command))
		return reject(Reason::UNAVAILABLE);
	const auto * hero = battleGetFightingHero(side);
	if(!hero)
		return reject(Reason::UNAVAILABLE);
	const auto owner = sideToPlayer(side);
	const auto ownCombatUnit = [this, side](const battle::Unit * unit)
	{
		return battleOwnOrderUnitRejection(side, unit) == Reason::NONE;
	};
	const auto * rules = &getBattle()->getHeroCommandRules()["commands"][heroCommands::key(command)];
	HeroOrderState result;
	result.command = command;
	result.issuedRound = battleGetRound();
	if(result.issuedRound < 1)
		return reject(Reason::UNAVAILABLE);
	const auto allowance = battleGetOrderActionAllowance(side);
	if(newHorizonsWarcasting::enabled(getBattle()->getMagicRules())
		&& allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO)
		result.warcastingBonusPercent = newHorizonsWarcasting::orderBonus(
			hero, getBattle()->getWarcastingState(side), result.issuedRound);
	if(allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::ORDER
		&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
	{
		result.sacredCommandEfficiencyBonusPercent =
			newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(hero);
		result.knightlySequenceEfficiencyBonusPercent =
			newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(hero);
	}
	if(command == HeroCommand::HOLD_THE_LINE
		&& hero->hasActivePerk(newHorizonsIronDiscipline::SKILL, newHorizonsIronDiscipline::PERK))
	{
		const int physicalReduction = std::clamp(heroCommands::coefficient(
			(*rules)["effects"]["damageReductionPercent"], *hero, result.warcastingBonusPercent,
			result.divineMandateEfficiencyBonusPercent()), 0, 100);
		result.holdMagicalReductionBasisPoints = static_cast<uint16_t>(physicalReduction
			* newHorizonsIronDiscipline::BASIS_POINTS_PER_PHYSICAL_PERCENT);
	}

	if(command == HeroCommand::FOCUS_FIRE)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto reason = battleFocusFireTargetRejection(side, command, targetUnitIds.front());
		if(reason != Reason::NONE)
			return reject(reason);
		result.primaryTargetUnitId = targetUnitIds.front();
		return result;
	}
	if(command == HeroCommand::FLANK)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto * target = battleGetUnitByID(targetUnitIds.front());
		if(!target)
			return reject(Reason::NO_STACK);
		if(!target->alive() || target->isGhost())
			return reject(Reason::NOT_LIVING);
		if(target->isTurret())
			return reject(Reason::ORDINARY_REQUIRED);
		if(battleGetOwner(target) == owner)
			return reject(Reason::ENEMY_REQUIRED);
		bool hasMelee = false;
		for(const auto * unit : battleAliveUnits())
			if(ownCombatUnit(unit) && unit->isMeleeAttacker())
				hasMelee = true;
		if(!hasMelee)
			return reject(Reason::NO_MELEE_RECIPIENT);
		result.primaryTargetUnitId = target->unitId();
		result.flankTargets.push_back({target->unitId(), 0});
		return result;
	}
	if(command == HeroCommand::PROTECT)
	{
		if(targetUnitIds.size() != 2)
			return reject(Reason::TARGET_COUNT);
		if(targetUnitIds.front() == targetUnitIds.back())
			return reject(Reason::SAME_STACK);
		const auto * protector = battleGetUnitByID(targetUnitIds.front());
		const auto * ward = battleGetUnitByID(targetUnitIds.back());
		for(const auto * unit : {protector, ward})
		{
			const auto reason = battleOwnOrderUnitRejection(side, unit);
			if(reason != Reason::NONE)
				return reject(reason);
		}
		if(!orderUnitsAdjacent(protector, ward))
			return reject(Reason::NOT_ADJACENT);
		result.primaryTargetUnitId = protector->unitId();
		result.secondaryTargetUnitId = ward->unitId();
		result.protectInterceptionLimit = hero->hasActivePerk(
			newHorizonsShieldMaster::SKILL, newHorizonsShieldMaster::PERK)
			? newHorizonsShieldMaster::SHIELD_MASTER_PROTECT_INTERCEPTION_LIMIT
			: newHorizonsShieldMaster::ORDINARY_PROTECT_INTERCEPTION_LIMIT;
		return result;
	}
	if(command == HeroCommand::SECOND_WIND)
	{
		if(targetUnitIds.size() != 1)
			return reject(Reason::TARGET_COUNT);
		const auto * target = battleGetUnitByID(targetUnitIds.front());
		const bool canonicalRules = heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules());
		const auto targetState = target ? target->acquireState() : nullptr;
		const bool hasSpentActivation = target
			&& (target->moved() || (canonicalRules && targetState && targetState->defending));
		const auto reason = battleOwnOrderUnitRejection(side, target);
		if(reason != Reason::NONE)
			return reject(reason);
		if(!hasSpentActivation)
			return reject(Reason::ACTIVATION_UNSPENT);
		result.primaryTargetUnitId = target->unitId();
		return result;
	}
	if(!targetUnitIds.empty())
		return reject(Reason::TARGET_COUNT);
	bool hasRecipient = false;
	for(const auto * unit : battleAliveUnits())
	{
		if(!ownCombatUnit(unit))
			continue;
		hasRecipient = true;
		if(command == HeroCommand::HOLD_THE_LINE)
			result.anchors.push_back({unit->unitId(), unit->getPosition().toInt()});
	}
	if(!hasRecipient)
		return reject(Reason::NO_RECIPIENT);
	(void)rules;
	result.validateShape();
	return result;
}

const battle::Unit * CBattleInfoCallback::battleResolveHeroOrderTarget(const battle::Unit * attacker,
	const battle::Unit * defender, bool shooting) const
{
	if(shooting || !getBattle() || !attacker || !defender || !defender->alive()
		|| battleGetOwner(attacker) == battleGetOwner(defender))
		return defender;
	const auto side = playerToSide(battleGetOwner(defender));
	const auto state = battleGetHeroOrderState(side, HeroCommand::PROTECT);
	if(!state || state->issuedRound != battleGetRound()
		|| state->protectInterceptionsConsumed >= battleHeroOrderProtectInterceptionLimit(side)
		|| state->protectBroken || state->secondaryTargetUnitId != defender->unitId())
		return defender;
	const auto * protector = battleGetUnitByID(state->primaryTargetUnitId);
	if(!protector || !protector->alive() || protector->isGhost()
		|| !orderUnitsAdjacent(protector, defender))
		return defender;
	return protector;
}

int CBattleInfoCallback::battleHeroOrderProtectInterceptionLimit(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return newHorizonsShieldMaster::ORDINARY_PROTECT_INTERCEPTION_LIMIT;
	const auto state = battleGetHeroOrderState(side, HeroCommand::PROTECT);
	return state
		? state->protectInterceptionLimit
		: newHorizonsShieldMaster::ORDINARY_PROTECT_INTERCEPTION_LIMIT;
}

bool CBattleInfoCallback::battleIsHoldTheLineRecipient(const HeroOrderState & state,
	const battle::Unit * unit) const
{
	if(!getBattle() || !unit || !unit->alive() || unit->isGhost() || unit->isTurret()
		|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return false;
	return state.isHoldTheLineRecipient(unit->unitId(), static_cast<int16_t>(unit->getPosition().toInt()),
		battleGetRound());
}

int CBattleInfoCallback::battleGetHoldTheLineMagicalReductionBasisPoints(const battle::Unit * unit) const
{
	if(!unit)
		return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const auto state = battleGetHeroOrderState(side, HeroCommand::HOLD_THE_LINE);
	return state && battleIsHoldTheLineRecipient(*state, unit)
		? state->holdMagicalReductionBasisPoints : 0;
}

bool CBattleInfoCallback::battleUsesNewHorizonsMultiplicativeMDR() const
{
	const auto * battleState = getBattle();
	if(!battleState)
		return false;

	const auto & magicRules = battleState->getMagicRules();
	return newHorizonsMagic::rulesActive(magicRules)
		&& magicRules["spells"].isStruct()
		&& magicRules["spells"].Struct().contains(HOLY_ARMOR_SAVED_ROSTER_KEY);
}

int CBattleInfoCallback::battleGetPerkMagicalReductionBasisPoints(const battle::Unit * unit) const
{
	if(!unit || !battleUsesNewHorizonsMultiplicativeMDR())
		return 0;

	// Resolve ownership now, rather than relying on the stack's original army
	// inheritance. Mind control, summoned stacks, gated stacks, and detached AI
	// units all use the same current-controller query.
	const auto * controllerHero = battleGetOwnerHero(unit);
	return controllerHero && controllerHero->hasActivePerk(WARCASTING_SKILL_ID, SPELLWARD_PERK_ID)
		? SPELLWARD_REDUCTION_BASIS_POINTS : 0;
}

bool CBattleInfoCallback::battleCanTriggerHeroOrderBrace(const battle::Unit * attacker,
	const battle::Unit * defender, int movementDistance, bool shooting, bool counter) const
{
	if(shooting || counter || movementDistance < 3 || !getBattle() || !attacker || !defender
		|| !attacker->alive() || !defender->alive() || attacker->isGhost() || defender->isGhost()
		|| attacker->isTurret() || defender->isTurret()
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| defender->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| defender->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| battleGetOwner(attacker) == battleGetOwner(defender))
		return false;
	const auto side = playerToSide(battleGetOwner(defender));
	const auto state = battleGetHeroOrderState(side, HeroCommand::BRACE);
	return state && state->issuedRound == battleGetRound()
		&& !attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		&& !defender->isTurret() && !defender->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

uint8_t CBattleInfoCallback::battleHeroOrderFlankSide(const battle::Unit * attacker,
	const battle::Unit * defender, const BattleHex & attackerPosition, const BattleHex & defenderPosition) const
{
	if(!attacker || !defender
		|| !(attackerPosition.isValid() || attacker->getPosition().isValid())
		|| !(defenderPosition.isValid() || defender->getPosition().isValid()))
		return 0;
	if(battleHasFormationFightingProtection(defender, defenderPosition))
		return 0;
	return orderContactingSideMask(attacker, defender, attackerPosition, defenderPosition);
}

int CBattleInfoCallback::battleHeroOrderFlankMeleeDamagePercent(const BattleAttackInfo & attack) const
{
	const auto * currentBattle = getBattle();
	if(!currentBattle || !heroCommands::isCanonicalRules(currentBattle->getHeroCommandRules())
		|| !attack.attacker || !attack.defender || attack.shooting
		|| attack.attacker->isGhost() || attack.defender->isGhost())
		return 0;

	const auto * attacker = attack.attacker;
	if(!attacker->alive() || attacker->isTurret() || attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return 0;

	const auto attackerSide = playerToSide(battleGetOwner(attacker));
	if(attackerSide != BattleSide::ATTACKER && attackerSide != BattleSide::DEFENDER)
		return 0;
	const auto * hero = battleGetOwnerHero(attacker);
	if(!hero)
		return 0;
	const auto state = battleGetHeroOrderState(attackerSide, HeroCommand::FLANK);
	if(!state || state->issuedRound != battleGetRound()
		|| state->primaryTargetUnitId != attack.defender->unitId())
		return 0;
	const auto * flank = state->flankFor(attack.defender->unitId());
	if(!flank || battleHasFormationFightingProtection(attack.defender, attack.defenderPos))
		return 0;

	const auto sideMask = battleHeroOrderFlankSide(attacker, attack.defender, attack.attackerPos, attack.defenderPos);
	int distinctSides = 0;
	for(auto bits = flank->sideMask; bits; bits &= static_cast<uint8_t>(bits - 1))
		++distinctSides;
	for(auto bits = static_cast<uint8_t>(sideMask & ~flank->sideMask); bits; bits &= static_cast<uint8_t>(bits - 1))
		++distinctSides;
	const int additionalSides = std::max(0, distinctSides - 1);
	const auto & formula = currentBattle->getHeroCommandRules()["commands"]["flank"]["effects"]["meleeDamagePercent"];
	const int baseDamagePercent = heroCommands::coefficient(formula, *hero,
		state->warcastingBonusPercent, state->divineMandateEfficiencyBonusPercent());
	const int additionalSidePercent = battleHeroOrderFlankAdditionalSidePercent(attackerSide,
		state->warcastingBonusPercent, state->divineMandateEfficiencyBonusPercent());
	return baseDamagePercent + additionalSides * additionalSidePercent;
}

int CBattleInfoCallback::battleHeroOrderFlankAdditionalSidePercent(BattleSide side,
	int warcastingBonusPercent) const
{
	return battleHeroOrderFlankAdditionalSidePercent(side, warcastingBonusPercent, 0);
}

int CBattleInfoCallback::battleHeroOrderFlankAdditionalSidePercent(BattleSide side,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const
{
	const auto * battle = getBattle();
	if(!battle || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return 0;
	const auto & formula = battle->getHeroCommandRules()["commands"]["flank"]["effects"]["additionalSidePercent"];
	const auto * hero = battle->getSideHero(side);
	if(hero && hero->hasActivePerk("new-horizons:offense", "new-horizons:offense.encirclement"))
		return heroCommands::ENCIRCLEMENT_ADDITIONAL_SIDE_PERCENT;
	if(hero)
		return heroCommands::coefficient(formula, *hero, warcastingBonusPercent,
			divineMandateEfficiencyBonusPercent);
	return heroCommands::coefficient(formula, 0, 0);
}

bool CBattleInfoCallback::battleIsFocusFireTargetActive(BattleSide side) const
{
	const auto mark = battleGetFocusFireState(side);
	if(!mark || mark->issuedRound != battleGetRound())
		return false;
	const auto * target = battleGetUnitByID(mark->targetUnitId);
	return target && target->alive() && !target->isGhost() && battleGetOwner(target) != sideToPlayer(side);
}

bool CBattleInfoCallback::battleIsTargetedRangedCommand(const battle::Unit * attacker,
	const battle::Unit * defender, bool shooting, bool secondaryAttack) const
{
	if(!shooting || secondaryAttack || !getBattle() || !attacker || !defender || !attacker->alive() || attacker->isGhost()
		|| !defender->alive() || defender->isGhost() || battleGetOwner(attacker) == battleGetOwner(defender))
		return false;
	const auto side = playerToSide(battleGetOwner(attacker));
	if(!battleIsFocusFireRecipient(attacker, side))
		return false;
	const auto mark = battleGetFocusFireState(side);
	return mark && mark->issuedRound == battleGetRound() && mark->targetUnitId == defender->unitId()
		&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), attacker->unitId());
}

int CBattleInfoCallback::battleTargetedRangedCommandPercent(const battle::Unit * attacker,
	const battle::Unit * defender, bool shooting, bool secondaryAttack) const
{
	if(!battleIsTargetedRangedCommand(attacker, defender, shooting, secondaryAttack))
		return 0;
	const auto side = playerToSide(battleGetOwner(attacker));
	const auto mark = battleGetFocusFireState(side);
	return mark->rangedDamagePercent;
}

ESpellCastProblem CBattleInfoCallback::battleCanCastSpell(const spells::Caster * caster, spells::Mode mode) const
{
	RETURN_IF_NOT_BATTLE(ESpellCastProblem::INVALID);
	if(caster == nullptr)
	{
		logGlobal->error("CBattleInfoCallback::battleCanCastSpell: no spellcaster.");
		return ESpellCastProblem::INVALID;
	}
	const PlayerColor player = caster->getCasterOwner();
	const auto side = playerToSide(player);
	if(side == BattleSide::NONE)
		return ESpellCastProblem::INVALID;
	if(!battleDoWeKnowAbout(side))
	{
		logGlobal->warn("You can't check if enemy can cast given spell!");
		return ESpellCastProblem::INVALID;
	}

	if(battleTacticDist())
		return ESpellCastProblem::ONGOING_TACTIC_PHASE;

	switch(mode)
	{
	case spells::Mode::HERO:
	{
		const auto * hero = caster->getHeroCaster();
		const bool metamagicFollowup = battleCanUseMetamagicFollowup(side);
		if(getBattle()->getPreCombatOrderState(side).isUnresolved())
			return ESpellCastProblem::CASTS_PER_TURN_LIMIT;

		if(!hero)
			return ESpellCastProblem::NO_HERO_TO_CAST_SPELL;
		if(battleUsesHeroCommands())
		{
			const auto round = battleGetRound();
			const auto & allowances = getBattle()->getHeroActionAllowances(side);
			if(round < 0 || allowances.currentRound != round || !allowances.eligibleAllowance(
				HeroActionAllowanceState::ActionKind::SPELL, round))
				return ESpellCastProblem::CASTS_PER_TURN_LIMIT;
		}
		if(!hero->hasSpellbook())
			return ESpellCastProblem::NO_SPELLBOOK;
		if(!battleUsesHeroCommands() && !metamagicFollowup
			&& battleCastSpells(side) >= hero->valOfBonuses(BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN))
			return ESpellCastProblem::CASTS_PER_TURN_LIMIT;
	}
		break;
	default:
		break;
	}

	//Orb of Inhibition blocks active spellcasting (hero and creature active abilities),
	//but not passive/triggered casts such as SPELL_BEFORE_ATTACK / SPELL_AFTER_ATTACK.
	//Level-0 creature abilities are excluded from this block in BattleSpellMechanics::canBeCast (spell level known there)
	if(mode == spells::Mode::HERO || mode == spells::Mode::CREATURE_ACTIVE)
	{
		const IBonusBearer * casterBonuses = caster->getHeroCaster();
		if(!casterBonuses)
			casterBonuses = battleGetUnitByID(caster->getCasterUnitId());
		if(casterBonuses && casterBonuses->hasBonusOfType(BonusType::BLOCK_ALL_MAGIC))
			return ESpellCastProblem::MAGIC_IS_BLOCKED;
	}

	return ESpellCastProblem::OK;
}

std::pair< BattleHexArray, int > CBattleInfoCallback::getPath(const BattleHex & start, const BattleHex & dest, const battle::Unit * stack) const
{
	auto reachability = getReachability(stack);

	if(!dest.isValid() || !reachability.isReachable(dest)
		|| reachability.predecessors[dest.toInt()] == -1) //cannot reach destination
	{
		return std::make_pair(BattleHexArray(), 0);
	}

	//making the Path
	BattleHexArray path;
	BattleHex curElem = dest;
	while(curElem != start)
	{
		path.insert(curElem);
		curElem = reachability.predecessors[curElem.toInt()];
	}

	return std::make_pair(path, reachability.distances[dest.toInt()]);
}

bool CBattleInfoCallback::battleIsInsideWalls(const BattleHex & from) const
{
	BattleHex wallPos = lineToWallHex(from.getY());

	if (from < wallPos)
		return false;

	if (wallPos < from)
		return true;

	// edge case - this is the wall. (or drawbridge)
	// since this method is used exclusively to determine behavior of defenders,
	// consider it inside walls, unless this is intact drawbridge - to prevent defenders standing on it and opening the gates
	if (from == BattleHex::GATE_INNER)
		return battleGetGateState() == EGateState::DESTROYED;
	return true;
}

bool CBattleInfoCallback::battleHasPenaltyOnLine(const BattleHex & from, const BattleHex & dest, bool checkWall, bool checkMoat) const
{
	if (!from.isAvailable() || !dest.isAvailable())
		throw std::runtime_error("Invalid hex (" + std::to_string(from.toInt()) + " and " + std::to_string(dest.toInt()) + ") received in battleHasPenaltyOnLine!" );

	if(battleGetFortifications().wallsHealth == 0)
		return false;

	auto isTileBlocked = [&](const BattleHex & tile)
	{
		EWallPart wallPart = battleHexToWallPart(tile);
		if (wallPart == EWallPart::INVALID)
			return false; // there is no wall here
		if (wallPart == EWallPart::INDESTRUCTIBLE_PART_OF_GATE)
			return false; // does not blocks ranged attacks
		if (wallPart == EWallPart::INDESTRUCTIBLE_PART)
			return true; // always blocks ranged attacks

		return isWallPartAttackable(wallPart);
	};
	// Count wall penalty requirement by shortest path, not by arbitrary line, to avoid various OH3 bugs
	auto getShortestPath = [](const BattleHex & from, const BattleHex & dest) -> BattleHexArray
	{
		//Out early
		if(from == dest)
			return {};

		BattleHexArray ret;
		auto next = from;
		//Not a real direction, only to indicate to which side we should search closest tile
		auto direction = from.getX() > dest.getX() ? BattleSide::DEFENDER : BattleSide::ATTACKER;

		while (next != dest)
		{
			next = BattleHex::getClosestTile(direction, dest, next.getNeighbouringTiles());
			ret.insert(next);
		}
		assert(!ret.empty());
		ret.pop_back(); //Remove destination hex
		return ret;
	};

	RETURN_IF_NOT_BATTLE(false);
	auto checkNeeded = !sameSideOfWall(from, dest);
	bool pathHasWall = false;
	bool pathHasMoat = false;

	for(const auto & hex : getShortestPath(from, dest))
	{
		pathHasWall |= isTileBlocked(hex);
		if(!checkMoat)
			continue;

		auto obstacles = battleGetAllObstaclesOnPos(hex, false);

		if(hex.toInt() != BattleHex::GATE_BRIDGE || (battleIsGatePassable()))
			for(const auto & obst : obstacles)
				if(obst->obstacleType ==  CObstacleInstance::MOAT)
					pathHasMoat |= true;
	}

	return checkNeeded && ( (checkWall && pathHasWall) || (checkMoat && pathHasMoat) );
}

bool CBattleInfoCallback::battleHasWallPenalty(const IBonusBearer * shooter, const BattleHex & shooterPosition, const BattleHex & destHex) const
{
	RETURN_IF_NOT_BATTLE(false);
	if(battleGetFortifications().wallsHealth == 0)
		return false;

	const std::string cachingStrNoWallPenalty = "type_NO_WALL_PENALTY";
	static const auto selectorNoWallPenalty = Selector::type()(BonusType::NO_WALL_PENALTY);

	if(shooter->hasBonus(selectorNoWallPenalty, cachingStrNoWallPenalty))
		return false;

	const auto shooterOutsideWalls = shooterPosition < lineToWallHex(shooterPosition.getY());

	return shooterOutsideWalls && battleHasPenaltyOnLine(shooterPosition, destHex, true, false);
}

std::vector<PossiblePlayerBattleAction> CBattleInfoCallback::getClientActionsForStack(const CStack * stack, const BattleClientInterfaceData & data)
{
	RETURN_IF_NOT_BATTLE(std::vector<PossiblePlayerBattleAction>());
	std::vector<PossiblePlayerBattleAction> allowedActionList;
	if(data.tacticsMode) //would "if(battleGetTacticDist() > 0)" work?
	{
		allowedActionList.push_back(PossiblePlayerBattleAction::MOVE_TACTICS);
		allowedActionList.push_back(PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK);
	}
	else
	{
		// Pursuit is the movement-only tail of an activation. Keep information
		// actions client-side, but never advertise another attack, creature spell,
		// Gate, Wait, or other creature action while its allowance is pending.
		if(stack->pursuitMovementRemaining > 0)
		{
			if(stack->canMove())
				allowedActionList.push_back(PossiblePlayerBattleAction::MOVE_STACK);
			return allowedActionList;
		}
		if(newHorizonsCreatureAbilitySuppression::suppressionLevel(*stack) > 0)
		{
			if(stack->isMeleeAttacker())
			{
				allowedActionList.push_back(PossiblePlayerBattleAction::ATTACK);
				allowedActionList.push_back(PossiblePlayerBattleAction::WALK_AND_ATTACK);
			}
			if(stack->canMove() && stack->getMovementRange(0))
				allowedActionList.push_back(PossiblePlayerBattleAction::MOVE_STACK);
			return allowedActionList;
		}
		if(battleHasPendingRangedFollowUp(stack))
		{
			if(battleCanTakeRangedFollowUp(stack))
				allowedActionList.push_back(PossiblePlayerBattleAction::SHOOT);
			return allowedActionList;
		}
		if(stack->canCast()) //TODO: check for battlefield effects that prevent casting?
		{
			if(stack->hasBonusOfType(BonusType::SPELLCASTER))
			{
				for(const auto & spellID : data.creatureSpellsToCast)
				{
					const CSpell *spell = spellID.toSpell();
					PossiblePlayerBattleAction act = getCasterAction(spell, stack, spells::Mode::CREATURE_ACTIVE);
					if(act.get() != PossiblePlayerBattleAction::INVALID)
						allowedActionList.push_back(act);
				}
			}
			if(stack->hasBonusOfType(BonusType::RANDOM_SPELLCASTER))
				allowedActionList.push_back(PossiblePlayerBattleAction::RANDOM_GENIE_SPELL);
		}
		if(battleCanShoot(stack))
			allowedActionList.push_back(PossiblePlayerBattleAction::SHOOT);
		if(stack->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE))
			allowedActionList.push_back(PossiblePlayerBattleAction::ATTACK_AND_RETURN);
		if(stack->hasBonusOfType(BonusType::LONG_WEAPON))
			allowedActionList.push_back(PossiblePlayerBattleAction::LONG_WEAPON_ATTACK);

		if(stack->isMeleeAttacker())
		{
			allowedActionList.push_back(PossiblePlayerBattleAction::ATTACK);
			allowedActionList.push_back(PossiblePlayerBattleAction::WALK_AND_ATTACK);
		}

		if(stack->canMove() && stack->getMovementRange(0)) //probably no reason to try move war machines or bound stacks
			allowedActionList.push_back(PossiblePlayerBattleAction::MOVE_STACK);

		const auto * hero = battleGetFightingHero(stack->unitSide());
		const auto * siegedTown = battleGetDefendedTown();
		if(siegedTown && siegedTown->fortificationsLevel().wallsHealth > 0 && stack->hasBonusOfType(BonusType::CATAPULT)) //TODO: check shots
			allowedActionList.push_back(PossiblePlayerBattleAction::CATAPULT);
		if(stack->hasBonusOfType(BonusType::HEALER))
			allowedActionList.push_back(PossiblePlayerBattleAction::HEAL);
		if(hero && stack->creatureId().toCreature()->getFactionID() == FactionID::INFERNO)
		{
			const int rank = hero->getPerkSkillRank("new-horizons:demonicGating");
			const auto & reserve = getBattle()->getDemonicReserve(stack->unitSide());
			const bool eligible = rank > 0 && std::ranges::any_of(reserve, [this, rank](const auto & entry)
			{
				const auto category = battleGetCreatureCategory(entry.first);
				return entry.second > 0 && category && static_cast<int>(category->category) < rank;
			});
			if(eligible)
				allowedActionList.push_back(PossiblePlayerBattleAction::DEMONIC_GATE);
		}
		if(stack->hasBonusOfType(BonusType::ADJACENT_SPELLCASTER))
		{
			SpellID spellID = stack->getBonus(Selector::type()(BonusType::ADJACENT_SPELLCASTER))->subtype.as<SpellID>();
			if(stack->canCast()) //TODO: check for battlefield effects that prevent casting?
				allowedActionList.push_back(PossiblePlayerBattleAction(PossiblePlayerBattleAction::WALK_AND_SPELLCAST, spellID));
		}
	}

	return allowedActionList;
}

PossiblePlayerBattleAction CBattleInfoCallback::getCasterAction(const CSpell * spell, const spells::Caster * caster, spells::Mode mode) const
{
	RETURN_IF_NOT_BATTLE(PossiblePlayerBattleAction::INVALID);

	std::optional<newHorizonsPuppetMaster::ActionControllerCaster> actionControllerCaster;
	const spells::Caster * effectiveCaster = caster;
	if(mode == spells::Mode::CREATURE_ACTIVE)
	{
		if(const auto * unit = dynamic_cast<const battle::Unit *>(caster))
		{
			actionControllerCaster.emplace(caster, battleGetActionController(unit));
			effectiveCaster = &*actionControllerCaster;
		}
	}

	const spells::BattleCast cast(this, effectiveCaster, mode, spell);
	if(spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL)
	{
		// Life Drain is a human hero spell with an ordered enemy/friendly pair;
		// it must not fall through to the legacy Sacrifice two-target selector.
		if(mode == spells::Mode::HERO)
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::LIFE_DRAIN, spell->id);
		return PossiblePlayerBattleAction::INVALID;
	}

	auto targetTypes = spell->battleMechanics(&cast)->getTargetTypes();

	if(targetTypes.empty() || targetTypes.front() == spells::AimType::NOTHING)
		return PossiblePlayerBattleAction(PossiblePlayerBattleAction::NO_LOCATION, spell->id);

	if(targetTypes.size() >= 2)
	{
		if(targetTypes[1] == spells::AimType::CREATURE)
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::SACRIFICE, spell->id);
		if(targetTypes[1] == spells::AimType::LOCATION)
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::TELEPORT, spell->id);
	}

	switch(targetTypes.front())
	{
		case spells::AimType::CREATURE:
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::AIMED_SPELL_CREATURE, spell->id);
		case spells::AimType::OBSTACLE:
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::OBSTACLE, spell->id);
		case spells::AimType::LOCATION:
		{
			const CSpell::TargetInfo ti(spell, effectiveCaster->getSpellSchoolLevel(spell), mode);
			if(ti.clearAffected)
				return PossiblePlayerBattleAction(PossiblePlayerBattleAction::FREE_LOCATION, spell->id);
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::ANY_LOCATION, spell->id);
		}
		default:
			return PossiblePlayerBattleAction(PossiblePlayerBattleAction::NO_LOCATION, spell->id);
	}
}

BattleHexArray CBattleInfoCallback::battleGetAttackedHexes(const battle::Unit * attacker, const BattleHex & destinationTile, const BattleHex & attackerPos) const
{
	BattleHexArray attackedHexes;
	RETURN_IF_NOT_BATTLE(attackedHexes);

	AttackableTiles at;
	try
	{
		at = getPotentiallyAttackableHexes(attacker, destinationTile, attackerPos);
	}
	catch(const std::runtime_error &)
	{
		return attackedHexes;
	}

	for (const BattleHex & tile : at.hostileCreaturePositions)
	{
		const auto * st = battleGetUnitByPos(tile, true);
		if(st && battleGetOwner(st) != battleGetOwner(attacker) && !st->isInvincible()) //only hostile stacks - does it work well with Berserk?
		{
			attackedHexes.insert(tile);
		}
	}
	for (const BattleHex & tile : at.friendlyCreaturePositions)
	{
		const auto * st = battleGetUnitByPos(tile, true);
		if(st && !st->isInvincible()) //friendly stacks can also be damaged by Dragon Breath
		{
			attackedHexes.insert(tile);
		}
	}
	return attackedHexes;
}

const CStack* CBattleInfoCallback::battleGetStackByPos(const BattleHex & pos, bool onlyAlive) const
{
	RETURN_IF_NOT_BATTLE(nullptr);
	for(const auto * s : battleGetAllStacks(true))
		if(s->getHexes().contains(pos) && (!onlyAlive || s->alive()))
			return s;

	return nullptr;
}

const battle::Unit * CBattleInfoCallback::battleGetUnitByPos(const BattleHex & pos, bool onlyAlive) const
{
	RETURN_IF_NOT_BATTLE(nullptr);

	auto ret = battleGetUnitsIf([=](const battle::Unit * unit)
	{
		return !unit->isGhost()
			&& unit->coversPos(pos)
			&& (!onlyAlive || unit->alive());
	});

	if(!ret.empty())
		return ret.front();
	else
		return nullptr;
}

battle::Units CBattleInfoCallback::battleAliveUnits() const
{
	return battleGetUnitsIf([](const battle::Unit * unit)
	{
		return unit->isValidTarget(false);
	});
}

battle::Units CBattleInfoCallback::battleAliveUnits(BattleSide side) const
{
	return battleGetUnitsIf([=](const battle::Unit * unit)
	{
		return unit->isValidTarget(false) && unit->unitSide() == side;
	});
}

using namespace battle;

static const battle::Unit * takeOneUnit(battle::Units & allUnits, const int turn, BattleSide & sideThatLastMoved, int phase)
{
	const battle::Unit * returnedUnit = nullptr;
	size_t currentUnitIndex = 0;

	for(size_t i = 0; i < allUnits.size(); i++)
	{
		int32_t currentUnitInitiative = -1;
		int32_t returnedUnitInitiative = -1;

		if(returnedUnit)
			returnedUnitInitiative = returnedUnit->getInitiative(turn);

		if(!allUnits[i])
			continue;

		auto currentUnit = allUnits[i];
		currentUnitInitiative = currentUnit->getInitiative(turn);

		switch(phase)
		{
		case BattlePhases::NORMAL: // Faster first, attacker priority, higher slot first
			if(returnedUnit == nullptr || currentUnitInitiative > returnedUnitInitiative)
			{
				returnedUnit = currentUnit;
				currentUnitIndex = i;
			}
			else if(currentUnitInitiative == returnedUnitInitiative)
			{
				if(sideThatLastMoved == BattleSide::NONE && turn <= 0 && currentUnit->unitSide() == BattleSide::ATTACKER
					&& !(returnedUnit->unitSide() == currentUnit->unitSide() && returnedUnit->unitSlot() < currentUnit->unitSlot())) // Turn 0 attacker priority
				{
					returnedUnit = currentUnit;
					currentUnitIndex = i;
				}
				else if(sideThatLastMoved != BattleSide::NONE && currentUnit->unitSide() != sideThatLastMoved
					&& !(returnedUnit->unitSide() == currentUnit->unitSide() && returnedUnit->unitSlot() < currentUnit->unitSlot())) // Alternate equal speeds units
				{
					returnedUnit = currentUnit;
					currentUnitIndex = i;
				}
			}
			break;
		case BattlePhases::WAIT_MORALE: // Slower first, higher slot first
		case BattlePhases::WAIT:
			if(returnedUnit == nullptr || currentUnitInitiative < returnedUnitInitiative)
			{
				returnedUnit = currentUnit;
				currentUnitIndex = i;
			}
			else if(currentUnitInitiative == returnedUnitInitiative && sideThatLastMoved != BattleSide::NONE && currentUnit->unitSide() != sideThatLastMoved
				&& !(returnedUnit->unitSide() == currentUnit->unitSide() && returnedUnit->unitSlot() < currentUnit->unitSlot())) // Alternate equal speeds units
			{
				returnedUnit = currentUnit;
				currentUnitIndex = i;
			}
			break;
		default:
			break;
		}
	}

	if(!returnedUnit)
		return nullptr;

	allUnits[currentUnitIndex] = nullptr;

	return returnedUnit;
}

void CBattleInfoCallback::battleGetTurnOrder(std::vector<battle::Units> & turns, const size_t maxUnits, const int maxTurns, const int turn, BattleSide sideThatLastMoved) const
{
	RETURN_IF_NOT_BATTLE();

	if(maxUnits == 0 && maxTurns == 0)
	{
		logGlobal->error("Attempt to get infinite battle queue");
		return;
	}

	auto actualTurn = turn > 0 ? turn : 0;

	auto turnsIsFull = [&]() -> bool
	{
		if(maxUnits == 0)
			return false;//no limit

		size_t turnsSize = 0;
		for(const auto & oneTurn : turns)
			turnsSize += oneTurn.size();
		return turnsSize >= maxUnits;
	};

	turns.emplace_back();

	// We'll split creatures with remaining movement to 4 buckets (SIEGE, NORMAL, WAIT_MORALE, WAIT)
	std::array<battle::Units, BattlePhases::NUMBER_OF_PHASES> phases; // Access using BattlePhases enum

	const battle::Unit * activeUnit = battleActiveUnit();
	// Time Stop removes a unit from every ordinary action path, but it must
	// still consume one queue slot so that a battle containing only stopped
	// units can advance to the next hero-action window.  BattleInfo marks that
	// synthetic activation as moved for this round; this is scheduling state,
	// not permission to move or perform an action.
	const auto stoppedTurnReady = [](const battle::Unit * unit)
	{
		return unit && unit->alive() && unit->isTimeStopped() && !unit->timeStopTurnConsumed();
	};

	if(activeUnit)
	{
		//its first turn and active unit hasn't taken any action yet - must be placed at the beginning of queue, no matter what
		if(turn == 0 && (activeUnit->willMove() || stoppedTurnReady(activeUnit)))
		{
			turns.back().push_back(activeUnit);
			if(turnsIsFull())
				return;
		}

		//its first or current turn, turn priority for active stack side
		//TODO: what if active stack mind-controlled?
		if(turn <= 0 && sideThatLastMoved == BattleSide::NONE)
			sideThatLastMoved = activeUnit->unitSide();
	}

	auto allUnits = battleGetUnitsIf([](const battle::Unit * unit)
	{
		return !unit->isGhost();
	});

	// If no unit will be EVER! able to move, battle is over.
	if(!vstd::contains_if(allUnits, [&stoppedTurnReady](const battle::Unit * unit)
	{
		return unit->willMove(100000) || stoppedTurnReady(unit);
	})) //little evil, but 100000 should be enough for all effects to disappear
	{
		turns.clear();
		return;
	}

	for(const auto * unit : allUnits)
	{
		if((actualTurn == 0 && !unit->willMove() && !stoppedTurnReady(unit)) //we are considering current round and unit won't move
		|| (actualTurn > 0 && !unit->canMove(turn)) //unit won't be able to move in later rounds
		|| (actualTurn == 0 && unit == activeUnit && !turns.at(0).empty() && unit == turns.front().front())) //it's active unit already added at the beginning of queue
		{
			continue;
		}

		int unitPhase = unit->battleQueuePhase(turn);

		phases[unitPhase].push_back(unit);
	}

	std::ranges::sort(phases[BattlePhases::SIEGE], CMP_stack(BattlePhases::SIEGE, actualTurn, sideThatLastMoved));
	std::copy(phases[BattlePhases::SIEGE].begin(), phases[BattlePhases::SIEGE].end(), std::back_inserter(turns.back()));

	if(turnsIsFull())
		return;

	for(uint8_t phase = BattlePhases::NORMAL; phase < BattlePhases::NUMBER_OF_PHASES; phase++)
		std::ranges::sort(phases[phase], CMP_stack(phase, actualTurn, sideThatLastMoved));

	uint8_t phase = BattlePhases::NORMAL;
	while(!turnsIsFull() && phase < BattlePhases::NUMBER_OF_PHASES)
	{
		const battle::Unit * currentUnit = nullptr;
		if(phases[phase].empty())
			phase++;
		else
		{
			currentUnit = takeOneUnit(phases[phase], actualTurn, sideThatLastMoved, phase);
			if(!currentUnit)
			{
				phase++;
			}
			else
			{
				turns.back().push_back(currentUnit);
				sideThatLastMoved = currentUnit->unitSide();
			}
		}
	}

	if(sideThatLastMoved == BattleSide::NONE)
		sideThatLastMoved = BattleSide::ATTACKER;

	if(!turnsIsFull() && (maxTurns == 0 || turns.size() < maxTurns))
		battleGetTurnOrder(turns, maxUnits, maxTurns, actualTurn + 1, sideThatLastMoved);
}

BattleHexArray CBattleInfoCallback::battleGetAvailableHexes(const battle::Unit * unit, bool obtainMovementRange) const
{

	RETURN_IF_NOT_BATTLE(BattleHexArray());
	if(!unit->getPosition().isValid()) //turrets
		return BattleHexArray();

	auto reachability = getReachability(unit);

	return battleGetAvailableHexes(reachability, unit, obtainMovementRange);
}

BattleHexArray CBattleInfoCallback::battleGetAvailableHexes(const ReachabilityInfo & cache, const battle::Unit * unit, bool obtainMovementRange) const
{
	BattleHexArray ret;

	RETURN_IF_NOT_BATTLE(ret);
	if(!unit->getPosition().isValid()) //turrets
		return ret;

	auto unitSpeed = unit->getMovementRange(0);
	if(const auto * state = dynamic_cast<const CUnitState *>(unit);
		state && state->pursuitMovementRemaining > 0)
		unitSpeed = std::min<int32_t>(unitSpeed, state->pursuitMovementRemaining);

	const bool tacticsPhase = battleTacticDist() && battleGetTacticsSide() == unit->unitSide();

	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		// If obstacles or other stacks makes movement impossible, it can't be helped.
		if(!cache.isReachable(i))
			continue;

		if(tacticsPhase && !obtainMovementRange) // if obtainMovementRange requested do not return tactics range
		{
			// Stack has to perform tactic-phase movement -> can enter any reachable tile within given range
			if(!isInTacticRange(i, *unit))
				continue;
		}
		else
		{
			// Not tactics phase -> destination must be reachable and within unit range.
			if(cache.distances[i] > static_cast<int>(unitSpeed))
				continue;
		}

		ret.insert(i);
	}

	return ret;
}

BattleHexArray CBattleInfoCallback::battleGetOccupiableHexes(const battle::Unit * unit, bool obtainMovementRange) const
{
	return battleGetOccupiableHexes(battleGetAvailableHexes(unit, obtainMovementRange), unit);
}

BattleHexArray CBattleInfoCallback::battleGetOccupiableHexes(const BattleHexArray & availableHexes, const battle::Unit * unit) const
{
	RETURN_IF_NOT_BATTLE(BattleHexArray());
	if (!unit)
		throw std::runtime_error("Undefined unit in battleGetOccupiableHexes!");

	if (unit->doubleWide())
	{ 
		auto occupiableHexes = BattleHexArray(availableHexes);
		for (auto hex : availableHexes)
			occupiableHexes.insert(unit->occupiedHex(hex));
		return occupiableHexes;
	}
	return availableHexes;
}

BattleHex CBattleInfoCallback::fromWhichHexAttack(const battle::Unit * attacker, const BattleHex & target, const BattleHex::EDir & direction, bool allowLongWeapon) const
{
	RETURN_IF_NOT_BATTLE(BattleHex::INVALID);
	if (!attacker)
		throw std::runtime_error("Undefined attacker in fromWhichHexAttack!");

	if (!target.isValid() || direction == BattleHex::NONE)
		return BattleHex::INVALID;

	if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
	{
		const auto longLine = getLongWeaponLineHexes(target, direction);
		if(longLine)
		{
			const auto [middleHex, longAttackFrom] = *longLine;
			if(attacker->coversPos(longAttackFrom) && isLongWeaponMiddleHexClear(*this, middleHex))
				return attacker->getPosition();
		}
	}

	// Long Reach is a normal melee attack from any reachable position in range.
	// It is independent of the direction used by legacy Long Weapon attacks.
	if(attacker->hasBonusOfType(BonusType::LONG_REACH))
	{
		const auto * defender = battleGetUnitByPos(target, false);
		if(defender && defender->alive() && !defender->isDead()
			&& !defender->isInvincible()
			&& !isMeleeAttackPossible(attacker, defender))
		{
			const auto reachability = getReachability(attacker);
			const auto availableHexes = battleGetAvailableHexes(reachability, attacker, false);
			const auto stoppingHexes = getStoppers(reachability.params.perspective);

			auto endsOnStoppingHazard = [&](const BattleHex & position)
			{
				if(position == attacker->getPosition() || attacker->hasBonusOfType(BonusType::FLYING))
					return false;

				for(const auto & occupiedHex : attacker->getHexes(position))
					if(stoppingHexes.contains(occupiedHex))
						return true;

				return false;
			};

			BattleHex bestPosition = BattleHex::INVALID;
			uint32_t bestMovementCost = ReachabilityInfo::INFINITE_DIST;
			for(const auto & candidate : availableHexes)
			{
				if(endsOnStoppingHazard(candidate)
					|| isMeleeAttackPossible(attacker, defender, candidate, defender->getPosition())
					|| !isMeleeAttackPossibleWithLongReach(attacker, defender, candidate, defender->getPosition()))
					continue;

				const auto movementCost = reachability.distances[candidate.toInt()];
				if(movementCost < bestMovementCost)
				{
					bestPosition = candidate;
					bestMovementCost = movementCost;
				}
			}

			if(bestPosition.isValid())
				return bestPosition;
		}
	}

	bool isAttacker = attacker->unitSide() == BattleSide::ATTACKER;
	if (attacker->doubleWide())
	{
		if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
		{
			const auto longLine = getLongWeaponLineHexes(target, direction);
			if(longLine)
			{
				const auto [middleHex, longAttackFrom] = *longLine;
				if(isLongWeaponMiddleHexClear(*this, middleHex))
				{
					const auto availableHexes = battleGetAvailableHexes(attacker, false);
					if(availableHexes.contains(longAttackFrom))
						return longAttackFrom;
				}
			}
		}

		// We need to find position of right hex of double-hex creature (or left for defending side)
		// | TOP_LEFT | TOP_RIGHT |  RIGHT  |BOTTOM_RIGHT|BOTTOM_LEFT|  LEFT   |  TOP   | BOTTOM
		// |  o o -   |    - o o  |  - -    |   - -      |    - -    |    - -  |  o o   |   - -
		// |   - x -  |   - x -   | - x o o |  - x -     |   - x -   | o o x - | - x -  |  - x -
		// |    - -   |    - -    |  - -    |   - o o    |  o o -    |    - -  |  - -   |   o o

		try
		{
			switch (direction)
			{
				case BattleHex::TOP_LEFT:
				case BattleHex::LEFT:
				case BattleHex::BOTTOM_LEFT:
					return target.cloneInDirection(direction, false)
						.cloneInDirection(isAttacker ? BattleHex::NONE : BattleHex::LEFT, false);

				case BattleHex::TOP_RIGHT:
				case BattleHex::RIGHT:
				case BattleHex::BOTTOM_RIGHT:
					return target.cloneInDirection(direction, false)
						.cloneInDirection(isAttacker ? BattleHex::RIGHT : BattleHex::NONE, false);

				case BattleHex::TOP:
					return target.cloneInDirection(isAttacker ? BattleHex::TOP_RIGHT : BattleHex::TOP_LEFT, false);

				case BattleHex::BOTTOM:
					return target.cloneInDirection(isAttacker ? BattleHex::BOTTOM_RIGHT : BattleHex::BOTTOM_LEFT, false);

				default:
					return BattleHex::INVALID;
			}
		}
		catch(const std::out_of_range &)
		{
			return BattleHex::INVALID;
		}
	}
	if (direction == BattleHex::TOP || direction == BattleHex::BOTTOM)
		return BattleHex::INVALID;

	BattleHex adjacentAttackFrom = BattleHex::INVALID;
	try
	{
		adjacentAttackFrom = target.cloneInDirection(direction, false);
	}
	catch(const std::out_of_range &)
	{
		return BattleHex::INVALID;
	}

	if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON))
	{
		const auto longLine = getLongWeaponLineHexes(target, direction);
		if(!longLine)
			return adjacentAttackFrom;

		const auto [middleHex, longAttackFrom] = *longLine;

		if(isLongWeaponMiddleHexClear(*this, middleHex))
		{
			const auto availableHexes = battleGetAvailableHexes(attacker, false);
			const bool longReachable = availableHexes.contains(longAttackFrom);

			if(longReachable)
				return longAttackFrom;
		}
	}

	return adjacentAttackFrom;
}

BattleHex CBattleInfoCallback::toWhichHexMove(const battle::Unit * unit, const BattleHex & position) const
{
	return toWhichHexMove(battleGetAvailableHexes(unit, false), unit, position);
}

BattleHex CBattleInfoCallback::toWhichHexMove(const BattleHexArray & availableHexes, const battle::Unit * unit, const BattleHex & position) const
{
	RETURN_IF_NOT_BATTLE(false);

	if (!unit)
		throw std::runtime_error("Undefined unit in toWhichHexMove!");
	if (!position.isValid())
		return BattleHex::INVALID;

	if (availableHexes.contains(position))
		return position;
	if (unit->doubleWide())
	{
		auto headPosition = position.cloneInDirection(unit->headDirection(), false);
		if (availableHexes.contains(headPosition))
			return headPosition;
	}
	return BattleHex::INVALID;
}

bool CBattleInfoCallback::battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position) const
{
	return battleCanAttackHex(battleGetAvailableHexes(attacker, false), attacker, position);
}

bool CBattleInfoCallback::battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position) const
{
	for (auto direction = 0; direction < 8; direction++)
	{
		if (battleCanAttackHex(availableHexes, attacker, position, BattleHex::EDir(direction)))
			return true;
	}
	return false;
}

bool CBattleInfoCallback::battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const
{
	return battleCanAttackHex(battleGetAvailableHexes(attacker, false), attacker, position, direction);
}

bool CBattleInfoCallback::battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const
{
	RETURN_IF_NOT_BATTLE(false);

	if (!attacker)
		throw std::runtime_error("Undefined attacker in battleCanAttackHex!");

	if (!position.isValid() || direction == BattleHex::NONE)
		return false;

	const auto canAttackFrom = [&](BattleHex fromHex)
	{
		//check if the attack is performed from an available hex
		if (!fromHex.isValid() || !availableHexes.contains(fromHex))
			return false;

		//if the movement ends in an obstacle, check if the obstacle allows attacking from that position
		if (attacker->getPosition() != fromHex)
		{
			if (!attacker->hasBonusOfType(BonusType::FLYING))
			{
				for (const auto & obstacle : battleGetAllObstacles())
				{
					if (obstacle->getStoppingTile().contains(fromHex))
						return false;
					if (attacker->doubleWide() && obstacle->getStoppingTile().contains(attacker->occupiedHex(fromHex)))
						return false;
				}
			}
			const battle::Unit * defender = battleGetUnitByPos(position, false); //Do not allow to target corpses when standing on them (a WALK_AND_SPELLCAST action)
			if (defender && defender->isDead() && defender->coversPos(fromHex))
				return false;
		}

		return true;
	};

	// Keep the ordinary adjacency/Long Weapon checks below direction-specific.
	// Long Reach is a separate non-adjacent melee option from any legal reachable
	// attack position and does not imply the legacy Long Weapon rules.
	if(attacker->hasBonusOfType(BonusType::LONG_REACH))
	{
		const battle::Unit * defender = battleGetUnitByPos(position, false);
		if(defender && defender->alive() && !defender->isDead() && !defender->isInvincible())
			for(const auto & attackFrom : availableHexes)
				if(canAttackFrom(attackFrom)
					&& !isMeleeAttackPossible(attacker, defender, attackFrom, defender->getPosition())
					&& isMeleeAttackPossibleWithLongReach(attacker, defender, attackFrom, defender->getPosition()))
					return true;
	}

	BattleHex fromHex = fromWhichHexAttack(attacker, position, direction);
	if (canAttackFrom(fromHex))
		return true;

	if(attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
	{
		const auto longLine = getLongWeaponLineHexes(position, direction);
		if(!longLine)
			return false;

		const auto [middleHex, longAttackFrom] = *longLine;

		if (isLongWeaponMiddleHexClear(*this, middleHex) && canAttackFrom(longAttackFrom))
			return true;
	}

	return false;
}

BattleHexArray CBattleInfoCallback::battleGetSkirmisherTargetHexes(const battle::Unit * attacker) const
{
	RETURN_IF_NOT_BATTLE(BattleHexArray{});
	if(!attacker || battleTacticDist())
		return {};
	const auto * hero = battleGetOwnerHero(attacker);
	if(!newHorizonsArchery::canUseSkirmisher(hero, attacker))
		return {};

	const int movementLimit = static_cast<int>(attacker->getMovementRange(0) / 2);
	if(movementLimit <= 0)
		return {};
	const auto reachability = getReachability(attacker);
	const BattleHexArray reachable = battleGetAvailableHexes(reachability, attacker, false);
	BattleHexArray firingPositions;
	for(const BattleHex & candidate : reachable)
	{
		if(candidate == attacker->getPosition())
			continue;
		const uint32_t distance = reachability.distances[candidate.toInt()];
		if(distance == ReachabilityInfo::INFINITE_DIST || distance == 0 || distance > static_cast<uint32_t>(movementLimit))
			continue;
		firingPositions.insert(candidate);
	}
	if(firingPositions.empty())
		return {};

	auto projected = attacker->acquireState();
	BattleHexArray result;
	for(const auto * target : battleAliveUnits())
	{
		// battleMatchOwner is true for hostile stacks; reject allies and accept enemies.
		if(!target || target->isInvincible() || !battleMatchActionController(attacker, target))
			continue;
		for(const BattleHex & targetHex : target->getHexes())
		{
			for(const BattleHex & candidate : firingPositions)
			{
				projected->setPosition(candidate);
				if(battleCanShootAction(projected.get(), targetHex))
				{
					result.insert(targetHex);
					break;
				}
			}
		}
	}
	return result;
}

BattleHexArray CBattleInfoCallback::battleGetSkirmisherAttackFromHexes(const battle::Unit * attacker,
	const BattleHex & targetHex, ReachabilityInfo::TDistances * distances) const
{
	if(distances)
		distances->fill(ReachabilityInfo::INFINITE_DIST);
	RETURN_IF_NOT_BATTLE(BattleHexArray{});
	if(!attacker || !targetHex.isAvailable() || battleTacticDist())
		return {};
	const auto * hero = battleGetOwnerHero(attacker);
	if(!newHorizonsArchery::canUseSkirmisher(hero, attacker))
		return {};
	const auto * target = battleGetUnitByPos(targetHex);
	if(!target || !target->alive() || target->isInvincible() || !battleMatchActionController(attacker, target))
		return {};

	const int movementLimit = static_cast<int>(attacker->getMovementRange(0) / 2);
	if(movementLimit <= 0)
		return {};
	const auto reachability = getReachability(attacker);
	const BattleHexArray reachable = battleGetAvailableHexes(reachability, attacker, false);
	BattleHexArray result;
	auto moved = attacker->acquireState();
	for(const BattleHex & candidate : reachable)
	{
		if(candidate == attacker->getPosition())
			continue;
		const uint32_t distance = reachability.distances[candidate.toInt()];
		if(distance == ReachabilityInfo::INFINITE_DIST || distance == 0 || distance > static_cast<uint32_t>(movementLimit))
			continue;

		moved->setPosition(candidate);
		if(!battleCanShootAction(moved.get(), targetHex))
			continue;
		result.insert(candidate);
		if(distances)
			(*distances)[candidate.toInt()] = distance;
	}
	return result;
}

bool CBattleInfoCallback::battleCanSkirmisherAttackFromHex(const battle::Unit * attacker,
	const BattleHex & targetHex, const BattleHex & attackFromHex) const
{
	if(!attackFromHex.isAvailable())
		return false;
	const auto candidates = battleGetSkirmisherAttackFromHexes(attacker, targetHex);
	return vstd::contains(candidates, attackFromHex);
}

bool CBattleInfoCallback::battleCanAttackUnit(const battle::Unit * attacker, const battle::Unit * target) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(battleTacticDist())
		return false;

	if (!attacker)
		throw std::runtime_error("Undefined attacker in battleCanAttackUnit!");

	if(!target || target->isInvincible()
		|| (target->hasBonusOfType(BonusType::SANCTIFIED) && battleMatchOwner(attacker, target)))
		return false;

	if(attacker == target || !battleMatchOwner(attacker, target))
		return false;

	if(!target->alive())
		return false;
	return attacker->isMeleeAttacker();
}

bool CBattleInfoCallback::battleCanAttackUnitAction(const battle::Unit * attacker,
	const battle::Unit * target) const
{
	RETURN_IF_NOT_BATTLE(false);
	if(battleTacticDist())
		return false;
	if(!attacker)
		throw std::runtime_error("Undefined attacker in battleCanAttackUnitAction!");
	if(!target || target->isInvincible()
		|| (target->hasBonusOfType(BonusType::SANCTIFIED)
			&& battleMatchActionController(attacker, target, false)))
		return false;
	if(attacker == target || !battleMatchActionController(attacker, target, false))
		return false;
	if(!target->alive())
		return false;
	return attacker->isMeleeAttacker();
}

bool CBattleInfoCallback::battleCanShoot(const battle::Unit * attacker) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(battleTacticDist()) //no shooting during tactics
		return false;

	if (!attacker)
		return false;
	if (attacker->isCatapult()) //catapult cannot attack creatures
		return false;

	if (!attacker->canShoot())
		return false;

	return attacker->canShootBlocked() || !battleIsUnitBlocked(attacker);
}

bool CBattleInfoCallback::battleCanTargetEmptyHex(const battle::Unit * attacker) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(!LIBRARY->engineSettings()->getBoolean(EGameSettings::COMBAT_AREA_SHOT_CAN_TARGET_EMPTY_HEX))
		return false;

	if(attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
	{
		auto bonus = attacker->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
		const CSpell * spell = bonus->subtype.as<SpellID>().toSpell();
		spells::BattleCast cast(this, attacker, spells::Mode::SPELL_LIKE_ATTACK, spell);
		BattleHex dummySpellTarget = BattleHex(50); //check arbitrary hex for general spell range since currently there is no general way to access amount of hexes

		if(spell->battleMechanics(&cast)->rangeInHexes(dummySpellTarget).size() > 1)
		{
			return true;
		}
	}

	return false;
}

BattleHexArray CBattleInfoCallback::meleeAttackHexes(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPosition, const BattleHex & defenderPosition) const
{
	BattleHexArray res;
	if(!defender)
		return res;

	BattleHex attackerPos = attackerPosition.isValid() ? attackerPosition : attacker->getPosition();
	BattleHex defenderPos = defenderPosition.isValid() ? defenderPosition : defender->getPosition();

	BattleHexArray defenderHexes = defender->getHexes(defenderPos);
	BattleHexArray attackerHexes = attacker->getHexes(attackerPos);

	for (BattleHex defenderHex : defenderHexes)
	{
		if (attackerHexes.contains(defenderHex))
		{
			logGlobal->debug("CBattleInfoCallback::meleeAttackHexes: defender and attacker positions overlap");
			return res;
		}
	}

	const BattleHexArray attackableHxs = attacker->getSurroundingHexes(attackerPos);

	for (BattleHex defenderHex : defenderHexes)
	{
		if (attackableHxs.contains(defenderHex))
			res.insert(defenderHex);
	}

	return res;
}

bool CBattleInfoCallback::isMeleeAttackPossible(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPos, const BattleHex & defenderPos) const
{
	if(defender->isInvincible())
		return false;

	return !meleeAttackHexes(attacker, defender, attackerPos, defenderPos).empty();
}

bool CBattleInfoCallback::isMeleeAttackPossibleWithLongReach(const battle::Unit * attacker, const battle::Unit * defender,
	const BattleHex & attackerPosition, const BattleHex & defenderPosition) const
{
	if(!attacker || !defender || defender->isInvincible())
		return false;

	if(isMeleeAttackPossible(attacker, defender, attackerPosition, defenderPosition))
		return true;

	const int64_t maximumGap = std::max<int64_t>(0, attacker->valOfBonuses(BonusType::LONG_REACH));
	if(maximumGap == 0)
		return false;

	const BattleHex attackerPos = attackerPosition.isValid() ? attackerPosition : attacker->getPosition();
	const BattleHex defenderPos = defenderPosition.isValid() ? defenderPosition : defender->getPosition();
	const BattleHexArray attackerHexes = attacker->getHexes(attackerPos);
	const BattleHexArray defenderHexes = defender->getHexes(defenderPos);

	// Long Reach is measured between the closest occupied cells. An overlap is
	// never an attack position, even if its zero distance falls within the range.
	for(const BattleHex & attackerHex : attackerHexes)
		if(defenderHexes.contains(attackerHex))
			return false;

	const int64_t maximumDistance = 1 + maximumGap;
	const auto accessibility = getAccessibility();
	BattleHexArray endpointFootprints = attackerHexes;
	endpointFootprints.insert(defenderHexes);
	// Forecasted positions leave the units' live footprints as stale ALIVE_STACK
	// cells in the battle snapshot; those cells are vacated by the proposed move.
	endpointFootprints.insert(attacker->getHexes());
	endpointFootprints.insert(defender->getHexes());

	for(const BattleHex & attackerHex : attackerHexes)
	{
		if(!attackerHex.isAvailable())
			continue;

		for(const BattleHex & defenderHex : defenderHexes)
		{
			if(!defenderHex.isAvailable())
				continue;

			const auto distance = BattleHex::getDistance(attackerHex, defenderHex);
			if(distance > 1 && distance <= maximumDistance
				&& accessibility.hasClearStraightHexRay(attackerHex, defenderHex, endpointFootprints))
				return true;
		}
	}

	return false;
}

bool CBattleInfoCallback::isLongWeaponAttack(const battle::Unit * attacker, const battle::Unit * defender) const
{
	return isLongWeaponAttack(attacker, defender, BattleHex::INVALID);
}

bool CBattleInfoCallback::isLongWeaponAttack(const battle::Unit * attacker, const battle::Unit * defender,
	const BattleHex & attackerPosition) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(!attacker)
		throw std::runtime_error("Undefined attacker in isLongWeaponAttack!");
	if(!defender)
		throw std::runtime_error("Undefined defender in isLongWeaponAttack!");

	if(!attacker->hasBonusOfType(BonusType::LONG_WEAPON))
		return false;

	if(isMeleeAttackPossible(attacker, defender, attackerPosition))
		return false;

	const auto projectedHexes = attackerPosition.isValid()
		? attacker->getHexes(attackerPosition) : attacker->getHexes();
	auto accessibility = getAccessibility();
	if(attackerPosition.isValid() && attackerPosition != attacker->getPosition())
	{
		for(const auto & originalHex : attacker->getHexes())
		{
			if(!originalHex.isAvailable() || projectedHexes.contains(originalHex)
				|| accessibility[originalHex.toInt()] != EAccessibility::ALIVE_STACK
				|| accessibility.isDemonicGateReserved(originalHex))
				continue;
			// Stacks overwrite the gate's base accessibility. Moving the actor
			// must not turn a closed or blocked gate into an open attack corridor.
			if((originalHex == BattleHex::GATE_OUTER || originalHex == BattleHex::GATE_INNER)
				&& battleGetFortifications().wallsHealth > 0
				&& (battleGetGateState() == EGateState::CLOSED || battleGetGateState() == EGateState::BLOCKED))
				continue;
			accessibility[originalHex.toInt()] = EAccessibility::ACCESSIBLE;
		}
	}

	for(const BattleHex & defenderHex : defender->getHexes())
	{
		for(int direction = 0; direction < 6; ++direction)
		{
			const auto longLine = getLongWeaponLineHexes(defenderHex, static_cast<BattleHex::EDir>(direction));
			if(!longLine)
				continue;

			const auto [middleHex, attackerHex] = *longLine;
			if(projectedHexes.contains(attackerHex) && middleHex.isValid()
				&& !projectedHexes.contains(middleHex)
				&& accessibility[middleHex.toInt()] == EAccessibility::ACCESSIBLE)
				return true;
		}
	}

	return false;
}

bool CBattleInfoCallback::battleCanShoot(const battle::Unit * attacker, const BattleHex & dest) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(!dest.isAvailable())
		return false;

	const battle::Unit * defender = battleGetUnitByPos(dest);
	if(!attacker)
		return false;

	bool emptyHexAreaAttack = battleCanTargetEmptyHex(attacker);

	if(!emptyHexAreaAttack)
	{
		if(!defender)
			return false;

		if(defender->isInvincible()
			|| (defender->hasBonusOfType(BonusType::SANCTIFIED) && battleMatchOwner(attacker, defender)))
			return false;
	}

	bool attackerIsBerserk = attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
	if(emptyHexAreaAttack || (defender->alive() && (attackerIsBerserk || battleMatchOwner(attacker, defender))))
	{
		const bool pointBlankAdjacentShot = defender && attacker->isShooter() && attacker->canShoot()
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker)
			&& !attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
			&& newHorizonsArchery::hasPointBlankShot(battleGetOwnerHero(attacker))
			&& battleMatchOwner(attacker, defender)
			&& isMeleeAttackPossible(attacker, defender);
		// Point-Blank Shot is the sole exception to the blocked-shooter gate,
		// and only for an adjacent enemy that can actually be hit as a ranged shot.
		if(battleCanShoot(attacker) || pointBlankAdjacentShot)
		{
			// e.g. Steel Elves - unit shoots freely while blocked, but adjacent units can only be attacked in melee
			if(defender && !canShootAdjacentUnits(attacker) && isMeleeAttackPossible(attacker, defender)
				&& !pointBlankAdjacentShot)
				return false;

			auto limitedRangeBonus = attacker->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
			if(limitedRangeBonus == nullptr)
			{
				return true;
			}

			int shootingRange = limitedRangeBonus->val;

			if(defender)
				return isEnemyUnitWithinSpecifiedRange(attacker->getPosition(), defender, shootingRange);
			else
				return isHexWithinSpecifiedRange(attacker->getPosition(), dest, shootingRange);
		}
	}

	return false;
}

bool CBattleInfoCallback::battleCanShootAction(const battle::Unit * attacker, const BattleHex & dest) const
{
	RETURN_IF_NOT_BATTLE(false);
	if(!dest.isAvailable() || !attacker)
		return false;

	const battle::Unit * defender = battleGetUnitByPos(dest);
	const bool emptyHexAreaAttack = battleCanTargetEmptyHex(attacker);
	if(!emptyHexAreaAttack)
	{
		if(!defender || defender->isInvincible()
			|| (defender->hasBonusOfType(BonusType::SANCTIFIED)
				&& battleMatchActionController(attacker, defender, false)))
			return false;
	}

	if(emptyHexAreaAttack || (defender->alive() && battleMatchActionController(attacker, defender, false)))
	{
		const bool pointBlankAdjacentShot = defender && attacker->isShooter() && attacker->canShoot()
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker)
			&& !attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
			&& newHorizonsArchery::hasPointBlankShot(battleGetOwnerHero(attacker))
			&& battleMatchActionController(attacker, defender, false)
			&& isMeleeAttackPossible(attacker, defender);
		if(battleCanShoot(attacker) || pointBlankAdjacentShot)
		{
			if(defender && !canShootAdjacentUnits(attacker) && isMeleeAttackPossible(attacker, defender)
				&& !pointBlankAdjacentShot)
				return false;

			const auto limitedRangeBonus = attacker->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
			if(!limitedRangeBonus)
				return true;

			const int shootingRange = limitedRangeBonus->val;
			if(defender)
				return isEnemyUnitWithinSpecifiedRange(attacker->getPosition(), defender, shootingRange);
			return isHexWithinSpecifiedRange(attacker->getPosition(), dest, shootingRange);
		}
	}
	return false;
}

RangedAttackPenetration CBattleInfoCallback::battleGetRangedAttackPenetration(
	const BattleAttackInfo & attack) const
{
	RangedAttackPenetration result;
	const auto * currentBattle = getBattle();
	if(!currentBattle || !attack.physicalDamage || !attack.shooting || !attack.attacker || !attack.defender
		|| !attack.defender->alive() || attack.defender->isGhost()
		|| !newHorizonsMagic::rulesActive(currentBattle->getMagicRules()))
		return result;

	const auto attackerSide = playerToSide(battleGetOwner(attack.attacker));
	const auto defenderSide = playerToSide(battleGetOwner(attack.defender));
	if(attackerSide == BattleSide::NONE || defenderSide == BattleSide::NONE || attackerSide == defenderSide)
		return result;

	int validMarks = 0;
	const auto triggers = attack.defender->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
	{
		if(bonus->source != BonusSource::SPELL_EFFECT || !bonus->parameters
			|| (Bonus::NTurns(bonus.get()) && bonus->turnsRemain <= 0))
			continue;
		try
		{
			// Check source spell identity first; only those candidates need the
			// script subtype's resolved name.
			if(bonus->sid.toString() != newHorizonsSorcery::ARCANE_BREACH_EFFECT
				|| bonus->subtype.toString() != newHorizonsSorcery::ARCANE_BREACH_TRIGGER)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		const JsonNode * markParameters = nullptr;
		try
		{
			markParameters = &bonus->parameters->toCustom<JsonNode>();
		}
		catch(const std::exception &)
		{
			continue;
		}
		if(!markParameters->isStruct())
			continue;

		const auto sideParameter = markParameters->Struct().find("beneficiarySide");
		// Lua numbers round-trip as JSON floats. Compare the exact enum value
		// rather than rejecting valid 0.0/1.0 or truncating fractional inputs.
		if(sideParameter == markParameters->Struct().end()
			|| !sideParameter->second.isNumber()
			|| sideParameter->second.Float() != static_cast<int32_t>(attackerSide)
			|| bonus->val <= 0)
			continue;

		const int perMarkBasisPoints = std::min(bonus->val,
			newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS);
		result.creatureDefenseIgnoreBasisPoints = std::min(
			result.creatureDefenseIgnoreBasisPoints + perMarkBasisPoints,
			newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS * newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS);
		if(++validMarks >= newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS)
			break;
	}

	if(validMarks < newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS
		|| !newHorizonsCombatSkills::isPhysicalCreatureAttack(attack.attacker, attack.physicalDamage)
		|| newHorizonsMagic::physicalDamageReductionCapPercent(currentBattle->getMagicRules()) < 0)
		return result;

	const auto * attackerHero = battleGetOwnerHero(attack.attacker);
	if(attackerHero && attackerHero->hasActivePerk(newHorizonsSorcery::SORCERY_MAGIC_SKILL,
		newHorizonsSorcery::ARCANE_BALLISTICS_PERK))
		result.physicalDamageReductionIgnorePercent = newHorizonsSorcery::ARCANE_BALLISTICS_PDR_IGNORE_PERCENT;

	return result;
}

DamageEstimation CBattleInfoCallback::calculateDmgRange(const BattleAttackInfo & info) const
{

	const auto * script = LIBRARY->scriptTypes()->getDamageCalculator();

	// core declares one, and there is no rule for what an attack is worth without it
	if(!script)
		throw std::runtime_error("No damage calculator script is loaded!");

	DamageAttackInfo payload;
	HeroCommand attackerOrderCause = HeroCommand::NONE;
	HeroCommand defenderOrderCause = HeroCommand::NONE;
	std::vector<HeroCommand> attackerOrderCauses;
	std::vector<HeroCommand> defenderOrderCauses;

	payload.attacker = info.attacker;
	payload.defender = info.defender;
	// the script is told where the blow happens rather than left to work it out, so that an
	// attack being weighed reads the same as one being dealt
	payload.attackerHex = info.attackerPos.isValid() ? info.attackerPos : info.attacker->getPosition();
	payload.defenderHex = info.defenderPos.isValid() ? info.defenderPos : info.defender->getPosition();
	payload.chargeDistance = info.chargeDistance;
	payload.shooting = info.shooting;
	payload.physicalDamage = info.physicalDamage;
	payload.activationOutputPercent = battleGetActivationOutputPercent(info.attacker);
	payload.archeryRangedDamageMultiplierPercent = info.archeryRangedDamageMultiplierPercent;
	if(info.shooting && info.physicalDamage && info.attacker && info.attacker->isBallista())
		payload.rangedFollowUpDamagePercent = battleGetRangedFollowUpDamagePercent(info.attacker);
	payload.relentlessAssaultDamagePercent = info.relentlessAssaultDamagePercent;
	const auto * currentBattle = getBattle();
	if(currentBattle)
		payload.physicalDamageReductionCapPercent = newHorizonsMagic::physicalDamageReductionCapPercent(currentBattle->getMagicRules());
	const auto rangedPenetration = battleGetRangedAttackPenetration(info);
	payload.rangedDefenseIgnoreBasisPoints = rangedPenetration.creatureDefenseIgnoreBasisPoints;
	payload.physicalDamageReductionIgnorePercent = rangedPenetration.physicalDamageReductionIgnorePercent;
	if(currentBattle && info.physicalDamage && info.shooting && info.defender
		&& newHorizonsMagic::rulesActive(currentBattle->getMagicRules()))
	{
		const auto galeBonuses = info.defender->getBonusesOfType(BonusType::HEAVENLY_GALE);
		if(galeBonuses && !galeBonuses->empty())
		{
			for(const auto & bonus : *galeBonuses)
				payload.heavenlyGaleDamageReductionBasisPoints = std::max(
					payload.heavenlyGaleDamageReductionBasisPoints, bonus->val);
		}
	}
	payload.targetedRangedCommand = battleIsTargetedRangedCommand(
		info.attacker, info.defender, info.shooting, info.secondaryAttack);
	payload.targetedRangedCommandPercent = battleTargetedRangedCommandPercent(
		info.attacker, info.defender, info.shooting, info.secondaryAttack);
	if(payload.targetedRangedCommandPercent > 0)
	{
		attackerOrderCause = HeroCommand::FOCUS_FIRE;
		attackerOrderCauses.push_back(HeroCommand::FOCUS_FIRE);
	}
	if(info.shooting && info.physicalDamage && info.attacker && info.defender)
	{
		const auto & adjacentHexes = info.attacker->getSurroundingHexes(payload.attackerHex);
		const auto & targetHexes = info.defender->getHexes(payload.defenderHex);
		payload.archeryAdjacentRangedTarget = std::ranges::any_of(targetHexes, [&adjacentHexes](BattleHex hex)
		{
			return adjacentHexes.contains(hex);
		});
		payload.archeryIgnoreAdjacentRangedPenalty = payload.archeryAdjacentRangedTarget
			&& newHorizonsArchery::hasPointBlankShot(battleGetOwnerHero(info.attacker));
	}
	if(payload.targetedRangedCommand && info.physicalDamage
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.attacker)
		&& newHorizonsArchery::hasTargetCaller(battleGetOwnerHero(info.attacker)))
	{
		payload.targetedRangedCommandPercent += newHorizonsArchery::TARGET_CALLER_DAMAGE_PERCENT;
		payload.archeryIgnoreObstaclePenalty = true;
	}
	const auto * archeryHero = info.attacker ? battleGetOwnerHero(info.attacker) : nullptr;
	if(info.physicalDamage && info.shooting && info.attacker && info.attacker->isBallista() && info.defender)
	{
		const auto * controllerHero = battleGetOwnerHero(info.attacker);
		if(controllerHero && controllerHero->hasActivePerk(
			"new-horizons:warMachines", "new-horizons:warMachines.piercingBolts"))
			payload.warMachinesPiercingBoltsDefenseIgnorePercent = PIERCING_BOLTS_DEFENSE_IGNORE_PERCENT;
	}
	const bool ordinaryArcheryShot = info.physicalDamage && info.shooting
		&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker) && archeryHero;
	if(ordinaryArcheryShot)
	{
		if(newHorizonsArchery::hasArmorPiercingShot(archeryHero))
			payload.archeryRangedDefenseIgnorePercent += newHorizonsArchery::ARMOR_PIERCING_DEFENSE_IGNORE_PERCENT;
		payload.archeryHighArc = newHorizonsArchery::hasHighArc(archeryHero);
		if(payload.archeryHighArc)
			payload.archeryIgnoreObstaclePenalty = true;

		if(newHorizonsArchery::hasDeadeye(archeryHero))
		{
			const auto attackerState = info.attacker->acquireState();
			if(attackerState->archeryDeadeyeRound != battleGetRound())
			{
				payload.archeryMaximumCreatureDamage = true;
				payload.archeryRangedDefenseIgnorePercent += newHorizonsArchery::DEADEYE_DEFENSE_IGNORE_PERCENT;
			}
		}

		if(newHorizonsArchery::hasCrossfire(archeryHero) && info.defender)
		{
			const auto attackerSide = playerToSide(battleGetOwner(info.attacker));
			const auto defenderState = std::dynamic_pointer_cast<battle::CUnitState>(info.defender->acquireState());
			if(defenderState && defenderState->archeryCrossfireAvailable(
				attackerSide, info.attacker->unitId(), battleGetRound()))
				payload.archeryCrossfireDamagePercent = newHorizonsArchery::CROSSFIRE_DAMAGE_PERCENT;
		}
	}
	if(payload.targetedRangedCommand && payload.targetedRangedCommandPercent > 0)
		attackerOrderCause = HeroCommand::FOCUS_FIRE;
	if(info.physicalDamage && !info.shooting && info.attacker && info.defender
		&& battleGetOwner(info.attacker) != battleGetOwner(info.defender))
	{
		if(const auto * hero = battleGetOwnerHero(info.attacker))
		{
			const auto hasOffensePerk = [hero](const char * perk)
			{
				return hero->hasActivePerk("new-horizons:offense", perk);
			};
			if(hasOffensePerk("new-horizons:offense.executioner")
				&& info.defender->getAvailableHealth() * 100 < info.defender->getTotalHealth() * 40)
				payload.executionerDamagePercent = EXECUTIONER_DAMAGE_PERCENT;
			if(hasOffensePerk("new-horizons:offense.armorPiercer"))
				payload.meleeDefenseIgnorePercent = ARMOR_PIERCER_DEFENSE_IGNORE_PERCENT;
			if(hasOffensePerk("new-horizons:offense.breakthrough") && info.defender->defended())
			{
				payload.defensiveStanceDamageReductionIgnorePercent = BREAKTHROUGH_DAMAGE_REDUCTION_IGNORE_PERCENT;
				payload.defensiveStanceDefenseBonus = std::max(0, info.defender->getDefense(false)
					- info.defender->getDefenseIgnoringDefensiveStance(false));
			}
		}
	}
	if(info.physicalDamage)
	{
		payload.bloodrageDamagePercent = battleGetBloodrageDamagePercent(info.attacker, info.defender);
		const bool ordinaryCreatureAttack = newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.attacker);
		if(info.shooting && ordinaryCreatureAttack)
			payload.newHorizonsArcheryDamagePercent = newHorizonsCombatSkills::archeryDamagePercent(
				battleGetOwnerHero(info.attacker));
		if(ordinaryCreatureAttack && info.attacker->battlecraftWaitBonusAvailable())
			payload.battlecraftWaitDamagePercent = newHorizonsBattlecraft::waitDamagePercent(
				battleGetOwnerHero(info.attacker), dynamic_cast<const battle::CUnitState *>(info.attacker));
		if(ordinaryCreatureAttack)
			payload.newHorizonsArmorerReductionPercent = newHorizonsCombatSkills::armorerReductionPercent(
				battleGetOwnerHero(info.defender));
		if(info.defender && battleHasFormationFightingProtection(info.defender, info.defenderPos))
			payload.formationFightingReductionPercent = newHorizonsCombatSkills::formationFightingReductionPercent(
				battleGetOwnerHero(info.defender));
		if(ordinaryCreatureAttack && info.defender && info.defender->defended())
		{
			payload.battlecraftDefendReductionPercent = newHorizonsBattlecraft::defendReductionPercent(
				battleGetOwnerHero(info.defender), dynamic_cast<const battle::CUnitState *>(info.defender));
			if(info.shooting)
				payload.paviseDamageReductionPercent = newHorizonsCombatSkills::paviseReductionPercent(
					battleGetOwnerHero(info.defender));
		}
		if(newHorizonsCombatSkills::isPhysicalCreatureAttack(info.attacker, info.physicalDamage)
			&& battleHasBastionProtection(info.defender))
			payload.armorerBastionFinalDamageMultiplier = newHorizonsCombatSkills::BASTION_FINAL_DAMAGE_MULTIPLIER;
		if(battleIsShroudFlankingAttack(info))
		{
			const auto * attackerHero = battleGetOwnerHero(info.attacker);
			payload.shroudFlankingDamagePercent = newHorizonsShroud::flankingDamagePercent(
				newHorizonsShroud::rank(attackerHero))
				+ newHorizonsShroud::backstabDamagePercent(attackerHero)
				+ newHorizonsShroud::ambusherDamagePercent(attackerHero, info.attacker);
			payload.meleeDefenseIgnorePercent += newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
				attackerHero, info.defender, playerToSide(battleGetOwner(info.attacker)));
		}
		if(info.defender && info.defender->defended() && ordinaryCreatureAttack
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.defender)
			&& (!info.shooting || !info.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)))
		{
			const auto reductionForHero = [this](const CGHeroInstance * hero)
			{
				const auto terrain = getBattle()->getTerrainType();
				const bool mireTerrain = newHorizonsBulwark::hasMireborn(hero)
					&& (terrain == TerrainId::SWAMP || terrain == TerrainId::ROUGH);
				return newHorizonsBulwark::reductionBasisPoints(
					newHorizonsBulwark::rank(hero), hero ? hero->getPrimSkillLevel(PrimarySkill::DEFENSE) : 0, mireTerrain);
			};
			const auto * hero = battleGetOwnerHero(info.defender);
			const int baseReduction = reductionForHero(hero);
			const auto defenderState = info.defender->acquireState();
			if(newHorizonsBulwark::hasImmovable(hero) && defenderState
				&& defenderState->bulwarkImmovableRound != battleGetRound())
				payload.bulwarkImmovableFinalDamageMultiplier = 75;
			payload.bulwarkDamageReductionBasisPoints = baseReduction;
			if(baseReduction > 0 && newHorizonsBulwark::hasSharedCover(hero))
			{
				const auto friends = battleAdjacentUnits(info.defender);
				for(const auto * friendUnit : friends)
				{
					if(friendUnit->unitSide() != info.defender->unitSide() || !friendUnit->defended()
						|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(friendUnit))
						continue;
					payload.bulwarkDamageReductionBasisPoints = std::min(10000,
						baseReduction + newHorizonsBulwark::sharedCoverBasisPoints(baseReduction));
					break;
				}
			}
		}
	}
	if(info.preemptiveDamagePercent > 0)
		payload.preemptiveDamageMultiplier = info.preemptiveDamagePercent;
	if(info.cleaveDamagePercent > 0)
		payload.cleaveFinalDamageMultiplier = info.cleaveDamagePercent;
	if(heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		&& info.attacker && info.defender && !info.attacker->isGhost() && !info.defender->isGhost())
	{
		const auto & rules = getBattle()->getHeroCommandRules()["commands"];
		const auto attack = battleGetOwnerHero(info.attacker);
		const auto defend = battleGetOwnerHero(info.defender);
		const auto attackerSide = playerToSide(battleGetOwner(info.attacker));
		const auto defenderSide = playerToSide(battleGetOwner(info.defender));
		const auto attackerStates = battleGetHeroOrderStates(attackerSide);
		const auto defenderStates = battleGetHeroOrderStates(defenderSide);
		const auto coefficientFor = [](const JsonNode & formula, const CGHeroInstance * hero,
			const HeroOrderState * orderState)
		{
			return hero ? heroCommands::coefficient(formula, *hero,
				orderState ? orderState->warcastingBonusPercent : 0,
				orderState ? orderState->divineMandateEfficiencyBonusPercent() : 0) : 0;
		};
		const auto eligibleOrderUnit = [](const battle::Unit * unit)
		{
			return unit && unit->alive() && !unit->isGhost() && !unit->isTurret()
				&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
		};
		const auto recordAttackerCause = [&](HeroCommand command)
		{
			attackerOrderCauses.push_back(command);
			if(attackerOrderCause == HeroCommand::NONE)
				attackerOrderCause = command;
		};
		const auto recordDefenderCause = [&](HeroCommand command)
		{
			defenderOrderCauses.push_back(command);
			if(defenderOrderCause == HeroCommand::NONE)
				defenderOrderCause = command;
		};
		for(const auto & attackerState : attackerStates)
		{
			if(attackerState.issuedRound != battleGetRound() || !attack)
				continue;
			switch(attackerState.command)
			{
			case HeroCommand::FOCUS_FIRE:
				if(eligibleOrderUnit(info.attacker) && info.attacker->isMeleeAttacker()
					&& !info.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
					&& info.physicalDamage && !info.shooting && !info.secondaryAttack
					&& info.defender->alive() && battleGetOwner(info.attacker) != battleGetOwner(info.defender)
					&& attackerState.primaryTargetUnitId == info.defender->unitId()
					&& battleIsFocusFireTargetActive(attackerSide))
				{
					const auto mark = battleGetFocusFireState(attackerSide);
					if(mark && mark->issuedRound == battleGetRound()
						&& mark->targetUnitId == info.defender->unitId()
						&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), info.attacker->unitId()))
					{
						const auto combinedArmsDamagePercent = heroCommands::combinedArmsFocusFirePercent(
							mark->rangedDamagePercent, *attack);
						payload.combinedArmsDamagePercent += combinedArmsDamagePercent;
						if(combinedArmsDamagePercent > 0)
							recordAttackerCause(HeroCommand::FOCUS_FIRE);
					}
				}
				break;
			case HeroCommand::CHARGE:
				if(eligibleOrderUnit(info.attacker) && !info.shooting && !info.secondaryAttack && info.chargeDistance >= 3
					&& !attackerState.containsConsumed(info.attacker->unitId()))
				{
					const int orderDamagePercent = coefficientFor(rules["charge"]["effects"]["meleeDamagePercent"], attack, &attackerState)
						+ 2 * (info.chargeDistance - 3);
					payload.heroOrderDamagePercent += orderDamagePercent;
					if(orderDamagePercent > 0)
						recordAttackerCause(HeroCommand::CHARGE);
					if(info.physicalDamage && battleGetOwner(info.attacker) != battleGetOwner(info.defender)
						&& attack->hasActivePerk("new-horizons:offense", "new-horizons:offense.shockAssault"))
						payload.chargeDefenseIgnorePercent = SHOCK_ASSAULT_DEFENSE_IGNORE_PERCENT;
				}
				break;
			case HeroCommand::RIPOSTE:
				if(eligibleOrderUnit(info.attacker) && info.retaliation && !info.shooting)
				{
					const int orderDamagePercent = coefficientFor(rules["riposte"]["effects"]["retaliationDamagePercent"], attack, &attackerState);
					payload.heroOrderDamagePercent += orderDamagePercent;
					if(orderDamagePercent > 0)
						recordAttackerCause(HeroCommand::RIPOSTE);
				}
				break;
			case HeroCommand::BRACE:
				if(eligibleOrderUnit(info.attacker) && info.bracePreemptive && !info.shooting)
				{
					const int orderMultiplier = newHorizonsCombatSkills::bracePreemptivePercent(
						coefficientFor(rules["brace"]["effects"]["preemptiveDamagePercent"], attack, &attackerState), attack);
					payload.heroOrderFinalDamageMultipliers.push_back(orderMultiplier);
					if(payload.heroOrderFinalDamageMultiplier == 100)
						payload.heroOrderFinalDamageMultiplier = orderMultiplier;
					recordAttackerCause(HeroCommand::BRACE);
				}
				break;
			case HeroCommand::FLANK:
				if(eligibleOrderUnit(info.attacker)
					&& attackerState.primaryTargetUnitId == info.defender->unitId())
				{
					if(!info.shooting)
					{
						const int orderDamagePercent = battleHeroOrderFlankMeleeDamagePercent(info);
						payload.heroOrderDamagePercent += orderDamagePercent;
						if(orderDamagePercent > 0)
							recordAttackerCause(HeroCommand::FLANK);
					}
					else if(info.shooting && info.physicalDamage && info.defender->alive()
						&& battleGetOwner(info.attacker) != battleGetOwner(info.defender)
						&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker)
						&& attackerState.flankFor(info.defender->unitId()))
					{
						const auto combinedArmsDamagePercent = heroCommands::combinedArmsFlankPercent(
							rules["flank"]["effects"]["meleeDamagePercent"], *attack,
							attackerState.warcastingBonusPercent,
							attackerState.divineMandateEfficiencyBonusPercent());
						payload.combinedArmsDamagePercent += combinedArmsDamagePercent;
						if(combinedArmsDamagePercent > 0)
							recordAttackerCause(HeroCommand::FLANK);
					}
				}
				break;
			case HeroCommand::SECOND_WIND:
				if(attackerState.secondWindActive && attackerState.primaryTargetUnitId == info.attacker->unitId())
				{
					const int orderMultiplier = heroCommands::secondWindPercent(
						*attack, attackerState.warcastingBonusPercent,
						attackerState.divineMandateEfficiencyBonusPercent());
					if(orderMultiplier < 100)
					{
						payload.heroOrderFinalDamageMultipliers.push_back(orderMultiplier);
						if(payload.heroOrderFinalDamageMultiplier == 100)
							payload.heroOrderFinalDamageMultiplier = orderMultiplier;
						recordAttackerCause(HeroCommand::SECOND_WIND);
					}
				}
				break;
			default:
				break;
			}
		}
		for(const auto & defenderState : defenderStates)
		{
			if(defenderState.issuedRound != battleGetRound() || !defend
				|| !eligibleOrderUnit(info.defender) || !info.physicalDamage)
				continue;
			int orderDamageReductionPercent = 0;
			switch(defenderState.command)
			{
			case HeroCommand::RIPOSTE:
				if(!info.shooting)
					orderDamageReductionPercent = coefficientFor(rules["riposte"]["effects"]["meleeDamageReductionPercent"], defend, &defenderState);
				break;
			case HeroCommand::HOLD_THE_LINE:
				if(battleIsHoldTheLineRecipient(defenderState, info.defender))
					orderDamageReductionPercent = coefficientFor(rules["holdTheLine"]["effects"]["damageReductionPercent"], defend, &defenderState);
				break;
			case HeroCommand::PROTECT:
				if(!info.shooting && info.protectIntercepted
					&& defenderState.primaryTargetUnitId == info.defender->unitId())
					orderDamageReductionPercent = coefficientFor(rules["protect"]["effects"]["interceptedDamageReductionPercent"], defend, &defenderState);
				break;
			default:
				break;
			}
			if(orderDamageReductionPercent > 0)
			{
				if(payload.heroOrderDamageReductionPercent == 0)
					payload.heroOrderDamageReductionPercent = orderDamageReductionPercent;
				payload.heroOrderDamageReductionPercents.push_back(orderDamageReductionPercent);
				recordDefenderCause(defenderState.command);
			}
		}
	}
	payload.luckyStrike = info.luckyStrike;
	if(info.physicalDamage && info.shooting && info.luckyStrike
		&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker))
	{
		if(const auto * hero = battleGetOwnerHero(info.attacker))
		{
			if(hero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"))
				payload.luckyRangedDefenseIgnorePercent += ELVEN_PRECISION_DEFENSE_IGNORE_PERCENT;
			if(hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.luckyAim"))
				payload.luckyRangedDefenseIgnorePercent += LUCKY_AIM_DEFENSE_IGNORE_PERCENT;
		}
	}
	payload.unluckyStrike = info.unluckyStrike;
	payload.deathBlow = info.deathBlow;
	payload.doubleDamage = info.doubleDamage;
	if(info.attacker->hasBonusOfType(BonusType::SIEGE_WEAPON))
	{
		if(const auto * hero = battleGetOwnerHero(info.attacker))
		{
			if(const auto siege = hero->getSiegeCapabilities())
			{
				const auto & capabilityRules = hero->getCapabilityRules();
				if(capabilityRules["rulesetVersion"].Integer() >= 3)
				{
					if(info.attacker->isBallista())
						payload.machineBaseDamage = siege->ballistaDamage;
					else if(info.attacker->isTurret())
					{
						int towerSiegeRating = siege->siegeRating;
						if(battleCanUseFortificationEngineer(info.attacker))
						{
							const int64_t boostedRating = static_cast<int64_t>(siege->siegeRating)
								* FORTIFICATION_ENGINEER_SIEGE_PERCENT / 100;
							const auto clampedRating = std::clamp<int64_t>(boostedRating, 0,
								static_cast<int64_t>(std::numeric_limits<int>::max()));
							towerSiegeRating = static_cast<int>(clampedRating);
						}
						payload.machineBaseDamage = newHorizonsHeroes::capabilitySiegeOutput(
							capabilityRules, towerSiegeRating, "defensiveTowerDamage");
					}
				}
				else
					payload.siegeSkillMultiplier = siege->ballistaDamageMultiplier;
			}
		}
		else if(info.attacker->isTurret())
		{
			if(const auto * town = battleGetDefendedTown())
			{
				if(const auto baseDamage = town->getNewHorizonsDefensiveTowerBaseDamage())
					payload.machineBaseDamage = *baseDamage;
			}
		}
	}
	payload.attackFactorPerPoint = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR);
	payload.attackFactorCap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR_CAP);
	payload.defenseFactorPerPoint = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR);
	payload.defenseFactorCap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR_CAP);

	auto result = script->calculate(*this, payload);
	result.attackerOrderCause = attackerOrderCause;
	result.defenderOrderCause = defenderOrderCause;
	result.attackerOrderCauses = std::move(attackerOrderCauses);
	result.defenderOrderCauses = std::move(defenderOrderCauses);
	result.archeryDefenseIgnorePercent = payload.archeryRangedDefenseIgnorePercent;
	result.archeryCrossfireDamagePercent = payload.archeryCrossfireDamagePercent;
	result.archeryDeadeye = payload.archeryMaximumCreatureDamage;
	result.archeryHighArc = payload.archeryHighArc;
	return result;
}

DamageEstimation CBattleInfoCallback::battleEstimateDamage(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPosition, DamageEstimation * retaliationDmg) const
{
	RETURN_IF_NOT_BATTLE({});
	auto reachability = battleGetDistances(attacker, attacker->getPosition());
	int movementRange = attackerPosition.isValid() ? reachability[attackerPosition.toInt()] : 0;
	return battleEstimateDamage(attacker, defender, movementRange, retaliationDmg);
}

DamageEstimation CBattleInfoCallback::battleEstimateDamage(const battle::Unit * attacker, const battle::Unit * defender, int movementRange, DamageEstimation * retaliationDmg) const
{
	RETURN_IF_NOT_BATTLE({});
	const bool shooting = battleCanShoot(attacker, defender->getPosition());
	const BattleAttackInfo bai(attacker, defender, movementRange, shooting);
	return battleEstimateDamage(bai, retaliationDmg);
}

int64_t CBattleInfoCallback::getFirstAidHealValue(const CGHeroInstance * owner, const battle::Unit * target) const
{
	RETURN_IF_NOT_BATTLE(0);
	if(!owner || !target)
		return 0;

	int64_t base = firstAidHealingBase(owner);
	if(const auto * activeHealer = battleActiveUnit(); activeHealer && activeHealer->isFirstAidTent()
		&& battleGetOwnerHero(activeHealer) == owner)
		base = battleGetFirstAidHealingOutput(activeHealer);

	if(base <= 0)
		return 0;

	auto state = target->acquireState();

	return state->heal(base, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
}

SpellEffectValUptr CBattleInfoCallback::getSpellEffectValue(
	const CSpell * spell,
	const spells::Caster * caster,
	const spells::Mode spellMode,
	const BattleHex & targetHex) const
{
	auto result = std::make_unique<spells::effects::SpellEffectValue>();
	RETURN_IF_NOT_BATTLE(result);
	if(!spell || !caster || !targetHex.isValid())
		return result;

	spells::BattleCast params(this, caster, spellMode, spell);
	std::unique_ptr<spells::Mechanics> mech = spell->battleMechanics(&params);
	if(!mech)
		return result;

	spells::Target aim;
	aim.emplace_back(targetHex);
	const battle::Unit * hoveredUnit = battleGetUnitByPos(targetHex, false);
	if(hoveredUnit)
		aim.emplace_back(spells::Destination(hoveredUnit));

	const spells::Target spellTarget = mech->canonicalizeTarget(aim);

	mech->forEachEffect([&](const spells::effects::Effect &e){
		auto effTarget = e.transformTarget(mech.get(), aim, spellTarget);
		// Cure-specific safety net: if empty, but hovering a healable friendly unit, evaluate just that unit
		if(effTarget.empty() && hoveredUnit && spell->getId() == SpellID::CURE)
		{
			spells::Target single;
			single.emplace_back(spells::Destination(hoveredUnit));
			*result += e.getHealthChange(mech.get(), single);
			return false;
		}

		if(!effTarget.empty())
			*result += e.getHealthChange(mech.get(), effTarget);
		return false; // continue iterating effects
	});

	return result;
}

DamageEstimation CBattleInfoCallback::estimateSpellLikeAttackDamage(const battle::Unit * shooter,
																	const CSpell * spell,
																	const BattleHex & aimHex) const
{
	RETURN_IF_NOT_BATTLE({});
	if(!spell || !shooter || !aimHex.isValid())
		return {};

	spells::ProxyCaster proxy(shooter);
	spells::BattleCast params(this, &proxy, spells::Mode::PASSIVE, spell);
	std::unique_ptr<spells::Mechanics> mech = spell->battleMechanics(&params);
	if(!mech)
		return {};

	spells::Target aim;
	aim.emplace_back(aimHex);
	const auto affected = mech->getAffectedStacks(aim);
	if(affected.empty())
		return {};

	DamageEstimation total {};
	const auto * primary = battleGetUnitByPos(aimHex);

	for(const battle::Unit * u : affected)
	{
		BattleAttackInfo bai(shooter, u, 0, true);
		bai.physicalDamage = false;
		bai.secondaryAttack = !primary || primary->unitId() != u->unitId();
		DamageEstimation de = calculateDmgRange(bai);

		total.damage.min += de.damage.min;
		total.damage.max += de.damage.max;
		total.kills.min  += de.kills.min;
		total.kills.max  += de.kills.max;
	}

	return total;
}

DamageEstimation CBattleInfoCallback::battleEstimateDamage(const BattleAttackInfo & bai, DamageEstimation * retaliationDmg) const
{
	RETURN_IF_NOT_BATTLE({});

	DamageEstimation ret = calculateDmgRange(bai);

	if(retaliationDmg == nullptr)
		return ret;

	*retaliationDmg = DamageEstimation();

	if(bai.shooting) //FIXME: handle RANGED_RETALIATION
		return ret;

	if (!bai.defender->ableToRetaliate())	//FIXME: handle situation when NO_RETALIATION bonus is removed during attack
		return ret;

	if (bai.attacker->hasBonusOfType(BonusType::BLOCKS_RETALIATION) || bai.attacker->isInvincible() || isLongWeaponAttack(bai.attacker, bai.defender))
		return ret;

	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	if(battleShroudDeniesRetaliation(bai) && !bai.defender->hasBonus(firstStrikeSelector))
		return ret;

	//TODO: rewire once more using interval-based fuzzy arithmetic

	const auto & estimateRetaliation = [&](int64_t damage)
	{
		auto retaliationAttack = bai.reverse();
		auto state = retaliationAttack.attacker->acquireState();
		state->damage(damage);
		retaliationAttack.attacker = state.get();
		if (state->alive())
			return calculateDmgRange(retaliationAttack);
		else
			return DamageEstimation();
	};

	DamageEstimation retaliationMin = estimateRetaliation(ret.damage.min);
	DamageEstimation retaliationMax = estimateRetaliation(ret.damage.max);

	retaliationDmg->damage.min = std::min(retaliationMin.damage.min, retaliationMax.damage.min);
	retaliationDmg->damage.max = std::max(retaliationMin.damage.max, retaliationMax.damage.max);

	retaliationDmg->kills.min = std::min(retaliationMin.kills.min, retaliationMax.kills.min);
	retaliationDmg->kills.max = std::max(retaliationMin.kills.max, retaliationMax.kills.max);

	return ret;
}

std::vector<std::shared_ptr<const CObstacleInstance>> CBattleInfoCallback::battleGetAllObstaclesOnPos(const BattleHex & tile, bool onlyBlocking) const
{
	auto obstacles = std::vector<std::shared_ptr<const CObstacleInstance>>();
	RETURN_IF_NOT_BATTLE(obstacles);
	for(auto & obs : battleGetAllObstacles())
	{
		if(obs->getBlockedTiles().contains(tile)
				|| (!onlyBlocking && obs->getAffectedTiles().contains(tile)))
		{
			obstacles.push_back(obs);
		}
	}
	return obstacles;
}

std::vector<std::shared_ptr<const CObstacleInstance>> CBattleInfoCallback::getAllAffectedObstaclesByStack(const battle::Unit * unit, const BattleHexArray & passed) const
{
	auto affectedObstacles = std::vector<std::shared_ptr<const CObstacleInstance>>();
	RETURN_IF_NOT_BATTLE(affectedObstacles);
	if(unit->alive())
	{
		if(!passed.contains(unit->getPosition()))
			affectedObstacles = battleGetAllObstaclesOnPos(unit->getPosition(), false);
		if(unit->doubleWide())
		{
			BattleHex otherHex = unit->occupiedHex();
			if(otherHex.isValid() && !passed.contains(otherHex))
				for(auto & i : battleGetAllObstaclesOnPos(otherHex, false))
					if(!vstd::contains(affectedObstacles, i))
						affectedObstacles.push_back(i);
		}
		for(const auto & hex : unit->getHexes())
			if(hex == BattleHex::GATE_BRIDGE && battleIsGatePassable())
				for(int i=0; i<affectedObstacles.size(); i++)
					if(affectedObstacles.at(i)->obstacleType == CObstacleInstance::MOAT)
						affectedObstacles.erase(affectedObstacles.begin()+i);
	}
	return affectedObstacles;
}

std::vector<std::shared_ptr<const CObstacleInstance>> CBattleInfoCallback::getAffectedObstaclesAtPositions(
	const battle::Unit * unit, const BattleHexArray & positions, const BattleHexArray & passed) const
{
	auto affectedObstacles = std::vector<std::shared_ptr<const CObstacleInstance>>();
	if(!unit || !unit->alive())
		return affectedObstacles;

	for(const auto & position : positions)
	{
		if(!position.isValid())
			continue;

		const auto footprint = unit->getHexes(position);
		auto positionObstacles = std::vector<std::shared_ptr<const CObstacleInstance>>();
		for(const auto & hex : footprint)
		{
			if(!hex.isValid() || passed.contains(hex))
				continue;
			for(const auto & obstacle : battleGetAllObstaclesOnPos(hex, false))
				if(!vstd::contains(positionObstacles, obstacle))
					positionObstacles.push_back(obstacle);
		}

		if(footprint.contains(BattleHex::GATE_BRIDGE) && battleIsGatePassable())
			vstd::erase_if(positionObstacles, [](const auto & obstacle)
			{
				return obstacle->obstacleType == CObstacleInstance::MOAT;
			});

		for(const auto & obstacle : positionObstacles)
			if(!vstd::contains(affectedObstacles, obstacle))
				affectedObstacles.push_back(obstacle);
	}
	return affectedObstacles;
}

bool CBattleInfoCallback::handleObstacleTriggersForUnit(SpellCastEnvironment & spellEnv, const battle::Unit & unit, const BattleHexArray & passed) const
{
	return handleObstacleTriggersForUnitWithObstacles(spellEnv, unit, getAllAffectedObstaclesByStack(&unit, passed));
}

bool CBattleInfoCallback::handleObstacleTriggersForUnitAtPositions(SpellCastEnvironment & spellEnv,
	const battle::Unit & unit, const BattleHexArray & positions, const BattleHexArray & passed) const
{
	return handleObstacleTriggersForUnitWithObstacles(spellEnv, unit, getAffectedObstaclesAtPositions(&unit, positions, passed));
}

bool CBattleInfoCallback::handleObstacleTriggersForUnitWithObstacles(SpellCastEnvironment & spellEnv,
	const battle::Unit & unit, const std::vector<std::shared_ptr<const CObstacleInstance>> & obstacles) const
{
	if(!unit.alive() || unit.isTimeStopped())
		return false;
	bool movementStopped = false;
	for(const auto & obstacle : obstacles)
	{
		//helper info
		const SpellCreatedObstacle * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());

		if(spellObstacle)
		{
			// Canonical NH Land Mine is a hidden, hostile-ground-only trap.  Do
			// this gate before visibility/reveal/removal so allied or airborne
			// units neither consume the mine nor leak its position.
			const bool canonicalLandMine = newHorizonsMagic::rulesActive(getBattle()->getMagicRules())
				&& newHorizonsMagic::isLandMine(SpellID(spellObstacle->ID));
			const bool canonicalFireWall = newHorizonsMagic::rulesActive(getBattle()->getMagicRules())
				&& newHorizonsMagic::isFireWall(SpellID(spellObstacle->ID));
			const bool alreadyTriggeredThisActivation = canonicalFireWall
				&& spellObstacle->lastTriggerUnit == static_cast<si32>(unit.unitId())
				&& spellObstacle->lastTriggerActivation == getBattle()->getActivationSerial();
			if(canonicalLandMine
				&& (unit.hasBonusOfType(BonusType::FLYING)
					|| battleGetOwner(&unit) == getBattle()->getSidePlayer(spellObstacle->casterSide)))
				continue;
			if(canonicalFireWall && (unit.hasBonusOfType(BonusType::FLYING) || alreadyTriggeredThisActivation))
				continue;

			auto revealObstacles = [&](const SpellCreatedObstacle & spellObstacle) -> void
			{
				// For the hidden spell created obstacles, e.g. QuickSand, it should be revealed after taking damage
				auto operation = ObstacleChanges::EOperation::UPDATE;
				if (spellObstacle.removeOnTrigger)
					operation = ObstacleChanges::EOperation::REMOVE;

				SpellCreatedObstacle changedObstacle = spellObstacle;
				changedObstacle.uniqueID = spellObstacle.uniqueID;
				changedObstacle.revealed = true;
				if(canonicalFireWall)
				{
					changedObstacle.lastTriggerUnit = static_cast<si32>(unit.unitId());
					changedObstacle.lastTriggerActivation = getBattle()->getActivationSerial();
				}

				BattleObstaclesChanged bocp;
				bocp.battleID = getBattle()->getBattleID();
				bocp.change = ObstacleChanges(spellObstacle.uniqueID, operation);
				changedObstacle.toInfo(bocp.change, operation);
				spellEnv.apply(bocp);
			};
			const auto side = unit.unitSide();
			auto shouldReveal = !spellObstacle->hidden || !battleIsObstacleVisibleForSide(*obstacle, side);
			// A neutral obstacle (casterSide == NONE) belongs to no side and must impede units of both sides;
			// treat it as hostile to whichever unit triggered it so its effect still applies (and to avoid an invalid side lookup)
			const bool neutralObstacle = spellObstacle->casterSide != BattleSide::ATTACKER && spellObstacle->casterSide != BattleSide::DEFENDER;
			const auto casterSide = neutralObstacle ? otherSide(side) : spellObstacle->casterSide;
			const auto * hero = neutralObstacle ? nullptr : battleGetFightingHero(casterSide);
			auto caster = spells::ObstacleCasterProxy(getBattle()->getSidePlayer(casterSide), hero, *spellObstacle);

			if(obstacle->triggersEffects() && obstacle->getTrigger().hasValue())
			{
				const auto * sp = obstacle->getTrigger().toSpell();
				auto cast = spells::BattleCast(this, &caster, spells::Mode::PASSIVE, sp);
				if(canonicalFireWall)
					cast.setForceNonSmartTargeting(true);
				spells::detail::ProblemImpl ignored;
				auto target = spells::Target(1, spells::Destination(&unit));
				if(sp->battleMechanics(&cast)->canBeCastAt(target, ignored)) // Obstacles should not be revealed by immune creatures
				{
					if(shouldReveal) { //hidden obstacle triggers effects after revealed
						revealObstacles(*spellObstacle);
						cast.cast(&spellEnv, target);
					}
				}
			}
			else if(shouldReveal)
				revealObstacles(*spellObstacle);
		}

		if(!unit.alive())
			return false;

		if(obstacle->stopsMovement() && !unit.hasBonusOfType(BonusType::FLYING))
			movementStopped = true;
	}

	return unit.alive() && !movementStopped;
}

AccessibilityInfo CBattleInfoCallback::getAccessibility() const
{
	AccessibilityInfo ret;
	ret.fill(EAccessibility::ACCESSIBLE);

	//removing accessibility for side columns of hexes
	for(int y = 0; y < GameConstants::BFIELD_HEIGHT; y++)
	{
		ret[BattleHex(GameConstants::BFIELD_WIDTH - 1, y).toInt()] = EAccessibility::SIDE_COLUMN;
		ret[BattleHex(0, y).toInt()] = EAccessibility::SIDE_COLUMN;
	}

	//special battlefields with logically unavailable tiles
	auto bFieldType = battleGetBattlefieldType();

	if(bFieldType != BattleField::NONE)
	{
		for(const auto & hex : bFieldType.getInfo()->impassableHexes)
			ret[hex.toInt()] = EAccessibility::UNAVAILABLE;
	}

	//gate -> should be before stacks
	if(battleGetFortifications().wallsHealth > 0)
	{
		EAccessibility accessibility = EAccessibility::ACCESSIBLE;
		switch(battleGetGateState())
		{
		case EGateState::CLOSED:
			accessibility = EAccessibility::GATE;
			break;

		case EGateState::BLOCKED:
			accessibility = EAccessibility::UNAVAILABLE;
			break;
		}
		ret[BattleHex::GATE_OUTER] = ret[BattleHex::GATE_INNER] = accessibility;
	}

	//tiles occupied by standing stacks
	for(const auto * unit : battleAliveUnits())
	{
		for(const auto & hex : unit->getHexes())
			if(hex.isAvailable()) //towers can have <0 pos; we don't also want to overwrite side columns
				ret[hex.toInt()] = EAccessibility::ALIVE_STACK;
	}

	//obstacles
	for(const auto &obst : battleGetAllObstacles())
	{
		for(const auto & hex : obst->getBlockedTiles())
			ret[hex.toInt()] = EAccessibility::OBSTACLE;
	}

	//walls
	if(battleGetFortifications().wallsHealth > 0)
	{
		static const int permanentlyLocked[] = {12, 45, 62, 112, 147, 165};
		for(const auto & hex : permanentlyLocked)
			ret[hex] = EAccessibility::UNAVAILABLE;

		//TODO likely duplicated logic
		static const std::pair<EWallPart, BattleHex> lockedIfNotDestroyed[] =
		{
			//which part of wall, which hex is blocked if this part of wall is not destroyed
			std::make_pair(EWallPart::BOTTOM_WALL, BattleHex(BattleHex::DESTRUCTIBLE_WALL_4)),
			std::make_pair(EWallPart::BELOW_GATE, BattleHex(BattleHex::DESTRUCTIBLE_WALL_3)),
			std::make_pair(EWallPart::OVER_GATE, BattleHex(BattleHex::DESTRUCTIBLE_WALL_2)),
			std::make_pair(EWallPart::UPPER_WALL, BattleHex(BattleHex::DESTRUCTIBLE_WALL_1))
		};

		for(const auto & elem : lockedIfNotDestroyed)
		{
			if(battleGetWallState(elem.first) != EWallState::DESTROYED)
				ret[elem.second.toInt()] = EAccessibility::DESTRUCTIBLE_WALL;
		}
	}

	// Pending Gates own their requested landing footprint until they resolve.
	// Keeping reservations in the same accessibility result makes movement,
	// placement and battle AI agree on which hexes remain unavailable.
	if(const auto * battleInfo = getBattle())
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			for(const auto & gate : battleInfo->getPendingDemonicGateFootprints(side))
			{
				if(!gate.creature.hasValue())
					continue;
				const auto * creature = gate.creature.toCreature();
				if(creature)
					ret.reserveDemonicGateFootprint(gate.position, creature->isDoubleWide(), side);
			}
		}
	}

	return ret;
}

AccessibilityInfo CBattleInfoCallback::getAccessibility(const battle::Unit * stack) const
{
	return getAccessibility(stack->getHexes());
}

AccessibilityInfo CBattleInfoCallback::getAccessibility(const BattleHexArray & accessibleHexes) const
{
	auto ret = getAccessibility();
	for(const auto & hex : accessibleHexes)
		if(hex.isValid() && !ret.isDemonicGateReserved(hex))
			ret[hex.toInt()] = EAccessibility::ACCESSIBLE;

	return ret;
}

ReachabilityInfo CBattleInfoCallback::makeBFS(const AccessibilityInfo & accessibility, const ReachabilityInfo::Parameters & params) const
{
	ReachabilityInfo ret;
	ret.accessibility = accessibility;
	ret.params = params;

	ret.predecessors.fill(BattleHex::INVALID);
	ret.distances.fill(ReachabilityInfo::INFINITE_DIST);

	if(!params.startPosition.isValid()) //if got call for arrow turrets
		return ret;

	const BattleHexArray obstacles = getStoppers(params.perspective);
	auto checkParams = params;
	checkParams.ignoreKnownAccessible = true; //Ignore starting hexes obstacles

	std::queue<BattleHex> hexq; //bfs queue

	//first element
	hexq.push(params.startPosition);
	ret.distances[params.startPosition.toInt()] = 0;

	std::array<bool, GameConstants::BFIELD_SIZE> accessibleCache{};
	std::array<int32_t, GameConstants::BFIELD_SIZE> movementCostByHex{};
	bool hasMovementCost = false;
	auto traversalAccessibility = accessibility;
	if(params.ghostWalk && !params.passThrough)
	{
		// Keep the original accessibility below for endpoint validation, while
		// letting the BFS treat every occupied creature tile as ordinary floor.
		for(auto & tile : traversalAccessibility)
			if(tile == EAccessibility::ALIVE_STACK)
				tile = EAccessibility::ACCESSIBLE;
	}
	else if(!params.passThrough)
	{
		// Passing Lines is narrower than Ghost Walk: only the selected friendly
		// footprints are relaxed, and only where the final accessibility map
		// still identifies a living stack (obstacles/walls remain blockers).
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			if(params.friendlyTransit.test(static_cast<size_t>(hex))
				&& traversalAccessibility[hex] == EAccessibility::ALIVE_STACK)
				traversalAccessibility[hex] = EAccessibility::ACCESSIBLE;
	}
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; hex++)
	{
		const BattleHex position(static_cast<si16>(hex));
		accessibleCache[hex] = params.passThrough
			? traversalAccessibility.accessibleForPassThroughTransit(position, params.doubleWide, params.side)
			: traversalAccessibility.accessible(position, params.doubleWide, params.side);
	}

	for(const auto & obstacle : battleGetAllObstacles(params.perspective))
	{
		const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		if(!spellObstacle || spellObstacle->turnsRemaining == 0 || spellObstacle->movementCost <= 0)
			continue;

		for(const auto & hex : spellObstacle->getAffectedTiles())
		{
			if(!hex.isValid())
				continue;

			auto & movementCost = movementCostByHex[hex.toInt()];
			movementCost = std::max(movementCost, spellObstacle->movementCost);
			hasMovementCost = true;
		}
	}

	while(!hexq.empty()) //bfs loop
	{
		const BattleHex curHex = hexq.front();
		hexq.pop();

		//walking stack can't step past the obstacles
		if(isInObstacle(curHex, obstacles, checkParams))
			continue;

		for(const BattleHex & neighbour : curHex.getNeighbouringTiles())
		{
			int additionalCost = 0;
			if(hasMovementCost)
			{
				const auto & currentFootprint = battle::Unit::getHexes(curHex, params.doubleWide, params.side);
				const auto & neighbourFootprint = battle::Unit::getHexes(neighbour, params.doubleWide, params.side);
				for(const auto & hex : neighbourFootprint)
					if(hex.isValid() && !currentFootprint.contains(hex))
						additionalCost += movementCostByHex[hex.toInt()];
			}

			if(params.bypassEnemyStacks && !params.passThrough)
			{
				auto enemyToBypass = params.destructibleEnemyTurns.at(neighbour.toInt());

				if(enemyToBypass >= 0)
					additionalCost += enemyToBypass;
			}

			const uint32_t costToNeighbour = ret.distances.at(curHex.toInt())
				+ static_cast<uint32_t>(1 + additionalCost);

			const uint32_t costFoundSoFar = ret.distances[neighbour.toInt()];

			if(accessibleCache[neighbour.toInt()] && costToNeighbour < costFoundSoFar)
			{
				hexq.push(neighbour);
				ret.distances[neighbour.toInt()] = costToNeighbour;
				ret.predecessors[neighbour.toInt()] = curHex;
			}
		}
	}

	return ret;
}

bool CBattleInfoCallback::isInObstacle(
	const BattleHex & hex,
	const BattleHexArray & obstacleHexes,
	const ReachabilityInfo::Parameters & params) const
{

	for(const auto & occupiedHex : battle::Unit::getHexes(hex, params.doubleWide, params.side))
	{
		if(params.ignoreKnownAccessible && params.knownAccessible->contains(occupiedHex))
			continue;

		if(obstacleHexes.contains(occupiedHex))
		{
			if(occupiedHex == BattleHex::GATE_BRIDGE)
			{
				if(battleGetGateState() != EGateState::DESTROYED && params.side == BattleSide::ATTACKER)
					return true;
			}
			else
				return true;
		}
	}

	return false;
}

BattleHexArray CBattleInfoCallback::getStoppers(BattleSide whichSidePerspective) const
{
	BattleHexArray ret;
	RETURN_IF_NOT_BATTLE(ret);

	for(auto &oi : battleGetAllObstacles(whichSidePerspective))
	{
		if(!battleIsObstacleVisibleForSide(*oi, whichSidePerspective))
			continue;

		for(const auto & hex : oi->getStoppingTile())
		{
			if(hex == BattleHex::GATE_BRIDGE && oi->obstacleType == CObstacleInstance::MOAT)
			{
				if(battleGetGateState() == EGateState::OPENED || battleGetGateState() == EGateState::DESTROYED)
					continue; // this tile is disabled by drawbridge on top of it
			}
			ret.insert(hex);
		}
	}
	return ret;
}

BattleHex CBattleInfoCallback::getClosestHexToTargetInRange(const ReachabilityInfo & cache, const Unit & unit, const BattleHex & targetHex) const
{
	if (unit.hasBonusOfType(BonusType::FLYING))
	{
		BattleHexArray reachableHexes = battleGetAvailableHexes(cache, &unit, false);
		if(reachableHexes.empty())
			return BattleHex::INVALID;

		return std::ranges::min_element(reachableHexes, [&targetHex](const BattleHex & lhs, const BattleHex & rhs)
		{
			return BattleHex::getDistance(lhs, targetHex) < BattleHex::getDistance(rhs, targetHex);
		})[0];
	}

	BattleHexArray path = getPath(unit.getPosition(), targetHex, &unit).first; //TODO: does not find path through moat
	if(!path.empty())
	{
		int pathHexIndex = path.size() - unit.getMovementRange();
		if(pathHexIndex < 0)
		{
			return targetHex;
		}
		return path[pathHexIndex];
	}

	// FALLBACK: If path is empty (target blocked by obstacles/units),
	// find the reachable hex that is geometrically closest to the target.
	BattleHexArray reachableHexes = battleGetAvailableHexes(cache, &unit, false);
	if (reachableHexes.empty())
		return BattleHex::INVALID;

	return *vstd::minElementByFun(reachableHexes, [&](const BattleHex & h)
	{
		return BattleHex::getDistance(h, targetHex);
	});
}

std::vector<ForcedAction> CBattleInfoCallback::getBerserkForcedActions(const battle::Unit * berserker) const
{
	logGlobal->trace("Handle Berserk effect");
	if(!berserker || !getBattle())
		return {};

	const bool canonicalBerserk = newHorizonsMagic::berserkUsesSingleCreatureTarget(getBattle()->getMagicRules());
	auto targets = battleGetUnitsIf([this, berserker, canonicalBerserk](const battle::Unit * u)
	{
		if(!u->isValidTarget(false) || u->unitId() == berserker->unitId())
			return false;
		if(!canonicalBerserk)
			return true;

		// V3 forces a melee attack. Exclude only targets rejected by the same
		// authoritative melee legality checks; Berserk still includes allies.
		return !u->isInvincible()
			&& !(u->hasBonusOfType(BonusType::SANCTIFIED) && battleMatchOwner(berserker, u));
	});
	if(targets.empty())
		return {};

	auto cache = getReachability(berserker);

	// Saved v1/v2 and ordinary battles retain vanilla Berserk's ranged attack.
	if(!canonicalBerserk && battleCanShoot(berserker))
	{
		const auto target = std::ranges::min_element(targets, [&berserker](const battle::Unit * lhs, const battle::Unit * rhs)
		{
			return BattleHex::getDistance(berserker->getPosition(), lhs->getPosition()) < BattleHex::getDistance(berserker->getPosition(), rhs->getPosition());
		});
		ForcedAction result = {
			EActionType::SHOOT,
			berserker->getPosition(),
			*target
		};
		return {result};
	}

	struct TargetData
	{
		const battle::Unit * target;
		BattleHex closestAttackableHex;
		uint32_t distance;
	};

	std::vector<TargetData> targetData;
	targetData.reserve(targets.size());
	for (const battle::Unit * target : targets)
	{
		const auto attackableHexes = target->getAttackableHexes(berserker);
		if(attackableHexes.empty())
			continue;

		BattleHex closestAttackableHex = BattleHex::INVALID;
		uint32_t distance = ReachabilityInfo::INFINITE_DIST;
		for(const auto & hex : attackableHexes)
		{
			if(!cache.isReachable(hex))
				continue;

			const auto hexDistance = cache.distances[hex.toInt()];
			if(hexDistance < distance)
			{
				distance = hexDistance;
				closestAttackableHex = hex;
			}
		}
		if(closestAttackableHex.isValid())
			targetData.push_back({target, closestAttackableHex, distance});
	}

	if(targetData.empty())
		return {};

	const auto closestDistance = std::ranges::min_element(targetData, [](const TargetData & lhs, const TargetData & rhs)
	{
		return lhs.distance < rhs.distance;
	})->distance;
	const auto movementRange = berserker->getMovementRange();
	std::vector<ForcedAction> actions;
	for(const auto & closestUnit : targetData)
	{
		if(closestUnit.distance != closestDistance)
			continue;
		if(!canonicalBerserk && !actions.empty())
			break;

		if(closestUnit.distance <= movementRange)
		{
			actions.push_back({
				EActionType::WALK_AND_ATTACK,
				closestUnit.closestAttackableHex,
				closestUnit.target
			});
		}
		else if(movementRange > 0)
		{
			const BattleHex intermediaryHex = getClosestHexToTargetInRange(cache, *berserker, closestUnit.closestAttackableHex);
			if(!intermediaryHex.isValid())
				continue;

			actions.push_back({
				EActionType::WALK,
				intermediaryHex,
				closestUnit.target
			});
		}
	}
	return actions;
}

ForcedAction CBattleInfoCallback::getBerserkForcedAction(const battle::Unit * berserker) const
{
	const auto candidates = getBerserkForcedActions(berserker);
	if(!candidates.empty())
		return candidates.front();

	return {
		EActionType::NO_ACTION,
		berserker ? berserker->getPosition() : BattleHex::INVALID,
		nullptr
	};
}

BattleHex CBattleInfoCallback::getAvailableHex(const Creature * creature, BattleSide side, BattleHex initialPos) const
{
	bool twoHex = creature->isDoubleWide();

	BattleHex pos;
	if (initialPos.isValid())
		pos = initialPos;
	else //summon elementals depending on player side
	{
 		if(side == BattleSide::ATTACKER)
	 		pos = 0; //top left
 		else
 			pos = GameConstants::BFIELD_WIDTH - 1; //top right
 	}

	auto accessibility = getAccessibility();

	BattleHexArray occupyable;
	for(int i = 0; i < accessibility.size(); i++)
		if(accessibility.accessible(i, twoHex, side))
			occupyable.insert(i);

	if(occupyable.empty())
	{
		return BattleHex::INVALID; //all tiles are covered
	}

	return BattleHex::getClosestTile(side, pos, occupyable);
}

si8 CBattleInfoCallback::battleGetTacticDist() const
{
	RETURN_IF_NOT_BATTLE(0);

	//TODO get rid of this method
	if(battleDoWeKnowAbout(battleGetTacticsSide()))
		return battleTacticDist();

	return 0;
}

bool CBattleInfoCallback::isInTacticRange(const BattleHex & dest) const
{
	RETURN_IF_NOT_BATTLE(false);
	if(!dest.isAvailable())
		return false;
	auto side = battleGetTacticsSide();
	auto dist = battleGetTacticDist();

	if (side == BattleSide::ATTACKER && dest.getX() > 0 && dest.getX() <= dist)
		return true;

	if (side == BattleSide::DEFENDER && dest.getX() < GameConstants::BFIELD_WIDTH - 1 && dest.getX() >= GameConstants::BFIELD_WIDTH - dist - 1)
		return true;

	return false;
}

bool CBattleInfoCallback::isInTacticRange(const BattleHex & dest, const battle::Unit & unit) const
{
	if(unit.unitSide() != battleGetTacticsSide() || !dest.isAvailable())
		return false;
	const auto footprint = unit.getHexes(dest);
	return !footprint.empty() && std::ranges::all_of(footprint, [this](const BattleHex & hex)
	{
		return isInTacticRange(hex);
	});
}

ReachabilityInfo CBattleInfoCallback::getReachability(const battle::Unit * unit) const
{
	ReachabilityInfo::Parameters params(unit, unit->getPosition());
	const auto controllerSide = playerToSide(battleGetOwner(unit));
	if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		params.ghostWalk = newHorizonsShroud::rank(battleGetFightingHero(controllerSide)) > 0;
	configurePassingLines(unit, params);

	if(!battleDoWeKnowAbout(unit->unitSide()))
	{
		//Stack is held by enemy, we can't use his perspective to check for reachability.
		// Happens ie. when hovering enemy stack for its range. The arg could be set properly, but it's easier to fix it here.
		params.perspective = battleGetMySide();
	}

	return getReachability(params);
}

void CBattleInfoCallback::configurePassingLines(const battle::Unit * mover, ReachabilityInfo::Parameters & params) const
{
	params.friendlyTransit.reset();
	if(!mover || !getBattle())
		return;

	const auto controller = battleGetOwner(mover);
	const auto controllerSide = playerToSide(controller);
	if(controllerSide != BattleSide::ATTACKER && controllerSide != BattleSide::DEFENDER)
		return;

	// Use the visibility-aware side callback. battleGetOwnerHero() reads the
	// underlying side hero directly and would expose hidden enemy skill state to
	// player-scoped reachability queries.
	const auto * controllerHero = battleGetFightingHero(controllerSide);
	if(!controllerHero || !controllerHero->hasActivePerk(
		"new-horizons:battlecraft", "new-horizons:battlecraft.passingLines"))
		return;

	const auto accessibility = getAccessibility();
	const auto gateState = battleGetGateState();
	const bool hasWalls = battleGetFortifications().wallsHealth > 0;
	const auto battlefield = battleGetBattlefieldType();
	const auto * battlefieldInfo = battlefield != BattleField::NONE ? battlefield.getInfo() : nullptr;

	for(const auto * friendly : battleAliveUnits())
	{
		if(!friendly || friendly->unitId() == mover->unitId() || battleGetOwner(friendly) != controller)
			continue;

		for(const auto & hex : friendly->getHexes())
		{
			if(!hex.isAvailable() || accessibility[hex.toInt()] != EAccessibility::ALIVE_STACK)
				continue;

			// These restrictions are established before living stacks are layered
			// into AccessibilityInfo. Preserve them if a unit happens to cover one.
			if(hex.getX() == 0 || hex.getX() == GameConstants::BFIELD_WIDTH - 1)
				continue;
			if(battlefieldInfo && vstd::contains(battlefieldInfo->impassableHexes, hex))
				continue;
			if(hasWalls && (hex == BattleHex::GATE_OUTER || hex == BattleHex::GATE_INNER)
				&& (gateState == EGateState::BLOCKED
					|| (gateState == EGateState::CLOSED && params.side != BattleSide::DEFENDER)))
				continue;

			params.friendlyTransit.set(static_cast<size_t>(hex.toInt()));
		}
	}
}

ReachabilityInfo CBattleInfoCallback::getReachability(const ReachabilityInfo::Parameters & params) const
{
	if(params.flying)
		return getFlyingReachability(params);
	else
	{
		auto accessibility = getAccessibility(* params.knownAccessible);

		accessibility.destructibleEnemyTurns = std::shared_ptr<const TBattlefieldTurnsArray>(
			& params.destructibleEnemyTurns,
			[](const TBattlefieldTurnsArray *) { }
		);

		return makeBFS(accessibility, params);
	}
}

ReachabilityInfo CBattleInfoCallback::getFlyingReachability(const ReachabilityInfo::Parameters & params) const
{
	ReachabilityInfo ret;
	ret.accessibility = getAccessibility(* params.knownAccessible);
	ret.params = params;

	for(int i = 0; i < GameConstants::BFIELD_SIZE; i++)
	{
		if(ret.accessibility.accessible(i, params.doubleWide, params.side))
		{
			ret.predecessors[i] = params.startPosition;
			ret.distances[i] = BattleHex::getDistance(params.startPosition, i);
		}
	}

	return ret;
}

AttackableTiles CBattleInfoCallback::getPotentiallyAttackableHexes(
	const battle::Unit * attacker,
	BattleHex destinationTile,
	BattleHex attackerPos) const
{
	const auto * defender = battleGetUnitByPos(destinationTile, true);

	if(!defender)
		return AttackableTiles(); // can't attack thin air

	return getPotentiallyAttackableHexes(
		attacker,
		defender,
		destinationTile,
		attackerPos,
		defender->getPosition());
}

AttackableTiles CBattleInfoCallback::getPotentiallyAttackableHexes(
	const battle::Unit* attacker,
	const battle::Unit * defender,
	BattleHex destinationTile,
	BattleHex attackerPos,
	BattleHex defenderPos) const
{
	//does not return hex attacked directly
	AttackableTiles at;
	RETURN_IF_NOT_BATTLE(at);

	BattleHex attackOriginHex = (attackerPos.toInt() != BattleHex::INVALID) ? attackerPos : attacker->getPosition(); //real or hypothetical (cursor) position
	
	defenderPos = (defenderPos.toInt() != BattleHex::INVALID) ? defenderPos : defender->getPosition(); //real or hypothetical (cursor) position
	
	if(attacker->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT))
		at.hostileCreaturePositions.insert(attacker->getSurroundingHexes(attackerPos));

	// If attacker is double-wide and its head is not adjacent to enemy we need to turn around
	if(attacker->doubleWide() && !vstd::contains(defender->getSurroundingHexes(defenderPos), attackOriginHex))
		attackOriginHex = attacker->occupiedHex(attackOriginHex);

	auto attackDirection = BattleHex::mutualPosition(attackOriginHex, defenderPos);

	// If defender is double-wide, attacker always prefers targeting its 'tail', if it is reachable
	if(defender->doubleWide() && BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos)) != BattleHex::NONE)
		attackDirection = BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos));

	if(attackDirection == BattleHex::NONE)
		throw std::runtime_error("!!!");

	const auto & processTargets = [&](const std::vector<int> & additionalTargets) -> BattleHexArray
	{
		BattleHexArray output;

		for (int targetPath : additionalTargets)
		{
			BattleHex target = attackOriginHex;
			std::vector<BattleHex::EDir> path;

			for (int targetPathLeft = targetPath; targetPathLeft != 0; targetPathLeft /= 10)
				path.push_back(static_cast<BattleHex::EDir>((attackDirection + targetPathLeft % 10 - 1) % 6));

			try
			{
				if(attacker->doubleWide() && attacker->coversPos(target.cloneInDirection(path.front())))
					target.moveInDirection(attackDirection);

				for(BattleHex::EDir nextDirection : path)
					target.moveInDirection(nextDirection);
			}
			catch(const std::out_of_range &)
			{
				// Hex out of range, for example outside of battlefield. This is valid situation, so skip this hex
				continue;
			}

			if (target.isValid() && !attacker->coversPos(target))
				output.insert(target);
		}
		return output;
	};

	const auto multihexUnit = attacker->getBonusesOfType(BonusType::MULTIHEX_UNIT_ATTACK);
	const auto multihexEnemy = attacker->getBonusesOfType(BonusType::MULTIHEX_ENEMY_ATTACK);
	const auto multihexAnimation = attacker->getBonusesOfType(BonusType::MULTIHEX_ANIMATION);

	for (const auto & bonus : *multihexUnit)
		at.friendlyCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexEnemy)
		at.hostileCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexAnimation)
		at.overrideAnimationPositions.insert(processTargets(bonus->parameters->toVector()));

	if(attacker->hasBonusOfType(BonusType::THREE_HEADED_ATTACK))
		at.hostileCreaturePositions.insert(processTargets({2,6}));

	if(attacker->hasBonusOfType(BonusType::WIDE_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 111, 2, 12, 6, 16 }));

	if(attacker->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11 }));

	if (attacker->hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 12, 16 }));

	return at;
}

AttackableTiles CBattleInfoCallback::getPotentiallyShootableHexes(const battle::Unit * attacker, const BattleHex & destinationTile, const BattleHex & attackerPos) const
{
	//does not return hex attacked directly
	AttackableTiles at;
	RETURN_IF_NOT_BATTLE(at);

	if(attacker->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT) && !attackerPos.getNeighbouringTiles().contains(destinationTile))
	{
		at.hostileCreaturePositions.insert(destinationTile.getNeighbouringTiles());
		at.hostileCreaturePositions.insert(destinationTile);
	}

	return at;
}

battle::Units CBattleInfoCallback::getAttackedBattleUnits(
	const battle::Unit * attacker,
	const  battle::Unit * defender,
	BattleHex destinationTile,
	bool rangedAttack,
	BattleHex attackerPos,
	BattleHex defenderPos) const
{
	battle::Units units;
	RETURN_IF_NOT_BATTLE(units);

	if(attackerPos == BattleHex::INVALID)
		attackerPos = attacker->getPosition();

	if(defenderPos == BattleHex::INVALID)
		defenderPos = defender->getPosition();

	AttackableTiles at;

	if (rangedAttack)
		at = getPotentiallyShootableHexes(attacker, destinationTile, attackerPos);
	else
		at = getPotentiallyAttackableHexes(attacker, defender, destinationTile, attackerPos, defenderPos);

	units = battleGetUnitsIf([this, at, attacker](const battle::Unit * unit)
	{
		if (unit->isGhost() || !unit->alive() || unit->isInvincible())
			return false;

		for (const BattleHex & hex : unit->getHexes())
		{
			// Match authoritative getAttackedCreatures: hostile-only splash must
			// use current control, while friendly-fire tiles can hit either side.
			if (at.hostileCreaturePositions.contains(hex) && battleGetOwner(attacker) != battleGetOwner(unit))
				return true;
			if (at.friendlyCreaturePositions.contains(hex))
				return true;
		}
		return false;
	});

	return units;
}

std::pair<battle::Units, bool> CBattleInfoCallback::getAttackedCreatures(const CStack* attacker, const BattleHex & destinationTile, bool rangedAttack, BattleHex attackerPos) const
{
	std::pair<battle::Units, bool> attackedCres;
	RETURN_IF_NOT_BATTLE(attackedCres);

	AttackableTiles at;
	
	if(rangedAttack)
		at = getPotentiallyShootableHexes(attacker, destinationTile, attackerPos);
	else
	{
		try
		{
			at = getPotentiallyAttackableHexes(attacker, destinationTile, attackerPos);
		}
		catch(const std::runtime_error &)
		{
			return attackedCres;
		}
	}

	// a double-wide unit is found through both of its hexes, so the same unit shows up twice
	const auto & addOnce = [&attackedCres](const battle::Unit * unit)
	{
		if(!vstd::contains(attackedCres.first, unit))
			attackedCres.first.push_back(unit);
	};

	for (const BattleHex & tile : at.hostileCreaturePositions) //all around & three-headed attack
	{
		const CStack * st = battleGetStackByPos(tile, true);
		if(st && battleGetOwner(st) != battleGetOwner(attacker) && !st->isInvincible()) //only hostile stacks - does it work well with Berserk?
			addOnce(st);
	}
	for (const BattleHex & tile : at.friendlyCreaturePositions)
	{
		const CStack * st = battleGetStackByPos(tile, true);
		if(st && !st->isInvincible()) //friendly stacks can also be damaged by Dragon Breath
			addOnce(st);
	}

	if (at.friendlyCreaturePositions.empty())
	{
		attackedCres.second = !attackedCres.first.empty();
	}
	else
	{
		for (const BattleHex & tile : at.friendlyCreaturePositions)
			for (const auto & st : attackedCres.first)
				if (st->coversPos(tile))
					attackedCres.second = true;
	}

	return attackedCres;
}

static bool isHexInFront(const BattleHex & hex, const BattleHex & testHex, BattleSide side )
{
	static const std::set<BattleHex::EDir> rightDirs { BattleHex::BOTTOM_RIGHT, BattleHex::TOP_RIGHT, BattleHex::RIGHT };
	static const std::set<BattleHex::EDir> leftDirs  { BattleHex::BOTTOM_LEFT, BattleHex::TOP_LEFT, BattleHex::LEFT };

	auto mutualPos = BattleHex::mutualPosition(hex, testHex);

	if (side == BattleSide::ATTACKER)
		return rightDirs.count(mutualPos);
	else
		return leftDirs.count(mutualPos);
}

//TODO: this should apply also to mechanics and cursor interface
bool CBattleInfoCallback::isToReverse(const battle::Unit * attacker, const battle::Unit * defender, BattleHex attackerHex, BattleHex defenderHex) const
{
	if(!defenderHex.isValid())
		defenderHex = defender->getPosition();

	if(!attackerHex.isValid())
		attackerHex = attacker->getPosition();

	if (attackerHex < 0 ) //turret
		return false;

	if(isHexInFront(attackerHex, defenderHex, attacker->unitSide()))
		return false;

	auto defenderOtherHex = defenderHex;
	auto attackerOtherHex = defenderHex;

	if (defender->doubleWide())
	{
		defenderOtherHex = battle::Unit::occupiedHex(defenderHex, true, defender->unitSide());

		if(isHexInFront(attackerHex, defenderOtherHex, attacker->unitSide()))
			return false;
	}

	if (attacker->doubleWide())
	{
		attackerOtherHex = battle::Unit::occupiedHex(attackerHex, true, attacker->unitSide());

		if(isHexInFront(attackerOtherHex, defenderHex, attacker->unitSide()))
			return false;
	}

	// a bit weird case since here defender is slightly behind attacker, so reversing seems preferable,
	// but this is how H3 handles it which is important, e.g. for direction of dragon breath attacks
	if (attacker->doubleWide() && defender->doubleWide())
	{
		if(isHexInFront(attackerOtherHex, defenderOtherHex, attacker->unitSide()))
			return false;
	}
	return true;
}

ReachabilityInfo::TDistances CBattleInfoCallback::battleGetDistances(const battle::Unit * unit, const BattleHex & assumedPosition) const
{
	ReachabilityInfo::TDistances ret;
	ret.fill(-1);
	RETURN_IF_NOT_BATTLE(ret);

	auto reachability = getReachability(unit);

	std::ranges::copy(reachability.distances, ret.begin());

	return ret;
}

bool CBattleInfoCallback::battleHasDistancePenalty(const IBonusBearer * shooter, const BattleHex & shooterPosition, const BattleHex & destHex) const
{
	RETURN_IF_NOT_BATTLE(false);

	const std::string cachingStrNoDistancePenalty = "type_NO_DISTANCE_PENALTY";
	static const auto selectorNoDistancePenalty = Selector::type()(BonusType::NO_DISTANCE_PENALTY);

	if(shooter->hasBonus(selectorNoDistancePenalty, cachingStrNoDistancePenalty))
		return false;

	if(const auto * target = battleGetUnitByPos(destHex, true))
	{
		//If any hex of target creature is within range, there is no penalty
		int range = GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE;

		auto bonus = shooter->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
		if(bonus != nullptr && bonus->parameters)
			range = bonus->parameters->toNumber();

		if(isEnemyUnitWithinSpecifiedRange(shooterPosition, target, range))
			return false;
	}
	else
	{
		if(BattleHex::getDistance(shooterPosition, destHex) <= GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE)
			return false;
	}

	return true;
}

bool CBattleInfoCallback::isEnemyUnitWithinSpecifiedRange(const BattleHex & attackerPosition, const battle::Unit * defenderUnit, unsigned int range) const
{
	for(const auto & hex : defenderUnit->getHexes())
		if(BattleHex::getDistance(attackerPosition, hex) <= range)
			return true;
	
	return false;
}

bool CBattleInfoCallback::isHexWithinSpecifiedRange(const BattleHex & attackerPosition, const BattleHex & targetPosition, unsigned int range) const
{
	if(BattleHex::getDistance(attackerPosition, targetPosition) <= range)
		return true;

	return false;
}

BattleHex CBattleInfoCallback::wallPartToBattleHex(EWallPart part) const
{
	RETURN_IF_NOT_BATTLE(BattleHex::INVALID);
	return WallPartToHex(part);
}

EWallPart CBattleInfoCallback::battleHexToWallPart(const BattleHex & hex) const
{
	RETURN_IF_NOT_BATTLE(EWallPart::INVALID);
	return hexToWallPart(hex);
}

bool CBattleInfoCallback::isWallPartPotentiallyAttackable(EWallPart wallPart) const
{
	RETURN_IF_NOT_BATTLE(false);
	return wallPart != EWallPart::INDESTRUCTIBLE_PART && wallPart != EWallPart::INDESTRUCTIBLE_PART_OF_GATE &&
																	wallPart != EWallPart::INVALID;
}

bool CBattleInfoCallback::isWallPartAttackable(EWallPart wallPart) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(isWallPartPotentiallyAttackable(wallPart))
	{
		auto wallState = battleGetWallState(wallPart);
		return (wallState != EWallState::NONE && wallState != EWallState::DESTROYED);
	}
	return false;
}

BattleHexArray CBattleInfoCallback::getAttackableWallParts() const
{
	BattleHexArray attackableBattleHexes;
	RETURN_IF_NOT_BATTLE(attackableBattleHexes);

	for(const auto & wallPartPair : wallParts)
	{
		if(isWallPartAttackable(wallPartPair.second))
			attackableBattleHexes.insert(wallPartPair.first);
	}

	return attackableBattleHexes;
}

SpellCostBreakdown CBattleInfoCallback::battleGetSpellCostBreakdown(const spells::Spell * sp,
	const CGHeroInstance * caster, int32_t listedCostMultiplier, bool metamagicFollowup) const
{
	SpellCostBreakdown breakdown;
	if(!duringBattle())
	{
		logGlobal->error("%s called when no battle!", __FUNCTION__);
		breakdown.finalCost = -1;
		return breakdown;
	}
	calculateBattleSpellCost(sp, caster, listedCostMultiplier, metamagicFollowup, &breakdown);
	return breakdown;
}

int32_t CBattleInfoCallback::battleGetSpellCost(const spells::Spell * sp, const CGHeroInstance * caster,
	int32_t listedCostMultiplier, bool metamagicFollowup) const
{
	RETURN_IF_NOT_BATTLE(-1);
	return calculateBattleSpellCost(sp, caster, listedCostMultiplier, metamagicFollowup, nullptr);
}

int32_t CBattleInfoCallback::calculateBattleSpellCost(const spells::Spell * sp, const CGHeroInstance * caster,
	int32_t listedCostMultiplier, bool metamagicFollowup, SpellCostBreakdown * breakdown) const
{
	if(breakdown)
	{
		breakdown->listedCost = 0;
		breakdown->finalCost = 0;
		breakdown->stages.clear();
	}
	//TODO should be replaced using bonus system facilities (propagation onto battle node)
	if(listedCostMultiplier < 1)
		throw std::invalid_argument("Spell cost multiplier must be positive");

	const int32_t listedCost = caster->getListedSpellCost(sp);
	if(breakdown)
		breakdown->listedCost = listedCost;
	auto recordStage = [breakdown](SpellCostStage::Kind kind, int32_t before, int32_t after)
	{
		if(breakdown)
			breakdown->stages.push_back({kind, before, after});
	};
	const int wisdom = newHorizonsMagic::isAdventureSpell(caster->getMagicRules(), sp->getId())
		? MasteryLevel::NONE : newHorizonsMagic::wisdomRank(caster);
	int32_t ret = wisdom == MasteryLevel::NONE ? listedCost * listedCostMultiplier
		: newHorizonsMagic::wisdomAdjustedCost(listedCost, listedCostMultiplier, wisdom);
	if(breakdown)
	{
		const int32_t multipliedListedCost = static_cast<int32_t>(static_cast<int64_t>(listedCost) * listedCostMultiplier);
		if(listedCostMultiplier != 1)
			recordStage(SpellCostStage::Kind::LISTED_MULTIPLIER, listedCost, multipliedListedCost);
		if(wisdom != MasteryLevel::NONE)
			recordStage(SpellCostStage::Kind::WISDOM, multipliedListedCost, ret);
	}
	const bool newHorizonsOrdinarySpell = newHorizonsMagic::rulesActive(caster->getMagicRules())
		&& sp->isCommonHeroSpell() && sp->isCombat() && !sp->isAdventure();
	const BattleSide casterSide = playerToSide(caster->tempOwner);
	const bool ordinarySideHero = newHorizonsOrdinarySpell
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER)
		&& getBattle()->getSideHero(casterSide) == caster;
	const auto spellAllowance = ordinarySideHero
		? battleGetSpellActionAllowance(casterSide, sp->getId()) : std::nullopt;
	if(spellAllowance
		&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
		&& spellAllowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
	{
		const int32_t before = ret;
		ret = std::max(1, ret - newHorizonsDivineMandate::knightlySequenceSpellCostReduction(caster));
		recordStage(SpellCostStage::Kind::KNIGHTLY_SEQUENCE, before, ret);
	}
	const bool preparedCaster = ordinarySideHero
		&& caster->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.preparedCaster")
		&& !getBattle()->hasCompletedHeroSpellCast(casterSide);
	if(preparedCaster)
	{
		const int32_t before = ret;
		ret = std::max(1, ret - 2);
		recordStage(SpellCostStage::Kind::PREPARED_CASTER, before, ret);
	}

	const int spellLevel = battleGetSpellLevel(sp->getId());
	const bool archmage = ordinarySideHero
		&& (spellLevel == 4 || spellLevel == 5)
		&& caster->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.archmage")
		&& !getBattle()->hasCompletedHeroSpellLevel(casterSide, 4)
		&& !getBattle()->hasCompletedHeroSpellLevel(casterSide, 5);
	if(archmage)
	{
		const int32_t before = ret;
		ret = std::max(1, ret - 3);
		recordStage(SpellCostStage::Kind::ARCHMAGE, before, ret);
	}

	//checking for friendly stacks reducing cost of the spell and
	//enemy stacks increasing it
	int32_t manaReduction = 0;
	int32_t manaIncrease = 0;

	for(const auto * unit : battleAliveUnits())
	{
		if(unit->unitOwner() == caster->tempOwner && unit->hasBonusOfType(BonusType::CHANGES_SPELL_COST_FOR_ALLY))
		{
			vstd::amax(manaReduction, unit->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ALLY));
		}
		if(unit->unitOwner() != caster->tempOwner && unit->hasBonusOfType(BonusType::CHANGES_SPELL_COST_FOR_ENEMY))
		{
			vstd::amax(manaIncrease, unit->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ENEMY));
		}
	}

	const int32_t afterAllies = ret - manaReduction;
	if(manaReduction != 0)
		recordStage(SpellCostStage::Kind::ALLIED_ARMY, ret, afterAllies);
	const int32_t beforeMinimum = afterAllies + manaIncrease;
	if(manaIncrease != 0)
		recordStage(SpellCostStage::Kind::ENEMY_ARMY, afterAllies, beforeMinimum);
	const int32_t minimum = newHorizonsOrdinarySpell ? 1 : 0;
	int32_t finalCost = std::max(minimum, beforeMinimum);
	if(finalCost != beforeMinimum)
		recordStage(SpellCostStage::Kind::MINIMUM_COST, beforeMinimum, finalCost);

	const bool metamagicArcaneEconomy = metamagicFollowup && newHorizonsOrdinarySpell
		&& !newHorizonsMagic::isAdventureSpell(caster->getMagicRules(), sp->getId())
		&& newHorizonsMagic::hasMetamagicPerk(caster, newHorizonsMagic::METAMAGIC_ARCANE_ECONOMY);
	if(metamagicArcaneEconomy)
	{
		const int32_t before = finalCost;
		finalCost = std::max(1, finalCost - 2);
		recordStage(SpellCostStage::Kind::METAMAGIC_ARCANE_ECONOMY, before, finalCost);
	}
	if(breakdown)
		breakdown->finalCost = finalCost;
	return finalCost;
}

bool CBattleInfoCallback::battleHasShootingPenalty(const battle::Unit * shooter, const BattleHex & destHex) const
{
	return battleHasDistancePenalty(shooter, shooter->getPosition(), destHex) || battleHasWallPenalty(shooter, shooter->getPosition(), destHex);
}

bool CBattleInfoCallback::battleIsUnitBlocked(const battle::Unit * unit) const
{
	RETURN_IF_NOT_BATTLE(false);

	bool isBerserk = unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
	for(const auto * adjacent : battleAdjacentUnits(unit))
	{
		if(adjacent->unitOwner() != unit->unitOwner() || isBerserk)
			return true;
	}
	return false;
}

battle::Units CBattleInfoCallback::battleAdjacentUnits(const battle::Unit * unit) const
{
	RETURN_IF_NOT_BATTLE({});

	const auto & hexes = unit->getSurroundingHexes();

	const auto & units = battleGetUnitsIf([&hexes](const battle::Unit * testedUnit)
	{
		if (!testedUnit->alive())
			return false;
		const auto & unitHexes = testedUnit->getHexes();
		for (const auto & hex : unitHexes)
			if (hexes.contains(hex))
				return true;
		return false;
	});

	return units;
}

std::vector<SpellID> CBattleInfoCallback::getAvailableBeneficialSpells(const battle::Unit * caster, const battle::Unit * subject) const
{
	RETURN_IF_NOT_BATTLE({});
	if(!caster || !subject)
		return {};
	//This is complete list. No spells from mods.
	//todo: this should be Spellbook of caster Stack
	static const std::set<SpellID> allPossibleSpells =
	{
		SpellID::AIR_SHIELD,
		SpellID::ANTI_MAGIC,
		SpellID::BLESS,
		SpellID::BLOODLUST,
		SpellID::COUNTERSTRIKE,
		SpellID::CURE,
		SpellID::FIRE_SHIELD,
		SpellID::FORTUNE,
		SpellID::HASTE,
		SpellID::MAGIC_MIRROR,
		SpellID::MIRTH,
		SpellID::PRAYER,
		SpellID::PRECISION,
		SpellID::PROTECTION_FROM_AIR,
		SpellID::PROTECTION_FROM_EARTH,
		SpellID::PROTECTION_FROM_FIRE,
		SpellID::PROTECTION_FROM_WATER,
		SpellID::SHIELD,
		SpellID::SLAYER,
		SpellID::STONE_SKIN
	};
	std::vector<SpellID> beneficialSpells;
	newHorizonsPuppetMaster::ActionControllerCaster actionCaster(caster,
		battleGetActionController(caster));

	auto getAliveEnemy = [&](const std::function<bool(const CStack *)> & pred) -> const CStack *
	{
		auto stacks = battleGetStacksIf([&](const CStack * stack)
		{
			return pred(stack) && stack->unitOwner() != subject->unitOwner() && stack->isValidTarget(false);
		});

		if(stacks.empty())
			return nullptr;
		else
			return stacks.front();
	};

	for(const SpellID& spellID : allPossibleSpells)
	{
		std::stringstream cachingStr;
		cachingStr << "source_" << vstd::to_underlying(BonusSource::SPELL_EFFECT) << "id_" << spellID.num;

		if(subject->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellID)), cachingStr.str()))
			continue;

		auto spellPtr = spellID.toSpell();
		spells::Target target;
		target.emplace_back(subject);

		spells::BattleCast cast(this, &actionCaster, spells::Mode::CREATURE_ACTIVE, spellPtr);

		auto m = spellPtr->battleMechanics(&cast);
		if (!m->canBeCastAt(target))
			continue;

		switch (spellID.toEnum())
		{
		case SpellID::SHIELD:
		case SpellID::FIRE_SHIELD: // not if all enemy units are shooters
		{
			const auto * walker = getAliveEnemy([&](const CStack * stack) //look for enemy, non-shooting stack
			{
				return !stack->canShoot();
			});

			if(!walker)
				continue;
		}
			break;
		case SpellID::AIR_SHIELD: //only against active shooters
		{
			const auto * shooter = getAliveEnemy([&](const CStack * stack) //look for enemy, non-shooting stack
			{
				return stack->canShoot();
			});
			if(!shooter)
				continue;
		}
			break;
		case SpellID::ANTI_MAGIC:
		case SpellID::MAGIC_MIRROR:
		case SpellID::PROTECTION_FROM_AIR:
		case SpellID::PROTECTION_FROM_EARTH:
		case SpellID::PROTECTION_FROM_FIRE:
		case SpellID::PROTECTION_FROM_WATER:
		{
			const BattleSide enemySide = otherSide(subject->unitSide());
			//todo: only if enemy has spellbook
			if (!battleHasHero(enemySide)) //only if there is enemy hero
				continue;
		}
			break;
		case SpellID::CURE: //only damaged units
		{
			//do not cast on affected by debuffs
			if(subject->getFirstHPleft() == subject->getMaxHealth())
				continue;
		}
			break;
		case SpellID::BLOODLUST:
		{
			if(subject->canShoot()) //TODO: if can shoot - only if enemy units are adjacent
				continue;
		}
			break;
		case SpellID::PRECISION:
		{
			if(!subject->canShoot())
				continue;
		}
			break;
		case SpellID::SLAYER://only if monsters are present
		{
			const auto * kingMonster = getAliveEnemy([&](const CStack * stack) -> bool //look for enemy, non-shooting stack
			{
				return stack->hasBonusOfType(BonusType::KING);
			});

			if (!kingMonster)
				continue;
		}
			break;
		}
		beneficialSpells.push_back(spellID);
	}

	return beneficialSpells;
}

SpellID CBattleInfoCallback::getRandomBeneficialSpell(vstd::RNG & rand, const battle::Unit * caster, const battle::Unit * subject) const
{
	const auto beneficialSpells = getAvailableBeneficialSpells(caster, subject);
	if(!beneficialSpells.empty())
	{
		return *RandomGeneratorUtil::nextItem(beneficialSpells, rand);
	}
	else
	{
		return SpellID::NONE;
	}
}

SpellID CBattleInfoCallback::getRandomCastedSpell(vstd::RNG & rand, const CStack * caster) const
{
	RETURN_IF_NOT_BATTLE(SpellID::NONE);

	TConstBonusListPtr bl = caster->getBonusesOfType(BonusType::SPELLCASTER);

	if(bl->empty())
		return SpellID::NONE;

	std::vector<int> weights;

	//spells with 0 weight are non-random, exclude them
	for(const auto & b : *bl)
	{
		if(b->parameters && b->parameters->toNumber() > 0)
			weights.push_back(b->parameters->toNumber());
		else
			weights.push_back(0);
	}

	int64_t itemIndex = RandomGeneratorUtil::nextItemWeighted(weights, rand);

	if(itemIndex < 0)
		return SpellID::NONE;

	auto result = *(bl->begin() + itemIndex);
	return result->subtype.as<SpellID>();
}

int CBattleInfoCallback::battleGetSurrenderCost(const PlayerColor & Player) const
{
	RETURN_IF_NOT_BATTLE(-3);
	if(!battleCanSurrender(Player))
		return -1;

	const BattleSide side = playerToSide(Player);
	if(side == BattleSide::NONE)
		return -1;

	int ret = 0;
	double discount = 0;

	for(const auto * unit : battleAliveUnits(side))
		ret += unit->getRawSurrenderCost();

	//H3 - hero pays half of the recruit cost of his remaining army
	double costDivisor = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_SURRENDER_COST_DIVISOR);
	if(costDivisor > 0)
		ret = static_cast<int>(ret / costDivisor);

	if(const CGHeroInstance * h = battleGetFightingHero(side))
		discount += h->valOfBonuses(BonusType::SURRENDER_DISCOUNT);

	ret = static_cast<int>(ret * (100.0 - discount) / 100.0);
	vstd::amax(ret, 0); //no negative costs for >100% discounts (impossible in original H3 mechanics, but some day...)
	return ret;
}

si8 CBattleInfoCallback::battleMinSpellLevel(BattleSide side) const
{
	const IBonusBearer * node = nullptr;
	if(const CGHeroInstance * h = battleGetFightingHero(side))
		node = h;
	else
		node = getBonusBearer();

	if(!node)
		return 0;

	auto b = node->getBonusesOfType(BonusType::BLOCK_MAGIC_BELOW);
	if(b->size())
		return b->totalValue();

	return 0;
}

si8 CBattleInfoCallback::battleMaxSpellLevel(BattleSide side) const
{
	const IBonusBearer *node = nullptr;
	if(const CGHeroInstance * h = battleGetFightingHero(side))
		node = h;
	else
		node = getBonusBearer();

	if(!node)
		return GameConstants::SPELL_LEVELS;

	//We can't "just get value" - it'd be 0 if there are bonuses (and all would be blocked)
	auto b = node->getBonusesOfType(BonusType::BLOCK_MAGIC_ABOVE);
	if(b->size())
		return b->totalValue();

	return GameConstants::SPELL_LEVELS;
}

std::optional<BattleSide> CBattleInfoCallback::battleIsFinished() const
{
	auto units = battleGetUnitsIf([=](const battle::Unit * unit)
	{
		return unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	});

	BattleSideArray<bool> hasUnit = {false, false}; //index is BattleSide

	for(auto & unit : units)
	{
		//todo: move SIEGE_WEAPON check to Unit state
		hasUnit.at(unit->unitSide()) = true;

		if(hasUnit[BattleSide::ATTACKER] && hasUnit[BattleSide::DEFENDER])
			return std::nullopt;
	}
	
	hasUnit = {false, false};

	for(auto & unit : units)
	{
		if(!unit->isClone() && !unit->acquireState()->summoned && !dynamic_cast <const CCommanderInstance *>(unit))
		{
			hasUnit.at(unit->unitSide()) = true;
		}
	}

	if(!hasUnit[BattleSide::ATTACKER] && !hasUnit[BattleSide::DEFENDER])
		return BattleSide::NONE;
	if(!hasUnit[BattleSide::DEFENDER])
		return BattleSide::ATTACKER;
	else
		return BattleSide::DEFENDER;
}

const scripting::Pool & CBattleInfoCallback::getScriptContextPool() const
{
	return getBattle()->getScriptContextPool();
}
