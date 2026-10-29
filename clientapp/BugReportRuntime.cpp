/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "BugReportBootstrap.h"
#include "CrashReportSession.h"
#include "../client/GameEngine.h"
#include "../client/GameEngineUser.h"
#include "../client/GameInstance.h" // GameShutdownException only; never constructs GameInstance.
#include "../client/gui/CursorHandler.h"
#include "../client/gui/WindowHandler.h"
#include "../client/render/Graphics.h"
#include "../client/render/IRenderHandler.h"
#include "../client/windows/BugReportWindow.h"
#include "../client/windows/CMessage.h"
#include "../lib/AsyncRunner.h"
#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/ScopeGuard.h"
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace
{
class ReportEngineUser final : public IGameEngineUser
{
public:
	void onGlobalLobbyInterfaceActivated() override {}
	void onUpdate() override {}
	void onShutdownRequested(bool) override { throw GameShutdownException(); }
	void onAppPaused() override {}
	bool capturedAllEvents() override { return false; }
	bool wantsFrameRendered() override { return true; }
};
}

int BugReportBootstrap::runReporter()
{
	ReportEngineUser user;
	std::unique_ptr<boost::interprocess::file_lock> reviewLock;
	auto cleanup = vstd::makeScopeGuard([]
	{
		if(ENGINE)
		{
			ENGINE->windows().clear();
			ENGINE->async().wait();
		}
		CMessage::dispose();
		delete graphics;
		graphics = nullptr;
		ENGINE.reset();
		delete LIBRARY;
		LIBRARY = nullptr;
	});
	try
	{
		if(requestedSession == "pending")
		{
			const auto available = CrashReportSession::inactiveSessions(logFile.parent_path());
			if(available.empty())
				return 0; // No eligible retained report; do not create an empty form.
			requestedSession = available.front().string();
		}
		if(!requestedSession.empty())
		{
			const boost::filesystem::path selected(requestedSession);
			const auto root = selected.parent_path();
			boost::interprocess::file_lock store((root / ".store.lock").string().c_str());
			boost::interprocess::scoped_lock storeGuard(store);
			const auto inactive = CrashReportSession::inactiveSessions(selected.parent_path().parent_path());
			if(std::find(inactive.begin(), inactive.end(), selected) == inactive.end())
				throw std::runtime_error("Report session is active or unavailable");
			{
				boost::filesystem::ofstream create(selected / "review.lock", std::ios::app);
			}
			reviewLock = std::make_unique<boost::interprocess::file_lock>((selected / "review.lock").string().c_str());
			if(!reviewLock->try_lock())
				throw std::runtime_error("Report session is already being reviewed");
		}
		// Filesystem/settings and native UI resources only. No initializeLibrary,
		// GameInstance, CServerHandler, simulation, network or gameplay requests.
		LIBRARY = new GameLibrary;
		LIBRARY->initializeFilesystem(false);
		configure();
		Settings session = settings.write["session"];
		session["disableVideo"].Bool() = true;
		session["reportOnly"].Bool() = true;
		ENGINE = std::make_unique<GameEngine>();
		ENGINE->setEngineUser(&user);
		graphics = new Graphics(false);
		ENGINE->renderHandler().initializeInterfaceResources();
		CMessage::init();
		ENGINE->cursor().init();
		ENGINE->cursor().show();

		BugReportWindow::openSession(requestedSession, [&user] { user.onShutdownRequested(false); });
		ENGINE->mainLoop();
	}
	catch(const GameShutdownException &)
	{
		return 0;
	}
	catch(const std::exception &)
	{
		std::cerr << "The report form could not open; retained evidence is unchanged.\n";
		return 1;
	}
}
