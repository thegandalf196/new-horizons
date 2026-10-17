/*
 * InitialExportBattleSubject.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "InitialExportWorld.h"
#include "../../lib/battle/BattleProxy.h"

class HypotheticBattle;

/// Explicit full-information sequence projection, not a player-view importer.
/// Owns private graph/history; native subject/library/configuration still require
/// the caller's REAL immutable source interval. No CAS or lease is created.
class InitialExportBattleSubject final : public BattleProxy
{
	std::unique_ptr<InitialExportWorld> owned;
	InitialExportBattleSubject(Subject subject, std::unique_ptr<InitialExportWorld> world);
	void copyValuesFrom(const HypotheticBattle & branch, const InitialExportBattleSubject & source) const;
	void captureBranchValuesFrom(const HypotheticBattle & branch, const InitialExportBattleSubject & source);
	int heroCommandCoefficient(const JsonNode & effect, const CGHeroInstance & hero,
		int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const override;
	TConstBonusListPtr visibleHeroBonuses(BattleSide side, const CSelector & selector, bool unstacked) const override;
public:
	static std::shared_ptr<InitialExportBattleSubject> capture(Subject subject, BattleInfo & native);
	/// Explicit native handle, exact full-information source and complete roster.
	/// Retains actual parent ownership; recipient overlays do not establish exports.
	static std::shared_ptr<InitialExportBattleSubject> captureLegacyMovement(Subject subject, BattleInfo & native);
	static Subject forkForBranch(Subject subject);
	InitialExportWorld & world() const { return *owned; }
	size_t occurrence(uint32_t id) const;
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	// This provider is the retained query/history source, NOT a mutable branch.
	// Packet/lifecycle mutation belongs to HypotheticBattle, which records its
	// originating operations through the typed methods below.
	BattleID getBattleID() const override;
	const scripting::Pool & getScriptContextPool() const override;
	std::vector<SpellID> getUsedSpells(BattleSide side) const override;
	uint32_t nextUnitId() const override;
	int64_t getActualDamage(const DamageRange &, int32_t, vstd::RNG &) const override;
	int3 getLocation() const override;
	BattleLayout getLayout() const override;
	void nextRound() override;
	void nextTurn(uint32_t, BattleUnitTurnReason) override;
	void setSeizeInitiativeState(const SeizeInitiativeState &) override;
	void setPreCombatOrderState(BattleSide, const PreCombatOrderState &) override;
	void setDeploymentState(const BattleDeploymentState &) override;
	void addUnit(uint32_t, const JsonNode &) override;
	void updateUnit(uint32_t, const JsonNode &, int64_t) override;
	void moveUnit(uint32_t, const BattleHex &) override;
	void removeUnit(uint32_t) override;
	void addUnitBonus(uint32_t, const std::vector<Bonus> &) override;
	void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override;
	void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override;
	void exchangeBattleEffects(const battle::BattleEffectExchange &) override;
	void setWallState(EWallPart, EWallState) override;
	void setWallStructuralHP(EWallPart, int32_t) override;
	void addObstacle(const ObstacleChanges &) override;
	void updateObstacle(const ObstacleChanges &) override;
	void removeObstacle(uint32_t) override;
	void setHeroOrderStates(BattleSide, const std::vector<HeroOrderState> &) override;
	void setHeroOrderState(BattleSide, const std::optional<HeroOrderState> &) override;
	void setDoubleCommandState(BattleSide, const DoubleCommandState &) override;
	void setRelentlessAssaultState(BattleSide, const RelentlessAssaultState &) override;
	void recordRelentlessAssaultAttack(BattleSide, uint32_t) override;
	void setAdverseCombatRerollState(BattleSide, const AdverseCombatRerollState &) override;
	void setMoraleSuppressionState(BattleSide, const MoraleSuppressionState &) override;
	void setRapidResponseState(BattleSide, const RapidResponseState &) override;
	void setReducedExtraActivationState(BattleSide, const ReducedExtraActivationState &) override;
	void setCrisisCommandState(const newHorizonsCrisisCommand::State &) override;
	void setCrossSchoolFormulaState(BattleSide, const newHorizonsCrossSchoolFormula::State &) override;
	void armReactiveWeave(BattleSide, int32_t, int32_t) override;
	void setSpellResponseState(BattleSide, const SpellResponseState &) override;
	void consumeExtendSpell(BattleSide) override;
	void setOverwhelmingFormulaState(BattleSide, const OverwhelmingFormulaState &) override;
	void setPerfectFortuneState(BattleSide, const PerfectFortuneState &) override;
	void setRebirthChainUsed(BattleSide, bool) override;
	void setPhoenixSparkUsed(BattleSide, bool) override;
	void setLuckSerendipityState(BattleSide, const LuckSerendipityState &) override;
	void setArmorerDefiantState(BattleSide, const ArmorerDefiantState &) override;
	void awardBattlecraftMastery(BattleSide, uint32_t, int32_t, BattlecraftMasteryAction) override;
	void consumeArmorerLastStand(BattleSide) override;
	void synchronizeValues(const HypotheticBattle & branch) const;
	TConstBonusListPtr query(const HypotheticBattle & branch, uint32_t id, const CSelector & selector, bool unstacked) const;
	void addSequenceBonus(const HypotheticBattle & branch, uint32_t id, const std::vector<Bonus> & prepared, bool forceAdd);
	void remove(const HypotheticBattle & branch, uint32_t id, const CSelector & selector);
	void recordState(const HypotheticBattle & branch, uint32_t id, int64_t delta, const JsonNode * before = nullptr);
	void captureBranchValues(const HypotheticBattle & branch);
	void advanceTimedRound(const HypotheticBattle & branch, uint32_t id, const CSelector & roundSelector);
};
