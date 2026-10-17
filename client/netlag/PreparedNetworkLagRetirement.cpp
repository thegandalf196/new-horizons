/*
 * PreparedNetworkLagRetirement.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedNetworkLagRetirement.h"
#include "NetworkLagReplyStateControl.h"
#include <ranges>
#include <stdexcept>

std::unique_ptr<PreparedNetworkLagRetirement> NetworkLagReplyState::prepareReplyRetirement(
	const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & interfaceAccess)
{
	return std::unique_ptr<PreparedNetworkLagRetirement>(new PreparedNetworkLagRetirement(*this, inspection, interfaceAccess));
}

PreparedNetworkLagRetirement::PreparedNetworkLagRetirement(NetworkLagReplyState & source,
	const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & access)
	: interfaceAccess(access), control(source.inspectionControl),
	  expectedStamp(inspection.stamp), action(inspection.verifiedResult)
{
	if(!access.owns_lock() || !control->interfaceMutex || access.mutex() != control->interfaceMutex
		|| inspection.control.lock() != control || !inspection.stillCurrent()
		|| action == BattleReplyInspection::Result::APPLY || source.currentPackageID == source.invalidPackageID
		|| source.predictedReplies.empty() || source.predictedReplies.front().writtenPacks.empty())
		throw std::invalid_argument("Queue retirement requires its exact active inspection and real held interface lock");
	prediction = &source.predictedReplies.front();
	first = &prediction->writtenPacks.front();
	writtenCount = prediction->writtenPacks.size();
	requestID = source.currentPackageID;
	if(prediction->requestID != requestID)
		throw std::invalid_argument("Active package does not own the front prediction");
	if(action == BattleReplyInspection::Result::ROLLBACK_THEN_APPLY)
	{
		for(const auto & written : std::views::reverse(prediction->writtenPacks))
			for(const auto & packet : written.rollbackPacks)
				rollbackInputs.push_back(packet.get());
	}
	else
		for(const auto & packet : first->rollbackPacks)
			rollbackInputs.push_back(packet.get());
	if(!ready())
		throw std::runtime_error("Prediction queue changed during retirement preparation");
}

PreparedNetworkLagRetirement::~PreparedNetworkLagRetirement() = default;

bool PreparedNetworkLagRetirement::ready() const noexcept
{
	if(thread != std::this_thread::get_id() || consumed || !interfaceAccess.owns_lock()
		|| interfaceAccess.mutex() != control->interfaceMutex || !control->owner
		|| !expectedStamp || expectedStamp != control->stamp)
		return false;
	const auto & source = *control->owner;
	return source.currentPackageID == requestID && !source.predictedReplies.empty()
		&& &source.predictedReplies.front() == prediction && prediction->requestID == requestID
		&& prediction->writtenPacks.size() == writtenCount && !prediction->writtenPacks.empty()
		&& &prediction->writtenPacks.front() == first;
}

bool PreparedNetworkLagRetirement::commit() noexcept
{
	if(!ready())
		return false;
	exchangePreparedStorage();
	return true;
}

const std::vector<const CPackForClient *> & PreparedNetworkLagRetirement::rollbackPackets() const
{
	if(!consumed && !ready())
		throw std::logic_error("Original rollback inputs are no longer retained for preparation");
	return rollbackInputs;
}

void PreparedNetworkLagRetirement::exchangePreparedStorage() noexcept
{
	auto & source = *control->owner;
	if(action == BattleReplyInspection::Result::CONSUMED)
	{
		auto & written = prediction->writtenPacks;
		static_assert(noexcept(retiredWritten.splice(retiredWritten.end(), written, written.begin())));
		retiredWritten.splice(retiredWritten.end(), written, written.begin());
	}
	if(action == BattleReplyInspection::Result::ROLLBACK_THEN_APPLY || writtenCount == 1)
	{
		static_assert(noexcept(retiredPredictions.splice(retiredPredictions.end(), source.predictedReplies, source.predictedReplies.begin())));
		retiredPredictions.splice(retiredPredictions.end(), source.predictedReplies, source.predictedReplies.begin());
		source.currentPackageID = source.invalidPackageID;
	}
	control->stamp.swap(retiredStamp); // no token drop/deallocation in the tail
	consumed = true;
}
