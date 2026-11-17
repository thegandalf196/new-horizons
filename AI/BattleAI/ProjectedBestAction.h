/* ProjectedBestAction.h, VCMI / New Horizons, GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
class HypotheticBattle;
class AttackPossibility;
namespace battle { class Unit; }
DLL_LINKAGE void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action);
