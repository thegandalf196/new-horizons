/*
 * IBattleState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once
#include <limits>
#include "CBattleInfoEssentials.h"
#include "BattleUnitTurnReason.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "SylvanLuckState.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"

class ObstacleChanges;
class UnitChanges;
struct Bonus;
struct BattleLayout;
class JsonNode;
class JsonSerializeFormat;
class BattleField;
class int3;

namespace vstd
{
	class RNG;
}

namespace battle
{
	class UnitInfo;
}

/// Read-only landing data used to derive Demonic Gate reservations in shared battle queries.
struct DLL_LINKAGE PendingDemonicGateFootprint
{
	CreatureID creature;
	BattleHex position;
};

class DLL_LINKAGE IBattleInfo : public IConstBonusProvider
{
public:
	using ObstacleCList = std::vector<std::shared_ptr<const CObstacleInstance>>;

	virtual ~IBattleInfo() = default;

	virtual BattleID getBattleID() const = 0;
	virtual const scripting::Pool & getScriptContextPool() const = 0;

	virtual int32_t getActiveStackID() const = 0;

	virtual TStacks getStacksIf(const TStackFilter & predicate) const = 0;

	virtual battle::Units getUnitsIf(const battle::UnitFilter & predicate) const = 0;

	virtual BattleField getBattlefieldType() const = 0;
	virtual TerrainId getTerrainType() const = 0;

	virtual ObstacleCList getAllObstacles() const = 0;

	virtual const CGTownInstance * getDefendedTown() const = 0;
	virtual EWallState getWallState(EWallPart partOfWall) const = 0;
	virtual int32_t getWallStructuralHP(EWallPart partOfWall) const { return 0; }
	virtual EGateState getGateState() const = 0;

	virtual PlayerColor getSidePlayer(BattleSide side) const = 0;
	virtual const CArmedInstance * getSideArmy(BattleSide side) const = 0;
	virtual const CGHeroInstance * getSideHero(BattleSide side) const = 0;
	/// Returns list of all spells used by specified side (and that can be learned by opposite hero)
	virtual std::vector<SpellID> getUsedSpells(BattleSide side) const = 0;

	virtual const JsonNode & getHeroCommandRules() const;
	virtual const JsonNode & getMagicRules() const;
	virtual const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const;
	virtual bool getHeroCommandUsed(BattleSide side) const { return false; }
	virtual HeroCommand getActiveDoctrine(BattleSide side) const { return HeroCommand::NONE; }
	virtual HeroCommand getActiveOrder(BattleSide side) const { return HeroCommand::NONE; }
	virtual std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const { return {}; }
	virtual std::optional<FocusFireState> getFocusFireState(BattleSide side) const { return {}; }
	virtual int32_t getCastSpells(BattleSide side) const = 0;
	virtual int32_t getEnchanterCounter(BattleSide side) const = 0;
	virtual bool getTemporalFieldUsed(BattleSide side) const { return false; }
	virtual bool getCounterspellArmed(BattleSide side) const { return false; }
	virtual int32_t getMetamagicPendingCount(BattleSide side) const { return 0; }
	virtual int32_t getMetamagicUsesConsumed(BattleSide side) const { return 0; }
	virtual bool getMetamagicGrandUsed(BattleSide side) const { return false; }
	virtual bool getMetamagicFormulaReserveUsed(BattleSide side) const { return false; }
	virtual bool getMetamagicCountersequenceArmed(BattleSide side) const { return false; }
	virtual SpellID getMetamagicFirstSpell(BattleSide side) const { return SpellID(); }
	virtual uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const { return std::numeric_limits<uint32_t>::max(); }
	virtual const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const
	{
		static const std::vector<SpellID> empty;
		return empty;
	}
	virtual bool getMetamagicFirstCounterspellNegated(BattleSide side) const { return false; }
	virtual int32_t getBloodrageDamagePercent(BattleSide side) const { return 0; }
	virtual int32_t getBloodrageRank(BattleSide side) const { return 0; }
	virtual SylvanLuckState getSylvanLuckState(BattleSide side) const { return {}; }
	virtual LuckRollRules getLuckRollRules() const { return {}; }
	virtual const std::map<CreatureID, TQuantity> & getDemonicReserve(BattleSide side) const
	{
		static const std::map<CreatureID, TQuantity> empty;
		return empty;
	}
	/// Pending Demonic Gate landing footprints are authoritative but derived into accessibility.
	virtual std::vector<PendingDemonicGateFootprint> getPendingDemonicGateFootprints(BattleSide side) const
	{
		(void)side;
		return {};
	}

	virtual ui8 getTacticDist() const = 0;
	virtual BattleSide getTacticsSide() const = 0;

	virtual uint32_t nextUnitId() const = 0;

	virtual int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const = 0;

	virtual int3 getLocation() const = 0;
	virtual BattleLayout getLayout() const = 0;

	virtual int32_t getRound() const = 0;
	/// Monotonic token for a creature activation. It changes on every
	/// authoritative nextTurn transition, including a same-round morale
	/// activation, so per-activation obstacle effects can be guarded without
	/// touching unit state.
	virtual int32_t getActivationSerial() const { return 0; }
};

class DLL_LINKAGE IBattleState : public IBattleInfo
{
public:
	virtual void nextRound() = 0;
	virtual void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) = 0;

	virtual void addUnit(uint32_t id, const JsonNode & data) = 0;
	virtual void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) = 0;
	virtual void moveUnit(uint32_t id, const BattleHex & destination) = 0;
	virtual void removeUnit(uint32_t id) = 0;

	virtual void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;
	virtual void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;
	virtual void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;

	virtual void setWallState(EWallPart partOfWall, EWallState state) = 0;
	virtual void setWallStructuralHP(EWallPart partOfWall, int32_t hp) { (void)partOfWall; (void)hp; }

	virtual void addObstacle(const ObstacleChanges & changes) = 0;
	virtual void updateObstacle(const ObstacleChanges & changes) = 0;
	virtual void removeObstacle(uint32_t id) = 0;

	/// Applies an authoritative snapshot of transient canonical Order state.
	/// The default keeps lightweight callback proxies and test doubles source
	/// compatible; concrete battle state stores it.
	virtual void setHeroOrderState(BattleSide, const std::optional<HeroOrderState> &) {}
};
