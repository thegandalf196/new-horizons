/* RegenerationForecast.h, VCMI / New Horizons, GPL v2.0 or later. */
#pragma once
#include "RegenerationBaselineReuse.h"
std::set<uint32_t> improvedRegenerationTargets(const HypotheticBattle & projectedBattle,
	const CBattleInfoCallback & originalBattle, SpellID spell);
float projectedRegenerationValue(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder, SpellID spell,
	DamageCache & damageCache, BattleSide ourSide, PlayerColor ourPlayer,
	float positiveEffectMultiplier, RegenerationBaselineReuse * reuse = nullptr);
