/*
 * BattleInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsFrozen.h"
#include "NewHorizonsSwiftRebirth.h"
#include "BattleForm.h"
#include "NewHorizonsElementalRebirth.h"
#include "NewHorizonsBloodrage.h"
#include "NewHorizonsBattlecraft.h"
#include "NewHorizonsArmorer.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsConfusionControl.h"
#include "NewHorizonsOffense.h"
#include "PhysicalAffliction.h"
#include "NewHorizonsPlague.h"
#include "TimeStopState.h"

#include "BattleLayout.h"
#include "CObstacleInstance.h"
#include "../bonuses/BonusSelector.h"
#include "bonuses/Limiters.h"
#include "bonuses/Updaters.h"
#include "../bonuses/BonusParameters.h"
#include "../CStack.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/artifact/CArtifact.h"
#include "../entities/building/TownFortifications.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../filesystem/Filesystem.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../IGameSettings.h"
#include "../mapObjects/CGTownInstance.h"
#include "../modding/ModScope.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../texts/CGeneralTextHandler.h"
#include "../BattleFieldHandler.h"
#include "../ObstacleHandler.h"

#include <vstd/RNG.h>

namespace
{
bool orderUnitsAdjacent(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second)
		return false;
	for(const auto & firstHex : first->getHexes())
	{
		if(!firstHex.isValid())
			continue;
		for(const auto & secondHex : second->getHexes())
			if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	}
	return false;
}

bool isSpellLocked(const CStack & stack)
{
	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto bonuses = stack.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock))
		.And(Selector::type()(BonusType::MAGIC_RESISTANCE)));
	return bonuses && vstd::contains_if(*bonuses, [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus && Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0;
	});
}

std::optional<bool> spellLockPreservesBeneficial(const CStack & stack)
{
	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto bonuses = stack.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock))
		.And(Selector::type()(BonusType::NONE)));
	if(!bonuses)
		return std::nullopt;

	for(const auto & bonus : *bonuses)
		if(bonus && Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0)
			return bonus->val > 0;
	return std::nullopt;
}

bool isPreservedSpellLockEffect(const Bonus * bonus, bool preserveBeneficial)
{
	if(!bonus || bonus->source != BonusSource::SPELL_EFFECT || !Bonus::NTurns(bonus))
		return false;

	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto sourceSpellID = bonus->sid.as<SpellID>();
	if(!sourceSpellID.hasValue() || sourceSpellID == spellLock)
		return false;

	const auto * sourceSpell = sourceSpellID.toSpell();
	if(!sourceSpell || sourceSpell->isAdventure() || !sourceSpell->isMagical())
		return false;

	// Spell Lock removes only the opposing polarity. Neutral magical effects
	// survive either alignment and their timers are frozen as well.
	return preserveBeneficial ? !sourceSpell->isNegative() : !sourceSpell->isPositive();
}

std::optional<SpellID> entangleSpellId()
{
	if(!LIBRARY || !LIBRARY->identifiers())
		return std::nullopt;
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", "new-horizons:entangle", true);
	if(!id || *id < 0)
		return std::nullopt;
	return SpellID(*id);
}
}

const SideInBattle & BattleInfo::getSide(BattleSide side) const
{
	return sides.at(side);
}

SideInBattle & BattleInfo::getSide(BattleSide side)
{
	return sides.at(side);
}

void BattleInfo::setAdverseCombatRerollState(BattleSide side, const AdverseCombatRerollState & state)
{
	if(state.used && !state.enabled)
		throw std::runtime_error("Adverse combat reroll expenditure without an enabled perk");
	sides.at(side).adverseCombatReroll = state;
}

void BattleInfo::setMoraleSuppressionState(BattleSide side, const MoraleSuppressionState & state)
{
	state.validate();
	sides.at(side).moraleSuppression = state;
}

void BattleInfo::setReducedExtraActivationState(BattleSide side, const ReducedExtraActivationState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for reduced extra activation state");
	state.validateShape();
	sides.at(side).reducedExtraActivation = state;
}

void BattleInfo::setArmorerDefiantState(BattleSide side, const ArmorerDefiantState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for Defiant state");
	state.validate();
	if(state.lastConsumedRound > round || state.lastConsumedRound < sides.at(side).armorerDefiant.lastConsumedRound)
		throw std::runtime_error("Defiant consumption history cannot be rewound or set in the future");
	sides.at(side).armorerDefiant = state;
}

void BattleInfo::setSpellResponseState(BattleSide side, const SpellResponseState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for Spell Response state");
	state.validate();
	if(state.hasState() && !state.isReadyAt(round))
		throw std::runtime_error("Spell Response state is outside its current round window");
	if(state.hasState())
	{
		const auto * hero = getSideHero(side);
		if(!hero || !hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)))
			throw std::runtime_error("Spell Response state requires the side's Counterpressure perk");
	}
	sides.at(side).spellResponseState = state;
}

void BattleInfo::setOverwhelmingFormulaState(BattleSide side, const OverwhelmingFormulaState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for Overwhelming Formula state");
	state.validateShape();
	sides.at(side).overwhelmingFormulaState = state;
}

void BattleInfo::setLuckSerendipityState(BattleSide side, const LuckSerendipityState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Luck Serendipity side");
	state.validateTransitionFrom(sides.at(side).luckSerendipity, round);
	sides.at(side).luckSerendipity = state;
}

void BattleInfo::setPerfectFortuneState(BattleSide side, const PerfectFortuneState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for Perfect Fortune state");
	state.validate();
	sides.at(side).perfectFortune = state;
}

bool BattleInfo::hasBattlecraftMasteryMarkers() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && (stack->battlecraftWaitMasteryDoubled || stack->battlecraftDefendMasteryDoubled);
	});
}

bool BattleInfo::hasBattlecraftMasteryState() const
{
	return sides[BattleSide::ATTACKER].battlecraftMasteryAwardRound >= 0
		|| sides[BattleSide::DEFENDER].battlecraftMasteryAwardRound >= 0
		|| hasBattlecraftMasteryMarkers();
}

void BattleInfo::validateBattlecraftMasteryState() const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto awardRound = sides.at(side).battlecraftMasteryAwardRound;
		if(awardRound < -1 || awardRound > round)
			throw std::runtime_error("Battlefield Mastery award is outside the current round");
	}
	for(const auto & stack : stacks)
	{
		if(stack && stack->battlecraftWaitMasteryDoubled && !stack->battlecraftWaitBonusAvailable())
			throw std::runtime_error("Battlefield Mastery Wait marker has no available Wait bonus");
		if(stack && stack->battlecraftDefendMasteryDoubled && !stack->defended())
			throw std::runtime_error("Battlefield Mastery Defend marker has no active defensive stance");
	}
}

void BattleInfo::awardBattlecraftMastery(BattleSide side, uint32_t unitId, int32_t awardRound,
	BattlecraftMasteryAction action)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid Battlefield Mastery side");
	if(awardRound != round)
		throw std::runtime_error("Battlefield Mastery award must use the current round");
	const auto previousAwardRound = sides.at(side).battlecraftMasteryAwardRound;
	if(previousAwardRound >= awardRound)
		throw std::runtime_error("Battlefield Mastery already awarded this side this round");
	const auto found = std::find_if(stacks.begin(), stacks.end(), [unitId](const auto & stack)
	{
		return stack && stack->unitId() == unitId;
	});
	if(found == stacks.end())
		throw std::runtime_error("Battlefield Mastery award references a missing stack");
	auto * stack = found->get();
	if(playerToSide(battleGetOwner(stack)) != side)
		throw std::runtime_error("Battlefield Mastery award does not match the stack's controlling side");
	const auto * hero = battleGetOwnerHero(stack);
	if(!newHorizonsBattlecraft::canAwardBattlefieldMastery(hero, stack, awardRound,
		previousAwardRound, action))
		throw std::runtime_error("Battlefield Mastery award does not match an eligible accepted action");
	if((action == BattlecraftMasteryAction::WAIT && stack->battlecraftWaitMasteryDoubled)
		|| (action == BattlecraftMasteryAction::DEFEND && stack->battlecraftDefendMasteryDoubled))
		throw std::runtime_error("Battlefield Mastery action marker is already set");

	if(action == BattlecraftMasteryAction::WAIT)
		stack->battlecraftWaitMasteryDoubled = true;
	else if(action == BattlecraftMasteryAction::DEFEND)
		stack->battlecraftDefendMasteryDoubled = true;
	else
		throw std::invalid_argument("Invalid Battlefield Mastery action");
	sides.at(side).battlecraftMasteryAwardRound = awardRound;
}

bool BattleInfo::hasArmorerLastStandTransientUnitState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		if(!stack)
			return false;
		const auto state = stack->acquireState();
		return state->armorerLastStandEndedActivation || state->armorerLastStandDefending;
	});
}

bool BattleInfo::hasArmorerLastStandState() const
{
	return sides[BattleSide::ATTACKER].armorerLastStandUsed
		|| sides[BattleSide::DEFENDER].armorerLastStandUsed
		|| hasArmorerLastStandTransientUnitState();
}

void BattleInfo::consumeArmorerLastStand(BattleSide side)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid side for Armorer Last Stand");
	if(sides.at(side).armorerLastStandUsed)
		throw std::runtime_error("Armorer Last Stand was already used by this side");
	if(!newHorizonsArmorer::hasLastStand(battleGetFightingHero(side)))
		throw std::runtime_error("Armorer Last Stand requires the active hero perk");
	sides.at(side).armorerLastStandUsed = true;
}

const AlternatingHeroActionState & BattleInfo::getWarcastingState(BattleSide side) const
{
	static const AlternatingHeroActionState empty;
	if(!newHorizonsWarcasting::enabled(magicRules))
		return empty;
	return sides.at(side).warcastingState;
}

BattleSide BattleInfo::gatedDemonicStackSide(uint32_t unitId) const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & gated = sides.at(side).gatedDemonicStacks;
		if(std::ranges::any_of(gated, [unitId](const auto & stack)
		{
			return stack.unitId == unitId;
		}))
			return side;
	}
	return BattleSide::NONE;
}

bool BattleInfo::hasGatedDemonicStack(BattleSide side, uint32_t unitId) const
{
	return gatedDemonicStackSide(unitId) == side;
}

void BattleInfo::armChainGate(BattleSide side)
{
	// The token is intentionally a bool: several kills in one BattleAttack or
	// across one round cannot bank more than the one promised acceleration.
	sides.at(side).chainGateArmed = true;
}

bool BattleInfo::consumeChainGate(BattleSide side)
{
	auto & token = sides.at(side).chainGateArmed;
	if(!token)
		return false;
	token = false;
	return true;
}

void BattleInfo::recordBloodrageStackDeath(uint32_t unitId)
{
	if(sides[BattleSide::ATTACKER].bloodrageRank == 0 && sides[BattleSide::DEFENDER].bloodrageRank == 0)
		return;
	if(!bloodrageDestroyedUnits.insert(unitId).second)
		return;
	const auto * unit = getStack(unitId, false);
	const auto category = unit
		? getCreatureCategoryRules().lookup(unit->unitType()->getJsonKey()) : std::nullopt;
	const bool eliteOrChampion = category
		&& category->category != newHorizonsCreatures::CreatureCategory::CORE;
	bool firstBloodSelected = false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = sides.at(side);
		const bool firstBlood = newHorizonsBloodrage::hasFirstBlood(getSideHero(side));
		firstBloodSelected |= firstBlood;
		const int increments = newHorizonsBloodrage::deathIncrementCount(!bloodrageFirstBloodUsed,
			eliteOrChampion, firstBlood, newHorizonsBloodrage::hasSlayer(getSideHero(side)));
		state.bloodrageDamagePercent = std::min(state.bloodrageCapPercent,
			state.bloodrageDamagePercent + increments * newHorizonsBloodrage::incrementForRank(state.bloodrageRank));
	}
	// A capped grant still spends First Blood; revived stacks never restore it.
	bloodrageFirstBloodUsed |= firstBloodSelected;
}

void BattleInfo::clearBloodrageStackDeath(uint32_t unitId)
{
	bloodrageDestroyedUnits.erase(unitId);
}

bool BattleInfo::consumeHeroOrderUnit(BattleSide side, uint32_t unitId)
{
	auto * state = sides.at(side).findOrder(HeroCommand::CHARGE);
	if(!state || state->containsConsumed(unitId))
		return false;
	state->consumedUnitIds.insert(std::lower_bound(state->consumedUnitIds.begin(), state->consumedUnitIds.end(), unitId), unitId);
	return true;
}

bool BattleInfo::triggerHeroOrderBrace(BattleSide side, uint32_t unitId)
{
	const auto * state = sides.at(side).findOrder(HeroCommand::BRACE);
	// Brace is a reaction to every qualifying incoming melee attack, not a
	// once-per-unit/round charge. Keep the old entry point for callers while
	// making the trigger itself stateless and therefore deterministic on clients.
	(void)unitId;
	return state != nullptr;
}

bool BattleInfo::breakHeroOrderHold(uint32_t unitId)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto * state = sides.at(side).findOrder(HeroCommand::HOLD_THE_LINE);
		if(!state || state->containsHoldBroken(unitId))
			continue;
		const auto * anchor = state->anchorFor(unitId);
		const auto * unit = battleGetUnitByID(unitId);
		if(anchor && unit && anchor->position != unit->getPosition().toInt())
		{
			state->holdBrokenUnitIds.insert(std::lower_bound(state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), unitId), unitId);
			return true;
		}
	}
	return false;
}

bool BattleInfo::interceptHeroOrderProtect(BattleSide side)
{
	auto * state = sides.at(side).findOrder(HeroCommand::PROTECT);
	if(!state || state->issuedRound != getRound()
		|| state->protectInterceptionsConsumed >= battleHeroOrderProtectInterceptionLimit(side)
		|| state->protectBroken)
		return false;
	++state->protectInterceptionsConsumed;
	return true;
}

std::vector<HeroOrderState> BattleInfo::getHeroOrderStates(BattleSide side) const
{
	return sides.at(side).orderStates;
}

void BattleInfo::validateDoubleCommandStructure() const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & sideState = sides.at(side);
		sideState.validateDoubleCommandState();
		const auto & continuation = sideState.doubleCommandState;
		if(!continuation.orderPending() && !continuation.secondWindReady())
			continue;
		if(continuation.issuedRound != round)
			throw std::runtime_error("Double Command continuation does not match battle round");
		const auto order = getHeroOrderState(side, continuation.firstOrder);
		if(!order || order->issuedRound != continuation.issuedRound)
			throw std::runtime_error("Double Command continuation has no matching issued Order");
		const auto activeStackId = getActiveStackID();
		const auto * anchor = battleGetStackByID(continuation.anchorStackId, false);
		if(!anchor || activeStackId < 0 || static_cast<uint32_t>(activeStackId) != continuation.anchorStackId)
			throw std::runtime_error("Double Command continuation has no matching active anchor descriptor");
		if(continuation.firstOrder == HeroCommand::SECOND_WIND)
		{
			const auto * target = battleGetStackByID(continuation.deferredSecondWindTargetUnitId, false);
			if(!target
				|| order->primaryTargetUnitId != continuation.deferredSecondWindTargetUnitId
				|| order->secondWindActive)
				throw std::runtime_error("Double Command Second Wind continuation has no matching stored target");
		}
	}
}

void BattleInfo::validateDoubleCommandContexts() const
{
	validateDoubleCommandStructure();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & continuation = sides.at(side).doubleCommandState;
		if(!continuation.orderPending() && !continuation.secondWindReady())
			continue;
		const auto * anchor = battleGetStackByID(continuation.anchorStackId, false);
		if(!anchor || !anchor->alive() || anchor->isGhost()
			|| playerToSide(battleGetOwner(anchor)) != side)
			throw std::runtime_error("Double Command continuation has no legal active anchor");
		if(continuation.firstOrder == HeroCommand::SECOND_WIND)
		{
			const auto * target = battleGetStackByID(continuation.deferredSecondWindTargetUnitId, false);
			if(!target || !target->alive() || target->isGhost()
				|| playerToSide(battleGetOwner(target)) != side)
				throw std::runtime_error("Double Command Second Wind continuation has no legal stored target");
		}
	}
}

void BattleInfo::validatePreCombatOrderStructure() const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & sideState = sides.at(side);
		sideState.validatePreCombatOrderState();
		const auto & opening = sideState.preCombatOrderState;
		if(opening.phase == PreCombatOrderState::Phase::AVAILABLE
			&& (round < 0 || round > 1 || activationSerial != 0))
			throw std::runtime_error("Unresolved Battle Plan state is outside its opening window");
		if(!opening.orderPending())
			continue;
		if(opening.issuedRound != round || round != 1 || activationSerial != 0)
			throw std::runtime_error("Battle Plan continuation is outside the round-one opening window");
		const auto * anchor = battleGetStackByID(opening.anchorStackId, false);
		const auto activeStackId = getActiveStackID();
		if(!anchor || activeStackId < 0 || static_cast<uint32_t>(activeStackId) != opening.anchorStackId)
			throw std::runtime_error("Battle Plan continuation has no matching active anchor descriptor");
	}
}

void BattleInfo::validatePreCombatOrderContexts() const
{
	validatePreCombatOrderStructure();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & opening = sides.at(side).preCombatOrderState;
		if(!opening.orderPending())
			continue;
		const auto * anchor = battleGetStackByID(opening.anchorStackId, false);
		if(!anchor || !anchor->alive() || anchor->isGhost() || anchor->isTurret()
			|| anchor->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| anchor->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| anchor->unitSlot() == SlotID::WAR_MACHINES_SLOT
			|| playerToSide(battleGetOwner(anchor)) != side)
			throw std::runtime_error("Battle Plan continuation has no legal live anchor");
	}
}

void BattleInfo::setHeroOrderStates(BattleSide side, const std::vector<HeroOrderState> & states)
{
	auto & sideState = sides.at(side);
	sideState.replaceOrders(states);
	sideState.activeOrder = sideState.orderStates.empty() ? HeroCommand::NONE : sideState.orderStates.back().command;
}

void BattleInfo::setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
{
	auto & sideState = sides.at(side);
	if(state)
	{
		sideState.upsertOrder(*state);
		sideState.activeOrder = sideState.orderStates.back().command;
	}
	else
	{
		sideState.orderStates.clear();
		sideState.activeOrder = HeroCommand::NONE;
	}
}

void BattleInfo::setDoubleCommandState(BattleSide side, const DoubleCommandState & state)
{
	state.validateShape();
	auto & sideState = sides.at(side);
	if(sideState.doubleCommandState.orderPending() && !state.orderPending())
		std::erase_if(sideState.heroActionAllowances.grants, [](const auto & grant)
		{
			return grant.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND;
		});
	sideState.doubleCommandState = state;
}

void BattleInfo::setPreCombatOrderState(BattleSide side, const PreCombatOrderState & state)
{
	auto & sideState = sides.at(side);
	const auto & previous = sideState.preCombatOrderState;
	state.validateTransitionFrom(previous);
	auto nextAllowances = sideState.heroActionAllowances;
	if(previous.phase == PreCombatOrderState::Phase::AVAILABLE && state.orderPending())
	{
		if(nextAllowances.currentRound != 1)
			throw std::runtime_error("Battle Plan grant requires round one");
		nextAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::BATTLE_PLAN, 1);
	}
	else if(previous.orderPending() && !state.orderPending())
	{
		std::erase_if(nextAllowances.grants, [](const auto & grant)
		{
			return grant.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN;
		});
	}
	nextAllowances.validateShape();
	if(state.orderPending())
	{
		if(nextAllowances.currentRound != 1 || nextAllowances.countBattlePlanOrderGrants(1) != 1)
			throw std::runtime_error("Battle Plan continuation requires its typed Order grant");
	}
	else if(nextAllowances.countBattlePlanOrderGrants(1) != 0)
		throw std::runtime_error("Orphaned Battle Plan Order grant");
	if(state.phase == PreCombatOrderState::Phase::AVAILABLE && nextAllowances.currentRound > 1)
		throw std::runtime_error("Unresolved Battle Plan state is stale");

	sideState.heroActionAllowances = std::move(nextAllowances);
	sideState.preCombatOrderState = state;
	sideState.validatePreCombatOrderState();
}

void BattleInfo::expireSeparatedHeroOrderProtect()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto * state = sides.at(side).findOrder(HeroCommand::PROTECT);
		if(!state || state->protectBroken)
			continue;
		const auto * protector = getStack(static_cast<int>(state->primaryTargetUnitId), false);
		const auto * ward = getStack(static_cast<int>(state->secondaryTargetUnitId), false);
		if(!protector || !ward || !protector->alive() || !ward->alive() || !orderUnitsAdjacent(protector, ward))
			state->protectBroken = true;
	}
}

bool BattleInfo::recordHeroOrderFlankSide(BattleSide side, uint32_t targetUnitId, uint8_t sideMask)
{
	auto * state = sides.at(side).findOrder(HeroCommand::FLANK);
	if(!state || sideMask == 0 || sideMask > 0x3f)
		return false;
	auto * target = state->flankFor(targetUnitId);
	if(!target || (target->sideMask & sideMask) == sideMask)
		return false;
	target->sideMask |= sideMask;
	return true;
}

bool BattleInfo::setHeroOrderSecondWindActive(BattleSide side, bool active)
{
	auto * state = sides.at(side).findOrder(HeroCommand::SECOND_WIND);
	if(!state)
		return false;
	state->secondWindActive = active;
	return true;
}

///BattleInfo
void BattleInfo::generateNewStack(uint32_t id, const CStackInstance & base, BattleSide side, const SlotID & slot, const BattleHex & position)
{
	PlayerColor owner = getSide(side).color;
	assert(!owner.isValidPlayer() || (base.getArmy() && base.getArmy()->tempOwner == owner));

	auto ret = std::make_unique<CStack>(&base, owner, id, side, slot);
	ret->initialPosition = getAvailableHex(base.getCreature(), side, position.toInt()); //TODO: what if no free tile on battlefield was found?
	stacks.push_back(std::move(ret));
}

void BattleInfo::generateNewStack(uint32_t id, const CStackBasicDescriptor & base, BattleSide side, const SlotID & slot, const BattleHex & position)
{
	PlayerColor owner = getSide(side).color;
	auto ret = std::make_unique<CStack>(&base, owner, id, side, slot);
	ret->initialPosition = position;
	stacks.push_back(std::move(ret));
}

void BattleInfo::localInit()
{
	for(BattleSide i : { BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto * armyObj = battleGetArmyObject(i);
		armyObj->battle = this;
		armyObj->attachTo(*this);
	}

	for(auto & s : stacks)
		s->localInit(this);

	exportBonuses();
	for(auto & stack : stacks)
		stack->captureBattleStartMaximumAggregateHP();
}


//RNG that works like H3 one
struct RandGen
{
	ui32 seed;

	void srand(ui32 s)
	{
		seed = s;
	}
	void srand(const int3 & pos)
	{
		srand(110291 * static_cast<ui32>(pos.x) + 167801 * static_cast<ui32>(pos.y) + 81569);
	}
	int rand()
	{
		seed = 214013 * seed + 2531011;
		return (seed >> 16) & 0x7FFF;
	}
	int rand(int min, int max)
	{
		if(min == max)
			return min;
		if(min > max)
			return min;
		return min + rand() % (max - min + 1);
	}
};

struct RangeGenerator
{
	class ExhaustedPossibilities : public std::exception
	{
	};

	RangeGenerator(int _min, int _max, std::function<int()> _myRand):
		min(_min),
		remainingCount(_max - _min + 1),
		remaining(remainingCount, true),
		myRand(std::move(_myRand))
	{
	}

	int generateNumber() const
	{
		if(!remainingCount)
			throw ExhaustedPossibilities();
		if(remainingCount == 1)
			return 0;
		return myRand() % remainingCount;
	}

	//get number fulfilling predicate. Never gives the same number twice.
	int getSuchNumber(const std::function<bool(int)> & goodNumberPred = nullptr)
	{
		int ret = -1;
		do
		{
			int n = generateNumber();
			int i = 0;
			for(;;i++)
			{
				assert(i < (int)remaining.size());
				if(!remaining[i])
					continue;
				if(!n)
					break;
				n--;
			}

			remainingCount--;
			remaining[i] = false;
			ret = i + min;
		} while(goodNumberPred && !goodNumberPred(ret));
		return ret;
	}

	int min;
	int remainingCount;
	std::vector<bool> remaining;
	std::function<int()> myRand;
};

std::unique_ptr<BattleInfo> BattleInfo::setupBattle(IGameInfoCallback *cb, const int3 & tile, TerrainId terrain, const BattleField & battlefieldType, BattleSideArray<const CArmedInstance *> armies, BattleSideArray<const CGHeroInstance *> heroes, const BattleLayout & layout, const CGTownInstance * town)
{
	CMP_stack cmpst;
	auto currentBattle = std::make_unique<BattleInfo>(cb, layout);
	currentBattle->luckRollRules.goodChance = cb->getSettings().getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
	currentBattle->luckRollRules.badChance = cb->getSettings().getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
	currentBattle->luckRollRules.diceSize = cb->getSettings().getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE);
	currentBattle->luckRollRules.affectsAllTargets = cb->getSettings().getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS);
	const auto currentDay = cb->getCalendar().getCurrentDay();

	for(auto i : { BattleSide::LEFT_SIDE, BattleSide::RIGHT_SIDE})
	{
		currentBattle->sides[i].init(heroes[i], armies[i], i == BattleSide::RIGHT_SIDE ? town : nullptr);
		if(heroes[i] && currentDay >= 0
			&& heroes[i]->getNewHorizonsForcedMarchPenaltyDay() == currentDay)
			currentBattle->sides[i].firstRoundMoraleModifier = -1;
		if(const auto * army = armies[i])
		{
			currentBattle->sides[i].initialArmyValue = army->getArmyStrength();
			currentBattle->sides[i].initialArmyIsWandering = army->ID == Obj::MONSTER;
		}
		else
		{
			currentBattle->sides[i].initialArmyValue.reset();
			currentBattle->sides[i].initialArmyIsWandering = false;
		}
		if(heroCommands::hasBattlePlan(heroes[i]))
			currentBattle->sides[i].preCombatOrderState.phase = PreCombatOrderState::Phase::AVAILABLE;
		if(heroes[i])
		{
			currentBattle->sides[i].luckSerendipity.enabled = heroes[i]->hasActivePerk(
				"new-horizons:luck", "new-horizons:luck.serendipity");
			currentBattle->sides[i].perfectFortune.enabled = heroes[i]->hasActivePerk(
				"new-horizons:luck", "new-horizons:luck.perfectFortune");
			currentBattle->sides[i].adverseCombatReroll.enabled = heroes[i]->hasActivePerk(
				"new-horizons:luck", "new-horizons:luck.twistOfFate");
			currentBattle->sides[i].moraleSuppression.enabled = heroes[i]->hasActivePerk(
				"new-horizons:discipline", "new-horizons:discipline.rally");
			currentBattle->sides[i].moraleSuppression.roundEnabled = heroes[i]->hasActivePerk(
				"new-horizons:discipline", "new-horizons:discipline.unbreakable");
			currentBattle->sides[i].reducedExtraActivation.enabled = heroes[i]->hasActivePerk(
				"new-horizons:warMachines", "new-horizons:warMachines.quartermaster");
			auto & fortune = currentBattle->sides[i].sylvanLuck;
			fortune.secondChance = heroes[i]->hasActivePerk("new-horizons:luck", "new-horizons:luck.secondChance");
			fortune.gambler = heroes[i]->hasActivePerk("new-horizons:luck", "new-horizons:luck.gambler");
			fortune.chainOfFortune = heroes[i]->hasActivePerk("new-horizons:luck", "new-horizons:luck.chainOfFortune");
			fortune.serendipity = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.serendipity");
			fortune.naturesProvidence = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.natureSProvidence");
			fortune.fortunateAim = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.fortunateAim");
			fortune.perfectMoment = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.perfectMoment");
			fortune.forestsFavor = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.forestSFavor");
			fortune.luckyRecovery = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.luckyRecovery");
			fortune.sharedFortune = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.sharedFortune");
			fortune.cascadingFortune = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.cascadingFortune");
		}
		currentBattle->sides[i].bloodrageRank = newHorizonsBloodrage::rank(heroes[i]);
		currentBattle->sides[i].bloodrageCapPercent = newHorizonsBloodrage::capForHero(heroes[i]);
		currentBattle->sides[i].bloodrageDamagePercent = newHorizonsBloodrage::initialDamagePercent(heroes[i]);
		currentBattle->sides[i].bloodrageSpeedBonus = newHorizonsBloodrage::hasUnrelenting(heroes[i]) ? 1 : 0;
		currentBattle->sides[i].bloodrageAdditionalRetaliations = newHorizonsBloodrage::hasBerserker(heroes[i]) ? 1 : 0;
		currentBattle->sides[i].bloodrageLowHealthIncrement = newHorizonsBloodrage::hasBloodScent(heroes[i])
			? newHorizonsBloodrage::incrementForRank(currentBattle->sides[i].bloodrageRank) : 0;
		currentBattle->sides[i].bloodragePainIncrement = newHorizonsBloodrage::hasRageThroughPain(heroes[i])
			? newHorizonsBloodrage::incrementForRank(currentBattle->sides[i].bloodrageRank) : 0;
	}

	currentBattle->tile = tile;
	currentBattle->terrainType = terrain;
	currentBattle->battlefieldType = battlefieldType;
	currentBattle->round = 0;
	currentBattle->activeStack = -1;
	currentBattle->replayAllowed = false;
	if (town)
		currentBattle->townID = town->id;

	//setting up siege obstacles
	if (town && town->fortificationsLevel().wallsHealth != 0)
	{
		auto fortification = town->fortificationsLevel();
		// Fortification HP belongs to the authoritative world snapshot, not to
		// whichever side happens to field a hero. This covers town garrisons and
		// prevents importing a v3 hero into a legacy world from changing durability.
		const auto canonicalSiege = cb->getHeroCapabilityRules()["rulesetVersion"].Integer() >= 3;
		currentBattle->si.canonicalStructuralHP = canonicalSiege;

		currentBattle->si.gateState = EGateState::CLOSED;

		currentBattle->si.wallState[EWallPart::GATE] = EWallState::INTACT;

		for(const auto wall : {EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL})
			currentBattle->si.wallState[wall] = static_cast<EWallState>(fortification.wallsHealth);

		if (fortification.citadelHealth != 0)
			currentBattle->si.wallState[EWallPart::KEEP] = static_cast<EWallState>(fortification.citadelHealth);

		if (fortification.upperTowerHealth != 0)
			currentBattle->si.wallState[EWallPart::UPPER_TOWER] = static_cast<EWallState>(fortification.upperTowerHealth);

		if (fortification.lowerTowerHealth != 0)
			currentBattle->si.wallState[EWallPart::BOTTOM_TOWER] = static_cast<EWallState>(fortification.lowerTowerHealth);

		if(canonicalSiege)
			for(const auto & [part, state] : currentBattle->si.wallState)
				if(state != EWallState::NONE)
					currentBattle->si.structuralHP[part] = SiegeInfo::maximumStructuralHP(part);
	}

	//randomize obstacles
	if (layout.obstaclesAllowed && (!town || !town->hasFort()))
 	{
		RandGen r{};
		auto ourRand = [&](){ return r.rand(); };
		r.srand(tile);
		r.rand(1,8); //battle sound ID to play... can't do anything with it here
		int tilesToBlock = r.rand(5,12);

		BattleHexArray blockedTiles;

		auto appropriateAbsoluteObstacle = [&](int id)
		{
			const auto * info = Obstacle(id).getInfo();
			return info && info->isAbsoluteObstacle && info->isAppropriate(currentBattle->terrainType, battlefieldType);
		};
		auto appropriateUsualObstacle = [&](int id)
		{
			const auto * info = Obstacle(id).getInfo();
			return info && !info->isAbsoluteObstacle && info->isAppropriate(currentBattle->terrainType, battlefieldType);
		};

		if(r.rand(1,100) <= 40) //put cliff-like obstacle
		{
			try
			{
				RangeGenerator obidgen(0, LIBRARY->obstacleHandler->size() - 1, ourRand);
				auto obstPtr = std::make_shared<CObstacleInstance>();
				obstPtr->obstacleType = CObstacleInstance::ABSOLUTE_OBSTACLE;
				obstPtr->ID = obidgen.getSuchNumber(appropriateAbsoluteObstacle);
				obstPtr->uniqueID = static_cast<si32>(currentBattle->obstacles.size());
				currentBattle->obstacles.push_back(obstPtr);

				for(const BattleHex & blocked : obstPtr->getBlockedTiles())
					blockedTiles.insert(blocked);
				tilesToBlock -= Obstacle(obstPtr->ID).getInfo()->blockedTiles.size() / 2;
			}
			catch(RangeGenerator::ExhaustedPossibilities &)
			{
				//silently ignore, if we can't place absolute obstacle, we'll go with the usual ones
				logGlobal->debug("RangeGenerator::ExhaustedPossibilities exception occurred - cannot place absolute obstacle");
			}
		}

		try
		{
			while(tilesToBlock > 0)
			{
				RangeGenerator obidgen(0, LIBRARY->obstacleHandler->size() - 1, ourRand);
				auto tileAccessibility = currentBattle->getAccessibility();
				const int obid = obidgen.getSuchNumber(appropriateUsualObstacle);
				const ObstacleInfo &obi = *Obstacle(obid).getInfo();

				auto validPosition = [&](const BattleHex & pos) -> bool
				{
					if(obi.height >= pos.getY())
						return false;
					if(pos.getX() == 0)
						return false;
					if(pos.getX() + obi.width > 15)
						return false;
					if(blockedTiles.contains(pos))
						return false;

					for(const BattleHex & blocked : obi.getBlocked(pos))
					{
						if(tileAccessibility[blocked.toInt()] == EAccessibility::UNAVAILABLE) //for ship-to-ship battlefield - exclude hardcoded unavailable tiles
							return false;
						if(blockedTiles.contains(blocked))
							return false;
						int x = blocked.getX();
						if(x <= 2 || x >= 14)
							return false;
					}

					return true;
				};

				RangeGenerator posgenerator(18, 168, ourRand);

				auto obstPtr = std::make_shared<CObstacleInstance>();
				obstPtr->ID = obid;
				obstPtr->pos = posgenerator.getSuchNumber(validPosition);
				obstPtr->uniqueID = static_cast<si32>(currentBattle->obstacles.size());
				currentBattle->obstacles.push_back(obstPtr);

				for(const BattleHex & blocked : obstPtr->getBlockedTiles())
					blockedTiles.insert(blocked);
				tilesToBlock -= static_cast<int>(obi.blockedTiles.size());
			}
		}
		catch(RangeGenerator::ExhaustedPossibilities &)
		{
			logGlobal->debug("RangeGenerator::ExhaustedPossibilities exception occurred - cannot place usual obstacle");
		}
	}

	//adding war machines
	//Checks if hero has artifact and create appropriate stack
	auto handleWarMachine = [&](BattleSide side, const ArtifactPosition & artslot, const BattleHex & hex)
	{
		const CArtifactInstance * warMachineArt = heroes[side]->getArt(artslot);

		if(nullptr != warMachineArt && hex.isValid())
		{
			CreatureID cre = warMachineArt->getType()->getWarMachine();

			if(cre != CreatureID::NONE)
				currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(cre, 1), side, SlotID::WAR_MACHINES_SLOT, hex);
		}
	};

	if(heroes[BattleSide::ATTACKER])
	{
		auto warMachineHexes = layout.warMachines.at(BattleSide::ATTACKER);

		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH1, warMachineHexes.at(0));
		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH2, warMachineHexes.at(1));
		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH3, warMachineHexes.at(2));
		if(town && town->fortificationsLevel().wallsHealth > 0)
			handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH4, warMachineHexes.at(3));
	}

	if(heroes[BattleSide::DEFENDER])
	{
		auto warMachineHexes = layout.warMachines.at(BattleSide::DEFENDER);

		if(!town) //defending hero shouldn't receive ballista (bug #551)
			handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH1, warMachineHexes.at(0));
		handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH2, warMachineHexes.at(1));
		handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH3, warMachineHexes.at(2));
	}
	//war machines added

	//battleStartpos read
	for(BattleSide side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		int formationNo = armies[side]->stacksCount() - 1;
		vstd::abetween(formationNo, 0, GameConstants::ARMY_SIZE - 1);

		int k = 0; //stack serial
		for(auto i = armies[side]->Slots().begin(); i != armies[side]->Slots().end(); i++, k++)
		{
			const BattleHex & pos = layout.units.at(side).at(k);

			if (pos.isValid())
				currentBattle->generateNewStack(currentBattle->nextUnitId(), *i->second, side, i->first, pos);
			else
				logMod->warn("Invalid battlefield layout! Failed to find position for unit %d for %s", k, side == BattleSide::ATTACKER ? "attacker" : "defender");
		}
	}

	//adding commanders
	for(BattleSide i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (heroes[i] && heroes[i]->getCommander() && heroes[i]->getCommander()->alive)
		{
			currentBattle->generateNewStack(currentBattle->nextUnitId(), *heroes[i]->getCommander(), i, SlotID::COMMANDER_SLOT_PLACEHOLDER, layout.commanders.at(i));
		}
	}

	if (currentBattle->townID.hasValue())
	{
		if (currentBattle->getDefendedTown()->fortificationsLevel().citadelHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_CENTRAL_TOWER);

		if (currentBattle->getDefendedTown()->fortificationsLevel().upperTowerHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_UPPER_TOWER);

		if (currentBattle->getDefendedTown()->fortificationsLevel().lowerTowerHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_BOTTOM_TOWER);

		//Moat generating is done on server
	}

	std::stable_sort(currentBattle->stacks.begin(), currentBattle->stacks.end(), [cmpst](const auto & left, const auto & right){ return cmpst(left.get(), right.get());});

	auto neutral = std::make_shared<CreatureAlignmentLimiter>(EAlignment::NEUTRAL);
	auto good = std::make_shared<CreatureAlignmentLimiter>(EAlignment::GOOD);
	auto evil = std::make_shared<CreatureAlignmentLimiter>(EAlignment::EVIL);

	const auto * bgInfo = LIBRARY->battlefields()->getById(battlefieldType);

	for(const std::shared_ptr<Bonus> & bonus : bgInfo->bonuses)
	{
		currentBattle->addNewBonus(bonus);
	}

	//native terrain bonuses - some battlefields, such as Cursed Ground, block them
	bool blocksNativeTerrainBonus = std::any_of(bgInfo->bonuses.begin(), bgInfo->bonuses.end(), [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus->type == BonusType::BLOCK_NATIVE_TERRAIN_BONUS;
	});

	if(!blocksNativeTerrainBonus)
	{
		auto nativeTerrain = std::make_shared<AllOfLimiter>();
		nativeTerrain->add(std::make_shared<TerrainLimiter>());
		nativeTerrain->add(std::make_shared<CreatureLevelLimiter>()); // creature only limiter - exclude hero

		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::TERRAIN_NATIVE, 1,  BonusSourceID())->addLimiter(nativeTerrain));
		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::TERRAIN_NATIVE, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK))->addLimiter(nativeTerrain));
		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::TERRAIN_NATIVE, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE))->addLimiter(nativeTerrain));
	}
	//////////////////////////////////////////////////////////////////////////

	//tactics
	BattleSideArray<int> battleRepositionHex = {};
	BattleSideArray<int> battleRepositionHexBlock = {};
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(heroes[i])
		{
			if(heroes[i]->tacticFormationEnabled)
				battleRepositionHex[i] += heroes[i]->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION);
			battleRepositionHexBlock[i] += heroes[i]->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION_BLOCK);
		}
	}
	int tacticsSkillDiffAttacker = battleRepositionHex[BattleSide::ATTACKER] - battleRepositionHexBlock[BattleSide::DEFENDER];
	int tacticsSkillDiffDefender = battleRepositionHex[BattleSide::DEFENDER] - battleRepositionHexBlock[BattleSide::ATTACKER];

	/* for current tactics, we need to choose one side, so, we will choose side when first - second > 0, and ignore sides
	   when first - second <= 0. If there will be situations when both > 0, attacker will be chosen. Anyway, in OH3 this
	   will not happen because tactics block opposite tactics on same value.
	   TODO: For now, it is an error to use BEFORE_BATTLE_REPOSITION bonus without counterpart, but it can be changed if
	   double tactics will be implemented.
	*/

	if(layout.tacticsAllowed)
	{
		if(newHorizonsHeroes::usesPerkRules(cb->getHeroPerkRules()))
		{
			BattleDeploymentState deployment;
			BattleSideArray<bool> hasGrandTactics{};
			for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			{
				const auto * hero = heroes[side];
				const bool hasTactics = hero && hero->tacticFormationEnabled
					&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.tactics");
				hasGrandTactics[side] = hero && hero->tacticFormationEnabled
					&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.grandTactics");
				if(hasTactics || hasGrandTactics[side])
					deployment.distances[side] = hasTactics ? 3 : 1;

				if(hero && hero->tacticFormationEnabled
					&& hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.redeployment"))
					deployment.finalRelocationDistances[side] = hasTactics ? 3 : 1;
			}
			if(hasGrandTactics[BattleSide::ATTACKER] != hasGrandTactics[BattleSide::DEFENDER]
				&& hasGrandTactics[BattleSide::ATTACKER])
				deployment.initialFirstSide = BattleSide::DEFENDER;
			if(deployment.distances[BattleSide::ATTACKER] > 0 || deployment.distances[BattleSide::DEFENDER] > 0
				|| deployment.finalRelocationDistances[BattleSide::ATTACKER] > 0
				|| deployment.finalRelocationDistances[BattleSide::DEFENDER] > 0)
				deployment.independent = true;
			currentBattle->deploymentState = deployment; // Initial setup is unpublished, not a live phase transition.
			const auto activeSide = deployment.activeSide();
			if(activeSide != BattleSide::NONE)
			{
				currentBattle->tacticsSide = activeSide;
				currentBattle->tacticDistance = deployment.activeDistance();
			}
		}
		else
		{
			if(tacticsSkillDiffAttacker > 0 && tacticsSkillDiffDefender > 0)
				logGlobal->warn("Double tactics is not implemented, only attacker will have tactics!");
			if(tacticsSkillDiffAttacker > 0)
			{
				currentBattle->tacticsSide = BattleSide::ATTACKER;
				//bonus specifies distance you can move beyond base row; this allows 100% compatibility with HMM3 mechanics
				currentBattle->tacticDistance = 1 + tacticsSkillDiffAttacker;
			}
			else if(tacticsSkillDiffDefender > 0)
			{
				currentBattle->tacticsSide = BattleSide::DEFENDER;
				//bonus specifies distance you can move beyond base row; this allows 100% compatibility with HMM3 mechanics
				currentBattle->tacticDistance = 1 + tacticsSkillDiffDefender;
			}
			else
				currentBattle->tacticDistance = 0;
		}
	}

	return currentBattle;
}

