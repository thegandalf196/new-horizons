/* ProjectedBestAction.h, VCMI / New Horizons, GPL v2.0 or later. */
#pragma once
class HypotheticBattle;
class AttackPossibility;
namespace battle { class Unit; }
void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action);
