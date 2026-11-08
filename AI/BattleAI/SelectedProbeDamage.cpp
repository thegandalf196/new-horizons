/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SelectedProbeDamage.h"
#include "BattleExchangeVariant.h"
#include "DamageCacheAdmission.h"
#include "HeroActionAdmission.h"
#include "VinesCandidateValueReuse.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/scripting/ScriptService.h"
#include <chrono>
#include <utility>

namespace
{
const IDamageCalculatorScript * supported(const HypotheticBattle & state)
{
	try
	{
		if(state.hasCanonicalDamageQuerySources()
			&& heroCommands::supportedByRules(state.getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE)
			&& VinesCandidateValueReuse::scoringScriptsSupported(state))
			return LIBRARY->scriptTypes()->getDamageCalculator();
	}
	catch(...) {} // Optional admission must not add a capability-query failure.
	return nullptr;
}

class ProbeScope
{
	BattleMovementTiming::StageTiming * stage;
	std::chrono::steady_clock::time_point started;
public:
	explicit ProbeScope(BattleMovementTiming::StageTiming * stage)
		: stage(stage), started(stage ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
	~ProbeScope()
	{
		if(stage)
		{
			++stage->calls;
			stage->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - started).count();
		}
	}
};
}

void SelectedProbeDamage::record(const HypotheticBattle & battle, const StackWithBonuses & attacker,
	const StackWithBonuses & defender, bool shot, int64_t damage, uint64_t healthLoss,
	float damageReduce, const DamageCache & cache)
{
	source = &battle;
	actor = &attacker;
	target = &defender;
	shooting = shot;
	value = Value{damage, healthLoss, damageReduce, &cache};
}

const IDamageCalculatorScript * SelectedProbeDamage::admit() const
{
	// First ordinary lookup/bootstrap already completed; never move this before it.
	return value && source ? supported(*source) : nullptr;
}

bool SelectedProbeDamage::admitScore() const
{
	if(!value || !calculator || !source || !actor || !target)
		return false;
	try
	{
		// Health/DPS getters need the full source graph proof, beyond the older
		// raw-receipt capability check. Certify only the selected completed probe,
		// once in this immutable batch; unsupported getter sources keep full scoring.
		heroActionProjection::CanonicalSourceAdmission admission(*actor, *source);
		return admission.admits(*target);
	}
	catch(...) { return false; }
}

std::optional<SelectedProbeDamage::Value> SelectedProbeDamage::consume(const HypotheticBattle & battle,
	const StackWithBonuses & attacker, const StackWithBonuses & defender, bool shot)
{
	const auto retained = std::exchange(value, std::nullopt);
	if(!retained || source != &battle || actor != &attacker || target != &defender || shooting != shot
		|| !calculator || supported(battle) != calculator)
		return {};
	return retained;
}

SelectedProbeDamage::Evaluation::Evaluation(const std::shared_ptr<StackWithBonuses> & attacker,
	const std::shared_ptr<StackWithBonuses> & defender,
	const std::shared_ptr<HypotheticBattle> & battle, DamageCache & cache,
	bool shooting, bool evaluateOnly, bool protectIntercepted,
	const BattleTargetProbeTiming * timing, DamageCacheAdmission * admission,
	SelectedProbeDamage * produced, SelectedProbeDamage * selected,
	uint64_t * damageUses, uint64_t * scoreUses)
	: attacker(attacker), defender(defender), battle(battle), cache(cache), shooting(shooting),
	  evaluateOnly(evaluateOnly), protectIntercepted(protectIntercepted), timing(timing), admission(admission),
	  produced(produced), selected(selected), damageUses(damageUses), scoreUses(scoreUses)
{}

int64_t SelectedProbeDamage::Evaluation::damage()
{
	ProbeScope measurement(evaluateOnly && timing ? timing->mainDamage : nullptr);
	// Redirected Protect spends interception state before this point: never reuse it.
	retained = !evaluateOnly && !protectIntercepted && selected
		? selected->consume(*battle, *attacker, *defender, shooting) : std::nullopt;
	if(retained)
	{
		rawDamage = retained->raw;
		if(damageUses) ++*damageUses;
	}
	else
		rawDamage = cache.getDamage(attacker.get(), defender.get(), battle,
			evaluateOnly && timing ? timing->damageCache : nullptr, evaluateOnly ? admission : nullptr);
	return rawDamage;
}

