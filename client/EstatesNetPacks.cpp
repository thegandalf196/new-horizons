/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "ClientNetPackVisitors.h"

void ApplyClientNetPackVisitor::visitEstatesKingdomChanged(EstatesKingdomChanged & pack)
{
	// The shared visitor has already applied the admitted payment and ledger.
	// Reuse the ordinary economic refresh instead of a second UI price/state.
	SetResources notification;
	notification.player = pack.owner;
	visitSetResources(notification);
}