const CGHeroInstance * BattleInfo::getHero(const PlayerColor & player) const
{
	for(const auto & side : sides)
		if(side.color == player)
			return side.getHero();

	logGlobal->error("Player %s is not in battle!", player.toString());
	return nullptr;
}

BattleSide BattleInfo::whatSide(const PlayerColor & player) const
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(sides[i].color == player)
			return i;

	logGlobal->warn("BattleInfo::whatSide: Player %s is not in battle!", player.toString());
	return BattleSide::NONE;
}

CStack * BattleInfo::getStack(int stackID, bool onlyAlive)
{
	return const_cast<CStack *>(battleGetStackByID(stackID, onlyAlive));
}

BattleInfo::BattleInfo(IGameInfoCallback *cb, const BattleLayout & layout):
	BattleInfo(cb)
{
	*this->layout = layout;
}

BattleInfo::BattleInfo(IGameInfoCallback *cb)
	:CBonusSystemNode(BonusNodeType::BATTLE_WIDE),
	GameCallbackHolder(cb),
	sides({SideInBattle(cb), SideInBattle(cb)}),
	layout(std::make_unique<BattleLayout>()),
	round(-1),
	activeStack(-1),
	tile(-1,-1,-1),
	battlefieldType(BattleField::NONE),
	tacticsSide(BattleSide::NONE),
	tacticDistance(0)
{
	if(cb)
	{
		heroCommandRules = cb->getHeroCommandRules();
		magicRules = cb->getMagicRules();
		creatureCategoryRules = cb->getCreatureCategoryRules();
	}
}

