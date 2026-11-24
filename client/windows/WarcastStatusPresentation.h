/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsWarcast.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusSelector.h"
#include <string>
#include <vector>

/// Warcasting status cards for the creature window: a friendly stack warcast by
/// a running Order, and an enemy Exposed by Opening. Both are public battlefield
/// facts read from the Order snapshot and the visible marker.
namespace warcastStatusPresentation
{
inline constexpr const char * WARCAST_ICON = "NH_warcasting2_rankart_v5_basic_small.png";
inline constexpr const char * EXPOSED_ICON = "NH_perk_warcasting2_opening_32.png";

struct Row
{
	std::string text;
	std::string image;
	std::string help;
};

inline std::vector<Row> rows(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	std::vector<Row> result;
	if(!unit)
		return result;
	if(unit->hasBonus(CSelector([](const Bonus * bonus) { return newHorizonsWarcast::isExposedMarker(bonus); })))
		result.push_back({"Exposed", EXPOSED_ICON,
			"{Exposed}\n\nHit by a warcast stack of a hero with Opening. That hero's damaging spells deal 20% more damage to this stack until the end of the next round."});
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return result;
	int best = 0;
	for(const auto & order : battle.battleGetHeroOrderStates(side))
		if(order.scheduledFor(unit->unitId(), battle.battleGetRound()))
			best = std::max(best, order.warcastPercentFor(unit->unitId()));
	if(best > 0)
		result.push_back({"Warcast +" + std::to_string(best) + "%", WARCAST_ICON,
			"{Warcast}\n\nThis stack carried its hero's beneficial spell when the Order was issued, so the Order is "
			+ std::to_string(best) + "% stronger on it while it lasts."});
	return result;
}
}
