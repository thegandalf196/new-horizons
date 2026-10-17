/*
 * PreparedBattleWorldCommitState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "PreparedBattleWorldCommit.h"
#include "../bonuses/NativeBonusGraphAdmission.h"
#include "ProjectedBattleWorldArenaState.h"
#include "PreparedBattleUnitRoots.h"
#include "PreparedBattleUnitSidecars.h"
#include "PreparedStackStateTransfer.h"
#include "../gameState/CGameState.h"
#include "../gameState/PreparedReplayAppend.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <thread>

namespace battle
{
struct PreparedBattleWorldCommit::Data
{
	enum class Phase { BUILDING, PREPARED, COMMITTED, FAILED };
	// Reverse retirement: participants, arena graph/identities, replay plan/packet,
	// lock, then shared world. Native root ownership never escapes that world.
	std::shared_ptr<CGameState> world;
	std::unique_lock<std::shared_mutex> access;
	// Retire participants/root pin BEFORE preparation; unlock state BEFORE world.
	std::unique_ptr<NativeBonusGraphPreparation> preparation;
	std::unique_ptr<BattleRootLifetime::Pin> rootLifetime;
	std::thread::id thread;
	BattleUnitsChanged packet;
	BattleInfo * original;
	uint32_t day;
	const std::unique_ptr<BattleInfo> * battleData;
	size_t battleCapacity;
	std::vector<const BattleInfo *> battleOccurrences;
	std::unique_ptr<PreparedReplayAppend> replay;
	std::unique_ptr<ProjectedBattleWorldArena> arena;
	std::vector<std::unique_ptr<PreparedStackStateTransfer>> values;
	std::unique_ptr<PreparedBattleUnitRoots> roots;
	std::unique_ptr<PreparedBattleUnitSidecars> sidecars;
	Phase phase = Phase::BUILDING;
	Data(std::shared_ptr<CGameState> world, std::unique_lock<std::shared_mutex> access,
		const BattleUnitsChanged & packet, std::unique_ptr<NativeBonusGraphPreparation> preparation);
	Data(std::shared_ptr<CGameState> world, std::unique_lock<std::shared_mutex> access,
		const BattleUnitsChanged & packet, std::unique_ptr<NativeBonusGraphPreparation> preparation,
		std::unique_ptr<PreparedReplayAppend> adoptedReplay);
	bool sameWorld() const noexcept;
};
}
