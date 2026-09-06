/*
 * ExperimentalSetFormation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#ifdef NH_PERF_EXPERIMENTS

#include <memory>

class INetworkConnection;
struct SetFormation;

enum class ExperimentalSetFormationResult
{
	NOT_ELIGIBLE, // Nothing queued; ordinary byte delivery may be used instead.
	QUEUED,       // Irrevocable: cancellation or later failure must NEVER trigger byte retry.
	CLOSED        // Transport unavailable; report failure, not fallback.
};

class DLL_LINKAGE ExperimentalSetFormationReceiver
{
public:
	virtual ~ExperimentalSetFormationReceiver() = default;
	virtual void onExperimentalSetFormation(const std::shared_ptr<INetworkConnection> & connection, SetFormation & pack) = 0;
};

// NetworkServer owns this lease; endpoints keep only weak references. It prevents
// pending tasks from accessing a destroyed receiver after stop/join. It does NOT
// permit destroying the server concurrently with an executing callback: the same
// executor stop/join lifetime contract as ordinary byte delivery remains required.
struct DLL_LINKAGE ExperimentalSetFormationLease
{
	ExperimentalSetFormationReceiver * const receiver;
	explicit ExperimentalSetFormationLease(ExperimentalSetFormationReceiver * receiver) : receiver(receiver) {}
};

DLL_LINKAGE ExperimentalSetFormationResult queueExperimentalSetFormation(
	const std::shared_ptr<INetworkConnection> & connection, const SetFormation & pack);

#endif
