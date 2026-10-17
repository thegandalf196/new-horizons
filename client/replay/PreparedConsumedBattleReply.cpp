/*
 * PreparedConsumedBattleReply.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "PreparedClientBattleReplay.h"
#include "../netlag/PreparedNetworkLagRetirement.h"
#include "../../lib/gameState/PreparedReplayAppend.h"

bool PreparedClientBattleReplay::commitConsumedReply() noexcept
{
	// ALL refusal/identity/thread/caller/queue/replay checks precede first write.
	if(!queueRetirement || adoptedWorld || replyAction() != BattleReplyInspection::Result::CONSUMED || !ready())
		return false;
	queueRetirement->exchangePreparedStorage();
	batch->exchangePreparedStorage(); // SAME single unconditional outer reservation
	static_assert(noexcept(caller.changedStacks.swap(writerProjection.changedStacks)));
	caller.changedStacks.swap(writerProjection.changedStacks);
	consumed = true;
	return true;
}
