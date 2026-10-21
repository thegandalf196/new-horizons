/*
 * TaskExecution.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TaskExecution.h"
#include "Nullkiller.h"
#include "../AIGateway.h"

namespace NK2AI
{
bool reserveUnavailableRouteReplan(
	const TaskExecutionResult & execution, std::set<ObjectInstanceID> & replannedRouteHeroes)
{
	return !execution.success && execution.unavailableRouteHero
		&& replannedRouteHeroes.insert(*execution.unavailableRouteHero).second;
}

bool Nullkiller::tryReplanUnavailableRoute(
	const TaskExecutionResult & execution, std::set<ObjectInstanceID> & replannedRouteHeroes)
{
	if(!execution.unavailableRouteHero)
		return false;
	if(reserveUnavailableRouteReplan(execution, replannedRouteHeroes))
	{
		logAi->debug("Current route for hero %d became unavailable. Replanning without locking the hero.",
			execution.unavailableRouteHero->getNum());
		return true;
	}
	logAi->error("Current route for hero %d remains unavailable after its bounded replan.",
		execution.unavailableRouteHero->getNum());
	return false;
}

TaskExecutionResult executeTask(AIGateway & gateway, const Goals::TTask & task)
{
	auto start = std::chrono::high_resolution_clock::now();
	std::string taskDescr = task->toString();
	auto & ai = *gateway.nullkiller;

	ai.makingTurnInterruption.interruptionPoint();
	logAi->debug("Trying to realize %s (value %2.3f)", taskDescr, task->priority);

	try
	{
		task->accept(&gateway);
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(goalFulfilledException &)
	{
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(const unavailableRouteException & e)
	{
		ai.invalidatePathfinderData();
		return {false, e.heroID};
	}
	catch(cannotFulfillGoalException & e)
	{
		ai.invalidatePathfinderData();
		logAi->error("Failed to realize subgoal of type %s.", taskDescr);
		logAi->error("The error message was: %s", e.what());
		return {false, std::nullopt};
	}

	return {true, std::nullopt};
}
}
