/*
 * NetworkLagReplyState.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkLagReplyState.h"
#include "NetworkLagReplyStateControl.h"
#include "NetworkLagRollbackCopy.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/BinarySerializer.h"
#include <ranges>
#include <stdexcept>

NetworkLagReplyState::NetworkLagReplyState(std::mutex * interfaceMutex)
	: inspectionControl(std::make_shared<NetworkLagInspectionControl>())
{
	inspectionControl->owner = this;
	inspectionControl->interfaceMutex = interfaceMutex;
}

NetworkLagReplyState::~NetworkLagReplyState()
{
	inspectionControl->owner = nullptr;
	inspectionControl->stamp.reset(); // BEFORE native queue/payload member retirement
}

void NetworkLagReplyState::invalidateInspection() noexcept { inspectionControl->stamp.reset(); }

void NetworkLagReplyState::beginPrediction(const CPackForServer & request)
{
	invalidateInspection();
	NetworkLagReplyPrediction prediction;
	prediction.requestID = request.requestID;
	prediction.senderID = request.player;
	predictedReplies.push_back(std::move(prediction));
}

void NetworkLagReplyState::appendPrediction(NetworkLagPredictedPack pack)
{
	invalidateInspection();
	predictedReplies.back().writtenPacks.push_back(std::move(pack));
}

void NetworkLagReplyState::openReplyPackage(const PackageReceived & packet)
{
	invalidateInspection();
	assert(currentPackageID == invalidPackageID);
	if(!predictedReplies.empty() && predictedReplies.front().requestID == packet.requestID)
	{
		[[maybe_unused]] const auto & nextPrediction = predictedReplies.front();
		assert(nextPrediction.senderID == packet.player);
		assert(nextPrediction.requestID == packet.requestID);
		currentPackageID = packet.requestID;
	}
}

bool NetworkLagReplyState::matchesNextReply(const CPackForClient & packet) const
{
	NetworkLagPredictedPack writer;
	BinarySerializer serializer(&writer);
	serializer & &packet;
	return writer.predictedPackData == predictedReplies.front().writtenPacks.front().predictedPackData;
}

BattleReplyInspection NetworkLagReplyState::inspectBattleReply(const BattleUnitsChanged & packet) const
{
	if(!inspectionControl->stamp)
		inspectionControl->stamp = std::make_shared<const unsigned char>(0);
	BattleReplyInspection result;
	result.control = inspectionControl;
	result.stamp = inspectionControl->stamp;
	if(currentPackageID == invalidPackageID)
		return result;
	if(predictedReplies.empty() || predictedReplies.front().writtenPacks.empty())
		throw std::logic_error("Active reply package has no predicted packet");
	if(matchesNextReply(packet))
		result.result = BattleReplyInspection::Result::CONSUMED;
	else
	{
		result.result = BattleReplyInspection::Result::ROLLBACK_THEN_APPLY;
		for(const auto & prediction : std::views::reverse(predictedReplies.front().writtenPacks))
			for(const auto & rollback : prediction.rollbackPacks)
				result.rollbacks.push_back(copyCurrentPredictionRollback(*rollback));
	}
	result.verifiedResult = result.result;
	return result;
}

bool BattleReplyInspection::stillCurrent() const noexcept
{
	const auto retained = control.lock();
	return retained && result == verifiedResult && stamp && stamp == retained->stamp;
}
