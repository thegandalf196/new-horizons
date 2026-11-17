/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../../lib/battle/BattleSide.h"
#include <memory>
#include <optional>
#include <vcmi/spells/Magic.h>
namespace spells { class Mechanics; }
namespace battleSearch { class SearchBudget; }
class Environment;
class CBattleInfoCallback;
class PossibleSpellcast;
class DamageCache;
class BattleExchangeEvaluator;
namespace heroDecisionPressure
{
class Baseline;
enum class SpellScoutDomain { Unknown, Damage, NumericTimed };
/// Exact native effect identity and finite numeric-record admission. This is
/// exposed separately from affordability/legality to characterize fallback.
DLL_LINKAGE SpellScoutDomain spellScoutDomain(const spells::Mechanics & mechanics, const spells::Target & target);
/// A scout never enumerates actions or extends a tactical horizon. Only native
/// damage and finite numeric timed effects enter; unknown/stochastic/automatic
/// contexts retain the original evaluator. The result is signed combat utility.
DLL_LINKAGE bool spellScoutCandidate(const PossibleSpellcast & candidate);
DLL_LINKAGE std::optional<float> spellScoutUtility(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
	const PossibleSpellcast & candidate, const DamageCache & originalDamage,
	const BattleExchangeEvaluator & utility, Baseline * pressure,
	const std::shared_ptr<battleSearch::SearchBudget> & budget = std::shared_ptr<battleSearch::SearchBudget>());
}