BattleLayout BattleInfo::getLayout() const
{
	return *layout;
}

BattleID BattleInfo::getBattleID() const
{
	return battleID;
}

const IBattleInfo * BattleInfo::getBattle() const
{
	return this;
}

const scripting::Pool & BattleInfo::getScriptContextPool() const
{
	return cb->getScriptContextPool();
}

std::optional<PlayerColor> BattleInfo::getPlayerID() const
{
	return std::nullopt;
}

BattleInfo::~BattleInfo()
{
	stacks.clear();

	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(auto * army = battleGetArmyObject(i); army && army->battle == this)
			army->battle = nullptr;
}

int32_t BattleInfo::getActiveStackID() const
{
	return activeStack;
}

TStacks BattleInfo::getStacksIf(const TStackFilter & predicate) const
{
	TStacks ret;
	for (const auto & stack : stacks)
		if (predicate(stack.get()))
			ret.push_back(stack.get());
	return ret;
}

battle::Units BattleInfo::getUnitsIf(const battle::UnitFilter & predicate) const
{
	battle::Units ret;
	for (const auto & stack : stacks)
		if (predicate(stack.get()))
			ret.push_back(stack.get());
	return ret;
}


BattleField BattleInfo::getBattlefieldType() const
{
	return battlefieldType;
}

