/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/entities/hero/NewHorizonsCommandArmy.h"
class CGameHandler;
namespace commandArmyAdmission
{
bool validateSlot(CGameHandler & handler, const CArmedInstance * army, SlotID slot,
	CreatureID creature, int64_t count, const char * operation);
/// Only call after the complete request (including resources) is validated.
/// Uses ordinary stack packets, retaining artifacts and recruitment provenance.
void rearrange(CGameHandler & handler, const CArmedInstance & army,
	const newHorizonsHeroes::CommandArmyPlan & plan);
bool transfer(CGameHandler & handler, const CArmedInstance & source, const CGHeroInstance & destination,
	bool allowMerging);
bool give(CGameHandler & handler, const CGHeroInstance & destination, const CCreatureSet & incoming);
/// Historical per-stack count admission only; never called for current Command heroes.
bool tryLegacyLimitedJoin(CGameHandler & handler, const CArmedInstance & source, const CGHeroInstance & destination);
}
