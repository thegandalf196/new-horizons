/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleWindow.h"
#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "../CPlayerInterface.h"
#include "../Client.h"
#include "../CServerHandler.h"
#include "../GameInstance.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/texts/CGeneralTextHandler.h"

void BattleWindow::bAutofightf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if(settings["battle"]["endWithAutocombat"].Bool() && onlyOnePlayerHuman)
	{
		endWithAutocombat();
		return;
	}

	//Stop auto-fight mode
	if(owner.curInt->isAutoFightOn)
	{
		assert(owner.curInt->autofightingAI);
		owner.curInt->isAutoFightOn = false;
		logGlobal->trace("Stopping the autofight...");
	}
	else if(!owner.curInt->autofightingAI)
	{
		if(!GAME->server().client->canStartInitialAutofight())
			return;
		owner.curInt->isAutoFightOn = true;
		blockUI(true);

		owner.curInt->prepareAutoFightingAI(owner.getBattleID(), owner.army1, owner.army2, int3(0,0,0), owner.attackingHeroInstance, owner.defendingHeroInstance, owner.getBattle()->battleGetMySide());
		if(!owner.requestAutofightingAIToTakeAction())
		{
			owner.curInt->isAutoFightOn = false;
			owner.curInt->unregisterBattleInterface(owner.curInt->autofightingAI);
			blockUI(false);
		}
	}
}


void BattleWindow::endWithAutocombat()
{
	if(!owner.makingTurn())
		return;

	GAME->interface()->showYesNoDialog(
		LIBRARY->generaltexth->translate("vcmi.battleWindow.endWithAutocombat"),
		[this]()
		{
			if(!GAME->server().client->canStartInitialAutofight())
				return;
			owner.curInt->isAutoFightEndBattle = true;
			owner.curInt->prepareAutoFightingAI(owner.getBattleID(), owner.army1, owner.army2, int3(0,0,0), owner.attackingHeroInstance, owner.defendingHeroInstance, owner.getBattle()->battleGetMySide(), true);

			if(!owner.requestAutofightingAIToTakeAction())
			{
				owner.curInt->isAutoFightOn = false;
				owner.curInt->isAutoFightEndBattle = false;
				owner.curInt->unregisterBattleInterface(owner.curInt->autofightingAI);
				return; // Keep the manual battle and its current activation intact.
			}

			blockUI(true); // Keep the ordinary window alive until task completion.
		},
		nullptr
	);
}
