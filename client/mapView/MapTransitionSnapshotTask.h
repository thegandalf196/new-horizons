/*
 * MapTransitionSnapshotTask.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>

/// Receive stack and GUI callback share this state, never a raw controller.
/// Failure is NOT completion: the receiver remains parked until orderly
/// shutdown calls cancel(). Cancel may override an already completed capture.
class MapTransitionSnapshotTask
{
	std::mutex mutex;
	std::condition_variable changed;
	std::function<void()> capture;
	bool completed = false;
	bool cancelled = false;
	bool failed = false;

public:
	explicit MapTransitionSnapshotTask(std::function<void()> capture)
		: capture(std::move(capture))
	{
	}

	bool runOnGui()
	{
		std::lock_guard lock(mutex);
		if(cancelled || completed)
			return true;
		if(failed)
			return false;
		try
		{
			capture(); // Caller owns the GUI thread and interface mutex.
		}
		catch(...)
		{
			failed = true;
			capture = {};
			return false;
		}
		capture = {};
		completed = true;
		changed.notify_all();
		return true;
	}

	void failDispatch()
	{
		std::lock_guard lock(mutex);
		failed = true;
		capture = {};
	}

	void cancel()
	{
		std::lock_guard lock(mutex);
		cancelled = true;
		capture = {}; // Release borrowed scene inputs before its destruction.
		changed.notify_all();
	}

	void wait()
	{
		std::unique_lock lock(mutex);
		changed.wait(lock, [this] { return completed || cancelled; });
	}

	bool wasCancelled()
	{
		std::lock_guard lock(mutex);
		return cancelled;
	}
};
