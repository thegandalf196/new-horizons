/*
 * MainThreadTaskQueue.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

/// Publish and wake atomically with respect to consumers and other producers.
/// wake only posts an SDL event; it must not synchronously consume this queue.
class MainThreadTaskQueue
{
	std::mutex mutex;
	std::deque<std::function<void()>> tasks;

public:
	bool tryPush(std::function<void()> task, const std::function<bool()> & wake)
	{
		std::function<void()> rejected;
		std::lock_guard lock(mutex);
		tasks.push_back(std::move(task));
		try
		{
			if(wake())
				return true;
		}
		catch(...)
		{
			rejected = std::move(tasks.back());
			tasks.pop_back(); // Our entry; no producer/consumer can interleave.
			throw;
		}
		rejected = std::move(tasks.back());
		tasks.pop_back(); // Destroy rejected captures only after unlocking.
		return false;
	}

	bool tryPop(std::function<void()> & task)
	{
		std::function<void()> next;
		{
			std::lock_guard lock(mutex);
			if(tasks.empty())
				return false;
			next = std::move(tasks.front());
			tasks.pop_front();
		}
		task = std::move(next);
		return true; // Invoke only after releasing this queue's lock.
	}
};
