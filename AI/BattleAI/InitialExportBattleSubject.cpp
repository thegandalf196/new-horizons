/*
 * InitialExportBattleSubject.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/UnitAdditionRules.h"
#include "../../lib/CStack.h"
#include "../../lib/int3.h"
#include <stdexcept>

namespace
{
[[noreturn]] void rejectSourceMutation()
{
	throw std::logic_error("Initial export source is read-only; mutate the owning HypotheticBattle branch");
}
}

BattleID InitialExportBattleSubject::getBattleID() const
{
	return subject->getBattle()->getBattleID();
}

const scripting::Pool & InitialExportBattleSubject::getScriptContextPool() const
{
	return subject->getBattle()->getScriptContextPool();
}

std::vector<SpellID> InitialExportBattleSubject::getUsedSpells(BattleSide side) const
{
	return subject->getBattle()->getUsedSpells(side);
}

uint32_t InitialExportBattleSubject::nextUnitId() const
{
	// This source owns only the original roster. Never consume a parent's
	// mutable hypothetical allocator merely to answer a source query.
	return battle::nextUnitAdditionId(owned->size(), getHeroCommandRules());
}

int64_t InitialExportBattleSubject::getActualDamage(const DamageRange &, int32_t, vstd::RNG &) const
{
	throw std::logic_error("Initial export source cannot realize damage; use the owning HypotheticBattle branch");
}

int3 InitialExportBattleSubject::getLocation() const
{
	return subject->getBattle()->getLocation();
}

BattleLayout InitialExportBattleSubject::getLayout() const
{
	return subject->getBattle()->getLayout();
}

void InitialExportBattleSubject::nextRound()
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::nextTurn(uint32_t, BattleUnitTurnReason)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setSeizeInitiativeState(const SeizeInitiativeState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setPreCombatOrderState(BattleSide, const PreCombatOrderState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setDeploymentState(const BattleDeploymentState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::addUnit(uint32_t, const JsonNode &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::updateUnit(uint32_t, const JsonNode &, int64_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::moveUnit(uint32_t, const BattleHex &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::removeUnit(uint32_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::addUnitBonus(uint32_t, const std::vector<Bonus> &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::updateUnitBonus(uint32_t, const std::vector<Bonus> &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::removeUnitBonus(uint32_t, const std::vector<Bonus> &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::exchangeBattleEffects(const battle::BattleEffectExchange &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setWallState(EWallPart, EWallState)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setWallStructuralHP(EWallPart, int32_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::addObstacle(const ObstacleChanges &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::updateObstacle(const ObstacleChanges &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::removeObstacle(uint32_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setHeroOrderStates(BattleSide, const std::vector<HeroOrderState> &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setHeroOrderState(BattleSide, const std::optional<HeroOrderState> &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setDoubleCommandState(BattleSide, const DoubleCommandState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setRelentlessAssaultState(BattleSide, const RelentlessAssaultState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::recordRelentlessAssaultAttack(BattleSide, uint32_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setAdverseCombatRerollState(BattleSide, const AdverseCombatRerollState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setMoraleSuppressionState(BattleSide, const MoraleSuppressionState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setRapidResponseState(BattleSide, const RapidResponseState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setReducedExtraActivationState(BattleSide, const ReducedExtraActivationState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setCrisisCommandState(const newHorizonsCrisisCommand::State &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setCrossSchoolFormulaState(BattleSide, const newHorizonsCrossSchoolFormula::State &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::armReactiveWeave(BattleSide, int32_t, int32_t)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setSpellResponseState(BattleSide, const SpellResponseState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::consumeExtendSpell(BattleSide)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setOverwhelmingFormulaState(BattleSide, const OverwhelmingFormulaState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setPerfectFortuneState(BattleSide, const PerfectFortuneState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setRebirthChainUsed(BattleSide, bool)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setPhoenixSparkUsed(BattleSide, bool)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setLuckSerendipityState(BattleSide, const LuckSerendipityState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::setArmorerDefiantState(BattleSide, const ArmorerDefiantState &)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::awardBattlecraftMastery(BattleSide, uint32_t, int32_t, BattlecraftMasteryAction)
{
	rejectSourceMutation();
}

void InitialExportBattleSubject::consumeArmorerLastStand(BattleSide)
{
	rejectSourceMutation();
}

InitialExportBattleSubject::InitialExportBattleSubject(Subject subject, std::unique_ptr<InitialExportWorld> world)
	: BattleProxy(std::move(subject)), owned(std::move(world)) {}

std::shared_ptr<InitialExportBattleSubject> InitialExportBattleSubject::capture(Subject subject, BattleInfo & native)
{
	if(!subject || subject->getBattle() != &native || subject->battleGetMySide() != BattleSide::ALL_KNOWING)
		throw std::invalid_argument("Initial export sequence requires its exact authorized full-information source");
	return std::shared_ptr<InitialExportBattleSubject>(new InitialExportBattleSubject(subject, std::make_unique<InitialExportWorld>(native)));
}

size_t InitialExportBattleSubject::occurrence(uint32_t id) const
{
	std::optional<size_t> found;
	for(size_t index = 0; index < owned->size(); ++index)
		if(owned->sourceAt(index).unitId == id)
		{
			if(found)
				throw std::invalid_argument("Sequence requires unique packet identities");
			found = index;
		}
	if(!found)
		throw std::invalid_argument("Sequence unit is outside the owned original roster");
	return *found;
}

battle::Units InitialExportBattleSubject::getUnitsIf(const battle::UnitFilter & predicate) const
{
	battle::Units result;
	for(size_t index = 0; index < owned->size(); ++index)
		if(predicate(&owned->unitAt(index)))
			result.push_back(&owned->unitAt(index));
	return result;
}

void InitialExportBattleSubject::copyValuesFrom(const HypotheticBattle & branch,
	const InitialExportBattleSubject & source) const
{
	owned->copyEnvironmentValues(battle::ProjectedUnitEnvironmentState::capture(branch));
	// The inventory performs value assignment only: no save, bonus query, provider
	// transfer, callback, expiry or lifecycle. ALL current scalar inputs are copied
	// before a graph query, including other recipients used by adjacency limiters.
	for(const auto & [id, state] : branch.stackStates)
	{
		const auto index = occurrence(id), origin = source.occurrence(id);
		if(typeid(*state) != typeid(StackWithBonuses) || state->owner != &branch
			|| state->origBearer != source.owned->unitAt(origin).getBonusBearer()
			|| owned->sourceAt(index).original != source.owned->sourceAt(origin).original)
			throw std::invalid_argument("Query values require their actual branch and canonical source provider");
		if(!state->bonusesToAdd.empty() || !state->bonusesToUpdate.empty() || !state->bonusesToRemove.empty())
			throw std::invalid_argument("Unrecorded legacy bonus edits have no originating graph operation");
		owned->copyQueryValues(owned->sourceAt(index), *state);
	}
}

void InitialExportBattleSubject::synchronizeValues(const HypotheticBattle & branch) const
{
	copyValuesFrom(branch, *this);
}

TConstBonusListPtr InitialExportBattleSubject::query(const HypotheticBattle & branch,
	uint32_t id, const CSelector & selector, bool unstacked) const
{
	synchronizeValues(branch);
	return owned->unitAt(occurrence(id)).getBonusesBeforeCreatureAbilitySuppression(selector, {}, unstacked);
}

void InitialExportBattleSubject::captureBranchValues(const HypotheticBattle & branch)
{
	captureBranchValuesFrom(branch, *this);
}

void InitialExportBattleSubject::captureBranchValuesFrom(const HypotheticBattle & branch,
	const InitialExportBattleSubject & source)
{
	copyValuesFrom(branch, source);
	owned->recordEnvironmentSnapshot(battle::ProjectedUnitEnvironmentState::capture(branch));
	for(const auto & [id, state] : branch.stackStates)
		owned->recordQuerySnapshot(owned->sourceAt(occurrence(id)));
}

BattleProxy::Subject InitialExportBattleSubject::forkForBranch(Subject subject)
{
	std::shared_ptr<InitialExportBattleSubject> prior;
	const auto * parent = dynamic_cast<const HypotheticBattle *>(subject.get());
	if(parent)
		prior = parent->initialExportSubject();
	else
		prior = std::dynamic_pointer_cast<InitialExportBattleSubject>(subject);
	if(!prior)
		return subject; // Ordinary/legacy projection is byte-for-byte unchanged.
	auto next = std::shared_ptr<InitialExportBattleSubject>(new InitialExportBattleSubject(subject, prior->owned->fork()));
	if(parent)
		next->captureBranchValuesFrom(*parent, *prior);
	return next;
}

void InitialExportBattleSubject::remove(const HypotheticBattle & branch, uint32_t id, const CSelector & selector)
{
	captureBranchValues(branch);
	owned->removeMatching(owned->sourceAt(occurrence(id)), selector);
}

void InitialExportBattleSubject::recordState(const HypotheticBattle & branch, uint32_t id,
	int64_t delta, const JsonNode * before)
{
	// The scalar/environment source already observed by this UPDATE must be
	// part of its ordered replay, not an unrecorded query-only synchronization.
	captureBranchValues(branch);
	const auto index = occurrence(id);
	const auto after = owned->snapshotAt(index);
	if(before)
		owned->updateUnitFrom(owned->sourceAt(index), *before, after, delta);
	else
		owned->updateUnit(id, after, delta);
}
