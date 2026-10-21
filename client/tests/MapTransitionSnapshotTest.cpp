/*
 * MapTransitionSnapshotTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../gui/MainThreadTaskQueue.h"
#include "../mapView/MapTransitionSnapshotTask.h"
#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

static void require(bool value)
{
	if(!value)
		throw std::runtime_error("Map transition snapshot regression");
}

struct DispatchOnDestruction
{
	MainThreadTaskQueue & queue;
	std::atomic<bool> & posted;
	std::atomic<int> & called;
	DispatchOnDestruction(MainThreadTaskQueue & queue, std::atomic<bool> & posted, std::atomic<int> & called)
		: queue(queue), posted(posted), called(called)
	{
	}
	~DispatchOnDestruction()
	{
		posted = queue.tryPush([&called = called] { ++called; }, [] { return true; });
	}
};

static void queuePublicationAndFailureOwnership()
{
	MainThreadTaskQueue queue;
	std::promise<void> consumerEntered;
	std::atomic<int> calls = 0;
	std::thread consumer;
	require(queue.tryPush([&] { ++calls; }, [&]
	{
		consumer = std::thread([&]
		{
			consumerEntered.set_value();
			std::function<void()> task;
			require(queue.tryPop(task));
			task();
		});
		consumerEntered.get_future().wait();
		return true;
	}));
	consumer.join();
	require(calls == 1);

	std::vector<int> order;
	require(queue.tryPush([&] { order.push_back(1); }, [] { return true; }));
	require(!queue.tryPush([&] { order.push_back(2); }, [] { return false; }));
	bool wakeThrew = false;
	try
	{
		queue.tryPush([&] { order.push_back(3); }, []() -> bool { throw std::runtime_error("wake"); });
	}
	catch(const std::runtime_error &)
	{
		wakeThrew = true;
	}
	require(wakeThrew);
	require(queue.tryPush([&]
	{
		order.push_back(4);
		require(queue.tryPush([&] { order.push_back(5); }, [] { return true; }));
	}, [] { return true; }));
	for(;;)
	{
		std::function<void()> task;
		if(!queue.tryPop(task))
			break;
		task(); // Recursive dispatch is outside the queue lock.
	}
	require(order == std::vector<int>({1, 4, 5}));
	std::atomic<bool> destructorPosted = false;
	std::atomic<int> destructorCallback = 0;
	auto token = std::make_shared<DispatchOnDestruction>(queue, destructorPosted, destructorCallback);
	std::function<void()> rejected = [token] {};
	token.reset();
	require(!queue.tryPush(std::move(rejected), [] { return false; }));
	require(destructorPosted);
	std::function<void()> destructorTask;
	require(queue.tryPop(destructorTask));
	destructorTask();
	require(destructorCallback == 1);
}

static void guiCaptureBeforeApply(bool cancelAfterCapture)
{
	MainThreadTaskQueue queue;
	std::mutex interfaceMutex;
	std::promise<void> queued;
	std::shared_ptr<MapTransitionSnapshotTask> shared;
	const auto gui = std::this_thread::get_id();
	std::thread::id capturedOn;
	int world = 0;
	std::atomic<bool> applied = false;
	std::thread receiver([&]
	{
		std::unique_lock lock(interfaceMutex);
		auto task = std::make_shared<MapTransitionSnapshotTask>([&]
		{
			require(world == 0);
			capturedOn = std::this_thread::get_id();
		});
		shared = task;
		require(queue.tryPush([task] { require(task->runOnGui()); }, [] { return true; }));
		queued.set_value();
		lock.unlock();
		task->wait();
		lock.lock();
		if(!task->wasCancelled())
		{
			world = 1;
			applied = true;
		}
	});
	queued.get_future().wait();
	{
		std::lock_guard lock(interfaceMutex);
		require(!applied && world == 0);
		std::function<void()> task;
		require(queue.tryPop(task));
		task();
		if(cancelAfterCapture)
			shared->cancel(); // Completed capture does not authorize retired apply.
	}
	receiver.join();
	require(capturedOn == gui);
	require(applied == !cancelAfterCapture);
	require(world == (cancelAfterCapture ? 0 : 1));
}

static void cancelledQueueDropsBorrowedScene()
{
	auto scene = std::make_shared<int>(0);
	std::weak_ptr<int> weak = scene;
	auto task = std::make_shared<MapTransitionSnapshotTask>([scene] { ++*scene; });
	task->cancel();
	scene.reset();
	require(weak.expired());
	require(task->runOnGui()); // Late callback has no controller/context to touch.
	task->wait();
	require(task->wasCancelled());
}

static void unexpectedFailureWaitsForActualShutdown(bool rejectedDispatch)
{
	auto task = std::make_shared<MapTransitionSnapshotTask>([] { throw std::runtime_error("capture"); });
	std::atomic<bool> failureRequested = false;
	if(rejectedDispatch)
	{
		task->failDispatch();
		failureRequested = true;
	}
	else
	{
		require(!task->runOnGui());
		failureRequested = true;
	}
	std::promise<void> entered;
	std::atomic<bool> cancellationStarted = false;
	std::atomic<bool> returnedBeforeCancel = false;
	auto receiver = std::async(std::launch::async, [&]
	{
		entered.set_value();
		task->wait();
		returnedBeforeCancel = !cancellationStarted.load();
		return task->wasCancelled();
	});
	entered.get_future().wait();
	require(failureRequested);
	// Bounded negative observation, not a zero-time readiness poll that can
	// pass just because the receiver was descheduled before entering wait().
	require(receiver.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
	require(!returnedBeforeCancel);
	cancellationStarted = true;
	task->cancel(); // Actual normal cleanup, not failure pretending to be success.
	require(receiver.get());
	require(!returnedBeforeCancel);
	require(task->runOnGui());
}

int main()
{
	try
	{
		queuePublicationAndFailureOwnership();
		guiCaptureBeforeApply(false);
		guiCaptureBeforeApply(true);
		cancelledQueueDropsBorrowedScene();
		unexpectedFailureWaitsForActualShutdown(false);
		unexpectedFailureWaitsForActualShutdown(true);
		std::cout << "Map transition task ownership/order/cancellation checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
