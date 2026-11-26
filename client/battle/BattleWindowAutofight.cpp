/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleWindow.h"
#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "../CPlayerInterface.h"
#include "../Client.h"
#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/ShortcutHandler.h"
#include "media/ISoundPlayer.h"
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
	confirmEndWithAutocombat();
}

void BattleWindow::confirmEndWithAutocombat()
{
	GAME->interface()->showYesNoDialog(
		LIBRARY->generaltexth->translate("vcmi.battleWindow.endWithAutocombat"),
		[this]()
		{
			if(owner.makingTurn())
				startEndWithAutocombat();
			else
				endWithAutocombatPending = true; // The opponent acts first; hand over at our activation.
		},
		nullptr
	);
}

void BattleWindow::startEndWithAutocombat()
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
}

void BattleWindow::resumeEndWithAutocombat()
{
	if(!endWithAutocombatPending || !owner.makingTurn())
		return;
	endWithAutocombatPending = false;
	logGlobal->debug("Handing the confirmed end-with-autocombat request over at the first activation");
	// Activation may arrive with a battle packet; start exactly as the key does, on the GUI thread.
	ENGINE->dispatchMainThread([battle = std::weak_ptr<BattleInterface>(CPlayerInterface::battleInt)]()
	{
		const auto presentation = battle.lock();
		if(presentation && presentation->windowObject && presentation->makingTurn()
			&& !presentation->getBattle()->battleIsFinished() && !presentation->curInt->isAutoFightOn)
			presentation->windowObject->startEndWithAutocombat();
	});
}

bool BattleWindow::captureThisKey(EShortcut key)
{
	// While the opening plays a key ends it, as the end-with-autocombat command when bound to it.
	return owner.openingPlaying() && (key == EShortcut::BATTLE_END_WITH_AUTOCOMBAT
		|| !ENGINE->shortcuts().sharesBinding(key, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
}

void BattleWindow::endOpeningWithKey(EShortcut key)
{
	const bool autocombatEnd = key == EShortcut::BATTLE_END_WITH_AUTOCOMBAT && onlyOnePlayerHuman
		&& !owner.isDeploymentPhase() && !owner.isInTacticsMode();
	if(autocombatEnd)
	{
		logGlobal->debug("The end-with-autocombat key ends the battle opening");
		ENGINE->sound().stopSound(owner.introSoundChannel); // The intro callback starts battle music.
	}
	owner.openingEnd();
	if(autocombatEnd && !owner.getBattle()->battleIsFinished())
		confirmEndWithAutocombat();
}
