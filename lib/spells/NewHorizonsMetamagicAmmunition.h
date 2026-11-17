/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
class ServerCallback;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsMetamagic
{
/// Original spell injuries only; the caller owns once-per-cast enemy admission.
DLL_LINKAGE void destroyShots(ServerCallback &, const CBattleInfoCallback &, const battle::Unit &);
}
