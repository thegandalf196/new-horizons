/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

class CBattleInfoCallback;
class ServerCallback;
struct BattleAttack;
namespace battle { class Unit; }

namespace battleFocusMagic
{
void capturePresentation(BattleAttack & attack, const battle::Unit & attacker);
void consumeAcceptedStrike(ServerCallback & server, const CBattleInfoCallback & battle,
	const BattleAttack & attack);
}
