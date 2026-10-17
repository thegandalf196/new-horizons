/*
 * BattleRootCommitAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleRootLifetime.h"
#include "CGameState.h"

BattleRootLifetime::CommitAdmission::CommitAdmission(std::shared_ptr<CGameState> world,
	BattleRootLifetime & owner, std::thread::id thread) noexcept
	: world(std::move(world)), owner(owner)
{
	if(thread != std::this_thread::get_id())
		return;
	// Contention and lock acquisition errors are refusal BEFORE graph CAS.
	// No wait/allocator/callback or root-lock acquisition is hidden in this gate.
	try
	{
		lock = std::unique_lock<std::recursive_mutex>(owner.pinStateMutex, std::try_to_lock);
	}
	catch(...)
	{
		return;
	}
	if(!lock.owns_lock() || owner.committing || !owner.pins || owner.pinThread != thread
		|| owner.mutations || owner.retirements.load() || owner.permanentlyRetired.load())
		return;
	owner.committing = true;
	accepted = true;
}

BattleRootLifetime::CommitAdmission::~CommitAdmission()
{
	if(accepted)
		owner.committing = false;
	// Already-owned state lock releases BEFORE the strong world reference.
	// No acquisition/query/callback/allocation occurs after a successful CAS.
}

BattleRootLifetime::CommitAdmission BattleRootLifetime::Pin::tryCommit() const noexcept
{
	return CommitAdmission(world, owner, thread);
}
