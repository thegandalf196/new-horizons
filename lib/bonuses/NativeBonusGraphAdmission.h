/*
 * NativeBonusGraphAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "NativeBonusGraphAccess.h"
#include <thread>

/// Checked ordinary MUTATION owner, not a guard on queries or mutable getters.
/// Acquires graph access before testing preparation admission and retains it
/// through the actual owner program. Nested ordinary owners remain supported.
/// Every participating owner must enter BEFORE replay/visitor/first mutation.
class DLL_LINKAGE NativeBonusGraphMutation final
{
	NativeBonusGraphAccess access;
public:
	NativeBonusGraphMutation();
	~NativeBonusGraphMutation();
	NativeBonusGraphMutation(const NativeBonusGraphMutation &) = delete;
	NativeBonusGraphMutation & operator=(const NativeBonusGraphMutation &) = delete;
	NativeBonusGraphMutation(NativeBonusGraphMutation &&) = delete;
	NativeBonusGraphMutation & operator=(NativeBonusGraphMutation &&) = delete;
};

/// Opt-in nonreentrant preparation interval; acquire AFTER state ownership and
/// BEFORE root capture. Queries may nest; participating ordinary mutations and
/// nested preparations refuse before writes. An ordinary mutation cannot become
/// a prepared transaction mid-visitor: a future router must intercept before
/// its ordinary mutation entry, then use prepared tails without legacy apply.
/// Own through participant/arena retirement on this thread. Does not grant node,
/// library, configuration or escaped-Bonus alias ownership or reader exclusion
/// outside integrated NativeBonusGraphAccess paths. No phase check/lock is added
/// to CAS or exchange tails. Ordinary bootstrap/shutdown qualification applies.
class DLL_LINKAGE NativeBonusGraphPreparation final
{
	NativeBonusGraphAccess access;
	const std::thread::id thread = std::this_thread::get_id();
public:
	NativeBonusGraphPreparation();
	/// Short reentry check BEFORE state locking; not a reservation/permit.
	static void checkEntry();
	/// Validate the exact live scope before an owning unique_ptr transfer.
	bool activeOnThisThread() const noexcept;
	~NativeBonusGraphPreparation();
	NativeBonusGraphPreparation(const NativeBonusGraphPreparation &) = delete;
	NativeBonusGraphPreparation & operator=(const NativeBonusGraphPreparation &) = delete;
	NativeBonusGraphPreparation(NativeBonusGraphPreparation &&) = delete;
	NativeBonusGraphPreparation & operator=(NativeBonusGraphPreparation &&) = delete;
};
