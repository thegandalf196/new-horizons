/*
 * NetworkLagReplyStateControl.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <memory>
#include <mutex>
class NetworkLagReplyState;

/// Private lifetime/freshness metadata, accessed under the real client interval.
struct NetworkLagInspectionControl
{
	std::shared_ptr<const unsigned char> stamp;
	NetworkLagReplyState * owner = nullptr;
	std::mutex * interfaceMutex = nullptr;
};
