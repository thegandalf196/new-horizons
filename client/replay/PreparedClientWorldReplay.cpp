/*
 * PreparedClientWorldReplay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "PreparedClientBattleReplay.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../../server/BattleUnitsChangedInput.h"
#include <stdexcept>

void PreparedClientBattleReplay::adoptIncomingBattleWorld()
{
	if(adoptedWorld || !ready())
		throw std::logic_error("Client replay batch is stale, adopted or consumed");
	if(replyAction() != BattleReplyInspection::Result::APPLY)
		throw std::logic_error("This incoming-only program cannot replace the pending lag world/queue/observer program");
	try
	{
		adoptedWorld.reset(new battle::PreparedBattleWorldCommit(world,
			std::move(stateAccess), writerProjection, preparation, batch));
	}
	catch(...)
	{
		// Failed construction may have consumed the interval/batch. Abandon this
		// private owner; no retry/fallback/prefix acceptance is permitted.
		consumed = true;
		preparation.reset();
		throw;
	}
}

const battle::ProjectedBattleWorldArena & PreparedClientBattleReplay::projectedWorld() const
{
	if(!adoptedWorld || consumed)
		throw std::logic_error("Client entry has no queryable adopted world");
	return adoptedWorld->projected();
}

void PreparedClientBattleReplay::prepareWorld(const TNodesVector & mutableNodes, const TCNodesVector & readOnlyNodes)
{
	if(!adoptedWorld || consumed || !sameBattleUnitsChangedInput(caller, originalInput)
		|| (inspection && !inspection->stillCurrent()))
		throw std::logic_error("Client input or lag inspection changed before world preparation");
	adoptedWorld->prepare(mutableNodes, readOnlyNodes);
}

bool PreparedClientBattleReplay::commitWorldAndReplay() noexcept
{
	if(!adoptedWorld || !ready() || !adoptedWorld->commitSimulation())
		return false;
	// Graph CAS and value/root/sidecar/single replay tail already succeeded.
	// Preserve the one retained caller writer projection without post-CAS work.
	static_assert(noexcept(caller.changedStacks.swap(writerProjection.changedStacks)));
	caller.changedStacks.swap(writerProjection.changedStacks);
	consumed = true;
	return true;
}
