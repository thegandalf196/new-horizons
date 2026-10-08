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
class JsonNode;

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
	float beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
		uint32_t spentCasterId, DamageCache & cache) const;
	float beneficialCreatureOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell, int spellLevel, float baselinePressure) const;
	static int creatureAbilitySpellLevel(const CStack * caster, const CSpell * spell, int abilityLevel);

public:
	BattleAction selectStackAction(const CStack * stack);
	bool attemptCastingSpell(const CStack * stack, bool allowSpells = true);
	bool canCastSpell();
	std::optional<PossibleSpellcast> findBestCreatureSpell(const CStack * stack);
	/// Deterministic full-pool forecast; does not choose the eventual random spell.
	float expectedBeneficialCreatureSpellValue(const CStack * caster, const battle::Unit * recipient) const;
	/// One detached outcome in the same forecast scale, exposed for deterministic parity tests.
	float beneficialCreatureSpellOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell) const;
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
	/// Shared projected value for Doom's reduced future attacks and Morale activations.
	static float estimateProjectedDoomTargetValue(const battle::Unit * original,
		const battle::Unit * projected, DamageCache & damageCache,
		const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Saved-profile candidate gate shared by Doom discovery and focused AI tests.
	static bool canonicalDoomAvailableInSavedRules(const JsonNode & magicRules, const CSpell * spell);
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
