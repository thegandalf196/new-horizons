/*
 * ProjectedBattleWorldArenaState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "ProjectedBattleWorldArena.h"
#include "ProjectedUnitEnvironmentState.h"
#include "ProjectedUnitEnvironment.h"
#include "ProjectedBattleAccessibility.h"
#include "ProjectedSummonHealthQuery.h"
#include "HeroCommand.h"
#include "NewHorizonsCrisisCommand.h"
#include "NewHorizonsReactiveDefense.h"
#include "NewHorizonsSeizeInitiative.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "CUnitState.h"
#include "BattleUnitSidecarFields.h"
#include "BattleUnitRootOrigin.h"
#include "PreparedBattleUnitRoots.h"
#include "../CStack.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include "../bonuses/PreparedBonusGraphFinalization.h"

namespace battle
{
/// Private implementation storage; declaration order is a lifetime contract.
struct ProjectedBattleWorldArena::Data
{
	struct Slot
	{
		std::unique_ptr<CStack> owned;
		CStack * original = nullptr;
		std::unique_ptr<CUnitStateDetached> state;
		std::unique_ptr<const BattleUnitRootIdentity> creationIdentity;
	};
	BattleInfo & source;
	std::unique_ptr<const BattleUnitSidecarOrigin> sidecarOrigin;
	std::unique_ptr<const BattleUnitRootOrigin> rootOrigin;
	std::vector<std::unique_ptr<Slot>> slots;
	std::vector<std::unique_ptr<newHorizonsElementalRebirth::ProjectedSummonHealthQuery>> queries;
	ProjectedBonusRoster roster;
	ProjectedUnitEnvironmentState sideValues;
	BattleAccessibilityState placementValues;
	JsonNode commandRules;
	BattleSideArray<std::vector<HeroOrderState>> orders;
	BattleSideArray<ReducedExtraActivationState> reduced;
	newHorizonsCrisisCommand::State crisis;
	SeizeInitiativeState seize;
	BattleSideArray<HeroActionAllowanceState> allowances;
	std::set<uint32_t> destroyedUnits;
	bool firstBlood = false;
	newHorizonsCreatures::CreatureCategoryRules categories;
	BattleSideArray<bool> chainUsed{};
	BattleSideArray<bool> sparkUsed{};
	std::optional<CreatureID> primaryElemental;
	BattleID battleId;
	BattleSideArray<CArmedInstance *> armies;
	BattleSideArray<const CArmedInstance *> queryArmies;
	BattleSideArray<const CGHeroInstance *> heroes;
	BattleSideArray<const CGHeroInstance *> sideHeroes;
	int32_t round;
	std::unique_ptr<ProjectedUnitEnvironment> environment;
	std::unique_ptr<ProjectedBattleAccessibilityContext> placement;
	// Reverse destruction: finalizer, graph, adapters, roster, query/slot owners.
	std::unique_ptr<ProjectedBonusGraph> graph;
	std::unique_ptr<PreparedBonusGraphFinalization> finalizer;
	bool failed = false;
	bool rootStorageTransferred = false;
	explicit Data(BattleInfo & source);
	void requireUsable() const;
	void abandon() noexcept;
	void captureUnits();
	Slot * find(uint32_t id, bool onlyAlive) const;
	std::vector<ProjectedUnitRemoval::Binding> removalBindings() const;
	int32_t summonCapacity(const UnitInfo & info);
	const CUnitStateDetached & add(uint32_t id, const JsonNode & data);
	void initializeAdded(Slot & slot, const UnitInfo & info);
	void expireProtect();
	void update(Slot & slot, const JsonNode & data, int64_t healthDelta);
	struct PacketCandidates
	{
		std::vector<newHorizonsCrisisCommand::DeathReceipt> crisis;
		std::vector<newHorizonsReactiveDefense::Destruction> solidarity;
		std::set<uint32_t> removed;
	};
	PacketCandidates captureCandidates(const std::vector<UnitChanges> & changes) const;
	void applyEntries(ProjectedBattleWorldArena & owner, const std::vector<UnitChanges> & changes);
	void finishCandidates(ProjectedBattleWorldArena & owner, const PacketCandidates & candidates, const std::vector<UnitChanges> & changes);
	void recordSolidarityDeaths(ProjectedBattleWorldArena & owner, std::span<const newHorizonsReactiveDefense::Destruction> deaths);
	bool applyConsumption(ProjectedBattleWorldArena & owner, const BattleUnitsChanged & packet);
	void recordBloodrageDeath(uint32_t id);
	void validateCrisisForStorage(const newHorizonsCrisisCommand::State & state) const;
	void recordCrisisDeaths(const std::vector<newHorizonsCrisisCommand::DeathReceipt> & candidates);
};
}
