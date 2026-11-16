/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CPlayerInterface.h"
#include "CServerHandler.h"
#include "Client.h"
#include "InitialAutofightTask.h"
#include "battle/BattleInterface.h"
#include "battle/BattleWindow.h"
#include "battle/BattleStacksController.h"
#include "GameEngine.h"
#include "gui/WindowHandler.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/CPlayerBattleCallback.h"
#include "GameInstance.h"
#include "../lib/CConfigHandler.h"
#include "../lib/callback/AIFactory.h"
#include "../lib/callback/CCallback.h"
#include "../lib/callback/BattleAISelection.h"
#include "../lib/callback/CBattleGameInterface.h"

void CPlayerInterface::prepareAutoFightingAI(const BattleID &bid, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide side, bool automatic)
{
	std::optional<std::string_view> quickAI;
	if(settings["ai"]["quickCombatAI"].getType() == JsonNode::JsonType::DATA_STRING)
		quickAI = settings["ai"]["quickCombatAI"].String();
	autofightingAI = AIFactory::createBattleAI(battleAISelection::factoryName(
		automatic, settings["ai"]["combatAlliedAI"].String(), quickAI));

	AutocombatPreferences autocombatPreferences = AutocombatPreferences();
	autocombatPreferences.enableSpellsUsage = settings["battle"]["enableAutocombatSpells"].Bool();
	autocombatPreferences.enableTacticsUsage = settings["battle"]["enableAutocombatTactics"].Bool();

	autofightingAI->initBattleInterface(env, cb, autocombatPreferences);
	autofightingAI->battleStart(bid, army1, army2, tile, hero1, hero2, side, false);
	isAutoFightOn = true;
	registerBattleInterface(autofightingAI);
}


void CPlayerInterface::registerBattleInterface(std::shared_ptr<CBattleGameInterface> battleEvents)
{
	autofightingAI = battleEvents;
	GAME->server().client->registerBattleInterface(battleEvents, playerID);
}

void CPlayerInterface::unregisterBattleInterface(std::shared_ptr<CBattleGameInterface> battleEvents)
{
	assert(battleEvents == autofightingAI);
	GAME->server().client->unregisterBattleInterface(autofightingAI, playerID);
	autofightingAI.reset();
}

bool CPlayerInterface::completeInitialAutofight(const InitialAutofightCompletion & completion)
{
	const auto & source = completion.source;
	if(completion.hasUncertainTransport()) return false;
	if(completion.result != InitialAutofightResult::COMPLETED && completion.waitingForRealization())
		return false;
	if(autofightingAI != source.controller || cb != source.callback || !battleInt
		|| battleInt->getBattleID() != source.battle)
		return true; // A later battle/session owns the presentation now.
	auto presentation = battleInt;
	if(completion.result == InitialAutofightResult::COMPLETED
		&& (!isAutoFightEndBattle || presentation->curInt.get() != this))
		return true;
	const bool manualInput = completion.stillAwaitingInput();
	if(completion.result != InitialAutofightResult::COMPLETED && !manualInput)
	{
		isAutoFightOn = false;
		isAutoFightEndBattle = false;
		unregisterBattleInterface(autofightingAI);
		return true; // No old activation is resurrected; normal future input is manual.
	}
	if(!ENGINE->windows().isTopWindow(presentation->windowObject.get())
		|| presentation->hasAnimations() || presentation->openingPlaying())
		return false; // Retry presentation on a later frame; never retry an AI action.
	if(completion.result == InitialAutofightResult::COMPLETED)
	{
		if(isAutoFightOn)
		{
			presentation->windowObject->close();
			battleInt.reset();
		}
		return true;
	}
	isAutoFightOn = false;
	isAutoFightEndBattle = false;
	unregisterBattleInterface(autofightingAI);
	presentation->windowObject->blockUI(false);
	const auto input = completion.recoveryInput();
	if(input->unit)
	{
		// The ordinary selector/activation path, without a GUI-unlocking wait.
		presentation->trySetActivePlayer(playerID);
		presentation->stacksController->setActiveStack(cb->getBattle(input->battle)->battleGetStackByID(*input->unit, false));
		presentation->activateStack();
	}
	else
		presentation->deploymentPhaseChanged();
	return true;
}
