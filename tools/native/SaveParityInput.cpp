/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "SaveParityInput.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/StartInfo.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/GameSaveReader.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include <boost/filesystem/operations.hpp>

namespace saveParity
{
namespace
{
[[noreturn]] void executionForbidden()
{
	throw std::runtime_error("Save inspection cannot execute or publish gameplay");
}
}

void InspectionServer::setState(EServerState) { executionForbidden(); }
EServerState InspectionServer::getState() const { return EServerState::LOBBY; }
bool InspectionServer::isPlayerHost(const PlayerColor &) const { return false; }
bool InspectionServer::hasPlayerAt(PlayerColor, GameConnectionID) const { return false; }
bool InspectionServer::hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const { return false; }
void InspectionServer::applyPack(CPackForClient &) { executionForbidden(); }
void InspectionServer::sendPack(CPackForClient &, GameConnectionID) { executionForbidden(); }

LoadedSave::LoadedSave() : world(std::make_shared<CGameState>())
{
	world->preInit(LIBRARY);
	handler = std::make_unique<CGameHandler>(server, world);
}

LoadedSave::~LoadedSave() = default;

void requireUntimedAdventure(const CGameState & world)
{
	if(!world.currentBattles.empty() || !world.getStartInfo() || !world.getInitialStartInfo())
		throw std::runtime_error("Inspection requires an ordinary adventure world");
	for(const auto * options : {world.getStartInfo(), world.getInitialStartInfo()})
		if(options->turnTimerInfo.isEnabled() || options->turnTimerInfo.isBattleEnabled())
			throw std::runtime_error("Timed games have no admitted save-parity normalization policy");
}

std::unique_ptr<LoadedSave> readSave(const boost::filesystem::path & path)
{
	if(boost::filesystem::is_symlink(path) || !boost::filesystem::is_regular_file(path)
		|| boost::filesystem::file_size(path) > 128 * 1024 * 1024)
		throw std::runtime_error("Inspection requires a regular save of at most 128 MiB");
	auto result = std::make_unique<LoadedSave>();
	CLoadFile file(path, result->world.get());
	result->version = file.formatVersion();
	if(!file.hasFeature(ESerializationVersion::GAME_REPLAY_RECORDING))
		throw std::runtime_error("Save format has no replay coverage");
	gameSave::readGameData(file, *result->world);
	file.load(*result->handler);
	if(!file.atEnd())
		throw std::runtime_error("Trailing data after ordinary world and full server state");
	requireUntimedAdventure(*result->world);
	return result;
}
}
