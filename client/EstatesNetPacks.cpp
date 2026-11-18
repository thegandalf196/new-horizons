/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "ClientNetPackVisitors.h"
#include "GameEngine.h"
#include "gui/WindowHandler.h"
#include "windows/CCastleInterface.h"
#include "../lib/CConfigHandler.h"
#include "../lib/mapObjects/CGTownInstance.h"

void ApplyClientNetPackVisitor::visitEstatesKingdomChanged(EstatesKingdomChanged & pack)
{
	// The shared visitor has already applied the admitted payment and ledger.
	// Reuse the ordinary economic refresh instead of a second UI price/state.
	SetResources notification;
	notification.player = pack.owner;
	visitSetResources(notification);
	if(settings["session"]["headless"].Bool())
		return;
	// Read only after shared state accepted the appointment/replacement/dismissal.
	for(const auto & window : ENGINE->windows().findWindows<CCastleInterface>())
		if(window->town->getOwner() == pack.owner)
			window->refreshIncome();
}
