/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/HeroCommand.h"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
class CBattleInfoCallback;
class Environment;
class HypotheticBattle;
class DamageCache;
class BattleExchangeEvaluator;
class BattleInfo;
class PossibleSpellcast;
namespace battle { class Unit; }

namespace heroDecisionPressure
{
/// Experimental AI policy only. Caller supplies existing accepted approach facts;
/// this component never discovers a route or claims the displacement is legal.
struct Approach
{
	uint32_t attacker;
	uint32_t victim;
	int voluntaryDistance;
	bool shooting;
};
struct Ranked
{
	size_t ordinal;
	float marginal;
};
/// At most two near-tied positive known estimates, in original ordinal order.
/// Unsupported candidates are NOT ranked here; caller retains its exact fallback.
std::vector<size_t> ambiguousRefinements(const std::vector<Ranked> & ranked);
/// Ranking only; final alternatives MUST use the existing full scorer.
std::vector<size_t> promisingRefinements(const std::vector<Ranked> & ranked);
struct Experiment
{
	bool enableShortlist = true;
	std::vector<Ranked> cheapScores;
	std::vector<size_t> refinedOrdinals;
	std::vector<bool> declined;
	size_t baselineAttempts = 0;
	bool baselineReady = false;
	size_t continuationCalls = 0;
	size_t refinementContinuationCalls = 0;
	void reset();
	void noteContinuation(size_t ordinal);
};
/// Optional call-local experiment, not a serialized/default policy switch.
/// Only fixed-opportunity current Focus Fire/Riposte enter the shortlist.
void prepareShortlist(Experiment & result, const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
	const std::vector<PossibleSpellcast> & candidates, const DamageCache & originalDamage,
	const BattleExchangeEvaluator & utility);


/// One visible, detached reference per Hero decision. No authoritative mutation,
/// RNG draw, movement search, action submission or serialized policy state.
/// The optional original cache borrows the SAME decision's established original-
/// damage role and must outlive this object; no current-damage result is reused.
class Baseline
{
public:
	static std::unique_ptr<Baseline> create(const Environment * environment,
		const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
		const std::optional<std::vector<Approach>> & completeExistingApproaches = std::nullopt,
		bool directOnly = false, const DamageCache * originalDamage = nullptr,
		const BattleExchangeEvaluator * utility = nullptr);
	~Baseline();
	/// Caller MUST have completed normal HeroAction admission AND accepted native
	/// projection. Marginal damage-reduction utility, never raw HP or absolute
	/// continuation. Absence retains the existing full evaluator.
	std::optional<float> orderUtility(const std::shared_ptr<HypotheticBattle> & accepted,
		HeroCommand command);
private:
	struct Pair
	{
		uint32_t attacker;
		uint32_t victim;
		bool shooting;
		int voluntaryDistance;
		float referenceValue;
	};
	Baseline(const Environment * environment,
		std::shared_ptr<CBattleInfoCallback> source, BattleSide side, const DamageCache * originalDamage,
		const BattleExchangeEvaluator * utility);
	bool prepare(const std::vector<Approach> & existingApproaches, bool directOnly, bool buildOriginal);
	float pairValue(const CBattleInfoCallback & view, const Pair & pair,
		bool grantedActivation = false);
	float pressure(const CBattleInfoCallback & view);
	std::optional<float> extraActivationUtility(const HypotheticBattle & accepted);
	float damageValue(uint32_t attacker, uint32_t victim, int64_t damage);
	std::shared_ptr<CBattleInfoCallback> source;
	std::shared_ptr<HypotheticBattle> reference;
	std::unique_ptr<DamageCache> damage;
	const BattleInfo * nativeWorld = nullptr;
	BattleSide side;
	std::vector<Pair> pairs;
	float referencePressure = 0;
	bool hasApproachFacts = false;
	bool directTopology = true;
	float positiveMultiplier = 1;
	float negativeMultiplier = 1;
};
}
