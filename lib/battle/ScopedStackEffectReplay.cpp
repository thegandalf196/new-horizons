/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ScopedStackEffectReplay.h"
#include "ScopedStackEffectQueries.h"
namespace battle
{
void replayScopedStackEffectVector(ScopedStackEffectQueries & queries, const IBattleInfo & battle, uint32_t id,
	const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
{
	replayStackEffectVector(queries, battle, id, records, operation);
}
}
