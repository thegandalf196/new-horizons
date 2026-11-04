/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsFactionSpellDamage.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <map>
class CGameHandler;
struct CPackForClient;
namespace elementalCataclysmAftermath
{
struct Trigger
{
	BattleID battleID = BattleID::NONE;
	newHorizonsFactionSpellDamage::Cataclysm captured;
};
using Triggers = std::map<uint32_t, Trigger>;
/// Snapshot the genuine lethal transition before state mutation. Duplicate victims
/// in one packet are resolved once in stable unit-ID order.
Triggers capture(const CGameHandler & game, const CPackForClient & packet);
void resolve(CGameHandler & game, const Triggers & triggers);
}
