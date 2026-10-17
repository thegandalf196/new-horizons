/*
 * ArmyExchangeExecution.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <memory>

class CCallback;
class CArmedInstance;

namespace NK2AI::armyFormation
{
struct ArmyExchangeProjection;

/// Execute the valued physical plan as-is; stop at the first stale or unapplied transfer.
/// Requests remain authoritative and restore the caller's synchronous wait mode.
DLL_EXPORT void executeArmyExchange(const std::shared_ptr<CCallback> & cc,
	const CArmedInstance * destinationArmy, const CArmedInstance * source,
	const ArmyExchangeProjection & projection);
}
