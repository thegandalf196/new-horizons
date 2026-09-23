/*
 * StackWithBonuses.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <vstd/RNG.h>
#include <optional>

#include <vcmi/Environment.h>
#include <vcmi/ServerCallback.h>

#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleProxy.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/HeroActionAllowanceState.h"

class HypotheticBattle;
class CSpell;

///Fake random generator, used by AI to evaluate random server behavior
class RNGStub final : public vstd::RNG
{
public:
	int nextInt() override
	{
		return 0;
	}

	int nextBinomialInt(int coinsCount, double coinChance) override
	{
		return coinsCount * coinChance;
	}

	int nextInt(int lower, int upper) override
	{
		return (lower + upper) / 2;
	}
	int64_t nextInt64(int64_t lower, int64_t upper) override
	{
		return (lower + upper) / 2;
	}
	double nextDouble(double lower, double upper) override
	{
		return (lower + upper) / 2;
	}

	int nextInt(int upper) override
	{
		return upper / 2;
	}
	int64_t nextInt64(int64_t upper) override
	{
		return upper / 2;
	}
	double nextDouble(double upper) override
	{
		return upper / 2;
	}
};

class StackWithBonuses : public battle::CUnitState, public virtual IBonusBearer
{
public:
	std::vector<Bonus> bonusesToAdd;
	std::vector<Bonus> bonusesToUpdate;
	std::set<std::shared_ptr<Bonus>> bonusesToRemove;
	int treeVersionLocal;

	StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info);

	virtual ~StackWithBonuses();

	StackWithBonuses & operator= (const battle::CUnitState & other);

	///IUnitInfo
	const CCreature * unitType() const override;

	int32_t unitBaseAmount() const override;

	uint32_t unitId() const override;
	BattleSide unitSide() const override;
	PlayerColor unitOwner() const override;
	SlotID unitSlot() const override;

	///IBonusBearer
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;

	int32_t getTreeVersion() const override;

	void addUnitBonus(const std::vector<Bonus> & bonus);
	void updateUnitBonus(const std::vector<Bonus> & bonus);
	void removeUnitBonus(const std::vector<Bonus> & bonus);

	void removeUnitBonus(const CSelector & selector);
	void advanceTimedRound();

	void spendMana(ServerCallback * server, const int spellCost) const override;
	std::string getDescription() const override;

private:
	// Value snapshots survive nested models whose bonus queries create fresh pointers.
	// Include all spell/command durations for removal; only N_TURNS are aged.
	std::optional<std::vector<Bonus>> projectedEffects;
	void captureEffects();
	const IBonusBearer * origBearer;
	const HypotheticBattle * owner;

	const CCreature * type;
	ui32 baseAmount;
	uint32_t id;
	BattleSide side;
	PlayerColor player;
	SlotID slot;
};

class HypotheticBattle final : public BattleProxy, public battle::IUnitEnvironment
{
public:
	std::map<uint32_t, std::shared_ptr<StackWithBonuses>> stackStates;

	const Environment * env;

	HypotheticBattle(const Environment * ENV, Subject realBattle);

	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
	int unitFortuneSpeed(const battle::Unit * unit) const override { return battleFortuneSpeed(unit); }

	std::shared_ptr<StackWithBonuses> getForUpdate(uint32_t id);

	BattleID getBattleID() const override;
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const override;
	const AlternatingHeroActionState & getWarcastingState(BattleSide side) const override;
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override;
	bool getCounterspellArmed(BattleSide side) const override { return counterspellArmedStates.at(side); }
	int32_t getMetamagicPendingCount(BattleSide side) const override { return metamagicStates.at(side).pending; }
	int32_t getMetamagicUsesConsumed(BattleSide side) const override { return metamagicStates.at(side).uses; }
	bool getMetamagicGrandUsed(BattleSide side) const override { return metamagicStates.at(side).grandUsed; }
	SpellID getMetamagicFirstSpell(BattleSide side) const override { return metamagicStates.at(side).firstSpell; }
	uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const override { return metamagicStates.at(side).firstTarget; }
	const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const override
	{
		return metamagicStates.at(side).sequence;
	}
	bool getMetamagicFirstCounterspellNegated(BattleSide side) const override
	{
		return metamagicStates.at(side).firstCountered;
	}
	bool getMetamagicCountersequenceArmed(BattleSide side) const override
	{
		return countersequenceArmedStates.at(side);
	}
	struct ProjectedActionReceipt
	{
		BattleSide side = BattleSide::NONE;
		HeroActionAllowanceState::Receipt receipt;
		bool typedLedger = true;
		uint64_t epoch = 0;

		bool operator==(const ProjectedActionReceipt &) const = default;

		bool isHeroAction() const
		{
			return receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO;
		}
	};
	struct ProjectedMetamagicSnapshot
	{
		uint8_t uses = 0;
		uint8_t pending = 0;
		bool grandUsed = false;
		SpellID firstSpell;
		uint32_t firstTarget = std::numeric_limits<uint32_t>::max();
		bool firstCountered = false;
		std::vector<SpellID> sequence;

		bool operator==(const ProjectedMetamagicSnapshot &) const = default;
	};
	struct ProjectedSpellAllowance
	{
		ProjectedActionReceipt action;
		HeroActionAllowanceState allowancesBefore;
		HeroActionAllowanceState allowancesAfter;
		ProjectedMetamagicSnapshot metamagicBefore;
		bool metamagicFollowup = false;
		bool grand = false;
		uint8_t usesAfter = 0;
		uint8_t pendingAfter = 0;
		bool grandUsedAfter = false;

		bool operator==(const ProjectedSpellAllowance &) const = default;
	};
	struct ProjectedOrderAllowance
	{
		ProjectedActionReceipt action;
		HeroActionAllowanceState allowancesBefore;
		HeroActionAllowanceState allowancesAfter;
		ProjectedMetamagicSnapshot metamagicBefore;

		bool operator==(const ProjectedOrderAllowance &) const = default;
	};
	struct ProjectedCounterspellOutcome
	{
		BattleSide wardSide = BattleSide::NONE;
		bool wardActive = false;
		bool resolutionKnown = false;
		std::optional<bool> negated;
		std::optional<int> manaCost;
	};
	std::optional<ProjectedSpellAllowance> prepareHeroSpellAllowance(BattleSide side,
		bool metamagicFollowup, bool grand) const;
	std::optional<ProjectedOrderAllowance> prepareHeroOrderAllowance(BattleSide side) const;
	bool beginProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared);
	bool beginProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared);
	bool projectAcceptedHeroSpell(BattleSide side, SpellID spell, uint32_t target,
		bool metamagicFollowup, bool grand, bool counterspellWardActive, bool counterspellNegated,
		const ProjectedSpellAllowance & prepared);
	bool projectAcceptedHeroOrder(BattleSide side, const ProjectedOrderAllowance & prepared);
	ProjectedCounterspellOutcome resolveProjectedCounterspell(BattleSide casterSide, const CSpell * spell) const;
	bool projectHeroSpellAllowance(BattleSide side, SpellID spell, uint32_t target,
		bool metamagicFollowup, bool grand);
	bool projectHeroOrderAllowance(BattleSide side);
	void expireProjectedTimeStops(BattleSide casterSide);
	ui8 getProjectedPendingTimeStopHeroActionSides() const { return pendingTimeStopHeroActionSides; }
	void setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state) override;
	std::optional<FocusFireState> getFocusFireState(BattleSide side) const override;
	void setFocusFireState(BattleSide side, const FocusFireState & state);
	ObstacleCList getAllObstacles() const override;
	bool hasObstacleChanges() const { return obstacleChanges; }
	bool hasWallChanges() const { return wallChanges; }
	EWallState getWallState(EWallPart part) const override;
	int32_t getWallStructuralHP(EWallPart part) const override;
	EGateState getGateState() const override;

	int32_t getActiveStackID() const override;
	int32_t getRound() const override;
	int32_t getBloodrageDamagePercent(BattleSide side) const override;
	SylvanLuckState getSylvanLuckState(BattleSide side) const override { return fortuneStates.at(side); }
	void setSylvanLuckState(BattleSide side, const SylvanLuckState & state) { fortuneStates.at(side) = state; }
	void endFortuneActivation() { for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) fortuneStates.at(side).endActivation(); }
	LuckRollRules getLuckRollRules() const override { return fortuneRollRules; }

	/// Apply a definitely lucky/unlucky strike to this model.  AI damage
	/// evaluation is deliberately probabilistic and side-effect free; callers
	/// invoke this only after committing a selected projected strike.
	bool fortuneStrikeIsCertain(const BattleAttackInfo & attack) const;
	void projectFortuneStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits,
		battle::CUnitState * attackerState, bool enemyStackKilled);

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	void nextRound() override;
	void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) override;

	void addUnit(uint32_t id, const JsonNode & data) override;
	void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) override;
	void moveUnit(uint32_t id, const BattleHex & destination) override;
	void removeUnit(uint32_t id) override;
	void recordBloodrageTransition(const std::shared_ptr<StackWithBonuses> & unit, bool wasAlive);

	void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;

	void setWallState(EWallPart partOfWall, EWallState state) override;
	void setWallStructuralHP(EWallPart partOfWall, int32_t hp) override;

	void addObstacle(const ObstacleChanges & changes) override;
	void updateObstacle(const ObstacleChanges& changes) override;
	void removeObstacle(uint32_t id) override;

	uint32_t nextUnitId() const override;

	int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override;
	std::vector<SpellID> getUsedSpells(BattleSide side) const override;
	int3 getLocation() const override;
	BattleLayout getLayout() const override;

	int32_t getTreeVersion() const;

	void makeWait(const battle::Unit * activeStack);

	void resetActiveUnit()
	{
		activeUnitId = -1;
	}

	ServerCallback * getServerCallback();
	const scripting::Pool & getScriptContextPool() const override;

private:
	BattleSideArray<std::optional<HeroOrderState>> heroOrderStates;
	BattleSideArray<AlternatingHeroActionState> warcastingStates;
	BattleSideArray<HeroActionAllowanceState> heroActionAllowances;
	BattleSideArray<bool> counterspellArmedStates;
	BattleSideArray<bool> countersequenceArmedStates;
	ui8 pendingTimeStopHeroActionSides = 0;
	using ProjectedMetamagic = ProjectedMetamagicSnapshot;
	BattleSideArray<ProjectedMetamagic> metamagicStates;
	BattleSideArray<uint64_t> projectedActionEpochs{};
	BattleSideArray<std::optional<ProjectedSpellAllowance>> begunProjectedSpellAllowances{};
	BattleSideArray<std::optional<ProjectedOrderAllowance>> begunProjectedOrderAllowances{};
	bool isCurrentPreparedSpellAction(BattleSide side, const ProjectedSpellAllowance & prepared,
		bool requireBegun) const;
	bool isCurrentPreparedOrderAction(BattleSide side, const ProjectedOrderAllowance & prepared,
		bool requireBegun) const;
	void beginProjectedHeroAction(BattleSide side, const ProjectedActionReceipt & receipt);
	void finishProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared);
	void finishProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared);
	std::map<BattleSide, std::optional<FocusFireState>> focusFireStates;
	BattleSideArray<int32_t> bloodrageRanks;
	BattleSideArray<int32_t> bloodrageDamagePercents;
	std::set<uint32_t> bloodrageDestroyedUnits;
	BattleSideArray<SylvanLuckState> fortuneStates;
	LuckRollRules fortuneRollRules;

	class HypotheticServerCallback : public ServerCallback
	{
	public:
		HypotheticServerCallback(HypotheticBattle * owner_);

		void complain(const std::string & problem) override;
		bool describeChanges() const override;

		vstd::RNG * getRNG() override;
		bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) override;

		void apply(CPackForClient & pack) override;

		void apply(BattleLogMessage & pack) override;
		void apply(BattleStackMoved & pack) override;
		void apply(BattleUnitsChanged & pack) override;
		void apply(SetStackEffect & pack) override;
		void apply(StacksInjured & pack) override;
		void apply(BattleObstaclesChanged & pack) override;
		void apply(CatapultAttack & pack) override;
	private:
		HypotheticBattle * owner;
		RNGStub rngStub;
	};

	class HypotheticEnvironment : public Environment
	{
	public:
		HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment);

		const Services * services() const override;
		const BattleCb * battle(const BattleID & battleID) const override;
		const GameCb * game() const override;

	private:
		HypotheticBattle * owner;
		const Environment * env;
	};

	int32_t bonusTreeVersion;
	int32_t activeUnitId;
	int32_t projectedRound;
	ObstacleCList projectedObstacles;
	bool obstacleChanges = false;
	std::map<EWallPart, EWallState> projectedWalls;
	std::map<EWallPart, int32_t> projectedStructuralHP;
	bool canonicalStructuralHP = false;
	EGateState initialGateState = EGateState::NONE;
	bool wallChanges = false;
	mutable uint32_t nextId;

	std::unique_ptr<HypotheticServerCallback> serverCallback;
	std::unique_ptr<HypotheticEnvironment> localEnvironment;
};
