/*
 * BattleProxy.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once
#include "CBattleInfoCallback.h"
#include "IBattleState.h"

class DLL_LINKAGE BattleProxy : public CBattleInfoCallback, public IBattleState
{
public:
	using Subject = std::shared_ptr<CBattleInfoCallback>;

	BattleProxy(Subject subject_);
	~BattleProxy();

	//////////////////////////////////////////////////////////////////////////
	// IBattleInfo
	const IBattleInfo * getBattle() const override;
	std::optional<PlayerColor> getPlayerID() const override;

	int32_t getActiveStackID() const override;
	int32_t getActivationSerial() const override { return subject->getBattle()->getActivationSerial(); }

	TStacks getStacksIf(const TStackFilter & predicate) const override;

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	BattleField getBattlefieldType() const override;
	TerrainId getTerrainType() const override;

	ObstacleCList getAllObstacles() const override;

	PlayerColor getSidePlayer(BattleSide side) const override;
	const CArmedInstance * getSideArmy(BattleSide side) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;
	int32_t getFirstRoundMoraleModifier(BattleSide side) const override
	{
		return subject->getBattle()->getFirstRoundMoraleModifier(side);
	}
	int battleGetPerkMagicalReductionBasisPoints(const battle::Unit * unit) const override;

	ui8 getTacticDist() const override;
	BattleSide getTacticsSide() const override;
	int32_t getRound() const override;

	const CGTownInstance * getDefendedTown() const override;
	EWallState getWallState(EWallPart partOfWall) const override;
	int32_t getWallStructuralHP(EWallPart partOfWall) const override;
	EGateState getGateState() const override;

	const JsonNode & getHeroCommandRules() const override { return subject->getBattle()->getHeroCommandRules(); }
	const JsonNode & getMagicRules() const override { return subject->getBattle()->getMagicRules(); }
	const SeizeInitiativeState & getSeizeInitiativeState() const override { return subject->getBattle()->getSeizeInitiativeState(); }
	const newHorizonsCrisisCommand::State & getCrisisCommandState() const override
	{
		return subject->getBattle()->getCrisisCommandState();
	}
	bool hasCompletedHeroSpellCast(BattleSide side) const override
	{
		return subject->getBattle()->hasCompletedHeroSpellCast(side);
	}
	bool hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const override
	{
		return subject->getBattle()->hasCompletedHeroSpellLevel(side, level);
	}
	const newHorizonsCrossSchoolFormula::State & getCrossSchoolFormulaState(BattleSide side) const override
	{
		return subject->getBattle()->getCrossSchoolFormulaState(side);
	}
	const AlternatingHeroActionState & getWarcastingState(BattleSide side) const override
	{
		return subject->getBattle()->getWarcastingState(side);
	}
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override
	{
		return subject->getBattle()->getHeroActionAllowances(side);
	}
	const DoubleCommandState & getDoubleCommandState(BattleSide side) const override
	{
		return subject->getBattle()->getDoubleCommandState(side);
	}
	const PreCombatOrderState & getPreCombatOrderState(BattleSide side) const override
	{
		return subject->getBattle()->getPreCombatOrderState(side);
	}
	const BattleDeploymentState & getDeploymentState() const override;
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { return subject->getBattle()->getCreatureCategoryRules(); }
	bool getHeroCommandUsed(BattleSide side) const override { return subject->getBattle()->getHeroCommandUsed(side); }
	HeroCommand getActiveDoctrine(BattleSide side) const override { return subject->getBattle()->getActiveDoctrine(side); }
	HeroCommand getActiveOrder(BattleSide side) const override { return subject->getBattle()->getActiveOrder(side); }
	std::vector<HeroOrderState> getHeroOrderStates(BattleSide side) const override
	{
		return subject->getBattle()->getHeroOrderStates(side);
	}
	std::optional<FocusFireState> getFocusFireState(BattleSide side) const override
	{
		return subject->getBattle()->getFocusFireState(side);
	}
	int32_t getCastSpells(BattleSide side) const override;
	int32_t getEnchanterCounter(BattleSide side) const override;
	bool getTemporalFieldUsed(BattleSide side) const override;
	bool getCounterspellArmed(BattleSide side) const override;
	int32_t getMetamagicPendingCount(BattleSide side) const override;
	int32_t getMetamagicUsesConsumed(BattleSide side) const override;
	bool getMetamagicGrandUsed(BattleSide side) const override;
	bool getMetamagicFormulaReserveUsed(BattleSide side) const override;
	bool getMetamagicCountersequenceArmed(BattleSide side) const override;
	SpellID getMetamagicFirstSpell(BattleSide side) const override;
	uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const override;
	const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const override;
	bool getMetamagicFirstCounterspellNegated(BattleSide side) const override;
	int32_t getBloodrageDamagePercent(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageDamagePercent(side);
	}
	int32_t getBloodrageRank(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageRank(side);
	}
	int32_t getBloodrageCapPercent(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageCapPercent(side);
	}
	int32_t getBloodrageSpeedBonus(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageSpeedBonus(side);
	}
	int32_t getBloodrageAdditionalRetaliations(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageAdditionalRetaliations(side);
	}
	int32_t getBloodrageLowHealthIncrement(BattleSide side) const override
	{
		return subject->getBattle()->getBloodrageLowHealthIncrement(side);
	}
	int32_t getBloodragePainIncrement(BattleSide side) const override
	{
		return subject->getBattle()->getBloodragePainIncrement(side);
	}
	std::vector<PendingDemonicGateFootprint> getPendingDemonicGateFootprints(BattleSide side) const override
	{
		return subject->getBattle()->getPendingDemonicGateFootprints(side);
	}
	SylvanLuckState getSylvanLuckState(BattleSide side) const override
	{
		return subject->getBattle()->getSylvanLuckState(side);
	}
	AdverseCombatRerollState getAdverseCombatRerollState(BattleSide side) const override
	{
		return subject->getBattle()->getAdverseCombatRerollState(side);
	}
	MoraleSuppressionState getMoraleSuppressionState(BattleSide side) const override
	{
		return subject->getBattle()->getMoraleSuppressionState(side);
	}
	const RapidResponseState & getRapidResponseState(BattleSide side) const override
	{
		return subject->getBattle()->getRapidResponseState(side);
	}
	const ReducedExtraActivationState & getReducedExtraActivationState(BattleSide side) const override
	{
		return subject->getBattle()->getReducedExtraActivationState(side);
	}
	LuckRollRules getLuckRollRules() const override { return subject->getBattle()->getLuckRollRules(); }

	const IBonusBearer * getBonusBearer() const override;
protected:
	Subject subject;
};
