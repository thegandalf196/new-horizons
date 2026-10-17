/*
 * PreparedClientBattleReplay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "PreparedClientBattleReplay.h"
#include "ClientReceiveAccess.h"
#include "ClientReplaySessionReadiness.h"
#include "../netlag/PreparedNetworkLagRetirement.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/gameState/PreparedReplaySnapshot.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../../server/BattleUnitsChangedInput.h"
#include <stdexcept>

PreparedClientBattleReplay::PreparedClientBattleReplay(std::shared_ptr<CGameState> world,
	BattleUnitsChanged & packet)
	: world(std::move(world)), stateAccess(CGameState::mutex),
	  preparation(std::make_unique<NativeBonusGraphPreparation>()), caller(packet),
	  originalInput(packet), writerProjection(packet)
{
	if(!this->world)
		throw std::invalid_argument("Client replay requires its real retained world");
	// This happens BEFORE outer writer/lag encoding, even when configured false.
	snapshot = capturePreparedReplaySnapshot(*this->world, true);
	outerRecord = ReplayPackSerializer::write(writerProjection);
}

void PreparedClientBattleReplay::finish(const NetworkLagReplyState * predictions)
{
	if(predictions)
		inspection = predictions->inspectBattleReply(writerProjection);
	PreparedReplayAppend::Records records;
	records.push_back(std::move(outerRecord)); // unconditional outer client record
	if(world->replayLog.isRecordingPacks())
	{
		if(inspection)
			for(const auto & rollback : inspection->rollbacks)
				records.push_back(ReplayPackSerializer::write(*rollback));
		if(replyAction() != BattleReplyInspection::Result::CONSUMED)
			records.push_back(ReplayPackSerializer::write(writerProjection));
	}
	auto capturedSnapshot = [this] { return std::move(snapshot); };
	batch = std::make_unique<PreparedReplayAppend>(world->replayLog, std::move(records), world->day,
		capturedSnapshot, PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
}

PreparedClientBattleReplay::~PreparedClientBattleReplay() = default;

bool PreparedClientBattleReplay::ready() const noexcept
{
	return (!receiveAccess || receiveAccess->owns(*this))
		&& (!receiveSession || receiveSession->ready(caller))
		&& (!queueRetirement || queueRetirement->ready())
		&& !consumed && sameBattleUnitsChangedInput(caller, originalInput)
		&& (!inspection || inspection->stillCurrent()) && (adoptedWorld ? adoptedWorld->readyToCommit() : batch && batch->readyToCommit());
}

bool PreparedClientBattleReplay::commitReplayOnly() noexcept
{
	if(queueRetirement || adoptedWorld || !ready() || !batch->commit())
		return false;
	static_assert(noexcept(caller.changedStacks.swap(writerProjection.changedStacks)));
	caller.changedStacks.swap(writerProjection.changedStacks);
	consumed = true;
	return true;
}

BattleReplyInspection::Result PreparedClientBattleReplay::replyAction() const noexcept
{
	return inspection ? inspection->result : BattleReplyInspection::Result::APPLY;
}

size_t PreparedClientBattleReplay::rollbackCount() const noexcept
{
	return inspection ? inspection->rollbacks.size() : 0;
}

std::unique_ptr<PreparedClientBattleReplay> prepareClientBattleReplayEntry(
	std::shared_ptr<CGameState> world, BattleUnitsChanged & packet, const NetworkLagReplyState * predictions)
{
	NativeBonusGraphPreparation::checkEntry(); // before nonrecursive state acquisition
	auto result = std::unique_ptr<PreparedClientBattleReplay>(new PreparedClientBattleReplay(std::move(world), packet));
	result->finish(predictions);
	return result;
}