TerrainId BattleInfo::getTerrainType() const
{
	return terrainType;
}

IBattleInfo::ObstacleCList BattleInfo::getAllObstacles() const
{
	ObstacleCList ret;

	for(const auto & obstacle : obstacles)
		ret.push_back(obstacle);

	return ret;
}

PlayerColor BattleInfo::getSidePlayer(BattleSide side) const
{
	return getSide(side).color;
}

const CArmedInstance * BattleInfo::getSideArmy(BattleSide side) const
{
	return getSide(side).getArmy();
}

const CGHeroInstance * BattleInfo::getSideHero(BattleSide side) const
{
	return getSide(side).getHero();
}

uint8_t BattleInfo::getTacticDist() const
{
	return tacticDistance;
}

BattleSide BattleInfo::getTacticsSide() const
{
	return tacticsSide;
}

void BattleInfo::setDeploymentState(const BattleDeploymentState & state)
{
	state.validateTransitionFrom(deploymentState);
	deploymentState = state;
	const auto activeSide = deploymentState.activeSide();
	tacticsSide = activeSide;
	tacticDistance = deploymentState.activeDistance();
}

int32_t BattleInfo::getRound() const
{
	return round;
}

const CGTownInstance * BattleInfo::getDefendedTown() const
{
	if (townID.hasValue())
		return cb->getTown(townID);
	return nullptr;
}

EWallState BattleInfo::getWallState(EWallPart partOfWall) const
{
	if(si.canonicalStructuralHP)
		if(const auto it = si.structuralHP.find(partOfWall); it != si.structuralHP.end())
		{
			if(it->second == SiegeInfo::maximumStructuralHP(partOfWall))
				return si.wallState.at(partOfWall); // preserve full-strength REINFORCED visuals
			return SiegeInfo::stateFromStructuralHP(partOfWall, it->second);
		}
	return si.wallState.at(partOfWall);
}

int32_t BattleInfo::getWallStructuralHP(EWallPart partOfWall) const
{
	if(!si.canonicalStructuralHP)
		return 0;
	if(const auto it = si.structuralHP.find(partOfWall); it != si.structuralHP.end())
		return it->second;
	return 0;
}

EGateState BattleInfo::getGateState() const
{
	return si.gateState;
}

int32_t BattleInfo::getCastSpells(BattleSide side) const
{
	return getSide(side).castSpellsCount;
}

int32_t BattleInfo::getEnchanterCounter(BattleSide side) const
{
	return getSide(side).enchanterCounter;
}

bool BattleInfo::getTemporalFieldUsed(BattleSide side) const
{
	return getSide(side).temporalFieldUsed;
}

bool BattleInfo::getCounterspellArmed(BattleSide side) const
{
	return getSide(side).counterspellArmed;
}

