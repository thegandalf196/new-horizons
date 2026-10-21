/*
 * MapViewTransitionSnapshot.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "MapViewController.h"
#include "MapViewCache.h"
#include "MapTransitionSnapshotTask.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../../lib/UnlockGuard.h"

MapViewController::~MapViewController()
{
	if(transitionSnapshotTask)
		transitionSnapshotTask->cancel();
}

void MapViewController::captureTransitionSnapshotOnGui()
{
	if(ENGINE->amIGuiThread())
	{
		try
		{
			view->createTransitionSnapshot(context);
		}
		catch(...)
		{
			logGlobal->error("GUI pre-teleport snapshot failed; entering orderly application shutdown");
			throw GameShutdownException();
		}
		return;
	}

	auto task = std::make_shared<MapTransitionSnapshotTask>([view = view, context = context]
	{
		view->createTransitionSnapshot(context);
	});
	transitionSnapshotTask = task;
	bool queued = false;
	try
	{
		queued = ENGINE->tryDispatchMainThread([task]
		{
			assert(ENGINE->amIGuiThread());
			if(!task->runOnGui())
			{
				logGlobal->error("GUI pre-teleport snapshot failed; requesting orderly application shutdown");
				ENGINE->requestGuiTaskFailureShutdown();
			}
		});
	}
	catch(...)
	{
		// Queue allocation/posting failure must not terminate just runNetwork.
	}
	if(!queued)
	{
		task->failDispatch();
		logGlobal->error("GUI pre-teleport snapshot dispatch failed; requesting orderly application shutdown");
		ENGINE->requestGuiTaskFailureShutdown();
	}

	{
		// Same observer seam as waitForOngoingAnimations; no prepared child.
		auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
		task->wait();
	}
	// Recheck AFTER reacquiring interfaceMutex: teardown may follow capture
	// before this receive stack resumes. Never apply into its retired world.
	if(task->wasCancelled())
		throw TerminationRequestedException();
}
