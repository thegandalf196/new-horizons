/*
 * GameplayReplayer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "GameplayReplayer.h"

#include "ReplayAbortOverlay.h"
#include "ReplayPackFilter.h"

#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "../battle/BattleInterface.h"
#include "../gui/CIntObject.h"
#include "../gui/WindowHandler.h"
#include "../mapView/mapHandler.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/networkPacks/NetPacksBase.h"

/// Everything of the live session that has to step aside while a replay is on screen
struct LiveSessionBackup
{
	ClientSession session;
	std::unique_ptr<CMapHandler> mapHandler;
	std::vector<std::shared_ptr<IShowActivatable>> windows;
	std::shared_ptr<AdventureMapInterface> adventureMap;
	std::shared_ptr<BattleInterface> battleInterface;
	CPlayerInterface * playerInterface = nullptr;
};

static CPackForClient & asClientPack(CPack & pack)
{
	auto * clientPack = dynamic_cast<CPackForClient *>(&pack);

	if(clientPack == nullptr)
		throw std::runtime_error("Recorded data contains a pack that is not meant for the client!");

	return *clientPack;
}

GameplayReplayer::GameplayReplayer() = default;

GameplayReplayer::~GameplayReplayer()
{
	requestStop();
	waitForFinish();

	if(worker.joinable())
		worker.join();
}

bool GameplayReplayer::isActive() const
{
	std::scoped_lock lock(stateMutex);
	return active;
}

std::optional<PlayerColor> replayFollowedPlayer()
{
	if(!GAME->server().isReplayActive())
		return std::nullopt;

	return GAME->server().replayer().followedPlayer();
}

std::optional<PlayerColor> GameplayReplayer::followedPlayer() const
{
	std::scoped_lock lock(stateMutex);
	return followed;
}

void GameplayReplayer::requestStop()
{
	{
		std::scoped_lock lock(stateMutex);
		stopRequested = true;
		paused = false;
	}
	stateChanged.notify_all();
}

void GameplayReplayer::setPaused(bool value)
{
	{
		std::scoped_lock lock(stateMutex);
		paused = value;
	}
	stateChanged.notify_all();
}

bool GameplayReplayer::waitWhilePaused()
{
	std::unique_lock lock(stateMutex);
	stateChanged.wait(lock, [this]() { return !paused || stopRequested; });
	return !stopRequested;
}

bool GameplayReplayer::isStopRequested() const
{
	std::scoped_lock lock(stateMutex);
	return stopRequested;
}

void GameplayReplayer::setFollowedPlayer(const std::optional<PlayerColor> & value)
{
	std::scoped_lock lock(stateMutex);
	followed = value;
}

void GameplayReplayer::waitForFinish() const
{
	std::unique_lock lock(stateMutex);
	stateChanged.wait(lock, [this]() { return !active; });
}

void GameplayReplayer::markFinished()
{
	{
		std::scoped_lock lock(stateMutex);
		active = false;
	}
	stateChanged.notify_all();
}

void GameplayReplayer::start(ReplaySequence sequence, PlayerColor observer, const Options & options)
{
	{
		std::scoped_lock lock(stateMutex);
		if(active)
			return;

		active = true;
		stopRequested = false;
		paused = false;
		followed = std::nullopt;
	}

	if(worker.joinable())
		worker.join();

	worker = std::thread(&GameplayReplayer::run, this, std::move(sequence), observer, options);
}

std::unique_ptr<CMapHandler> GameplayReplayer::beginPass(CClient & client, CGameState & state, PlayerColor observer, PlayerColor turnPlayer, const int3 & viewCenter)
{
	auto previousMapHandler = GAME->swapMapInstance(std::make_unique<CMapHandler>(&state.getMap()));

	client.installObserverInterface(observer);

	ENGINE->windows().pushWindow(adventureInt);

	// isHuman=false is deliberate: only that state blocks input on a throw-away game
	adventureInt->onEnemyTurnStarted(turnPlayer, false);
	adventureInt->onMapTilesChanged(std::nullopt);

	// start off where the player was looking, instead of at the top left corner of the map
	if(state.isInTheMap(viewCenter))
		adventureInt->centerOnTile(viewCenter);

	// drawn outside of the window stack, so it stays reachable even during a combat
	ENGINE->windows().setOverlay(std::make_shared<ReplayAbortOverlay>(
		[this](){ requestStop(); },
		[this](bool value){ setPaused(value); }));

	return previousMapHandler;
}

void GameplayReplayer::endPass(CClient & client, const std::shared_ptr<CGameState> & state)
{
	auto rootRetirement = BattleRootLifetime::retire(state);
	// windows go first, so that a battle window is gone before the interface it points to
	ENGINE->windows().setOverlay(nullptr);
	ENGINE->windows().clear();
	CPlayerInterface::battleInt.reset();
	adventureInt.reset();

	// the gamestate is handed straight back, only the interfaces built on top of it are dropped
	ClientSession next;
	next.gamestate = state;
	ClientSession previous = client.swapSession(std::move(next));
	previous = ClientSession();

	auto rootMutation = rootRetirement.mutate();
	state->currentBattles.clear();
}

static void restoreLiveSession(CClient & client, LiveSessionBackup & backup)
{
	std::scoped_lock interfaceLock(ENGINE->interfaceMutex);
	auto retiringReplay = client.gameStatePtr();
	auto rootRetirement = BattleRootLifetime::retire(retiringReplay, BattleRootLifetime::RetirementMode::PERMANENT);

	// windows go first, so that a battle window is gone before the interface it points to
	ENGINE->windows().setOverlay(nullptr);
	ENGINE->windows().clear();
	CPlayerInterface::battleInt.reset();

	// dropping the replay session destroys its interfaces and callbacks
	ClientSession replaySession = client.swapSession(std::move(backup.session));
	if(replaySession.gamestate)
	{
		if(replaySession.gamestate != retiringReplay)
			throw std::logic_error("Replay retirement owner changed during session restoration");
		auto rootMutation = rootRetirement.mutate();
		replaySession.gamestate->currentBattles.clear();
	}
	replaySession = ClientSession();

	GAME->swapMapInstance(std::move(backup.mapHandler));

	adventureInt = backup.adventureMap;
	CPlayerInterface::battleInt = backup.battleInterface;
	GAME->setInterfaceInstance(backup.playerInterface);

	ENGINE->windows().attachAll(std::move(backup.windows));
	ENGINE->windows().totalRedraw();

	client.observerMode = false;
}

void GameplayReplayer::run(ReplaySequence sequence, PlayerColor observer, Options options)
{
	setThreadName("replay");

	logGlobal->info("Replay: day %d, player %s, %d days", sequence.day, sequence.replayedPlayer.toString(), sequence.days.size());

	CClient & client = *GAME->server().client;
	LiveSessionBackup backup;
	std::shared_ptr<CGameState> replayState;
	bool sessionInstalled = false;

	try
	{
		sequence.validate();
		// 1. rebuild the state the replayed turn started from. No UI is attached yet, so
		// fast-forwarding through earlier turns costs nothing but CPU time.
		replayState = std::make_shared<CGameState>();
		replayState->preInit(LIBRARY);
		replayState->loadReplaySnapshot(std::move(sequence.snapshot));

		for(const auto & data : sequence.fastForwardPacks)
		{
			auto pack = ReplayPackSerializer::read(data, replayState.get());
			replayState->apply(asClientPack(*pack));
		}

		// a recording of an earlier game may not know the color we are playing right now
		if(!replayState->players.count(observer))
			observer = sequence.replayedPlayer;

		if(!replayState->players.count(observer))
			throw std::runtime_error("Recorded game has no player to watch it with!");

		// 2. put the live session aside and let the client operate on the replayed state instead
		int3 previousViewCenter;
		{
			std::scoped_lock interfaceLock(ENGINE->interfaceMutex);

			backup.playerInterface = GAME->interface();
			backup.adventureMap = adventureInt;
			previousViewCenter = adventureInt ? adventureInt->getMapViewCenter() : int3();
			backup.battleInterface = CPlayerInterface::battleInt;
			backup.windows = ENGINE->windows().detachAll();

			ClientSession replaySession;
			replaySession.gamestate = replayState;
			backup.session = client.swapSession(std::move(replaySession));
			sessionInstalled = true;
			client.observerMode = true;

			CPlayerInterface::battleInt.reset();
			adventureInt.reset();

			const std::optional<PlayerColor> firstFollowed = sequence.days.empty() || sequence.days.front().passes.empty()
				? std::nullopt
				: sequence.days.front().passes.front().followedPlayer;

			setFollowedPlayer(firstFollowed);
			backup.mapHandler = beginPass(client, *replayState, observer, firstFollowed.value_or(sequence.replayedPlayer), previousViewCenter);
		}

		// 3. play the recording back, one pack at a time. A day of simultaneous turns is watched
		// once per player, so it is rewound to its own start before every further pass.
		for(const auto & day : sequence.days)
		{
			ReplayPayload dayStart;
			if(day.passes.size() > 1)
				dayStart = replayState->saveReplaySnapshot();

			for(size_t passIndex = 0; passIndex < day.passes.size(); ++passIndex)
			{
				const ReplayPass & pass = day.passes[passIndex];

				{
					std::scoped_lock interfaceLock(ENGINE->interfaceMutex);

					setFollowedPlayer(pass.followedPlayer);

					// rewinding replaces the map and every object in it, so the interface that
					// points into them has to be built anew
					if(passIndex > 0)
					{
						endPass(client, replayState);
						replayState->loadReplaySnapshot(dayStart);
						beginPass(client, *replayState, observer, pass.followedPlayer.value_or(sequence.replayedPlayer), previousViewCenter);
					}
				}

				for(size_t packIndex = 0; packIndex < pass.packCount && packIndex < day.packs.size(); ++packIndex)
				{
					// waiting happens without the interface mutex, so a paused replay stays responsive
					if(!waitWhilePaused())
						break;

					std::scoped_lock interfaceLock(ENGINE->interfaceMutex);

					auto pack = ReplayPackSerializer::read(day.packs[packIndex], replayState.get());
					CPackForClient & clientPack = asClientPack(*pack);

					switch(ReplayPackFilter::classify(clientPack))
					{
						case EReplayPackKind::INTERACTIVE:
							// dialogs, queries and session bookkeeping are applied, but never shown
							client.applyPackSilently(clientPack);
							break;
						case EReplayPackKind::BATTLE:
							if(options.showBattles)
								client.handlePack(clientPack);
							else
								client.applyPackSilently(clientPack);
							break;
						case EReplayPackKind::REGULAR:
							client.handlePack(clientPack);
							break;
					}
				}

				if(isStopRequested())
					break;
			}

			if(isStopRequested())
				break;
		}
	}
	catch(const std::exception & e)
	{
		logGlobal->error("Replay failed: %s", e.what());
	}

	// 4. put the live session back. This has to happen even if the replay threw, otherwise the
	// player would be left looking at a game that no longer exists.
	if(sessionInstalled)
		restoreLiveSession(client, backup);

	replayState.reset();
	markFinished();
}
