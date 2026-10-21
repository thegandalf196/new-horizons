/*
 * TaskExecution.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../Goals/AbstractGoal.h"

#include <optional>

namespace NK2AI
{
struct TaskExecutionResult
{
	bool success;
	std::optional<ObjectInstanceID> unavailableRouteHero;
};

TaskExecutionResult executeTask(AIGateway & gateway, const Goals::TTask & task);
}
