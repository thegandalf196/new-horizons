/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
class CGameHandler;
class BattleAction;
struct BattleStackAttacked;
namespace newHorizonsSerpentsEmbrace
{
/// Pure pre-StartAction rejection; adjacent actions and forced displacement survive.
bool rejectsVoluntaryMovement(const CBattleInfoCallback & battle, const BattleAction & action,
	const battle::Unit * source);
struct PreparedBinding
{
	uint32_t source;
	uint32_t target;
	Bonus restraint;
};
std::optional<PreparedBinding> prepareBinding(const CBattleInfoCallback & battle,
	const battle::Unit * source, const battle::Unit * primary, bool ranged, bool physical,
	bool secondary, const std::vector<BattleStackAttacked> & hits);
void applyBinding(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::optional<PreparedBinding> & binding);
}
