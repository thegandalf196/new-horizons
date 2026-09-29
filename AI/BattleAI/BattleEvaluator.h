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

class CSpell;
class CBattleCallback;
class BattleAction;

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
	CachedAttack cachedAttack;
	PlayerColor playerID;
	BattleID battleID;
	BattleSide side;
	DamageCache damageCache;
	float strengthRatio;
	int simulationTurnsCount;

public:
	BattleAction selectStackAction(const CStack * stack);
	bool attemptCastingSpell(const CStack * stack, bool allowSpells = true);
	bool canCastSpell();
	std::optional<PossibleSpellcast> findBestCreatureSpell(const CStack * stack);
	BattleAction goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets);
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
	void print(const std::string & text) const;
	BattleAction moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets);

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
