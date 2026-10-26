/*
 * HypotheticBattle.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticEffectSnapshotRules.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/battle/SiegeInfo.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsCrossSchoolFormula.h"
#include "../../lib/int3.h"

using hypotheticEffectSnapshotRules::hasSnapshotHeroSpecialEffects;
using hypotheticEffectSnapshotRules::hasTimedMagicalSpellEffects;

HypotheticBattle::HypotheticBattle(const Environment * ENV, Subject realBattle)
	: BattleProxy(InitialExportBattleSubject::forkForBranch(realBattle)),
	boundSubjectPointer(subject.get()),
	boundSubjectOwner(subject),
	initialExportView(dynamic_cast<InitialExportBattleSubject *>(subject.get())),
	env(ENV),
	bonusTreeVersion(1)
{
	captureNecromancyPassives(*realBattle);
	crisisCommand = realBattle->getBattle()->getCrisisCommandState();
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(realBattle.get()))
	{
		elementalRebirthSpawnUnitIds = parent->elementalRebirthSpawnUnitIds;
		projectedPrimalBurstHits = parent->projectedPrimalBurstHits;
		projectedPlagueHits = parent->projectedPlagueHits;
		bloodrageFirstBloodUsed = parent->bloodrageFirstBloodUsed;
	}
	else if(const auto * concrete = dynamic_cast<const BattleInfo *>(realBattle->getBattle()))
		bloodrageFirstBloodUsed = concrete->bloodrageFirstBloodUsed;
	auto activeUnit = realBattle->battleActiveUnit();
	activeUnitId = activeUnit ? activeUnit->unitId() : -1;
	projectedRound = realBattle->battleGetRound();
	seizeInitiative = realBattle->getBattle()->getSeizeInitiativeState();
	deploymentState = realBattle->getBattle()->getDeploymentState();
	fortuneRollRules = realBattle->getBattle()->getLuckRollRules();
	if(const auto * concreteBattle = dynamic_cast<const BattleInfo *>(realBattle->getBattle()))
		pendingTimeStopHeroActionSides = concreteBattle->getPendingTimeStopHeroActionSides();
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		projectedWalls[part] = BattleProxy::getWallState(part);
		projectedStructuralHP[part] = BattleProxy::getWallStructuralHP(part);
		canonicalStructuralHP |= projectedStructuralHP[part] > 0;
	}
	initialGateState = BattleProxy::getGateState();
	// Use the subject's visible view, not its unfiltered authoritative obstacle list.
	for(const auto & obstacle : BattleProxy::getAllObstacles())
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
			projectedObstacles.push_back(std::make_shared<SpellCreatedObstacle>(*spell));
		else
			projectedObstacles.push_back(obstacle);
	}

	nextId = 0x00F00000;
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		rapidResponseStates[side] = realBattle->getBattle()->getRapidResponseState(side);
		reducedExtraActivationStates[side] = realBattle->getBattle()->getReducedExtraActivationState(side);
		crossSchoolFormulaStates[side] = realBattle->getBattle()->getCrossSchoolFormulaState(side);
		spellResponseStates[side] = realBattle->getBattle()->getSpellResponseState(side);
		extendSpellRounds[side] = realBattle->getBattle()->getExtendSpellLastRound(side);
		overwhelmingFormulaStates[side] = realBattle->getBattle()->getOverwhelmingFormulaState(side);
		heroOrderStates[side] = realBattle->getBattle()->getHeroOrderStates(side);
		relentlessAssaultStates[side] = realBattle->getBattle()->getRelentlessAssaultState(side);
		battlecraftMasteryAwardRounds[side] = realBattle->getBattle()->getBattlecraftMasteryAwardRound(side);
		armorerLastStandUsedStates[side] = realBattle->getBattle()->armorerLastStandUsed(side);
		rebirthChainUsedStates[side] = realBattle->getBattle()->getRebirthChainUsed(side);
		phoenixSparkUsedStates[side] = realBattle->getBattle()->getPhoenixSparkUsed(side);
		armorerDefiantStates[side] = realBattle->getBattle()->getArmorerDefiantState(side);
		warcastingStates[side] = realBattle->getBattle()->getWarcastingState(side);
		heroActionAllowances[side] = realBattle->getBattle()->getHeroActionAllowances(side);
		doubleCommandStates[side] = realBattle->getBattle()->getDoubleCommandState(side);
		preCombatOrderStates[side] = realBattle->getBattle()->getPreCombatOrderState(side);
		heroSpellCastCompletedStates[side] = realBattle->getBattle()->hasCompletedHeroSpellCast(side);
		completedHeroSpellLevelMasks[side] = 0;
		for(int32_t level = 1; level <= GameConstants::SPELL_LEVELS; ++level)
			if(realBattle->getBattle()->hasCompletedHeroSpellLevel(side, level))
				completedHeroSpellLevelMasks[side] |= static_cast<std::uint8_t>(1u << (level - 1));
		counterspellArmedStates[side] = realBattle->getBattle()->getCounterspellArmed(side);
		countersequenceArmedStates[side] = realBattle->getBattle()->getMetamagicCountersequenceArmed(side);
		auto & meta = metamagicStates[side];
		meta.uses = realBattle->getBattle()->getMetamagicUsesConsumed(side);
		meta.pending = realBattle->getBattle()->getMetamagicPendingCount(side);
		meta.grandUsed = realBattle->getBattle()->getMetamagicGrandUsed(side);
		meta.firstSpell = realBattle->getBattle()->getMetamagicFirstSpell(side);
		meta.firstTarget = realBattle->getBattle()->getMetamagicFirstTargetUnitId(side);
		meta.firstCountered = realBattle->getBattle()->getMetamagicFirstCounterspellNegated(side);
		meta.sequence = realBattle->getBattle()->getMetamagicSequenceSpells(side);
		focusFireStates[side] = realBattle->battleGetFocusFireState(side);
		fortuneStates[side] = realBattle->getBattle()->getSylvanLuckState(side);
		perfectFortuneStates[side] = realBattle->getBattle()->getPerfectFortuneState(side);
		luckSerendipityStates[side] = realBattle->getBattle()->getLuckSerendipityState(side);
		adverseRerollStates[side] = realBattle->getBattle()->getAdverseCombatRerollState(side);
		moraleSuppressionStates[side] = realBattle->getBattle()->getMoraleSuppressionState(side);
		bloodrageRanks[side] = realBattle->getBattle()->getBloodrageRank(side);
		bloodrageDamagePercents[side] = realBattle->getBattle()->getBloodrageDamagePercent(side);
		bloodrageCaps[side] = realBattle->getBattle()->getBloodrageCapPercent(side);
		bloodrageSpeedBonuses[side] = realBattle->getBattle()->getBloodrageSpeedBonus(side);
		bloodrageAdditionalRetaliations[side] = realBattle->getBattle()->getBloodrageAdditionalRetaliations(side);
		bloodrageLowHealthIncrements[side] = realBattle->getBattle()->getBloodrageLowHealthIncrement(side);
		bloodragePainIncrements[side] = realBattle->getBattle()->getBloodragePainIncrement(side);
	}

	localEnvironment.reset(new HypotheticEnvironment(this, env));
	serverCallback.reset(new HypotheticServerCallback(this));
	// The remaining units retain the existing lazy projection policy. Frozen's
	// next-slot receipt must be a branch snapshot: reading it for the first time
	// after a parent Shatter/thaw cannot import that later parent's mutation.
	// Likewise, capture current Morale origin before the parent can advance.
	for(const auto * unit : realBattle->battleGetAllUnits(false))
		if(unit->unitId() == static_cast<uint32_t>(activeUnitId)
			|| hasTimedMagicalSpellEffects(*unit)
			|| hasSnapshotHeroSpecialEffects(*unit)
			|| newHorizonsNecromancy::needsCombatPassiveSnapshot(*unit)
			|| newHorizonsFrozen::isFrozen(*unit)
			|| (dynamic_cast<const battle::CUnitState *>(unit)
				&& dynamic_cast<const battle::CUnitState *>(unit)->moraleExtraActivation))
			getForUpdate(unit->unitId());
}

bool HypotheticBattle::hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| level < 1 || level > GameConstants::SPELL_LEVELS)
		return false;

	const auto bit = static_cast<std::uint8_t>(1u << (level - 1));
	return (completedHeroSpellLevelMasks.at(side) & bit) != 0;
}

bool HypotheticBattle::unitHasAmmoCart(const battle::Unit * unit) const
{
	return battleUnitHasAmmoCart(unit);
}

int HypotheticBattle::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	return battleBloodragePainIncrement(unit);
}

bool HypotheticBattle::unitHasVeteranCohesion(const battle::Unit * unit) const
{
	if(!unit)
		return false;
	const auto owner = battleGetOwner(unit);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(getSidePlayer(side) == owner)
			return newHorizonsDiscipline::hasVeteranCohesion(getSideHero(side));
	return false;
}

std::optional<int> HypotheticBattle::unitMagicResistance(const battle::Unit * unit) const
{
	if(!unit || !newHorizonsMagic::rulesActive(getMagicRules()))
		return std::nullopt;
	return battleGetMagicResistance(unit);
}

std::optional<std::pair<int32_t, int32_t>> HypotheticBattle::unitMoraleLimits(const battle::Unit * unit) const
{
	if(!unit)
		return std::nullopt;
	return newHorizonsMagic::moraleLimits(getMagicRules());
}

PlayerColor HypotheticBattle::unitEffectiveOwner(const battle::Unit * unit) const
{
	return battleGetOwner(unit);
}

std::shared_ptr<StackWithBonuses> HypotheticBattle::getForUpdate(uint32_t id)
{
	invalidateLifeDrainFriendlyObservation(id);
	return getForUpdateImpl(id);
}

BattleID HypotheticBattle::getBattleID() const
{
	return subject->getBattle()->getBattleID();
}

ui8 HypotheticBattle::getTacticDist() const
{
	if(deploymentState.independent)
	{
		return deploymentState.activeDistance();
	}
	return BattleProxy::getTacticDist();
}

BattleSide HypotheticBattle::getTacticsSide() const
{
	if(deploymentState.independent)
		return deploymentState.activeSide();
	return BattleProxy::getTacticsSide();
}

void HypotheticBattle::setDeploymentState(const BattleDeploymentState & state)
{
	state.validateTransitionFrom(deploymentState);
	deploymentState = state;
}

const ReducedExtraActivationState & HypotheticBattle::getReducedExtraActivationState(BattleSide side) const
{
	return reducedExtraActivationStates.at(side);
}

void HypotheticBattle::consumeExtendSpell(BattleSide side)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| getRound() < 0 || getExtendSpellLastRound(side) >= getRound())
		throw std::runtime_error("Invalid or repeated projected Extend Spell consumption");
	extendSpellRounds.at(side) = getRound();
}

const SpellResponseState & HypotheticBattle::getSpellResponseState(BattleSide side) const
{
	static const SpellResponseState empty;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return empty;
	return spellResponseStates.at(side);
}

void HypotheticBattle::setCrossSchoolFormulaState(BattleSide side, const newHorizonsCrossSchoolFormula::State & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid detached Cross-School Formula side");
	newHorizonsCrossSchoolFormula::validateState(*this, state);
	crossSchoolFormulaStates.at(side) = state;
}

void HypotheticBattle::armReactiveWeave(BattleSide side, int32_t receiptRound, int32_t empowerment)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| receiptRound != getRound() || !newHorizonsWarcasting::enabled(getMagicRules())
		|| empowerment <= 0 || empowerment != newHorizonsWarcasting::reactiveEmpowerment(getSideHero(side)))
		throw std::runtime_error("Reactive Weave receipt does not match the captured hero capability");
	auto next = warcastingStates.at(side);
	next.armReactive(receiptRound, empowerment);
	warcastingStates.at(side) = std::move(next);
}

void HypotheticBattle::setSpellResponseState(BattleSide side, const SpellResponseState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Spell Response side");
	state.validate();
	if(state.hasState() && !state.isReadyAt(projectedRound))
		throw std::runtime_error("Hypothetical Spell Response state is outside its round window");
	if(state.hasState())
	{
		// Validate an internal native receipt against its actual side owner.
		// Public hero queries remain player-filtered.
		const auto * hero = getSideHero(side);
		if(!hero || !hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)))
			throw std::runtime_error("Hypothetical Spell Response state requires Counterpressure");
	}
	spellResponseStates.at(side) = state;
}

const OverwhelmingFormulaState & HypotheticBattle::getOverwhelmingFormulaState(BattleSide side) const
{
	static const OverwhelmingFormulaState empty;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return empty;
	return overwhelmingFormulaStates.at(side);
}

void HypotheticBattle::setOverwhelmingFormulaState(BattleSide side, const OverwhelmingFormulaState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Overwhelming Formula side");
	state.validateShape();
	overwhelmingFormulaStates.at(side) = state;
}

const RapidResponseState & HypotheticBattle::getRapidResponseState(BattleSide side) const
{
	static const RapidResponseState empty;
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER ? rapidResponseStates.at(side) : empty;
}

void HypotheticBattle::setRapidResponseState(BattleSide side, const RapidResponseState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid detached Rapid Response side");
	state.validateShape();
	rapidResponseStates.at(side) = state;
}

void HypotheticBattle::completeRapidResponseActivation(uint32_t unitId, bool syntheticStoppedSlot)
{
	const auto * unit = battleGetUnitByID(unitId);
	if(!unit || syntheticStoppedSlot)
		return;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		const auto responding = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		setRapidResponseState(responding, newHorizonsRapidResponse::capture(*this, responding));
	}
}

void HypotheticBattle::setReducedExtraActivationState(BattleSide side,
	const ReducedExtraActivationState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical reduced extra activation side");
	state.validateShape();
	reducedExtraActivationStates.at(side) = state;
}

void HypotheticBattle::setPreCombatOrderState(BattleSide side, const PreCombatOrderState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Battle Plan side");
	state.validateShape();
	preCombatOrderStates.at(side) = state;
}

std::vector<HeroOrderState> HypotheticBattle::getHeroOrderStates(BattleSide side) const
{
	return heroOrderStates.at(side);
}

std::optional<HeroOrderState> HypotheticBattle::getHeroOrderState(BattleSide side, HeroCommand command) const
{
	const auto & states = heroOrderStates.at(side);
	const auto found = std::find_if(states.begin(), states.end(), [command](const HeroOrderState & state)
	{
		return state.command == command;
	});
	return found == states.end() ? std::optional<HeroOrderState>() : *found;
}

std::optional<HeroOrderState> HypotheticBattle::getHeroOrderState(BattleSide side) const
{
	const auto & states = heroOrderStates.at(side);
	return states.empty() ? std::optional<HeroOrderState>() : states.back();
}

std::vector<HeroOrderState> HypotheticBattle::battleGetHeroOrderStates(BattleSide side) const
{
	return getHeroOrderStates(side);
}

std::optional<HeroOrderState> HypotheticBattle::battleGetHeroOrderState(BattleSide side,
	HeroCommand command) const
{
	return getHeroOrderState(side, command);
}

std::optional<HeroOrderState> HypotheticBattle::battleGetHeroOrderState(BattleSide side) const
{
	return getHeroOrderState(side);
}

HeroCommand HypotheticBattle::getActiveOrder(BattleSide side) const
{
	if(!heroCommands::isCanonicalRules(getHeroCommandRules()))
		return BattleProxy::getActiveOrder(side);
	const auto & states = heroOrderStates.at(side);
	return states.empty() ? HeroCommand::NONE : states.back().command;
}

const RelentlessAssaultState & HypotheticBattle::battleGetRelentlessAssaultState(BattleSide side) const
{
	return relentlessAssaultStates.at(side);
}

const RelentlessAssaultState & HypotheticBattle::getRelentlessAssaultState(BattleSide side) const
{
	return relentlessAssaultStates.at(side);
}

void HypotheticBattle::setRelentlessAssaultState(BattleSide side, const RelentlessAssaultState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Relentless Assault side");
	state.validateShape();
	const auto * hero = battleGetFightingHero(side);
	if((!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		&& state.hasState())
		throw std::runtime_error("Hypothetical Relentless Assault state requires its hero perk");
	relentlessAssaultStates.at(side) = state;
}

void HypotheticBattle::recordRelentlessAssaultAttack(BattleSide side, uint32_t targetUnitId)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	const auto * hero = battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		return;
	relentlessAssaultStates.at(side).recordAttack(targetUnitId);
}

const AlternatingHeroActionState & HypotheticBattle::getWarcastingState(BattleSide side) const
{
	return warcastingStates.at(side);
}

const HeroActionAllowanceState & HypotheticBattle::getHeroActionAllowances(BattleSide side) const
{
	return heroActionAllowances.at(side);
}

void HypotheticBattle::setHeroOrderStates(BattleSide side, const std::vector<HeroOrderState> & states)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Hero Order side");
	SideInBattle::validateOrderCollection(states);
	if(heroOrderStates.at(side) == states)
		return;
	heroOrderStates.at(side) = states;
	++bonusTreeVersion;
}

void HypotheticBattle::setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Hero Order side");
	if(!state)
	{
		setHeroOrderStates(side, {});
		return;
	}
	state->validateShape();
	auto states = heroOrderStates.at(side);
	const auto found = std::find_if(states.begin(), states.end(), [&](const HeroOrderState & current)
	{
		return current.command == state->command;
	});
	if(found == states.end())
		states.push_back(*state);
	else if(*found == *state)
		return;
	else
		*found = *state;
	setHeroOrderStates(side, states);
}

bool HypotheticBattle::consumeHeroOrderProtectInterception(uint32_t wardUnitId, uint32_t protectorUnitId)
{
	const auto * ward = battleGetUnitByID(wardUnitId);
	if(!ward)
		return false;
	const auto side = ward->unitSide();
	auto state = battleGetHeroOrderState(side, HeroCommand::PROTECT);
	if(!state || !state->scheduledFor(wardUnitId, battleGetRound())
		|| !state->scheduledFor(protectorUnitId, battleGetRound())
		|| state->secondaryTargetUnitId != wardUnitId || state->primaryTargetUnitId != protectorUnitId
		|| state->protectBroken
		|| state->protectInterceptionsConsumed >= battleHeroOrderProtectInterceptionLimit(side))
		return false;
	++state->protectInterceptionsConsumed;
	setHeroOrderState(side, state);
	return true;
}

std::optional<FocusFireState> HypotheticBattle::getFocusFireState(BattleSide side) const
{
	const auto found = focusFireStates.find(side);
	return found == focusFireStates.end() ? std::optional<FocusFireState>() : found->second;
}

void HypotheticBattle::setFocusFireState(BattleSide side, const FocusFireState & state)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !heroCommands::supportedByRules(getHeroCommandRules(), HeroCommand::FOCUS_FIRE))
		throw std::invalid_argument("Invalid hypothetical Focus Fire context");
	state.validateShape();
	if(state.issuedRound != battleGetRound())
		throw std::invalid_argument("Invalid hypothetical Focus Fire round");
	focusFireStates[side] = state;
	++bonusTreeVersion;
}

int32_t HypotheticBattle::getActiveStackID() const
{
	return activeUnitId;
}

float HypotheticBattle::projectMoraleActivationDelta(const battle::Unit * original,
	const battle::Unit * projected, float before, float after, float horizon)
{
	const float delta = after - before;
	if(!original || !projected || horizon <= 0.0f || delta == 0.0f)
		return delta * std::max(0.0f, horizon);
	const auto side = playerToSide(battleGetOwner(projected));
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| playerToSide(battleGetOwner(original)) != side)
		return delta * horizon;
	auto & suppression = moraleSuppressionStates.at(side);
	if(!suppression.consume(before < 0.0f || after < 0.0f))
		return delta * horizon;

	// Approximate one prospective activation in this candidate branch. The rest
	// of the horizon remains exposed; actual first-trigger ordering is unknown.
	const float protectedDelta = std::max(0.0f, after) - std::max(0.0f, before);
	return delta * horizon + (protectedDelta - delta) * std::min(1.0f, horizon);
}

int32_t HypotheticBattle::getRound() const
{
	return projectedRound;
}

int32_t HypotheticBattle::getBattlecraftMasteryAwardRound(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return -1;
	return battlecraftMasteryAwardRounds.at(side);
}

void HypotheticBattle::awardBattlecraftMastery(BattleSide side, uint32_t unitId, int32_t round,
	BattlecraftMasteryAction action)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || round != getRound())
		throw std::runtime_error("Invalid hypothetical Battlefield Mastery award context");

	auto unit = getForUpdate(unitId);
	if(!unit || playerToSide(battleGetOwner(unit.get())) != side)
		throw std::runtime_error("Hypothetical Battlefield Mastery award targets the wrong side");

	const auto previousAwardRound = battlecraftMasteryAwardRounds.at(side);
	if(!newHorizonsBattlecraft::canAwardBattlefieldMastery(
		battleGetOwnerHero(unit.get()), unit.get(), round, previousAwardRound, action))
		throw std::runtime_error("Invalid hypothetical Battlefield Mastery award");
	if((action == BattlecraftMasteryAction::WAIT && unit->battlecraftWaitMasteryDoubled)
		|| (action == BattlecraftMasteryAction::DEFEND && unit->battlecraftDefendMasteryDoubled))
		throw std::runtime_error("Hypothetical Battlefield Mastery action marker is already set");

	switch(action)
	{
	case BattlecraftMasteryAction::WAIT:
		unit->battlecraftWaitMasteryDoubled = true;
		break;
	case BattlecraftMasteryAction::DEFEND:
		unit->battlecraftDefendMasteryDoubled = true;
		break;
	default:
		throw std::runtime_error("Unknown hypothetical Battlefield Mastery action");
	}

	battlecraftMasteryAwardRounds.at(side) = round;
}

bool HypotheticBattle::armorerLastStandUsed(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return armorerLastStandUsedStates.at(side);
}

const CGHeroInstance * HypotheticBattle::getSideHero(BattleSide side) const
{
	// Internal IBattleInfo rule resolution needs both actual side heroes. Public
	// battleGetFightingHero still applies the callback's player visibility gate.
	return subject->getBattle()->getSideHero(side);
}

ArmorerDefiantState HypotheticBattle::getArmorerDefiantState(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid hypothetical Defiant side");
	return armorerDefiantStates.at(side);
}

void HypotheticBattle::setArmorerDefiantState(BattleSide side, const ArmorerDefiantState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid hypothetical Defiant side");
	state.validate();
	const auto previous = armorerDefiantStates.at(side);
	if(state == previous)
		return;
	if(state.lastConsumedRound != battleGetRound() || !previous.availableAt(battleGetRound()))
		throw std::runtime_error("Invalid hypothetical Defiant consumption transition");
	armorerDefiantStates.at(side) = state;
}

void HypotheticBattle::consumeArmorerLastStand(BattleSide side)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid hypothetical Armorer Last Stand side");
	if(armorerLastStandUsedStates.at(side))
		throw std::runtime_error("Hypothetical Armorer Last Stand was already consumed");
	if(!newHorizonsArmorer::hasLastStand(getSideHero(side)))
		throw std::runtime_error("Hypothetical Armorer Last Stand requires the active hero perk");
	armorerLastStandUsedStates.at(side) = true;
}

void HypotheticBattle::applyArmorerLastStandDefend(uint32_t unitId)
{
	auto unit = getForUpdate(unitId);
	if(!unit || !unit->alive())
		throw std::runtime_error("Hypothetical Armorer Last Stand cannot defend a dead unit");

	const auto * hero = battleGetOwnerHero(unit.get());
	const bool holdFastApplies = heroCommands::isCanonicalRules(getHeroCommandRules())
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit.get())
		&& newHorizonsDiscipline::hasHoldFast(hero);
	const auto stance = newHorizonsArmorer::buildDefendStance(unit.get(), holdFastApplies);
	unit->defending = true;
	unit->armorerLastStandDefending = true;
	unit->defensiveStanceMeleeBonus = stance.meleeDefenseBonus;
	unit->defensiveStanceRangedBonus = stance.rangedDefenseBonus;
	updateUnitBonus(unitId, stance.bonuses);
	if(stance.holdFastBonus)
		addUnitBonus(unitId, {*stance.holdFastBonus});
}

bool HypotheticBattle::getRebirthChainUsed(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return rebirthChainUsedStates.at(side);
}

void HypotheticBattle::setRebirthChainUsed(BattleSide side, bool used)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Rebirth Chain side");
	rebirthChainUsedStates.at(side) = used;
}

bool HypotheticBattle::getPhoenixSparkUsed(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Phoenix Spark side");
	return phoenixSparkUsedStates.at(side);
}

void HypotheticBattle::setPhoenixSparkUsed(BattleSide side, bool used)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Phoenix Spark side");
	if(!used && phoenixSparkUsedStates.at(side))
		throw std::invalid_argument("Cannot restore a spent hypothetical Phoenix Spark use");
	phoenixSparkUsedStates.at(side) = used;
}

int32_t HypotheticBattle::getBloodrageDamagePercent(BattleSide side) const
{
	return bloodrageDamagePercents.at(side);
}

int32_t HypotheticBattle::getBloodrageCapPercent(BattleSide side) const
{
	return bloodrageCaps.at(side);
}

int32_t HypotheticBattle::getBloodrageSpeedBonus(BattleSide side) const
{
	return bloodrageSpeedBonuses.at(side);
}

int32_t HypotheticBattle::getBloodrageAdditionalRetaliations(BattleSide side) const
{
	return bloodrageAdditionalRetaliations.at(side);
}

int32_t HypotheticBattle::getBloodrageLowHealthIncrement(BattleSide side) const
{
	return bloodrageLowHealthIncrements.at(side);
}

int32_t HypotheticBattle::getBloodragePainIncrement(BattleSide side) const
{
	return bloodragePainIncrements.at(side);
}

IBattleInfo::ObstacleCList HypotheticBattle::getAllObstacles() const
{
	return projectedObstacles;
}

EWallState HypotheticBattle::getWallState(EWallPart part) const
{
	return projectedWalls.at(part);
}

int32_t HypotheticBattle::getWallStructuralHP(EWallPart part) const
{
	if(!canonicalStructuralHP)
		return 0;
	if(const auto it = projectedStructuralHP.find(part); it != projectedStructuralHP.end())
		return it->second;
	return 0;
}

EGateState HypotheticBattle::getGateState() const
{
	// The authoritative BattleProcessor derives this after catapult/earthquake
	// damage. A destroyed gate must not remain closed in the projected pathfinder.
	return getWallState(EWallPart::GATE) == EWallState::DESTROYED
		? EGateState::DESTROYED : initialGateState;
}

void HypotheticBattle::setWallState(EWallPart partOfWall, EWallState state)
{
	wallChanges |= projectedWalls[partOfWall] != state;
	projectedWalls[partOfWall] = state;
	if(canonicalStructuralHP && state == EWallState::DESTROYED)
		projectedStructuralHP[partOfWall] = 0;
}

void HypotheticBattle::setWallStructuralHP(EWallPart partOfWall, int32_t hp)
{
	if(!canonicalStructuralHP)
		return;

	const auto maximum = SiegeInfo::maximumStructuralHP(partOfWall);
	if(maximum <= 0)
		return;

	const auto bounded = std::clamp(hp, 0, maximum);
	wallChanges |= getWallStructuralHP(partOfWall) != bounded;
	projectedStructuralHP[partOfWall] = bounded;
	setWallState(partOfWall, SiegeInfo::stateFromStructuralHP(partOfWall, bounded));
}

void HypotheticBattle::addObstacle(const ObstacleChanges & changes)
{
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->fromInfo(changes);
	projectedObstacles.push_back(obstacle);
	obstacleChanges = true;
}

void HypotheticBattle::updateObstacle(const ObstacleChanges& changes)
{
	auto changed = std::make_shared<SpellCreatedObstacle>();
	changed->fromInfo(changes);
	for(auto & obstacle : projectedObstacles)
	{
		if(obstacle->uniqueID != changes.id)
			continue;
		const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		assert(spell);
		if(!spell)
			return;
		auto replacement = std::make_shared<SpellCreatedObstacle>(*spell);
		// Match BattleInfo: UPDATE currently changes revealed, not geometry/TTL.
		replacement->revealed = changed->revealed;
		obstacleChanges |= replacement->revealed != spell->revealed;
		obstacle = replacement;
		break;
	}
}

void HypotheticBattle::removeObstacle(uint32_t id)
{
	const auto found = std::find_if(projectedObstacles.begin(), projectedObstacles.end(),
		[id](const auto & obstacle) { return obstacle->uniqueID == id; });
	if(found != projectedObstacles.end())
	{
		projectedObstacles.erase(found);
		obstacleChanges = true;
	}
}

int64_t HypotheticBattle::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	return (damage.min + damage.max) / 2;
}

std::vector<SpellID> HypotheticBattle::getUsedSpells(BattleSide side) const
{
	// TODO
	return {};
}

int3 HypotheticBattle::getLocation() const
{
	// Projection changes battle units, not the captured adventure location.
	return subject->getBattle()->getLocation();
}

BattleLayout HypotheticBattle::getLayout() const
{
	return subject->getBattle()->getLayout();
}

int32_t HypotheticBattle::getTreeVersion() const
{
	return getBonusBearer()->getTreeVersion() + bonusTreeVersion;
}

ServerCallback * HypotheticBattle::getServerCallback()
{
	return serverCallback.get();
}

const scripting::Pool & HypotheticBattle::getScriptContextPool() const
{
	return subject->getBattle()->getScriptContextPool();
}