int32_t BattleInfo::getMetamagicPendingCount(BattleSide side) const
{
	return getSide(side).metamagicPendingCount;
}

int32_t BattleInfo::getMetamagicUsesConsumed(BattleSide side) const
{
	return getSide(side).metamagicUsesConsumed;
}

bool BattleInfo::getMetamagicGrandUsed(BattleSide side) const
{
	return getSide(side).metamagicGrandUsed;
}

bool BattleInfo::getMetamagicFormulaReserveUsed(BattleSide side) const
{
	return getSide(side).metamagicFormulaReserveUsed;
}

bool BattleInfo::getMetamagicCountersequenceArmed(BattleSide side) const
{
	return getSide(side).metamagicCountersequenceArmed;
}

SpellID BattleInfo::getMetamagicFirstSpell(BattleSide side) const
{
	return getSide(side).metamagicFirstSpell;
}

uint32_t BattleInfo::getMetamagicFirstTargetUnitId(BattleSide side) const
{
	return getSide(side).metamagicFirstTargetUnitId;
}

const std::vector<SpellID> & BattleInfo::getMetamagicSequenceSpells(BattleSide side) const
{
	return getSide(side).metamagicSequenceSpells;
}

bool BattleInfo::getMetamagicFirstCounterspellNegated(BattleSide side) const
{
	return getSide(side).metamagicFirstCounterspellNegated;
}

const IBonusBearer * BattleInfo::getBonusBearer() const
{
	return this;
}

int64_t BattleInfo::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	if(damage.min != damage.max)
	{
		int64_t sum = 0;

		auto howManyToAv = std::min<int32_t>(10, attackerCount);

		for(int32_t g = 0; g < howManyToAv; ++g)
			sum += rng.nextInt64(damage.min, damage.max);

		return sum / howManyToAv;
	}
	else
	{
		return damage.min;
	}
}

int3 BattleInfo::getLocation() const
{
	return tile;
}

std::vector<SpellID> BattleInfo::getUsedSpells(BattleSide side) const
{
	return getSide(side).usedSpellsHistory;
}

void BattleInfo::nextRound()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(sides.at(side).doubleCommandState.orderPending()
			|| sides.at(side).doubleCommandState.secondWindReady())
			throw std::runtime_error("Cannot advance a battle round with unresolved Double Command continuation");
		if(round >= 1 && sides.at(side).preCombatOrderState.isUnresolved())
			throw std::runtime_error("Cannot advance a battle round with unresolved Battle Plan Order");
	}
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto extraActivation = sides.at(i).reducedExtraActivation;
		extraActivation.activeUnitId = ReducedExtraActivationState::INVALID_UNIT_ID;
		extraActivation.outputPercent = 100;
		sides.at(i).reducedExtraActivation = extraActivation;
		sides.at(i).sylvanLuck.nextRound();
		sides.at(i).luckSerendipity.nextRound(round + 1);
		sides.at(i).castSpellsCount = 0;
		sides.at(i).moraleSuppression.nextRound();
		sides.at(i).heroCommandUsed = false;
		sides.at(i).activeOrder = HeroCommand::NONE;
		sides.at(i).orderStates.clear();
		sides.at(i).focusFire.reset();
		// Unspent round-long Metamagic Spell grants expire below; the per-combat
		// Metamagic and Grand/Formula budgets remain.
		sides.at(i).clearMetamagicSequence();
		vstd::amax(--sides.at(i).enchanterCounter, 0);
	}
	// first round starts right after pre-battle effects (built-in enchants, OPENING_BATTLE_SPELL)
	// are applied, so skip the decrement here to grant them their full configured duration
	bool isFirstRound = round == 0;
	round += 1;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & spellResponse = sides.at(side).spellResponseState;
		if(spellResponse.hasState() && !spellResponse.isReadyAt(round))
			spellResponse = {};
		if(heroCommands::supportedByRules(heroCommandRules, HeroCommand::CHARGE))
			sides.at(side).heroActionAllowances.resetForRound(round);
		sides.at(side).warcastingState = sides.at(side).warcastingState.clearedIfExpired(round);
	}

	const auto plagueMarker = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)))))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	// Plague's three-round countdown is advanced only after its end-of-stack-turn
	// tick, so a cast made after the target already acted still receives all three
	// scheduled ticks instead of expiring at the next round boundary.
	const auto roundTimedEffects = CSelector(Bonus::NTurns).And(plagueMarker.Not());
	std::vector<uint32_t> expiringBattleForms;
	for(auto & s : stacks)
	{
		const bool formPaused = s->hasBattleForm() && battle::battleFormDurationPaused(*s);
		if(!isFirstRound && s->hasBattleForm() && s->getBattleFormRoundsRemaining() <= 1 && !formPaused)
			expiringBattleForms.push_back(s->unitId());
		// new turn effects
		if(!isFirstRound && !s->isTimeStopped())
		{
			const auto preserveBeneficial = spellLockPreservesBeneficial(*s);
			if(isSpellLocked(*s) && preserveBeneficial.has_value())
			{
				const auto preservedMagic = CSelector([preserveBeneficial](const Bonus * bonus)
				{
					return isPreservedSpellLockEffect(bonus, *preserveBeneficial);
				});
				s->reduceBonusDurations(roundTimedEffects.And(preservedMagic.Not()));
			}
			else
				s->reduceBonusDurations(roundTimedEffects);
		}

		s->afterNewRound(isFirstRound, true, formPaused);
	}
	for(auto & obst : obstacles)
		obst->battleTurnPassed();
	// All duration/death hooks complete before restoring footprints. Each ID
	// sees a fresh board, including earlier relocations, never a shared stale scan.
	std::ranges::sort(expiringBattleForms);
	for(const auto id : expiringBattleForms)
	{
		auto * stack = getStack(id, false);
		if(stack && battle::endBattleFormAtNearestLegalPosition(*stack, getAccessibility(stack)))
			stack->removeBonusesRecursive(CSelector(battle::isPolymorphMarker));
	}
	for(auto & stack : stacks)
		if(!stack->hasBattleForm())
			stack->removeBonusesRecursive(CSelector(battle::isPolymorphMarker));
}

void BattleInfo::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
	activeStack = unitId;
	if(reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION)
		return;

	CStack * st = getStack(activeStack);
	if(battleBeginsActivation(st, reason))
	{
		st->removeBonusesRecursive(CSelector(Bonus::UntilNextCreatureActivation));

		// Second Wind is a genuine activation too, but must not broaden the
		// lifetime of unrelated legacy STACK_GETS_TURN bonuses.
		st->removeBonusesRecursive(CSelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));
		const auto owner = playerToSide(battleGetOwner(st));
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			sides.at(side).sylvanLuck.beginActivation(unitId, side == owner);
	}
	if(st->isTimeStopped() && reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		// A stopped unit receives a synthetic queue activation solely so the
		// authoritative flow can reach a Hero Action window. Mark that queue
		// slot consumed without changing movement/action permission; the flag is
		// reset by afterNewRound and is used only by turn ordering.
		st->timeStopTurnConsumedFlag = true;
	}

	// A hero/creature spell that does not consume the active unit's turn is a
	// continuation of the same activation.  Keep the Fire Wall activation token
	// stable across those transitions, otherwise a repeated movement callback
	// could deal the same wall damage twice.  HERO_COMMAND is normally another
	// continuation, except for the explicit Second Wind activation whose state
	// is armed before the transition is published.
	bool newActivation = reason != BattleUnitTurnReason::HERO_SPELLCAST
		&& reason != BattleUnitTurnReason::UNIT_SPELLCAST;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		newActivation = false;
		if(st)
		{
			const auto controllerSide = playerToSide(battleGetOwner(st));
			if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
			{
				const auto * orderState = sides.at(controllerSide).findOrder(HeroCommand::SECOND_WIND);
				newActivation = orderState
					&& orderState->secondWindActive
					&& orderState->primaryTargetUnitId == unitId;
			}
		}
	}
	if(newActivation)
	{
		// Last Stand ends the current activation without removing the surviving
		// stack. Keep the marker through all continuations, and clear it only
		// when the stack actually receives a new activation.
		st->armorerLastStandEndedActivation = false;
		st->armorerLastStandDefending = false;
		st->pursuitMovementRemaining = 0;
		st->cleaveUsedThisActivation = false;
		st->setRangedFollowUpDamagePercent(0);
		const auto side = playerToSide(battleGetOwner(st));
		const bool ordinaryCreature = st->alive() && !st->isGhost() && !st->isTurret()
			&& !st->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& st->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
		const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
			? battleGetFightingHero(side) : nullptr;
		if(ordinaryCreature && hero
			&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
			sides.at(side).relentlessAssault.beginActivation();
	}
	if(newActivation && activationSerial < std::numeric_limits<si32>::max())
		++activationSerial;

	if (reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
	{
		//remove bonuses that last until when stack gets new turn
		if(!st->isTimeStopped())
			st->removeBonusesRecursive(CSelector([](const Bonus * bonus)
			{
				return Bonus::UntilGetsTurn(bonus)
					&& !newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
			}));
	}

	if(battleBeginsActivation(st, reason))
		st->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
			battleGetOwnerHero(st), st, reason));

	st->afterGetsTurn(reason);
}

bool BattleInfo::hasPursuitState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->pursuitMovementRemaining > 0;
	});
}

bool BattleInfo::hasCleaveState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->cleaveUsedThisActivation;
	});
}

bool BattleInfo::hasNoQuarterState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		if(!stack)
			return false;
		if(stack->noQuarterMoraleActivationsRemaining > 0)
			return true;
		const auto bonuses = stack->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterBonus(bonus);
		}));
		return bonuses && !bonuses->empty();
	});
}

bool BattleInfo::hasRageThroughPainState() const
{
	if(sides[BattleSide::ATTACKER].bloodragePainIncrement != 0
		|| sides[BattleSide::DEFENDER].bloodragePainIncrement != 0)
		return true;
	return std::ranges::any_of(stacks, [](const auto & stack)
	{
		return stack && stack->getPersonalBloodrageIncrement() != 0;
	});
}

bool BattleInfo::hasConfusionState() const
{
	return std::ranges::any_of(stacks, [](const auto & stack)
	{
		return stack && stack->confusionState.hasState();
	});
}

void BattleInfo::validateConfusionStates() const
{
	for(const auto & stack : stacks)
	{
		if(stack)
			stack->confusionState.validate();
	}
}

bool BattleInfo::hasCasualtyProvenanceState() const
{
	return std::ranges::any_of(stacks, [](const auto & stack)
	{
		return stack && stack->hasCasualtyProvenanceState();
	});
}

bool BattleInfo::hasElementalRebirthBasisState() const
{
	return std::ranges::any_of(stacks, [](const auto & stack)
	{
		return stack && stack->getBattleStartMaximumAggregateHP() > 0;
	});
}

void BattleInfo::setRebirthChainUsed(BattleSide side, bool used)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Rebirth Chain side");
	sides.at(side).rebirthChainUsed = used;
}

void BattleInfo::setPhoenixSparkUsed(BattleSide side, bool used)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Phoenix Spark side");
	if(!used && sides.at(side).phoenixSparkUsed)
		throw std::runtime_error("Cannot restore a spent Phoenix Spark combat use");
	sides.at(side).phoenixSparkUsed = used;
}

bool BattleInfo::hasRebirthOutputOriginalHPState() const
{
	return std::ranges::any_of(stacks, [](const auto & stack)
	{
		return stack && stack->getRebirthOriginalAggregateHP() > 0;
	});
}

