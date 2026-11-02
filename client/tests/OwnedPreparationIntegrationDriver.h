/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../GameEngineUser.h"
#include <memory>
class GameInstance;
class OwnedPreparationIntegrationDriver final : public IGameEngineUser
{
	struct State;
	GameInstance & game;
	std::unique_ptr<State> state;
public:
	explicit OwnedPreparationIntegrationDriver(GameInstance &);
	~OwnedPreparationIntegrationDriver();
	void detach();
	int result() const;
	void onGlobalLobbyInterfaceActivated() override;
	void onUpdate() override;
	void onShutdownRequested(bool ask) override;
	void onAppPaused() override;
	bool capturedAllEvents() override;
	bool wantsFrameRendered() override;
};
