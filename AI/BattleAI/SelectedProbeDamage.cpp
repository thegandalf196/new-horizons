/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SelectedProbeDamage.h"
#include "BattleExchangeVariant.h"
#include "DamageCacheAdmission.h"
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
	const StackWithBonuses & defender, bool shot, int64_t damage)
{
	source = &battle;
	actor = &attacker;
	target = &defender;
	shooting = shot;
	raw = damage;
}

const IDamageCalculatorScript * SelectedProbeDamage::admit() const
{
	// First ordinary lookup/bootstrap already completed; never move this before it.
	return raw && source ? supported(*source) : nullptr;
}

std::optional<int64_t> SelectedProbeDamage::consume(const HypotheticBattle & battle,
	const StackWithBonuses & attacker, const StackWithBonuses & defender, bool shot)
{
	const auto value = std::exchange(raw, std::nullopt);
	if(!value || source != &battle || actor != &attacker || target != &defender || shooting != shot
		|| !calculator || supported(battle) != calculator)
		return {};
	return value;
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
		mainProbeAdmission, actionResult, manaLeechSequenceSpent, bloodSequence, nullptr, nullptr, nullptr);
}

const battle::Unit * BattleExchangeVariant::selectExchangeTarget(const battle::Units & candidates,
	const std::shared_ptr<StackWithBonuses> & attacker, bool shooting, bool isOur,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & battle,
	const BattleTargetProbeTiming * timing, SelectedProbeDamage & selected, uint64_t * offers)
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
				&receipt, nullptr, nullptr);
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
	if(offers && selected.raw && admitted)
		++*offers;
	return best;
}
