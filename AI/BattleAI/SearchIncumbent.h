/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../../lib/battle/BattleAction.h"
#include <memory>
class CStack;
class Environment;
class CPlayerBattleCallback;
class PotentialTargets;
namespace battleSearch
{
/// Mandatory legality precedes optional forecasting. Uses only the player view
/// and existing authoritative forced-action query, never target scoring/search.
DLL_LINKAGE BattleAction visibleIncumbent(const Environment *, const std::shared_ptr<CPlayerBattleCallback> &, const CStack *);
/// Complete already-admitted AP/forced roster; no budgeted work or new forecast.
DLL_LINKAGE BattleAction preparedIncumbent(const CStack *, const PotentialTargets &);
}