void BattleInfo::addUnit(uint32_t id, const JsonNode & data)
{
	if(heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE) && id != nextUnitId())
		throw std::runtime_error("Invalid New Horizons targeted unit allocation");
	battle::UnitInfo info;
	info.load(id, data);
	if(info.rebirthOriginalAggregateHP < 0)
		throw std::runtime_error("Invalid Rebirth output original HP");
	if(info.phantomIntegrity < 0 || info.phantomDuration < 0
		|| ((info.phantomIntegrity == 0) != (info.phantomDuration == 0)))
		throw std::runtime_error("Invalid Phantom Army spawn profile");
	if(info.phantomIntegrity > 0
		&& (info.count <= 0 || !info.summoned || info.natureSummoned
			|| !newHorizonsSorcery::phantomArmyDurationSupported(info.phantomDuration)))
		throw std::runtime_error("Invalid Phantom Army spawn profile");
	if(info.rebirthOriginalAggregateHP > 0
		&& (info.count <= 0 || !info.type.hasValue() || !info.type.toCreature()
			|| !info.summoned || info.natureSummoned || info.phantomIntegrity > 0))
		throw std::runtime_error("Invalid Rebirth output spawn metadata");
	if(info.rebirthOriginalAggregateHP > 0)
	{
		if(info.side != BattleSide::ATTACKER && info.side != BattleSide::DEFENDER)
			throw std::runtime_error("Invalid Rebirth output side");
		const auto effectiveMaxHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
			getSideArmy(info.side), info.type, getSidePlayer(info.side), info.side);
		if(effectiveMaxHP <= 0
			|| info.count > std::numeric_limits<int64_t>::max() / effectiveMaxHP
			|| info.rebirthOriginalAggregateHP > static_cast<int64_t>(info.count) * effectiveMaxHP)
			throw std::runtime_error("Rebirth output original HP exceeds its aggregate capacity");
	}

	CStackBasicDescriptor base(info.type, info.count);

	PlayerColor owner = getSidePlayer(info.side);

	auto ret = std::make_unique<CStack>(&base, owner, info.id, info.side, SlotID::SUMMONED_SLOT_PLACEHOLDER);
	ret->initialPosition = info.position;
	// Summon provenance affects inherited-bonus acceptance, so it must be set
	// before localInit attaches the stack to the bonus graph and fills caches.
	ret->summoned = info.summoned;
	ret->natureSummoned = info.natureSummoned;
	stacks.push_back(std::move(ret));
	stacks.back()->localInit(this);
	// CUnitState::localInit resets transient state, including summon provenance.
	// Restore the authoritative packet values before any subsequent bonus query.
	stacks.back()->summoned = info.summoned;
	stacks.back()->natureSummoned = info.natureSummoned;
	stacks.back()->initializeRebirthOriginalAggregateHP(info.rebirthOriginalAggregateHP);
	if(info.phantomIntegrity > 0)
		stacks.back()->initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
	const auto * orderState = sides.at(info.side).findOrder(HeroCommand::RIPOSTE);
	const auto * hero = battleGetFightingHero(info.side);
	if(orderState && orderState->issuedRound == round
		&& hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE))
	{
		auto * addedUnit = stacks.back().get();
		if(addedUnit->alive() && !addedUnit->isGhost() && !addedUnit->isTurret()
			&& !addedUnit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& addedUnit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& !newHorizonsOffense::hasVengeanceRetaliationBonus(addedUnit))
			addedUnit->addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::vengeanceRetaliationBonus()));
	}
	stacks.back()->nodeHasChanged();
}

void BattleInfo::moveUnit(uint32_t id, const BattleHex & destination)
{
	auto * sta = getStack(id);
	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	if(sta->isTimeStopped())
	{
		logNetwork->warn("Ignoring movement of Time Stop unit %d", id);
		return;
	}
	if(sta->getPosition() != destination)
	{
		// Entangle is a position effect: an accepted forced move or teleport
		// removes only Entangle's own root marker. Unrelated BIND_EFFECT sources
		// (including classic Bind) keep their original lifecycle.
		if(sta->hasBonusOfType(BonusType::BIND_EFFECT))
		{
			if(const auto entangleSpell = entangleSpellId())
				sta->removeBonusesRecursive(Selector::type()(BonusType::BIND_EFFECT)
					.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(*entangleSpell))));
		}

		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto * state = sides.at(side).findOrder(HeroCommand::HOLD_THE_LINE);
			if(!state || state->containsHoldBroken(id))
				continue;
			if(const auto * anchor = state->anchorFor(id); anchor && anchor->position != destination.toInt())
			{
				state->holdBrokenUnitIds.insert(std::lower_bound(state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), id), id);
				break;
			}
		}
	}
	sta->position = destination;
	expireSeparatedHeroOrderProtect();
	//Bonuses can be limited by unit placement, so, change tree version
	//to force updating a bonus. TODO: update version only when such bonuses are present
	nodeHasChanged();
}

void BattleInfo::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	CStack * changedStack = getStack(id, false);
	if(!changedStack)
		throw std::runtime_error("Invalid unit id in BattleInfo update");
	if(changedStack->isTimeStopped() && healthDelta != 0)
	{
		logNetwork->warn("Ignoring health update of Time Stop unit %d", id);
		return;
	}
	if(changedStack->isTimeStopped())
	{
		// UnitChanges also carries a serialized state.  A stopped unit cannot
		// move, wait, defend, cast, or otherwise mutate activation state; the
		// authoritative flow advances it with a no-op instead.  Reject all
		// external state updates until the marker is released.
		logNetwork->warn("Ignoring state update of Time Stop unit %d", id);
		return;
	}

	if(!changedStack->alive() && healthDelta > 0)
	{
		//checking if we resurrect a stack that is under a living stack
		auto accessibility = getAccessibility();

		if(!accessibility.accessible(changedStack->getPosition(), changedStack))
		{
			logNetwork->error("Cannot resurrect %s because hex %d is occupied!", changedStack->nodeName(), changedStack->getPosition());
			return; //position is already occupied
		}
	}

	bool killed = (-healthDelta) >= changedStack->getAvailableHealth();//todo: check using alive state once rebirth will be handled separately

	bool resurrected = !changedStack->alive() && healthDelta > 0;

	//applying changes
	changedStack->load(data);
	expireSeparatedHeroOrderProtect();


	if(healthDelta < 0)
	{
		changedStack->removeBonusesRecursive(Bonus::UntilBeingAttacked);
	}

	if(healthDelta < 0)
	{
		changedStack->nodeHasChanged();	//bonuses with TIMES_STACK_SIZE updater may change
	}

	resurrected = resurrected || (killed && changedStack->alive());

	if(killed)
	{
		if(changedStack->cloneID >= 0)
		{
			//remove clone as well
			CStack * clone = getStack(changedStack->cloneID);
			if(clone)
				clone->makeGhost();

			changedStack->cloneID = -1;
		}
	}

	if(resurrected || killed)
	{
		//removing all spells effects
		auto selector = [](const Bonus * b)
		{
			//Special case: persistent effects, such as DISRUPTING_RAY, survive death
			return b->source == BonusSource::SPELL_EFFECT && !b->sid.as<SpellID>().toSpell()->isPersistent();
		};
		changedStack->removeBonusesRecursive(selector);
	}

	if(!changedStack->alive() && changedStack->isClone())
	{
		for(auto & s : stacks)
		{
			if(s->cloneID == changedStack->unitId())
				s->cloneID = -1;
		}
	}
	if(!changedStack->alive())
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto extraActivation = sides.at(side).reducedExtraActivation;
			if(extraActivation.activeUnitId != id)
				continue;
			extraActivation.activeUnitId = ReducedExtraActivationState::INVALID_UNIT_ID;
			extraActivation.outputPercent = 100;
			sides.at(side).reducedExtraActivation = extraActivation;
		}
	}
}

void BattleInfo::removeUnit(uint32_t id)
{
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();
		auto * toRemove = getStack(toRemoveId, false);

		if(!toRemove)
		{
			logGlobal->error("Cannot find stack %d", toRemoveId);
			return;
		}

		if(!toRemove->ghost)
		{
			toRemove->onRemoved();
			toRemove->detachFromAll();

			//stack may be removed instantly (not being killed first)
			//handle clone remove also here
			if(toRemove->cloneID >= 0)
			{
				ids.insert(toRemove->cloneID);
				toRemove->cloneID = -1;
			}

			//cleanup remaining clone links if any
			for(const auto & s : stacks)
			{
				if(s->cloneID == toRemoveId)
					s->cloneID = -1;
			}
		}

		ids.erase(toRemoveId);
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto extraActivation = sides.at(side).reducedExtraActivation;
		if(extraActivation.activeUnitId != id)
			continue;
		extraActivation.activeUnitId = ReducedExtraActivationState::INVALID_UNIT_ID;
		extraActivation.outputPercent = 100;
		sides.at(side).reducedExtraActivation = extraActivation;
	}
	expireSeparatedHeroOrderProtect();
}

battle::BattleEffectSnapshot BattleInfo::captureBattleEffects(uint32_t id) const
{
	const auto candidates = getStacksIf([id](const CStack * candidate) { return candidate->unitId() == id; });
	const auto * stack = candidates.empty() ? nullptr : candidates.front();
	if(!stack || !stack->alive() || stack->isGhost() || stack->isTurret() || !stack->getPosition().isValid())
		throw std::runtime_error("Spell-effect exchange endpoint is not a living battlefield stack");
	battle::BattleEffectSnapshot result;
	for(const auto & bonus : stack->getExportedBonusList())
		if(bonus->source == BonusSource::SPELL_EFFECT)
			result.effects.push_back(*bonus);
	result.sidecars = battle::captureBattleEffectSidecars(*stack);
	result.recipientHealth = stack->acquireState()->save()["state"]["health"];
	result.capacityHealthReferenceMax = stack->getCapacityHealthReferenceMax();
	return result;
}

void BattleInfo::exchangeBattleEffects(const battle::BattleEffectExchange & exchange)
{
	exchange.validateShape();
	for(const auto & endpoint : exchange.endpoints)
		if(!battle::exactBattleEffectSnapshotEqual(captureBattleEffects(endpoint.id), endpoint.expected))
			throw std::runtime_error("Stale spell-effect exchange endpoint snapshot");
	struct Prepared
	{
		CStack * stack;
		std::unique_ptr<CBonusSystemNode::PreparedLocalBonusReplacement> bonuses;
		battle::PreparedBattleEffectHealth health;
	};
	std::array<Prepared, 2> prepared;
	for(size_t index = 0; index < prepared.size(); ++index)
	{
		const auto & endpoint = exchange.endpoints[index];
		auto * stack = getStack(endpoint.id, false);
		std::vector<std::shared_ptr<Bonus>> replacement;
		for(const auto & bonus : stack->getExportedBonusList())
			if(bonus->source != BonusSource::SPELL_EFFECT)
				replacement.push_back(bonus);
		std::vector<bool> retained(stack->getExportedBonusList().size(), false);
		for(const auto & effect : endpoint.replacement.effects)
		{
			std::shared_ptr<Bonus> copy;
			if(effect.propagator)
			{
				size_t candidate = 0;
				for(const auto & original : stack->getExportedBonusList())
				{
					if(!retained[candidate] && battle::exactBattleEffectEqual(*original, effect))
					{
						retained[candidate] = true;
						copy = original;
						break;
					}
					++candidate;
				}
				if(!copy)
					throw std::runtime_error("Spell-effect exchange cannot change local propagated effects");
			}
			else
				copy = std::make_shared<Bonus>(effect);
			replacement.push_back(std::move(copy));
		}
		prepared[index].stack = stack;
		prepared[index].health = battle::prepareBattleEffectHealth(*stack, endpoint.expected, endpoint.replacement);
		prepared[index].bonuses = stack->prepareLocalBonusReplacement(replacement);
	}
	// Everything below is allocation-free. No refresh path can reset duration,
	// Guardian pool or Confusion history, and the second endpoint cannot fail.
	for(size_t index = 0; index < prepared.size(); ++index)
	{
		auto & entry = prepared[index];
		entry.stack->commitLocalBonusReplacement(*entry.bonuses);
		entry.stack->commitPreparedCapacityHealth(*entry.health.unit);
		battle::commitBattleEffectSidecars(*entry.stack, exchange.endpoints[index].replacement.sidecars);
	}
}

