/*
 * BattleAI.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include <atomic>
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/callback/CGameInterface.h"
#include "AttackPossibility.h"
#include "PossibleSpellcast.h"
#include "PotentialTargets.h"
#include "TacticsHandler.h"
#include "StackWithBonuses.h"
#include "HypotheticBattleTypes.h"
#include "VinesCandidateValueReuse.h"

class CSpell;
class CBattleInfoCallback;
class BattleEvaluator;
namespace heroActionSelection { class Observer; }

/*
struct CurrentOffensivePotential
{
	std::map<const CStack *, PotentialTargets> ourAttacks;
	std::map<const CStack *, PotentialTargets> enemyAttacks;

	CurrentOffensivePotential(BattleSide side)
	{
		for(auto stack : cbc->battleGetStacks())
		{
			if(stack->unitSide() == side)
				ourAttacks[stack] = PotentialTargets(stack);
			else
				enemyAttacks[stack] = PotentialTargets(stack);
		}
	}

	int potentialValue()
	{
		int ourPotential = 0, enemyPotential = 0;
		for(auto &p : ourAttacks)
			ourPotential += p.second.bestAction().attackValue();

		for(auto &p : enemyAttacks)
			enemyPotential += p.second.bestAction().attackValue();

		return ourPotential - enemyPotential;
	}
};
*/ // These lines may be useful but they are't used in the code.

class CBattleAI : public CBattleGameInterface
{
	struct TimingState;
	class TimingScope;
	static std::shared_ptr<TimingState> makeTimingState(BattleID battleID, PlayerColor player);
	std::atomic<std::shared_ptr<TimingState>> timingState;
	// Optional test-driver-owned value observers; never preferences or game state.
	heroActionSelection::Observer * heroValueObserver;
	VinesCandidateValueReuse::TestControl * vinesReuseControl;
	BattleEvaluator makeEvaluator(const BattleID & battleID, const CStack * stack);

	BattleSide side;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;

	//Previous setting of cb
	bool wasWaitingForRealize;
	int movesSkippedByDefense;

	std::unique_ptr<TacticsHandler> tacticsHandler;

public:
	struct StageTiming
	{
		uint64_t calls = 0;
		uint64_t totalMicroseconds = 0;
	};

	/// Diagnostic callback wall time only; not CPU time or authoritative action latency.
	struct TimingSummary
	{
		uint64_t battleWallMicroseconds = 0;
		uint64_t activeStackCalls = 0;
		uint64_t activeStackTotalMicroseconds = 0;
		uint64_t activeStackMaxMicroseconds = 0;
		StageTiming evaluatorConstruction;
		StageTiming stackActionSelection;
		/// Disjoint subsets of stackActionSelection, not additional callback time.
		StageTiming selectionCreatureSpell;
		StageTiming selectionMovementSearch;
		/// Disjoint subsets of selectionMovementSearch, plus wrapper overhead.
		StageTiming movementFindMove;
		StageTiming movementGoTowards;
		uint64_t movementRemainderMicroseconds = 0;
		/// Nested, nonadditive subsets of their corresponding movement functions.
		StageTiming movementFindMoveExchangeSubset;
		/// Disjoint regions inside the measured findMove calculateExchange body.
		StageTiming movementExchangeBody;
		StageTiming movementExchangeMembership;
		StageTiming movementExchangeProjectionSetup;
		StageTiming movementExchangeTargetProbes;
		/// Nested target-probe subsets, excluded from body closure.
		StageTiming movementExchangeTargetProbePreparation;
		StageTiming movementExchangeTargetProbeMainDamage;
		StageTiming movementExchangeTargetProbeDpsScore;
		DamageCacheProbeTiming movementExchangeProbeDamageCache;
		StageTiming movementExchangeAppliedAttacks;
		StageTiming movementExchangeActivationStart;
		StageTiming movementExchangeActivationEnd;
		StageTiming movementExchangeRoundAdvance;
		/// Nested lifecycle subsets; excluded from disjoint exchange body closure.
		StageTiming movementExchangeActivationExpiryRemoval;
		StageTiming movementExchangeActivationGamblerRemoval;
		StageTiming movementExchangeRoundTimedUpdates;
		HypotheticLifecycleTiming::EffectWorkTiming movementExchangeActivationExpiryWork;
		HypotheticLifecycleTiming::EffectWorkTiming movementExchangeActivationGamblerWork;
		HypotheticLifecycleTiming::EffectWorkTiming movementExchangeRoundTimedWork;
		uint64_t movementExchangeRemainderMicroseconds = 0;
		StageTiming movementGoBlockingSubset;
		StageTiming selectionTargetExchange;
		/// Disjoint partitions of selectionTargetExchange, not extra callback time.
		StageTiming selectionBestTarget;
		/// Candidate exchange wrappers are subsets of selectionBestTarget.
		StageTiming bestTargetWaitedExchange;
		StageTiming bestTargetWaitedBody;
		StageTiming bestTargetWaitedMembership;
		StageTiming bestTargetWaitedProjectionSetup;
		StageTiming bestTargetWaitedTargetProbes;
		StageTiming bestTargetWaitedTargetProbePreparation;
		StageTiming bestTargetWaitedTargetProbeMainDamage;
		StageTiming bestTargetWaitedTargetProbeDpsScore;
		DamageCacheProbeTiming bestTargetWaitedProbeDamageCache;
		StageTiming bestTargetWaitedAppliedAttacks;
		StageTiming bestTargetWaitedActivationStart;
		StageTiming bestTargetWaitedActivationEnd;
		StageTiming bestTargetWaitedRoundAdvance;
		StageTiming bestTargetWaitedActivationExpiryRemoval;
		StageTiming bestTargetWaitedActivationGamblerRemoval;
		StageTiming bestTargetWaitedRoundTimedUpdates;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetWaitedActivationExpiryWork;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetWaitedActivationGamblerWork;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetWaitedRoundTimedWork;
		uint64_t bestTargetWaitedRemainderMicroseconds = 0;
		StageTiming bestTargetNormalExchange;
		StageTiming bestTargetNormalBody;
		StageTiming bestTargetNormalMembership;
		StageTiming bestTargetNormalProjectionSetup;
		StageTiming bestTargetNormalTargetProbes;
		StageTiming bestTargetNormalTargetProbePreparation;
		StageTiming bestTargetNormalTargetProbeMainDamage;
		StageTiming bestTargetNormalTargetProbeDpsScore;
		DamageCacheProbeTiming bestTargetNormalProbeDamageCache;
		StageTiming bestTargetNormalAppliedAttacks;
		StageTiming bestTargetNormalActivationStart;
		StageTiming bestTargetNormalActivationEnd;
		StageTiming bestTargetNormalRoundAdvance;
		StageTiming bestTargetNormalActivationExpiryRemoval;
		StageTiming bestTargetNormalActivationGamblerRemoval;
		StageTiming bestTargetNormalRoundTimedUpdates;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetNormalActivationExpiryWork;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetNormalActivationGamblerWork;
		HypotheticLifecycleTiming::EffectWorkTiming bestTargetNormalRoundTimedWork;
		uint64_t bestTargetNormalRemainderMicroseconds = 0;
		/// Clone/reachability/filter/comparison and wrapper overhead.
		uint64_t bestTargetRemainderMicroseconds = 0;

