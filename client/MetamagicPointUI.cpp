/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ClientNetPackVisitors.h"
#include "CPlayerInterface.h"
#include "battle/BattleInterface.h"
#include "battle/BattleWindow.h"
#include "../lib/CConfigHandler.h"
#include "../lib/networkPacks/SetMetamagicPointState.h"

void ApplyClientNetPackVisitor::visitSetMetamagicPointState(SetMetamagicPointState & pack)
{
	if(settings["session"]["headless"].Bool())
		return;
	// The client visitor runs after the authoritative packet has been applied.
	// The one displayed battle owns both hero panels, including hotseat views.
	const auto battle = CPlayerInterface::battleInt;
	if(battle && battle->getBattleID() == pack.battleID && battle->windowObject)
		battle->windowObject->updateCounterspellStatus();
}