void BattleInfo::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth ADD");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), entry, true);
		}

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, true);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth UPDATE");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), entry, false);
		}

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, false);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}

	for(const Bonus & one : bonus)
	{
		if(sta->isTimeStopped() && !timeStopState::isStateBonus(one)
			&& !newHorizonsOffense::isNoQuarterBonus(&one))
		{
			logNetwork->warn("Ignoring effect removal from Time Stop unit %d", id);
			continue;
		}
		if(battle::isPolymorphMarker(&one) && sta->hasBattleForm())
		{
			const auto markers = sta->getBonuses(CSelector([&one](const Bonus * candidate)
			{
				return battle::isPolymorphMarker(candidate) && candidate->sid == one.sid
					&& candidate->val == one.val;
			}));
			if(markers && !markers->empty()
				&& !battle::endBattleFormAtNearestLegalPosition(*sta, getAccessibility(sta)))
				continue; // Retain both marker and form until a legal round boundary.
		}

		auto selector = [one](const Bonus * b)
		{
			//compare everything but turnsRemain, limiter and propagator
			return one.duration == b->duration
			&& one.type == b->type
			&& one.subtype == b->subtype
			&& one.source == b->source
			&& one.val == b->val
			&& one.sid == b->sid
			&& one.valType == b->valType
			&& one.effectRange == b->effectRange
			&& (one.type != BonusType::CONFUSION_PENDING || one.spellCasterOwner == b->spellCasterOwner);
		};
		const auto confusionMarkers = sta->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
		const bool removesConfusion = std::ranges::any_of(*confusionMarkers, [&](const auto & marker)
		{
			return newHorizonsConfusionControl::isPendingMarker(marker.get()) && selector(marker.get());
		});
		sta->removeBonusesRecursive(selector);
		if(removesConfusion && !sta->hasBonusOfType(BonusType::CONFUSION_PENDING))
			sta->confusionState.clearPending();
		if(one.type == BonusType::GUARDIAN_SPIRIT)
		{
			const auto remaining = sta->getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
			if(!remaining || remaining->empty())
			{
				sta->guardianSpiritHitPoints = 0;
				sta->guardianSpiritRoundsRemaining = 0;
			}
			else
			{
				sta->guardianSpiritRoundsRemaining = 0;
				for(const auto & marker : *remaining)
					if(marker)
						sta->guardianSpiritRoundsRemaining = std::max<int32_t>(
							sta->guardianSpiritRoundsRemaining, marker->turnsRemain);
			}
		}
	}
}

void BattleInfo::expireTimeStops(BattleSide casterSide)
{
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return;

	for(auto & stack : stacks)
	{
		if(!stack)
			continue;

		stack->removeBonusesRecursive(CSelector([casterSide](const Bonus * bonus)
		{
			return bonus && timeStopState::isStateBonus(*bonus)
				&& timeStopState::belongsToSide(*bonus, casterSide);
		}));
	}

	clearPendingTimeStopHeroAction(casterSide);
}

namespace
{
ui8 timeStopSideBit(BattleSide side)
{
	if(side == BattleSide::ATTACKER)
		return 1u;
	if(side == BattleSide::DEFENDER)
		return 2u;
	return 0u;
}
}

ui8 BattleInfo::getPendingTimeStopHeroActionSides() const
{
	ui8 result = pendingTimeStopHeroActionSides;
	// Saves written with the first Time Stop format only have the scalar field.
	// Treat that field as a one-origin mask until the battle reaches a new
	// serialization boundary and can persist the complete set.
	if(result == 0)
		result = timeStopSideBit(pendingTimeStopHeroActionSide);
	return result;
}

BattleSide BattleInfo::getPendingTimeStopHeroActionSide() const
{
	const auto pending = getPendingTimeStopHeroActionSides();
	if(pending & timeStopSideBit(BattleSide::ATTACKER))
		return BattleSide::ATTACKER;
	if(pending & timeStopSideBit(BattleSide::DEFENDER))
		return BattleSide::DEFENDER;
	return BattleSide::NONE;
}

bool BattleInfo::hasPendingTimeStopHeroAction(BattleSide side) const
{
	return (getPendingTimeStopHeroActionSides() & timeStopSideBit(side)) != 0;
}

void BattleInfo::notePendingTimeStopHeroAction(BattleSide side)
{
	const auto bit = timeStopSideBit(side);
	if(!bit)
		return;

	pendingTimeStopHeroActionSides = getPendingTimeStopHeroActionSides() | bit;
	if(pendingTimeStopHeroActionSide == BattleSide::NONE)
		pendingTimeStopHeroActionSide = side;
}

void BattleInfo::clearPendingTimeStopHeroAction(BattleSide side)
{
	const auto bit = timeStopSideBit(side);
	if(!bit)
		return;

	pendingTimeStopHeroActionSides = getPendingTimeStopHeroActionSides() & static_cast<ui8>(~bit);
	if(pendingTimeStopHeroActionSides == 0)
		pendingTimeStopHeroActionSide = BattleSide::NONE;
	else if(pendingTimeStopHeroActionSide == side)
		pendingTimeStopHeroActionSide = getPendingTimeStopHeroActionSide();
}

uint32_t BattleInfo::nextUnitId() const
{
	if(heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE)
		&& stacks.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("New Horizons targeted unit identities exhausted");
	return static_cast<uint32_t>(stacks.size());
}

void BattleInfo::addOrUpdateUnitBonus(CStack * sta, const Bonus & value, bool forceAdd)
{
	if(newHorizonsSwiftRebirth::isLifecycleMarker(value))
	{
		newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), value, forceAdd);
		std::shared_ptr<Bonus> previous;
		for(const auto & existing : sta->getExportedBonusList())
			if(existing && newHorizonsSwiftRebirth::isLifecycleMarker(*existing))
				previous = existing;
		if(!forceAdd && !previous)
			throw std::invalid_argument("Swift Rebirth update requires a local lifecycle");
		if(previous)
			sta->removeBonus(previous);
		sta->addNewBonus(std::make_shared<Bonus>(value));
		return; // Lifecycle bookkeeping remains writable during Time Stop.
	}
	if(value.type == BonusType::CONFUSION_PENDING)
		newHorizonsConfusionControl::validateMarker(value);
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
		physicalAfflictions::markerMetadata(value);

	if(sta->isTimeStopped() && !timeStopState::isStateBonus(value))
	{
		logNetwork->warn("Ignoring new effect on Time Stop unit %d", sta->unitId());
		return;
	}
	if(value.type == BonusType::CONFUSION_PENDING)
	{
		auto next = sta->confusionState;
		next.applyPending(value.spellCasterOwner, value.val == 2);
		sta->removeBonusesRecursive(Selector::type()(BonusType::CONFUSION_PENDING));
		sta->addNewBonus(std::make_shared<Bonus>(value));
		sta->confusionState = next;
		return;
	}
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
	{
		// A source/sid pair owns one marker. Replace duplicate local markers while
		// retaining the longest same-duration timer, so repeated SetStackEffect
		// entries cannot leave multiple ordering records for one status group.
		Bonus marker = value;
		bool found = false;
		for(const auto & existing : sta->getExportedBonusList())
		{
			if(!existing || existing->type != BonusType::PHYSICAL_AFFLICTION
				|| existing->source != value.source || existing->sid != value.sid)
				continue;
			found = true;
			if(existing->duration == marker.duration && Bonus::NTurns(existing.get()))
				marker.turnsRemain = std::max(marker.turnsRemain, existing->turnsRemain);
		}
		if(found)
		{
			const CSelector markerGroup([&value](const Bonus * bonus)
			{
				return bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION
					&& bonus->source == value.source && bonus->sid == value.sid;
			});
			sta->removeBonusesRecursive(markerGroup);
		}
		sta->addNewBonus(std::make_shared<Bonus>(marker));
		return;
	}
	if(value.type == BonusType::GUARDIAN_SPIRIT)
	{
		if(value.val <= 0 || !Bonus::NTurns(&value) || value.turnsRemain <= 0)
			throw std::runtime_error("Invalid Guardian Spirit bonus pool or duration");

		// Guardian Spirit is a refreshable pool, not a stackable bonus. The
		// marker carries the cast-time maximum while the saved unit state carries
		// the remaining pool consumed by physical damage.
		sta->removeBonusesRecursive(Selector::type()(BonusType::GUARDIAN_SPIRIT));
		sta->addNewBonus(std::make_shared<Bonus>(value));
		sta->guardianSpiritHitPoints = value.val;
		sta->guardianSpiritRoundsRemaining = value.turnsRemain;
		return;
	}

	const auto matchesRefreshIdentity = [&value](const std::shared_ptr<Bonus> & stackBonus)
	{
		return stackBonus && stackBonus->source == value.source && stackBonus->sid == value.sid
			&& stackBonus->type == value.type && stackBonus->subtype == value.subtype
			&& stackBonus->valType == value.valType && stackBonus->statusIdentity == value.statusIdentity;
	};
	const auto updateMatchingBonuses = [&]()
	{
		for(const auto & stackBonus : sta->getExportedBonusList()) //TODO: optimize
		{
			if(!matchesRefreshIdentity(stackBonus))
				continue;
			stackBonus->turnsRemain = std::max(stackBonus->turnsRemain, value.turnsRemain);
			for(const BonusStatusTag tag : value.statusTags)
				if(std::find(stackBonus->statusTags.begin(), stackBonus->statusTags.end(), tag) == stackBonus->statusTags.end())
					stackBonus->statusTags.push_back(tag);
		}
		sta->nodeHasChanged();
	};
	const CSelector matchingStatusIdentity([&value](const Bonus * candidate)
	{
		return candidate && candidate->statusIdentity == value.statusIdentity;
	});
	const bool hasLegacyRefreshCandidate = sta->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, value.sid)
		.And(Selector::typeSubtypeValueType(value.type, value.subtype, value.valType)).And(matchingStatusIdentity));
	if(!value.hasStatusMetadata())
	{
		// Preserve legacy selector behavior for ordinary untagged bonuses. The
		// local refresh still avoids merging into a distinct explicit identity.
		if(forceAdd || !hasLegacyRefreshCandidate)
		{
			logBonus->trace("%s receives a new bonus: %s", sta->nodeName(), value.Description(nullptr));
			sta->addNewBonus(std::make_shared<Bonus>(value));
		}
		else
			updateMatchingBonuses();
	}
	else
	{
		// Explicit statuses are keyed by the ordinary component fields plus their
		// identity. Different identities must coexist, including non-spell sources.
		const bool hasMatchingIdentity = std::ranges::any_of(sta->getExportedBonusList(), matchesRefreshIdentity);
		if(forceAdd || !hasMatchingIdentity)
		{
			logBonus->trace("%s receives a new identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
			sta->addNewBonus(std::make_shared<Bonus>(value));
		}
		else
		{
			logBonus->trace("%s updated identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
			updateMatchingBonuses();
		}
	}
}

void BattleInfo::setWallState(EWallPart partOfWall, EWallState state)
{
	si.wallState[partOfWall] = state;
	if(si.canonicalStructuralHP && state == EWallState::DESTROYED)
		si.structuralHP[partOfWall] = 0;
}

void BattleInfo::setWallStructuralHP(EWallPart partOfWall, int32_t hp)
{
	if(!si.canonicalStructuralHP)
		return;
	si.structuralHP[partOfWall] = std::clamp(hp, 0, SiegeInfo::maximumStructuralHP(partOfWall));
	si.wallState[partOfWall] = SiegeInfo::stateFromStructuralHP(partOfWall, si.structuralHP[partOfWall]);
}

void BattleInfo::addObstacle(const ObstacleChanges & changes)
{
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->fromInfo(changes);
	obstacles.push_back(obstacle);
}

void BattleInfo::updateObstacle(const ObstacleChanges& changes)
{
	auto changedObstacle = std::make_shared<SpellCreatedObstacle>();
	changedObstacle->fromInfo(changes);

	for(auto & obstacle : obstacles)
	{
		if(obstacle->uniqueID == changes.id) // update this obstacle
		{
			auto * spellObstacle = dynamic_cast<SpellCreatedObstacle *>(obstacle.get());
			assert(spellObstacle);

			// Most legacy obstacle updates only change visibility.  Canonical
			// New Horizons Fire Wall also publishes its per-activation trigger
			// token, which must be retained on the authoritative obstacle or a
			// repeated movement callback could deal damage twice.
			spellObstacle->revealed = changedObstacle->revealed;
			spellObstacle->lastTriggerUnit = changedObstacle->lastTriggerUnit;
			spellObstacle->lastTriggerActivation = changedObstacle->lastTriggerActivation;

			break;
		}
	}
}

void BattleInfo::removeObstacle(uint32_t id)
{
	for(int i=0; i < obstacles.size(); ++i)
	{
		if(obstacles[i]->uniqueID == id) //remove this obstacle
		{
			obstacles.erase(obstacles.begin() + i);
			break;
		}
	}
}

CArmedInstance * BattleInfo::battleGetArmyObject(BattleSide side) const
{
	return const_cast<CArmedInstance*>(CBattleInfoEssentials::battleGetArmyObject(side));
}

CGHeroInstance * BattleInfo::battleGetFightingHero(BattleSide side) const
{
	return const_cast<CGHeroInstance*>(CBattleInfoEssentials::battleGetFightingHero(side));
}

void BattleInfo::validateFocusFireStates() const
{
	std::set<uint32_t> unitIds;
	const bool targetedRules = heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE);
	for(const auto & unit : stacks)
	{
		if(!unit)
			throw std::runtime_error("Invalid null battle unit reference");
		// nextUnitId allocates by vector size. Unique IDs below size imply dense
		// coverage even after sorting; removed units remain as retained descriptors.
		if(targetedRules && (unit->unitId() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
			|| unit->unitId() >= stacks.size() || !unitIds.insert(unit->unitId()).second))
			throw std::runtime_error("Invalid New Horizons targeted battle unit identity");
	}
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side);
		state.validateOrderStates();
		const bool canonicalOrderRules = heroCommands::isCanonicalRules(heroCommandRules);
		// In legacy saves the spell count doubled as an action-budget marker.
		// Once the typed ledger is initialized for this round, spell history is
		// descriptive only and must not invalidate a later Order.
		const bool legacySpellHistoryBlocksOrder = state.castSpellsCount != 0
			&& (!heroCommands::supportedByRules(heroCommandRules, HeroCommand::CHARGE)
				|| state.heroActionAllowances.currentRound != round);
		if((state.activeDoctrine != HeroCommand::NONE
				&& (!heroCommands::isDoctrine(state.activeDoctrine)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeDoctrine)))
			|| (state.activeOrder != HeroCommand::NONE
				&& (heroCommands::isDoctrine(state.activeOrder)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeOrder)
					|| !state.heroCommandUsed || legacySpellHistoryBlocksOrder)))
			throw std::runtime_error("Invalid New Horizons saved command state");
		if(!state.orderStates.empty())
		{
			if(!canonicalOrderRules
				|| state.orderStates.back().command != state.activeOrder
				|| !state.heroCommandUsed || legacySpellHistoryBlocksOrder)
				throw std::runtime_error("Invalid New Horizons canonical Order context");
			for(const auto & order : state.orderStates)
			{
				if(order.issuedRound != round
					|| !heroCommands::supportedByRules(heroCommandRules, order.command))
					throw std::runtime_error("Invalid New Horizons canonical Order context");
				if(order.primaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
					&& !battleGetUnitByID(order.primaryTargetUnitId))
					throw std::runtime_error("Invalid New Horizons canonical Order target");
				if(order.secondaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
					&& !battleGetUnitByID(order.secondaryTargetUnitId))
					throw std::runtime_error("Invalid New Horizons canonical Order secondary target");
				if(order.command == HeroCommand::PROTECT
					&& order.protectInterceptionsConsumed > battleHeroOrderProtectInterceptionLimit(side))
					throw std::runtime_error("Shield Master Protect interception count exceeds the saved hero perk limit");
			}
		}
		else if(canonicalOrderRules && state.activeOrder != HeroCommand::NONE)
			throw std::runtime_error("Missing New Horizons canonical Order state");
		const bool focusFireOrderExists = state.findOrder(HeroCommand::FOCUS_FIRE) != nullptr;
		const bool focusFireOrderExpected = canonicalOrderRules
			? focusFireOrderExists : state.activeOrder == HeroCommand::FOCUS_FIRE;
		if(state.focusFire.has_value() != focusFireOrderExpected)
			throw std::runtime_error("Inconsistent New Horizons Focus Fire order state");
		if(!state.focusFire)
			continue;
		const auto & mark = *state.focusFire;
		mark.validateShape();
		if(!heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE)
			|| !getSideHero(side) || !state.heroCommandUsed || legacySpellHistoryBlocksOrder || mark.issuedRound != round
			|| !battleGetUnitByID(mark.targetUnitId))
			throw std::runtime_error("Invalid New Horizons Focus Fire battle context");
		for(auto id : mark.recipientUnitIds)
		{
			// Ghosts and changed controllers are valid retained, possibly inactive references.
			if(!battleGetUnitByID(id))
				throw std::runtime_error("Invalid New Horizons Focus Fire recipient reference");
		}
	}
}