float SelectedProbeDamage::Evaluation::damageReduce(uint64_t healthLoss)
{
	ProbeScope measurement(evaluateOnly && timing ? timing->dpsScore : nullptr);
	float score;
	// This private one-use receipt certifies the unchanged selector-to-first-hit
	// interval. Its probe completed original-damage tracking; Focus Fire support
	// forces fallback lookups to bypass numerical pair caching. Later target
	// probes can only add idempotent tracking markers to this cache.
	// The caller still resolves Last Stand and projects health loss live.
	if(retained && selected->reuseScore && retained->cache == &cache && retained->healthLoss == healthLoss)
	{
		score = retained->damageReduce;
		if(scoreUses) ++*scoreUses;
	}
	else
		score = AttackPossibility::calculateDamageReduce(attacker.get(), defender.get(), healthLoss,
			cache, battle, evaluateOnly && timing ? timing->damageCache : nullptr);
	if(evaluateOnly && !protectIntercepted && produced)
		produced->record(*battle, *attacker, *defender, shooting, rawDamage, healthLoss, score, cache);
	return score;
}

float BattleExchangeVariant::trackAttack(std::shared_ptr<StackWithBonuses> attacker,
	std::shared_ptr<StackWithBonuses> defender, bool shooting, bool isOurAttack,
	DamageCache & damageCache, std::shared_ptr<HypotheticBattle> hb, bool evaluateOnly,
	bool allowRetaliation, std::optional<bool> * readyOverwatch,
	const BattleTargetProbeTiming * probeTiming, DamageCacheAdmission * mainProbeAdmission,
	BattleExchangeAttackResult * actionResult, bool * manaLeechSequenceSpent,
	std::vector<petrifyingBloodAI::Pending> * bloodSequence)
{
	return trackAttackWithSelectedDamage(std::move(attacker), std::move(defender), shooting, isOurAttack,
		damageCache, std::move(hb), evaluateOnly, allowRetaliation, readyOverwatch, probeTiming,
		mainProbeAdmission, actionResult, manaLeechSequenceSpent, bloodSequence, nullptr, nullptr, nullptr, nullptr);
}

const battle::Unit * BattleExchangeVariant::selectExchangeTarget(const battle::Units & candidates,
	const std::shared_ptr<StackWithBonuses> & attacker, bool shooting, bool isOur,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & battle,
	const BattleTargetProbeTiming * timing, SelectedProbeDamage & selected, uint64_t * offers, bool reuseScore)
{
	selected = {};
	if(candidates.empty())
		return nullptr;
	const auto * best = candidates.front();
	// Preserve maxElementByFun's singleton/no-comparison behavior.
	if(candidates.size() == 1)
		return best;
	std::optional<bool> readyOverwatch;
	DamageCacheAdmission admission(*attacker, *battle);
	const auto estimate = [&](const battle::Unit * unit, SelectedProbeDamage & receipt)
	{
		auto defender = battle->getForUpdate(unit->unitId());
		const auto score = [&]()
		{
			ProbeScope measurement(timing ? timing->wholeProbe : nullptr);
			return trackAttackWithSelectedDamage(attacker, defender, shooting, isOur, damageCache,
				battle, true, true, &readyOverwatch, timing, &admission, nullptr, nullptr, nullptr,
				&receipt, nullptr, nullptr, nullptr);
		}();
#if BATTLE_TRACE_LEVEL >= 2
		logAi->trace("Best target selector %s->%s score = %2f", attacker->getDescription(), defender->getDescription(), score);
#endif
		return score;
	};
	float bestScore = estimate(best, selected);
	const auto * admitted = selected.admit();
	for(size_t index = 1; index < candidates.size(); ++index)
	{
		SelectedProbeDamage candidate;
		const float candidateScore = estimate(candidates[index], candidate);
		if(candidateScore > bestScore)
		{
			best = candidates[index];
			bestScore = candidateScore;
			selected = candidate; // Keep the winning receipt, never the last losing probe.
		}
	}
	selected.calculator = admitted;
	selected.reuseScore = reuseScore && selected.admitScore();
	if(offers && selected.value && admitted)
		++*offers;
	return best;
}
