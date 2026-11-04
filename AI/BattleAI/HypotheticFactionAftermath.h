/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class CBattleInfoCallback;
class HypotheticBattle;
namespace factionAftermathAI
{
bool needsProjection(const CBattleInfoCallback & battle);
void restoreCataclysmRecipients(HypotheticBattle & destination, const HypotheticBattle & captured);
}
