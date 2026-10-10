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
#include <algorithm>
#include <cstdint>
#include <limits>
#include "CBattleInfoEssentials.h"
#include "BattleUnitTurnReason.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "AdverseCombatRerollState.h"
#include "MoraleSuppressionState.h"
#include "ReducedExtraActivationState.h"
#include "NewHorizonsRapidResponse.h"
#include "SylvanLuckState.h"
#include "HeroActionAllowanceState.h"
#include "AlternatingHeroActionState.h"
#include "RelentlessAssaultState.h"
#include "SpellResponseState.h"
#include "NewHorizonsSeizeInitiative.h"
#include "../spells/NewHorizonsCrossSchoolFormula.h"
#include "OverwhelmingFormulaState.h"
#include "PerfectFortuneState.h"
#include "LuckSerendipityState.h"
#include "ArmorerDefiantState.h"
#include "BattleDeploymentState.h"
#include "BattleEffectExchange.h"
#include "NewHorizonsCrisisCommand.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"

class ObstacleChanges;
class UnitChanges;
struct Bonus;
struct BattleLayout;
class JsonNode;
class JsonSerializeFormat;
class BattleField;
class int3;
enum class BattlecraftMasteryAction : uint8_t;

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
	virtual const SeizeInitiativeState & getSeizeInitiativeState() const
	{
		static const SeizeInitiativeState empty;
		return empty;
	}

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
	/// First-round-only Morale modifier captured when this battle was created.
	virtual int32_t getFirstRoundMoraleModifier(BattleSide side) const
	{
		(void)side;
		return 0;
	}
	/// Returns list of all spells used by specified side (and that can be learned by opposite hero)
	virtual std::vector<SpellID> getUsedSpells(BattleSide side) const = 0;

	virtual const JsonNode & getHeroCommandRules() const;
	virtual const JsonNode & getMagicRules() const;
	virtual const newHorizonsCrisisCommand::State & getCrisisCommandState() const
	{
		static const newHorizonsCrisisCommand::State empty;
		return empty;
	}
	virtual const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const;
	virtual bool getHeroCommandUsed(BattleSide side) const { return false; }
	virtual const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const
	{
		(void)side;
		static const HeroActionAllowanceState empty;
		return empty;
	}
	virtual const DoubleCommandState & getDoubleCommandState(BattleSide side) const
	{
		(void)side;
		static const DoubleCommandState empty;
		return empty;
	}
	virtual const PreCombatOrderState & getPreCombatOrderState(BattleSide side) const
	{
		(void)side;
		static const PreCombatOrderState empty;
		return empty;
	}
	virtual const BattleDeploymentState & getDeploymentState() const
	{
		static const BattleDeploymentState empty;
		return empty;
	}
	virtual const AlternatingHeroActionState & getWarcastingState(BattleSide side) const
	{
		(void)side;
		static const AlternatingHeroActionState empty;
		return empty;
	}
	virtual const RelentlessAssaultState & getRelentlessAssaultState(BattleSide side) const
	{
		(void)side;
		static const RelentlessAssaultState empty;
		return empty;
	}
	virtual HeroCommand getActiveDoctrine(BattleSide side) const { return HeroCommand::NONE; }
	virtual HeroCommand getActiveOrder(BattleSide side) const { return HeroCommand::NONE; }
	/// Returns all independent canonical Order snapshots, oldest issue first.
	virtual std::vector<HeroOrderState> getHeroOrderStates(BattleSide side) const
	{
		(void)side;
		return {};
	}
	/// Compatibility view of the latest issued Order.
	virtual std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const
	{
		const auto orders = getHeroOrderStates(side);
		if(orders.empty())
			return {};
		return orders.back();
	}
	/// Looks up an Order by its stable command identifier, regardless of which
	/// command was issued most recently.
	virtual std::optional<HeroOrderState> getHeroOrderState(BattleSide side, HeroCommand command) const
	{
		const auto orders = getHeroOrderStates(side);
		const auto found = std::find_if(orders.begin(), orders.end(), [command](const HeroOrderState & order)
		{
			return order.command == command;
		});
		if(found == orders.end())
			return {};
		return *found;
	}
	virtual std::optional<FocusFireState> getFocusFireState(BattleSide side) const { return {}; }
	virtual bool hasCompletedHeroSpellCast(BattleSide side) const { (void)side; return false; }
	virtual int32_t getExtendSpellLastRound(BattleSide) const { return -1; }
	virtual bool hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const
	{
		(void)side;
		(void)level;
		return false;
	}
	virtual const newHorizonsCrossSchoolFormula::State & getCrossSchoolFormulaState(BattleSide side) const
	{
		static const newHorizonsCrossSchoolFormula::State empty;
		return empty;
	}
	virtual int32_t getCastSpells(BattleSide side) const = 0;
	virtual int32_t getEnchanterCounter(BattleSide side) const = 0;
	virtual bool getTemporalFieldUsed(BattleSide side) const { return false; }
	virtual bool getCounterspellArmed(BattleSide side) const { return false; }
	virtual int32_t getMetamagicPendingCount(BattleSide side) const { return 0; }
	virtual int32_t getMetamagicUsesConsumed(BattleSide side) const { return 0; }
	virtual bool getMetamagicGrandUsed(BattleSide side) const { return false; }
	virtual bool getRebirthChainUsed(BattleSide side) const { (void)side; return false; }
	virtual bool getPhoenixSparkUsed(BattleSide side) const { (void)side; return false; }
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
	virtual int32_t getBloodrageCapPercent(BattleSide side) const { return 0; }
	virtual int32_t getBloodrageSpeedBonus(BattleSide side) const { return 0; }
	virtual int32_t getBloodrageAdditionalRetaliations(BattleSide side) const { return 0; }
	virtual int32_t getBloodrageLowHealthIncrement(BattleSide side) const { return 0; }
	virtual int32_t getBloodragePainIncrement(BattleSide side) const { (void)side; return 0; }
	virtual SylvanLuckState getSylvanLuckState(BattleSide side) const { return {}; }
	virtual PerfectFortuneState getPerfectFortuneState(BattleSide side) const { (void)side; return {}; }
	virtual LuckSerendipityState getLuckSerendipityState(BattleSide side) const { (void)side; return {}; }
	virtual ArmorerDefiantState getArmorerDefiantState(BattleSide side) const { (void)side; return {}; }
	virtual AdverseCombatRerollState getAdverseCombatRerollState(BattleSide side) const { (void)side; return {}; }
	virtual MoraleSuppressionState getMoraleSuppressionState(BattleSide side) const { (void)side; return {}; }
	virtual const RapidResponseState & getRapidResponseState(BattleSide side) const
	{
		(void)side;
		static const RapidResponseState empty;
		return empty;
	}
	virtual const ReducedExtraActivationState & getReducedExtraActivationState(BattleSide side) const
	{
		(void)side;
		static const ReducedExtraActivationState empty;
		return empty;
	}
	virtual const SpellResponseState & getSpellResponseState(BattleSide side) const
	{
		(void)side;
		static const SpellResponseState empty;
		return empty;
	}
	virtual int32_t getBattlecraftMasteryAwardRound(BattleSide side) const { (void)side; return -1; }
	virtual const OverwhelmingFormulaState & getOverwhelmingFormulaState(BattleSide side) const
	{
		(void)side;
		static const OverwhelmingFormulaState empty;
		return empty;
	}
	/// Whether this side has already spent Armorer's once-per-combat Last Stand.
	virtual bool armorerLastStandUsed(BattleSide side) const { (void)side; return false; }
	virtual void consumeArmorerLastStand(BattleSide side)
	{
		(void)side;
		throw std::logic_error("Battle state does not support Armorer Last Stand updates");
	}
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
	virtual int32_t getMoraleExtraDamagePercent() const { return 100; }
};

