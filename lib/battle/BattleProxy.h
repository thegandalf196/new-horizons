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

	TStacks getStacksIf(const TStackFilter & predicate) const override;

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	BattleField getBattlefieldType() const override;
	TerrainId getTerrainType() const override;

	ObstacleCList getAllObstacles() const override;

	PlayerColor getSidePlayer(BattleSide side) const override;
	const CArmedInstance * getSideArmy(BattleSide side) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;

	ui8 getTacticDist() const override;
	BattleSide getTacticsSide() const override;
	int32_t getRound() const override;

	const CGTownInstance * getDefendedTown() const override;
	EWallState getWallState(EWallPart partOfWall) const override;
	EGateState getGateState() const override;

	const JsonNode & getHeroCommandRules() const override { return subject->getBattle()->getHeroCommandRules(); }
	const JsonNode & getMagicRules() const override { return subject->getBattle()->getMagicRules(); }
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { return subject->getBattle()->getCreatureCategoryRules(); }
	bool getHeroCommandUsed(BattleSide side) const override { return subject->getBattle()->getHeroCommandUsed(side); }
	HeroCommand getActiveDoctrine(BattleSide side) const override { return subject->getBattle()->getActiveDoctrine(side); }
	HeroCommand getActiveOrder(BattleSide side) const override { return subject->getBattle()->getActiveOrder(side); }
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

	const IBonusBearer * getBonusBearer() const override;
protected:
	Subject subject;
};