void BattleInfo::validateRelentlessAssaultStates() const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side).relentlessAssault;
		state.validateShape();
		if(!state.hasState())
			continue;

		const auto * hero = battleGetFightingHero(side);
		if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
			throw std::runtime_error("Relentless Assault state requires its hero perk");

		if(state.targetUnitId != RelentlessAssaultState::INVALID_TARGET)
		{
			const auto * target = battleGetUnitByID(state.targetUnitId);
			if(state.targetUnitId >= nextUnitId() || (target && target->unitSide() == side))
				throw std::runtime_error("Invalid Relentless Assault streak target");
		}
	}
}

void BattleInfo::normalizeLegacyHeroCommandState()
{
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		sides.at(side).activeDoctrine = HeroCommand::NONE;
		const bool invalidLegacyOrder = sides.at(side).activeOrder != HeroCommand::NONE
			&& !heroCommands::isActive(sides.at(side).activeOrder);
		if(invalidLegacyOrder)
			sides.at(side).activeOrder = HeroCommand::NONE;
		const auto & orders = sides.at(side).orderStates;
		if(!orders.empty() && (!heroCommands::isCanonicalRules(heroCommandRules)
			|| orders.back().command != sides.at(side).activeOrder))
			sides.at(side).orderStates.clear();
		if(invalidLegacyOrder)
		{
			const auto legacyRoundOrder = Selector::sourceTypeSel(BonusSource::HERO_COMMAND)
				.And(CSelector(Bonus::NTurns));
			for(auto & unit : stacks)
				if(unit && unit->unitSide() == side)
					unit->removeBonusesRecursive(legacyRoundOrder);
		}
	}

	// Doctrine effects were the only HERO_COMMAND bonuses with ONE_BATTLE
	// duration. Remove those stale effects from decoded snapshots while leaving
	// all round-scoped Order bonuses untouched.
	const auto legacyDoctrine = Selector::sourceTypeSel(BonusSource::HERO_COMMAND)
		.And(CSelector(Bonus::OneBattle));
	for(auto & unit : stacks)
		if(unit)
			unit->removeBonusesRecursive(legacyDoctrine);
}

void BattleInfo::postDeserialize()
{
	for (const auto & unit : stacks)
		unit->postDeserialize(getSideArmy(unit->unitSide()));

	std::set<uint32_t> activeQuartermasterUnits;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side).reducedExtraActivation;
		state.validateShape();
		if(!state.hasActiveUnit())
			continue;
		const auto * unit = getStack(state.activeUnitId, false);
		if(!unit || !unit->alive() || !(unit->isBallista() || unit->isCatapult() || unit->isFirstAidTent())
			|| !activeQuartermasterUnits.insert(state.activeUnitId).second)
			throw std::runtime_error("Invalid restored reduced extra activation identity");
	}

	uint32_t pendingFollowUps = 0;
	for(const auto & unit : stacks)
	{
		if(!unit || unit->rangedFollowUpDamagePercent == 0)
			continue;
		if(unit->rangedFollowUpDamagePercent < 0 || unit->rangedFollowUpDamagePercent > 100
			|| !unit->isBallista()
			|| getActiveStackID() < 0
			|| unit->unitId() != static_cast<uint32_t>(getActiveStackID())
			|| ++pendingFollowUps > 1)
			throw std::runtime_error("Invalid restored ranged follow-up battle state");
	}
}

bool BattleInfo::hasBattleFormState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->hasBattleFormState();
	});
}

bool BattleInfo::hasVeteranDamageHistory() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->veteranPhysicalDamageSinceActivation != 0;
	});
}

bool BattleInfo::hasReserveMovementState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->getActivationMovementBonus() != 0;
	});
}

bool BattleInfo::hasRangedFollowUpState() const
{
	return std::any_of(stacks.begin(), stacks.end(), [](const auto & stack)
	{
		return stack && stack->rangedFollowUpDamagePercent != 0;
	});
}

std::map<uint32_t, int32_t> BattleInfo::collectReserveMovementBonuses() const
{
	std::map<uint32_t, int32_t> bonuses;
	for(const auto & stack : stacks)
	{
		if(!stack)
			continue;
		const int32_t bonus = stack->getActivationMovementBonus();
		if(bonus < 0)
			throw std::runtime_error("Invalid negative activation movement bonus");
		if(bonus != 0 && !bonuses.emplace(stack->unitId(), bonus).second)
			throw std::runtime_error("Duplicate stack identity in activation movement state");
	}
	return bonuses;
}

void BattleInfo::restoreReserveMovementBonuses(const std::map<uint32_t, int32_t> & bonuses)
{
	for(auto & stack : stacks)
		if(stack)
			stack->setActivationMovementBonus(0);

	for(const auto & [unitId, bonus] : bonuses)
	{
		if(bonus < 0)
			throw std::runtime_error("Invalid negative activation movement bonus in battle snapshot");
		const auto found = std::find_if(stacks.begin(), stacks.end(), [unitId](const auto & stack)
		{
			return stack && stack->unitId() == unitId;
		});
		if(found == stacks.end())
			throw std::runtime_error("Activation movement bonus references a missing stack");
		(*found)->setActivationMovementBonus(bonus);
	}
}

std::map<uint32_t, int32_t> BattleInfo::collectRangedFollowUps() const
{
	std::map<uint32_t, int32_t> followUps;
	for(const auto & stack : stacks)
	{
		if(!stack)
			continue;
		const int32_t percent = stack->rangedFollowUpDamagePercent;
		if(percent < 0 || percent > 100)
			throw std::runtime_error("Invalid ranged follow-up percentage in battle snapshot");
		if(percent == 0)
			continue;
		if(!stack->isBallista() || getActiveStackID() < 0
			|| stack->unitId() != static_cast<uint32_t>(getActiveStackID())
			|| !followUps.emplace(stack->unitId(), percent).second || followUps.size() > 1)
			throw std::runtime_error("Invalid pending ranged follow-up in battle snapshot");
	}
	return followUps;
}

void BattleInfo::restoreRangedFollowUps(const std::map<uint32_t, int32_t> & followUps)
{
	if(followUps.size() > 1)
		throw std::runtime_error("Multiple pending ranged follow-ups in battle snapshot");

	for(const auto & [unitId, percent] : followUps)
	{
		if(percent <= 0 || percent > 100 || getActiveStackID() < 0
			|| unitId != static_cast<uint32_t>(getActiveStackID()))
			throw std::runtime_error("Invalid ranged follow-up entry in battle snapshot");
		const auto found = std::find_if(stacks.begin(), stacks.end(), [unitId](const auto & stack)
		{
			return stack && stack->unitId() == unitId;
		});
		if(found == stacks.end())
			throw std::runtime_error("Ranged follow-up references a missing stack");
	}

	for(auto & stack : stacks)
		if(stack)
			stack->setRangedFollowUpDamagePercent(0);

	for(const auto & [unitId, percent] : followUps)
	{
		const auto found = std::find_if(stacks.begin(), stacks.end(), [unitId](const auto & stack)
		{
			return stack && stack->unitId() == unitId;
		});
		(*found)->setRangedFollowUpDamagePercent(percent);
	}
}

bool CMP_stack::operator()(const battle::Unit * a, const battle::Unit * b) const
{
	switch(phase)
	{
	case 0: //catapult moves after turrets
		return a->isTurret() && !b->isTurret(); //turrets move before catapult
	case 1:
	case 2:
	case 3:
		{
			int as = a->getInitiative(turn);
			int bs = b->getInitiative(turn);

			if(as != bs)
				return as > bs;

			if(a->unitSide() == b->unitSide())
				return a->unitSlot() < b->unitSlot();

			return (a->unitSide() == side || b->unitSide() == side)
				? a->unitSide() != side
				: a->unitSide() < b->unitSide();
			}
	default:
		assert(false);
		return false;
	}

	assert(false);
	return false;
}

CMP_stack::CMP_stack(int Phase, int Turn, BattleSide Side):
	phase(Phase), 
	turn(Turn), 
	side(Side) 
{
}
