/* License: GNU General Public License v2.0 or later; see license.txt. */
#pragma once

#include "../GameEngineUser.h"
#include <memory>

class GameInstance;

/// Only the dedicated executable constructs this wrapper. Every engine-user
/// operation delegates to the ordinary GameInstance on the same GUI thread.
class CureIntegrationDriver final : public IGameEngineUser
{
	struct State;
	GameInstance & game;
	std::shared_ptr<State> state;

public:
	explicit CureIntegrationDriver(GameInstance & instance);
	~CureIntegrationDriver();
	void detach();
	int result() const;
	void onGlobalLobbyInterfaceActivated() override;
	void onUpdate() override;
	void onShutdownRequested(bool ask) override;
	void onAppPaused() override;
	bool capturedAllEvents() override;
	bool wantsFrameRendered() override;
};
