/* License: GNU General Public License v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CureIntegrationDriver.h"
#include "CureIntegrationBootstrap.h"
#include "CureCallbackRequestLease.h"
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../battle/BattleInterface.h"
#include "../battle/BattleActionsController.h"
#include "../battle/CureAfflictionPresenter.h"
#include "../gui/WindowHandler.h"
#include "../windows/GUIClasses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace
{
void require(bool value, const char * reason)
{
	if(!value)
		throw std::runtime_error(reason);
}
}

struct CureIntegrationDriver::State final : CureAfflictionPresenter
{
	CureIntegrationBootstrap bootstrap{std::getenv("NH_CURE_BOOTSTRAP")};
	std::string scenario;
	std::weak_ptr<BattleInterface> observedOwner;
	std::weak_ptr<CObjectListWindow> modal;
	std::function<void(int)> confirm;
	std::function<void()> cancel;
	std::unique_ptr<CureCallbackRequestLease> lease;
	const std::chrono::steady_clock::time_point startupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
	std::optional<std::chrono::steady_clock::time_point> deadline;
	bool captured = false;
	bool awaitingReplacement = false;
	bool passed = false;

	State()
	{
		const auto * mode = std::getenv("NH_CURE_CALLBACK_CASE");
		require(mode, "Explicit NH_CURE_CALLBACK_CASE required");
		scenario = mode;
		require(scenario == "same-session" || scenario == "late-session" || scenario == "replaced-owner",
			"Unsupported Cure integration scenario");
	}

	void show(const std::vector<std::string> & names,
		const CStack & initialTarget, std::function<void(int)> selected,
		std::function<void()> exited) override
	{
		require(ENGINE->amIGuiThread(), "Cure presenter must run on GUI thread");
		const auto owner = observedOwner.lock();
		require(!captured && owner && owner == CPlayerInterface::battleInt,
			"Requires first actual current-owner Cure modal");
		const auto callback = owner->getBattle();
		require(callback && callback->getBattle(), "Actual Cure battle required");
		const auto & rules = callback->getBattle()->getMagicRules();
		const auto frozenLabel = LIBRARY->generaltexth->translate("new-horizons.combat.frozen.label");
		require(names == std::vector<std::string>{frozenLabel}
			&& callback->battleGetUnitByID(initialTarget.unitId()) == &initialTarget
			&& initialTarget.alive() && newHorizonsFrozen::isFrozen(initialTarget)
			&& newHorizonsFrozen::chancePercent(rules) > 0
			&& newHorizonsMagic::cureAfflictions(rules, &initialTarget).empty(),
			"Requires exact Frozen row and actual live Frozen-only target");
		bootstrap.verifyFixtureTarget(initialTarget, *callback);
		confirm = selected;
		cancel = exited;
		modal = showCureAfflictionModal(names, std::move(selected), std::move(exited));
		captured = true;
	}

	void attach(const std::shared_ptr<BattleInterface> & battle, const std::shared_ptr<State> & self)
	{
		require(battle->curInt && battle->curInt->cb && battle->actionsController,
			"Real player/controller required");
		observedOwner = battle;
		battle->setCureAfflictionPresenter(self);
		if(!deadline)
		{
			const auto attachedAt = std::chrono::steady_clock::now();
			require(attachedAt < startupDeadline, "Cure integration startup deadline expired");
			deadline = attachedAt + std::chrono::seconds(180);
			std::cout << "Cure integration assertion window started at first actual battle attachment\n";
		}
	}

	void complete()
	{
		lease.reset(); // Restore original callback before ACKs/ordinary restart.
		passed = true;
		std::cout << "Cure installed callback integration PASS: " << scenario << '\n';
	}

	void checkRetained(const std::shared_ptr<BattleInterface> & battle)
	{
		require(battle->curInt && battle->curInt->cb && battle->actionsController
			&& GAME->server().client, "Real replacement/current player and client required");
		const auto generation = battle->actionsController->getCastingSession();
		const auto active = battle->actionsController->heroSpellcastingModeActive();
		lease = std::make_unique<CureCallbackRequestLease>(battle, *GAME->server().client);
		confirm(0);
		cancel();
		require(lease->requests == 0, "Stale installed callback submitted a request");
		require(battle->actionsController->getCastingSession() == generation
			&& battle->actionsController->heroSpellcastingModeActive() == active,
			"Stale installed callback changed current casting session");
		complete();
	}

	void exercise(const std::shared_ptr<BattleInterface> & battle)
	{
		const auto window = modal.lock();
		require(window && ENGINE->windows().isTopWindow(window), "Actual modal must still be top window");
		require(battle->actionsController->heroSpellcastingModeActive() && battle->makingTurn(),
			"Actual casting session/turn changed before test");
		lease = std::make_unique<CureCallbackRequestLease>(battle, *GAME->server().client);
		if(scenario == "same-session")
		{
			window->elementSelected(); // Actual close-before-select invokes installed confirm.
			require(lease->requests == 1 && lease->spellRequests.size() == 1 && lease->endedBeforeSubmission,
				"Expected exactly one request after real endCastingSpell");
			const auto & action = lease->spellRequests.front();
			require(action.spell == SpellID(SpellID::CURE) && action.spellCurePhysicalAffliction == "frozen"
				&& !action.target.empty(), "Expected actual targeted Frozen Cure request");
			confirm(0); // Retained original callback must not submit twice.
			cancel();
			require(lease->requests == 1, "Repeated installed callback submitted again");
			complete();
			return;
		}
		window->close();
		cancel(); // Same close-before-onExit order as ordinary exitPressed.
		require(lease->requests == 0, "Actual modal cancellation submitted a spell");
		lease.reset();
		if(scenario == "late-session")
		{
			battle->actionsController->castThisSpell(SpellID(SpellID::CURE));
			require(battle->actionsController->heroSpellcastingModeActive(), "Ordinary new Cure targeting failed");
			checkRetained(battle);
		}
		else
		{
			awaitingReplacement = true;
			std::cout << "Cure integration waiting for ordinary restart/new battle\n";
		}
	}

	void tick(const std::shared_ptr<State> & self)
	{
		require(ENGINE->amIGuiThread(), "Cure integration tick must run on GUI thread");
		if(passed)
			return;
		const auto now = std::chrono::steady_clock::now();
		if(deadline)
			require(now < *deadline, "Cure integration bounded deadline expired");
		else
			require(now < startupDeadline, "Cure integration startup deadline expired");
		const auto current = CPlayerInterface::battleInt;
		if(awaitingReplacement)
		{
			if(current && current != observedOwner.lock())
			{
				std::cout << "Cure integration observed ordinary replacement owner; old expired="
					<< observedOwner.expired() << '\n';
				checkRetained(current); // Only an actual ordinary new BattleInterface qualifies.
			}
			return;
		}
		if(!current)
			return;
		if(observedOwner.expired() && !captured)
			attach(current, self);
		if(current->makingTurn())
			bootstrap.verifyReplicatedFixture(*current->getBattle());
		if(captured)
		{
			require(current == observedOwner.lock(), "Battle changed before first modal callback check");
			exercise(current);
		}
	}
};

CureIntegrationDriver::CureIntegrationDriver(GameInstance & instance)
	: game(instance), state(std::make_shared<State>())
{
}

CureIntegrationDriver::~CureIntegrationDriver() = default;

void CureIntegrationDriver::detach() { state->lease.reset(); }
int CureIntegrationDriver::result() const { return state->passed ? 0 : 1; }
void CureIntegrationDriver::onGlobalLobbyInterfaceActivated() { game.onGlobalLobbyInterfaceActivated(); }
void CureIntegrationDriver::onShutdownRequested(bool ask) { game.onShutdownRequested(ask); }
void CureIntegrationDriver::onAppPaused() { game.onAppPaused(); }
bool CureIntegrationDriver::capturedAllEvents() { return game.capturedAllEvents(); }
bool CureIntegrationDriver::wantsFrameRendered() { return game.wantsFrameRendered(); }
void CureIntegrationDriver::onUpdate()
{
	game.onUpdate();
	try
	{
		state->tick(state);
		state->bootstrap.update();
	}
	catch(const std::exception & error)
	{
		std::cerr << "Cure installed callback integration FAIL: " << error.what() << '\n';
		detach();
		throw GameShutdownException(); // Reuse ordinary main-loop cleanup, never bypass it.
	}
}