		StageTiming selectionSiegeDefense;
		StageTiming selectionDefensiveStance;
		uint64_t selectionRemainderMicroseconds = 0;
		/// Includes Hero Action evaluation and any submission performed inside it.
		StageTiming heroAction;
		StageTiming directUnitSubmission;
		uint64_t unclassifiedMicroseconds = 0;
		uint32_t roundsObserved = 0;
		uint32_t activeStackInFlight = 0;
		bool finished = false;
		bool reported = false;
	};

	std::optional<TimingSummary> getTimingSummary() const;

	explicit CBattleAI(heroActionSelection::Observer * observer = nullptr,
		VinesCandidateValueReuse::TestControl * vinesReuse = nullptr);
	~CBattleAI();

	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB) override;
	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences) override;

	void activeStack(const BattleID & battleID, const CStack * stack) override; //called when it's turn of that stack
	void yourTacticPhase(const BattleID & battleID, int distance) override;

	std::optional<BattleAction> considerFleeingOrSurrendering(const BattleID & battleID);

	void print(const std::string &text) const;
	BattleAction useCatapult(const BattleID & battleID, const CStack *stack);
	/// Whether a legal creature Catapult shot should replace the ordinary action.
	static bool shouldUseCreatureCatapult(CBattleInfoCallback & battle,
		const CStack * stack, const BattleAction & ordinaryAction, PlayerColor actionController);
	BattleAction useHealingTent(const BattleID & battleID, const CStack *stack);
	/// Chooses the movement-only tail of a Pursuit activation. Exposed for the
	/// deterministic AI regression; callers must submit it through the server.
	static BattleAction choosePursuitMovement(const std::shared_ptr<CBattleInfoCallback> & battle,
		const CStack * source);

	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	void battleEnd(const BattleID & battleID, const BattleResult * result, QueryID queryID) override;
	void battleNewRound(const BattleID & battleID) override;
	void actionFinished(const BattleID & battleID, const BattleAction & action) override;
	//void actionStarted(const BattleAction &action) override;//occurs BEFORE every action taken by any stack or by the hero
	//void battleAttack(const BattleAttack *ba) override; //called when stack is performing attack
	//void battleStacksAttacked(const std::vector<BattleStackAttacked> & bsa, bool ranged) override; //called when stack receives damage (after battleAttack())
	//void battleEnd(const BattleResult *br, QueryID queryID) override;
	//void battleResultsApplied() override; //called when all effects of last battle are applied
	//void battleNewRoundFirst(int round) override; //called at the beginning of each turn before changes are applied;
	//void battleNewRound(int round) override; //called at the beginning of each turn, round=-1 is the tactic phase, round=0 is the first "normal" turn
	//void battleStackMoved(const BattleID & battleID, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport) override;
	//void battleSpellCast(const BattleSpellCast *sc) override;
	//void battleStacksEffectsSet(const SetStackEffect & sse) override;//called when a specific effect is set to stacks
	//void battleTriggerEffect(const BattleTriggerEffect & bte) override;
	//void battleStart(const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide side) override; //called by engine when battle starts; side=0 - left, side=1 - right
	//void battleCatapultAttacked(const CatapultAttack & ca) override; //called when catapult makes an attack
	AutocombatPreferences autobattlePreferences = AutocombatPreferences();
};
