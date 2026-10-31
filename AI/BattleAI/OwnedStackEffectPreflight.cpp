/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedStackEffectPreflight.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "InitialExportEffectQueries.h"
#include "../../lib/battle/GraveDustOrderedPreflight.h"
#include "../../lib/battle/ScopedStackEffectReplay.h"
#include <stdexcept>
void graveDustAI::validateOwnedEffectChanges(const HypotheticBattle & branch, const SetStackEffect & effects)
{
	if(!newHorizonsGraveDust::containsEffect(effects)) return;
	if(effects.battleID != branch.getBattleID())
		throw std::invalid_argument("Owned effect preflight requires its actual battle");
	const auto source = branch.initialExportSubject();
	if(!source) throw std::invalid_argument("Owned effect preflight requires an existing export ledger");
	auto queries = source->effectQueries(branch);
	newHorizonsGraveDust::validateOrderedEffectChanges(branch, effects,
		[&](uint32_t id) { return queries->find(id); },
		[&](uint32_t id, const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
		{
			battle::replayStackEffectVector(*queries, branch, id, records, operation);
		});
}
