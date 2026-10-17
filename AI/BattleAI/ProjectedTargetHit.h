/*
 * ProjectedTargetHit.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>

class HypotheticBattle;
namespace battle { class Unit; class CUnitState; }

namespace projectedTargetHit
{
/// Damage was computed upstream with damageRetaliation. Reactive activation
/// semantics are separate: Brace has damageRetaliation=false/reactive=true.
struct Context
{
	uint32_t targetId;
	bool eligibleForLastStand;
	bool damageRetaliation;
	bool reactiveForLastStand;
};

/// One ordered hit preparation. Owned values only; neither this nor Context
/// retains a Unit/provider. It is not damage, aftermath or a completed action.
struct Prepared
{
	Context context;
	int64_t damageToApply;
	bool lastStandTriggered;
};

/// Branch-only: clamp request, check Last Stand, then consume its side token.
/// Caller next captures Bastion/Rebirth and applies ordinary health damage.
Prepared prepare(HypotheticBattle & battle, const battle::Unit & target,
	int64_t requestedDamage, Context context);

/// Call AFTER health damage and AFTER_TARGET_HIT Defiant, before Bastion's
/// receipt and later aftermath. Shared canonical Defend owns bonus/lifecycle
/// changes. Returns whether this hit ended the current incoming activation.
bool finishAfterDefiant(HypotheticBattle & battle, battle::CUnitState & target,
	const Prepared & prepared);
}
