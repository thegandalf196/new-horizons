/*
 * NativeBonusGraphAccess.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <mutex>

/// Native graph/cache access interval, NOT node ownership or an alias lease.
/// First-party graph operations may nest on their existing execution thread.
/// Recursion is NOT same-thread nonreentrancy; selectors/updaters remain pure.
/// Acquire state/interface locks, if needed, BEFORE this interval, then native
/// root/cache locks. Do not wait for interface/state locks, transport/GUI callbacks
/// or thread joins while held. Prepared commit tails must perform no I/O or callbacks.
/// Ordinary library bootstrap scopes include resource/configuration I/O and script
/// initialization; they are not callback-free prepared commit intervals.
/// Ordinary shutdown may run Lua finalizers. Its owner must establish quiescence
/// and compatible callback/lock ordering before destruction, retaining the outer
/// world/library lifetime; this mutex alone does not prove those obligations.
/// Returned raw Bonus/vector aliases require a longer outer owner interval;
/// locking a getter does not make an escaped alias safe.
/// Thread-affine through destruction. No lock is acquired in a commit tail;
/// the common owner must already retain this interval until controlled retirement.
/// Process-global exclusive access can serialize native/projected reads; its
/// performance impact requires measurement, not an assumed negligible cost.
class DLL_LINKAGE NativeBonusGraphAccess final
{
	std::unique_lock<std::recursive_mutex> access;
public:
	NativeBonusGraphAccess();
	~NativeBonusGraphAccess();
	NativeBonusGraphAccess(const NativeBonusGraphAccess &) = delete;
	NativeBonusGraphAccess & operator=(const NativeBonusGraphAccess &) = delete;
	NativeBonusGraphAccess(NativeBonusGraphAccess &&) = delete;
	NativeBonusGraphAccess & operator=(NativeBonusGraphAccess &&) = delete;
};
