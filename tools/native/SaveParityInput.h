/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../server/IGameServer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <boost/filesystem/path.hpp>
#include <memory>

class CGameHandler;
class CGameState;

namespace saveParity
{
/// Any attempt to execute or publish through the inspection owner fails closed.
class InspectionServer final : public IGameServer
{
public:
	void setState(EServerState) override;
	EServerState getState() const override;
	bool isPlayerHost(const PlayerColor &) const override;
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override;
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override;
	void applyPack(CPackForClient &) override;
	void sendPack(CPackForClient &, GameConnectionID) override;
};

struct LoadedSave
{
	InspectionServer server;
	std::shared_ptr<CGameState> world;
	std::unique_ptr<CGameHandler> handler;
	ESerializationVersion version = ESerializationVersion::NONE;
	LoadedSave();
	~LoadedSave();
};

/// Reads a trusted, bounded ordinary adventure save. No load/resume/gameplay wrapper.
std::unique_ptr<LoadedSave> readSave(const boost::filesystem::path & path);
void requireUntimedAdventure(const CGameState & world);
}
