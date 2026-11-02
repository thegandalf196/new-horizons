/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "IBattleState.h"
#include "BattleLayout.h"
#include "../int3.h"
#include "../constants/EntityIdentifiers.h"
#include <stdexcept>
namespace battle::ownedFrameCapture
{
/// INTERNAL typed refusal base, no source pointer or fabricated fallback values.
/// Every current virtual default is explicit; future interface additions require
/// an explicit admission review instead of legacy false/global-query fallback.
class RefusingBattleInfo : public IBattleInfo
{
public:
	const IBonusBearer * getBonusBearer() const override { throw std::logic_error("Owned battle bonus source was not captured"); }
	BattleID getBattleID() const override { throw std::logic_error("Owned battle query is outside its captured scope: getBattleID"); }
	const scripting::Pool & getScriptContextPool() const override { throw std::logic_error("Owned battle query is outside its captured scope: getScriptContextPool"); }
	int32_t getActiveStackID() const override { throw std::logic_error("Owned battle query is outside its captured scope: getActiveStackID"); }
	const SeizeInitiativeState & getSeizeInitiativeState() const override { throw std::logic_error("Owned battle query is outside its captured scope: getSeizeInitiativeState"); }
	TStacks getStacksIf(const TStackFilter & predicate) const override { throw std::logic_error("Owned battle query is outside its captured scope: getStacksIf"); }
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override { throw std::logic_error("Owned battle query is outside its captured scope: getUnitsIf"); }
	BattleField getBattlefieldType() const override { throw std::logic_error("Owned battle query is outside its captured scope: getBattlefieldType"); }
	TerrainId getTerrainType() const override { throw std::logic_error("Owned battle query is outside its captured scope: getTerrainType"); }
	ObstacleCList getAllObstacles() const override { throw std::logic_error("Owned battle query is outside its captured scope: getAllObstacles"); }
	const CGTownInstance * getDefendedTown() const override { throw std::logic_error("Owned battle query is outside its captured scope: getDefendedTown"); }
	EWallState getWallState(EWallPart partOfWall) const override { throw std::logic_error("Owned battle query is outside its captured scope: getWallState"); }
	int32_t getWallStructuralHP(EWallPart partOfWall) const override { throw std::logic_error("Owned battle query is outside its captured scope: getWallStructuralHP"); }
	EGateState getGateState() const override { throw std::logic_error("Owned battle query is outside its captured scope: getGateState"); }
	PlayerColor getSidePlayer(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getSidePlayer"); }
	const CArmedInstance * getSideArmy(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getSideArmy"); }
	const CGHeroInstance * getSideHero(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getSideHero"); }
	int32_t getTemporaryBufferRemaining(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getTemporaryBufferRemaining"); }
	int32_t getFirstRoundMoraleModifier(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getFirstRoundMoraleModifier"); }
	std::vector<SpellID> getUsedSpells(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getUsedSpells"); }
	const JsonNode & getHeroCommandRules() const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroCommandRules"); }
	const JsonNode & getMagicRules() const override { throw std::logic_error("Owned battle query is outside its captured scope: getMagicRules"); }
	const newHorizonsCrisisCommand::State & getCrisisCommandState() const override { throw std::logic_error("Owned battle query is outside its captured scope: getCrisisCommandState"); }
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { throw std::logic_error("Owned battle query is outside its captured scope: getCreatureCategoryRules"); }
	bool getHeroCommandUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroCommandUsed"); }
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroActionAllowances"); }
	const DoubleCommandState & getDoubleCommandState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getDoubleCommandState"); }
	const PreCombatOrderState & getPreCombatOrderState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getPreCombatOrderState"); }
	const BattleDeploymentState & getDeploymentState() const override { throw std::logic_error("Owned battle query is outside its captured scope: getDeploymentState"); }
	const AlternatingHeroActionState & getWarcastingState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getWarcastingState"); }
	const RelentlessAssaultState & getRelentlessAssaultState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getRelentlessAssaultState"); }
	HeroCommand getActiveDoctrine(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getActiveDoctrine"); }
	HeroCommand getActiveOrder(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getActiveOrder"); }
	std::vector<HeroOrderState> getHeroOrderStates(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroOrderStates"); }
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroOrderState"); }
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side, HeroCommand command) const override { throw std::logic_error("Owned battle query is outside its captured scope: getHeroOrderState"); }
	std::optional<FocusFireState> getFocusFireState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getFocusFireState"); }
	bool hasCompletedHeroSpellCast(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: hasCompletedHeroSpellCast"); }
	int32_t getExtendSpellLastRound(BattleSide) const override { throw std::logic_error("Owned battle query is outside its captured scope: getExtendSpellLastRound"); }
	bool hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const override { throw std::logic_error("Owned battle query is outside its captured scope: hasCompletedHeroSpellLevel"); }
	const newHorizonsCrossSchoolFormula::State & getCrossSchoolFormulaState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getCrossSchoolFormulaState"); }
	int32_t getCastSpells(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getCastSpells"); }
	int32_t getEnchanterCounter(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getEnchanterCounter"); }
	bool getTemporalFieldUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getTemporalFieldUsed"); }
	bool getCounterspellArmed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getCounterspellArmed"); }
	int32_t getMetamagicPendingCount(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicPendingCount"); }
	int32_t getMetamagicUsesConsumed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicUsesConsumed"); }
	bool getMetamagicGrandUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicGrandUsed"); }
	bool getRebirthChainUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getRebirthChainUsed"); }
	bool getPhoenixSparkUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getPhoenixSparkUsed"); }
	bool getMetamagicFormulaReserveUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicFormulaReserveUsed"); }
	bool getMetamagicCountersequenceArmed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicCountersequenceArmed"); }
	SpellID getMetamagicFirstSpell(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicFirstSpell"); }
	uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicFirstTargetUnitId"); }
	const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicSequenceSpells"); }
	bool getMetamagicFirstCounterspellNegated(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMetamagicFirstCounterspellNegated"); }
	int32_t getBloodrageDamagePercent(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageDamagePercent"); }
	int32_t getBloodrageRank(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageRank"); }
	int32_t getBloodrageCapPercent(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageCapPercent"); }
	int32_t getBloodrageSpeedBonus(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageSpeedBonus"); }
	int32_t getBloodrageAdditionalRetaliations(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageAdditionalRetaliations"); }
	int32_t getBloodrageLowHealthIncrement(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodrageLowHealthIncrement"); }
	int32_t getBloodragePainIncrement(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBloodragePainIncrement"); }
	SylvanLuckState getSylvanLuckState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getSylvanLuckState"); }
	PerfectFortuneState getPerfectFortuneState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getPerfectFortuneState"); }
	LuckSerendipityState getLuckSerendipityState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getLuckSerendipityState"); }
	ArmorerDefiantState getArmorerDefiantState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getArmorerDefiantState"); }
	AdverseCombatRerollState getAdverseCombatRerollState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getAdverseCombatRerollState"); }
	MoraleSuppressionState getMoraleSuppressionState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getMoraleSuppressionState"); }
	const RapidResponseState & getRapidResponseState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getRapidResponseState"); }
	const ReducedExtraActivationState & getReducedExtraActivationState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getReducedExtraActivationState"); }
	const SpellResponseState & getSpellResponseState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getSpellResponseState"); }
	int32_t getBattlecraftMasteryAwardRound(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getBattlecraftMasteryAwardRound"); }
	const OverwhelmingFormulaState & getOverwhelmingFormulaState(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getOverwhelmingFormulaState"); }
	bool armorerLastStandUsed(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: armorerLastStandUsed"); }
	void consumeArmorerLastStand(BattleSide side) override { throw std::logic_error("Owned battle query is outside its captured scope: consumeArmorerLastStand"); }
	LuckRollRules getLuckRollRules() const override { throw std::logic_error("Owned battle query is outside its captured scope: getLuckRollRules"); }
	bool getRawFinalDirectPhysicalLuckPolicy() const override { throw std::logic_error("Owned battle query is outside its captured scope: getRawFinalDirectPhysicalLuckPolicy"); }
	const std::map<CreatureID, TQuantity> & getDemonicReserve(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getDemonicReserve"); }
	std::vector<PendingDemonicGateFootprint> getPendingDemonicGateFootprints(BattleSide side) const override { throw std::logic_error("Owned battle query is outside its captured scope: getPendingDemonicGateFootprints"); }
	ui8 getTacticDist() const override { throw std::logic_error("Owned battle query is outside its captured scope: getTacticDist"); }
	BattleSide getTacticsSide() const override { throw std::logic_error("Owned battle query is outside its captured scope: getTacticsSide"); }
	uint32_t nextUnitId() const override { throw std::logic_error("Owned battle query is outside its captured scope: nextUnitId"); }
	int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override { throw std::logic_error("Owned battle query is outside its captured scope: getActualDamage"); }
	int3 getLocation() const override { throw std::logic_error("Owned battle query is outside its captured scope: getLocation"); }
	BattleLayout getLayout() const override { throw std::logic_error("Owned battle query is outside its captured scope: getLayout"); }
	int32_t getRound() const override { throw std::logic_error("Owned battle query is outside its captured scope: getRound"); }
	int32_t getActivationSerial() const override { throw std::logic_error("Owned battle query is outside its captured scope: getActivationSerial"); }
	int32_t getMoraleExtraDamagePercent() const override { throw std::logic_error("Owned battle query is outside its captured scope: getMoraleExtraDamagePercent"); }
};
}
