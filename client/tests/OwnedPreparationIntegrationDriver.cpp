/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationIntegrationDriver.h"
#include "OwnedPreparationBootstrap.h"
#include "OwnedPreparationController.h"
#include "../../server/tests/OwnedPreparationFirstWriter.h"
#include "../Client.h"
#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace
{
ownedPreparationIntegration::Scenario selectedCase()
{
	const auto * value = std::getenv("NH_OWNED_PREPARATION_CASE");
	if(value && std::string_view(value) == "ordered-removal") return ownedPreparationIntegration::Scenario::ORDERED_REMOVAL;
	if(value && std::string_view(value) == "incoming-clock") return ownedPreparationIntegration::Scenario::INCOMING_CLOCK;
	throw std::runtime_error("Explicit ordered-removal or incoming-clock case required");
}
}
struct OwnedPreparationIntegrationDriver::State
{
	ownedPreparationIntegration::Bootstrap bootstrap;
	std::shared_ptr<ownedPreparationAudit::Context> audit = std::make_shared<ownedPreparationAudit::Context>();
	std::shared_ptr<ownedPreparationIntegration::Controller> controller;
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
	bool installed = false, passed = false;
	State() : controller(std::make_shared<ownedPreparationIntegration::Controller>(selectedCase(), audit))
	{ ownedPreparationAudit::registerContext(audit); }
	void tick()
	{
		if(passed) return;
		if(std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Owned-preparation startup/analysis deadline expired");
		bootstrap.update();
		if(bootstrap.loaded() && !installed)
		{
			if(!ownedPreparationAudit::firstWriterVerified())
				throw std::runtime_error("Missing genuine negotiated first-writer world/hero observation");
			auto & client = *GAME->server().client;
			if(!client.battleints.contains(PlayerColor(0)) || !client.battleints.at(PlayerColor(0))->human)
				throw std::runtime_error("Expected original P0 human battle interface before first battle");
			// Retain the actual callback also owned by the human adventure/UI interface.
			// battleStarted must notify that same callback before creating its battle UI.
			controller->initBattleInterface(client.playerEnvironments.at(PlayerColor(0)), client.battleCallbacks.at(PlayerColor(0)));
			client.installNewBattleInterface(controller, PlayerColor(0), false);
			installed = true;
			std::cout << "Owned-preparation actual registered P0 controller READY; enter first Angel2 battle\n";
		}
		if(!controller->problem().empty()) throw std::runtime_error(controller->problem());
		if(!audit->problem().empty()) throw std::runtime_error(audit->problem());
		if(controller->done() && audit->finished() && audit->clientSourceVerified())
		{
			const auto evidence = controller->finish();
			if(evidence.status != ownedPreparationIntegration::Status::COMPLETE)
				throw std::runtime_error(evidence.issues.empty() ? "Incomplete genuine source/owned oracle" : evidence.issues.front());
			passed = true; std::cout << evidence.originalCase << " genuine internal-client PASS\n";
			throw GameShutdownException(); // Same main-loop cleanup and owned-thread join path as ordinary client/Cure harness.
		}
	}
};
OwnedPreparationIntegrationDriver::OwnedPreparationIntegrationDriver(GameInstance & instance)
	: game(instance), state(std::make_unique<State>()) {}
OwnedPreparationIntegrationDriver::~OwnedPreparationIntegrationDriver() = default;
void OwnedPreparationIntegrationDriver::detach() { state->audit->cancel("ordinary integration teardown"); }
int OwnedPreparationIntegrationDriver::result() const { return state->passed ? 0 : 1; }
void OwnedPreparationIntegrationDriver::onGlobalLobbyInterfaceActivated() { game.onGlobalLobbyInterfaceActivated(); }
void OwnedPreparationIntegrationDriver::onShutdownRequested(bool ask) { game.onShutdownRequested(ask); }
void OwnedPreparationIntegrationDriver::onAppPaused() { game.onAppPaused(); }
bool OwnedPreparationIntegrationDriver::capturedAllEvents() { return game.capturedAllEvents(); }
bool OwnedPreparationIntegrationDriver::wantsFrameRendered() { return game.wantsFrameRendered(); }
void OwnedPreparationIntegrationDriver::onUpdate()
{
	game.onUpdate();
	try { state->tick(); }
	catch(const GameShutdownException &) { throw; }
	catch(const std::exception & error)
	{
		std::cerr << "Owned-preparation genuine client FAIL: " << error.what() << '\n';
		detach(); throw GameShutdownException();
	}
}
