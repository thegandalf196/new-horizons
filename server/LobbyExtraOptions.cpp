/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LobbyNetPackVisitors.h"
#include "CVCMIServer.h"
#include "../lib/StartInfo.h"
#include "../lib/networkPacks/PacksForLobby.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/mapping/CMapHeader.h"
#include <stdexcept>

void ApplyOnServerNetPackVisitor::visitLobbySetExtraOptions(LobbySetExtraOptions & pack)
{
	if(!srv.si)
	{
		result = false;
		return;
	}
	try { pack.extraOptionsInfo.validateNeutralArmySize(); }
	catch(const std::exception &)
	{
		result = false;
		return;
	}
	const bool changed = pack.extraOptionsInfo.neutralArmySizePercent != srv.si->extraOptionsInfo.neutralArmySizePercent;
	const bool freshScenario = srv.getState() == EServerState::LOBBY && srv.si->mode == EStartMode::NEW_GAME
		&& (!srv.mi || !srv.mi->mapHeader || !srv.mi->mapHeader->battleOnly);
	if(changed && !freshScenario)
	{
		result = false;
		return;
	}
	// Existing host permission dispatch precedes this visitor. Commit all flags
	// together only after validation; other flags retain their existing behavior.
	srv.si->extraOptionsInfo = pack.extraOptionsInfo;
	result = true;
}
