/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "AttackPossibility.h"
namespace attackPreviewMovement
{
/// Existing pre-attack movement/reaction prefix; no additional candidate or path search.
std::optional<AttackPossibility> project(const BattleAttackInfo & attack, BattleHex hex,
	DamageCache & damageCache, const std::shared_ptr<CBattleInfoCallback> & state, bool perfectMoment,
	const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial, const dreadCavalryAI::Action & dreadAction, bool validatedVoluntaryApproach, BattleHex approachOrigin);
/// The existing committed-landing damage estimate, with no extra path or obstacle query.
void applyObstacleDamage(AttackPossibility &, DamageCache &, const std::shared_ptr<CBattleInfoCallback> &,
	const std::shared_ptr<HypotheticBattle> &, bool rootMayHaveSerpents, std::vector<uint32_t> & serpentSourceDeaths);
}