class DLL_LINKAGE IBattleState : public IBattleInfo
{
public:
	virtual void nextRound() = 0;
	virtual void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) = 0;
	virtual void setSeizeInitiativeState(const SeizeInitiativeState &) {}
	virtual void setPreCombatOrderState(BattleSide side, const PreCombatOrderState & state)
	{
		(void)side;
		(void)state;
	}
	virtual void setDeploymentState(const BattleDeploymentState & state)
	{
		(void)state;
		throw std::runtime_error("Battle state does not support deployment phase updates");
	}

	virtual void addUnit(uint32_t id, const JsonNode & data) = 0;
	virtual void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) = 0;
	virtual void moveUnit(uint32_t id, const BattleHex & destination) = 0;
	virtual void removeUnit(uint32_t id) = 0;

	virtual void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;
	virtual battle::BattleEffectSnapshot captureBattleEffects(uint32_t id) const
	{
		throw std::runtime_error("Battle state does not support exact spell-effect snapshots");
	}
	virtual void exchangeBattleEffects(const battle::BattleEffectExchange & exchange)
	{
		throw std::runtime_error("Battle state does not support atomic spell-effect exchange");
	}
	virtual void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;
	virtual void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) = 0;

	virtual void setWallState(EWallPart partOfWall, EWallState state) = 0;
	virtual void setWallStructuralHP(EWallPart partOfWall, int32_t hp) { (void)partOfWall; (void)hp; }

	virtual void addObstacle(const ObstacleChanges & changes) = 0;
	virtual void updateObstacle(const ObstacleChanges & changes) = 0;
	virtual void removeObstacle(uint32_t id) = 0;

	/// Applies an authoritative full snapshot of transient canonical Order state.
	virtual void setHeroOrderStates(BattleSide, const std::vector<HeroOrderState> &) {}
	/// Updates one command in place without replacing its sibling Orders.
	/// A null state clears the collection; issuance authorization is separate.
	virtual void setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
	{
		if(!state)
		{
			setHeroOrderStates(side, {});
			return;
		}
		state->validateShape();
		auto orders = getHeroOrderStates(side);
		const auto existing = std::find_if(orders.begin(), orders.end(), [&state](const HeroOrderState & order)
		{
			return order.command == state->command;
		});
		if(existing == orders.end())
			orders.push_back(*state);
		else
			*existing = *state;
		setHeroOrderStates(side, orders);
	}
	/// Applies an authoritative Double Command phase snapshot.
	virtual void setDoubleCommandState(BattleSide, const DoubleCommandState &) {}
	virtual void setRelentlessAssaultState(BattleSide, const RelentlessAssaultState &) {}
	virtual void recordRelentlessAssaultAttack(BattleSide, uint32_t) {}
	virtual void setAdverseCombatRerollState(BattleSide, const AdverseCombatRerollState &) {}
	virtual void setMoraleSuppressionState(BattleSide, const MoraleSuppressionState &) {}
	virtual void setRapidResponseState(BattleSide, const RapidResponseState &) {}
	virtual void setReducedExtraActivationState(BattleSide, const ReducedExtraActivationState &) {}
	virtual void setCrisisCommandState(const newHorizonsCrisisCommand::State &)
	{
		throw std::runtime_error("Battle does not support Crisis Command suspension");
	}
	virtual void setCrossSchoolFormulaState(BattleSide, const newHorizonsCrossSchoolFormula::State &)
	{
		throw std::runtime_error("Battle state cannot record Cross-School Formula");
	}
	virtual void armReactiveWeave(BattleSide, int32_t, int32_t)
	{
		throw std::runtime_error("Reactive Weave state is unsupported by this battle");
	}
	virtual void setSpellResponseState(BattleSide, const SpellResponseState &) {}
	virtual void consumeExtendSpell(BattleSide) { throw std::runtime_error("Battle state cannot consume Extend Spell"); }
	virtual void setOverwhelmingFormulaState(BattleSide, const OverwhelmingFormulaState &) {}
	virtual void setPerfectFortuneState(BattleSide, const PerfectFortuneState &) {}
	virtual void setRebirthChainUsed(BattleSide, bool) {}
	virtual void setPhoenixSparkUsed(BattleSide, bool) {}
	virtual void setLuckSerendipityState(BattleSide, const LuckSerendipityState &) {}
	virtual void setArmorerDefiantState(BattleSide, const ArmorerDefiantState &) {}
	/// Applies the accepted first Wait/Defend Battlefield Mastery award.
	/// Implementations with detached state should update only their own branch.
	virtual void awardBattlecraftMastery(BattleSide, uint32_t, int32_t, BattlecraftMasteryAction) {}
	/// Commits the first accepted Last Stand trigger for a side.
	virtual void consumeArmorerLastStand(BattleSide)
	{
		throw std::runtime_error("Battle state does not support Armorer Last Stand updates");
	}
};
