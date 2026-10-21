/*
 * BattleEvaluator.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "../../lib/battle/ReachabilityInfo.h"
#include "PossibleSpellcast.h"
#include "PotentialTargets.h"
#include "BattleExchangeVariant.h"
#include "VinesCandidateValueReuse.h"
#include <utility>
#include "HypotheticBattle.h"

class CSpell;
class CBattleCallback;
class BattleAction;
class JsonNode;
class CPlayerBattleCallback;
class CGHeroInstance;
struct DivineMandateStatus;
namespace heroActionTiming { struct Summary; }
namespace heroActionSelection { class Observer; }

struct CachedAttack
{
	std::optional<AttackPossibility> ap;
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	uint8_t turn = 255;
	bool waited = false;
};

class DLL_EXPORT BattleEvaluator
{
	std::unique_ptr<PotentialTargets> targets;
	std::shared_ptr<HypotheticBattle> hb;
	BattleExchangeEvaluator scoreEvaluator;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	bool activeActionMade = false;
	bool submitHeroAction(const PossibleSpellcast & castToPerform, heroActionTiming::Summary * timing);
	CachedAttack cachedAttack;
	PlayerColor playerID;
	BattleID battleID;
	BattleSide side;
	DamageCache damageCache;
	float strengthRatio;
	int simulationTurnsCount;
	struct SpellOption
	{
		const CSpell * spell = nullptr;
		bool metamagicFollowup = false;
		bool divineMandateFollowup = false;
		bool metamagicGrand = false;
	};
	std::vector<SpellOption> collectCastableHeroSpellOptions(
		const std::shared_ptr<CPlayerBattleCallback> & battleView, const CGHeroInstance * hero,
		bool allowSpells, bool mandatoryOrder, bool legacyMetamagicFollowup,
		const DivineMandateStatus & divineMandateStatus) const;
	bool evaluateHeroActionQueue(PossibleSpellcast::ValueMap & values,
		const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state,
		size_t minTurnSpan, bool * enemyHadTurnOut);
	void projectHeroActionQueueActivation(PossibleSpellcast::ValueMap & values,
		const std::shared_ptr<HypotheticBattle> & state, const battle::Unit * unit,
		battle::Units & remainingQueue, size_t queueIndex);
	float beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
		uint32_t spentCasterId, DamageCache & cache) const;
	float beneficialCreatureOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell, int spellLevel, float baselinePressure) const;
	/// Leaves cast-only admission/value in ps; returns HP and future-DPS completion values.
	std::pair<float, float> evaluateCreatureCastBeforeCompletion(const CStack * stack, PossibleSpellcast & ps);

public:
	/// Effective chosen-spell proficiency, matching authoritative creature-cast resolution.
	static int creatureAbilitySpellLevel(const CStack * caster, const CSpell * spell, int abilityLevel);
	/// Optional call-local wall diagnostics. Not gameplay state and never serialized.
	struct DLL_LINKAGE SelectionTiming
	{
		struct DLL_LINKAGE StageTiming
		{
			uint64_t calls = 0;
			uint64_t totalMicroseconds = 0;
		};
		StageTiming creatureSpell;
		StageTiming movementSearch;
		BattleMovementTiming movementDetails;
		/// Counts measured call sites, not the number of candidate exchanges.
		StageTiming targetExchange;
		/// Disjoint call-site partitions sharing the same elapsed sample as targetExchange.
		StageTiming bestTarget;
		BattleBestTargetTiming bestTargetDetails;
		StageTiming siegeDefense;
		StageTiming defensiveStance;
	};

	BattleAction selectStackAction(const CStack * stack, SelectionTiming * timing = nullptr);
	bool attemptCastingSpell(const CStack * stack, bool allowSpells = true,
		heroActionSelection::Observer * observer = nullptr,
		VinesCandidateValueReuse::TestControl * vinesReuse = nullptr);
	bool canCastSpell();
	std::optional<PossibleSpellcast> findBestCreatureSpell(const CStack * stack);
	/// Deterministic full-pool forecast; does not choose the eventual random spell.
	float expectedBeneficialCreatureSpellValue(const CStack * caster, const battle::Unit * recipient) const;
	/// One detached outcome in the same forecast scale, exposed for deterministic parity tests.
	float beneficialCreatureSpellOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell) const;
	BattleAction goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets,
		BattleMovementTiming * timing = nullptr, std::pair<float, float> * movementCompletionValue = nullptr);
	std::vector<BattleHex> getBrokenWallMoatHexes() const;
	bool hasWorkingTowers() const;
	void evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps); //for offensive damaging spells only
	/// Shared projected value for one Sorrow target; public for deterministic AI-focused tests.
	static float estimateProjectedSorrowTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for one Curse target; public for deterministic AI-focused tests.
	static float estimateProjectedCurseTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for Frailty's bounded physical-damage increase on one target.
	static float estimateProjectedFrailtyTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for one Hex of Pain target's reduced future attack value.
	static float estimateProjectedHexOfPainTargetValue(const battle::Unit * original,
		const battle::Unit * projected, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for Doom's reduced future attacks and Morale activations.
	static float estimateProjectedDoomTargetValue(const battle::Unit * original,
		const battle::Unit * projected, DamageCache & damageCache,
		const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Saved-profile candidate gate shared by Doom discovery and focused AI tests.
	static bool canonicalDoomAvailableInSavedRules(const JsonNode & magicRules, const CSpell * spell);
	void print(const std::string & text) const;
	BattleAction moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets,
		std::pair<float, float> * movementCompletionValue = nullptr);
	struct DLL_LINKAGE MovementCompletionProjection
	{
		std::shared_ptr<HypotheticBattle> battle;
		std::pair<float, float> value{};
		bool arrivedAlive = false;
	};
	/// Explicit ordinary MOVE only; clones the parent and scores its real completion receipts.
	static MovementCompletionProjection projectOrdinaryMoveCompletion(const Environment * environment,
		const std::shared_ptr<HypotheticBattle> & parent, const BattleAction & action, PlayerColor perspective);

	BattleEvaluator(
		std::shared_ptr<Environment> env,
		std::shared_ptr<CBattleCallback> cb,
		const battle::Unit * activeStack,
		PlayerColor playerID,
		BattleID battleID,
		BattleSide side,
		float strengthRatio,
		int simulationTurnsCount);

	BattleEvaluator(
		std::shared_ptr<Environment> env,
		std::shared_ptr<CBattleCallback> cb,
		std::shared_ptr<HypotheticBattle> hb,
		DamageCache & damageCache,
		const battle::Unit * activeStack,
		PlayerColor playerID,
		BattleID battleID,
		BattleSide side,
		float strengthRatio,
		int simulationTurnsCount);
};
