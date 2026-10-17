/*
 * PreparedBattleWorldReplayAdoption.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleWorldCommitState.h"
#include <stdexcept>

battle::PreparedBattleWorldCommit::Data::Data(std::shared_ptr<CGameState> world,
	std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet,
	std::unique_ptr<NativeBonusGraphPreparation> preparation)
	: Data(std::move(world), std::move(access), packet, std::move(preparation), {}) {}

battle::PreparedBattleWorldCommit::PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
	std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet,
	std::unique_ptr<NativeBonusGraphPreparation> & preparation,
	std::unique_ptr<PreparedReplayAppend> & replay)
{
	if(!world || !access.owns_lock() || access.mutex() != &CGameState::mutex
		|| !preparation || !preparation->activeOnThisThread() || !replay
		|| &replay->owner != &world->replayLog || !replay->readyToCommit())
		throw std::invalid_argument("Replay adoption requires its exact world, active scope and fresh log batch");
	// Continuous SAME state/native owners; no unlock/relock and no replay writer,
	// snapshot, new reservation or standalone replay acceptance before world CAS.
	impl = std::make_unique<Data>(std::move(world), std::move(access), packet,
		std::move(preparation), std::move(replay));
}
