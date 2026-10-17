/*
 * AttackCount.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

namespace battle { class Unit; }

namespace attackCount
{
/// Existing physical count/follow-up distinction, shared by AP continuations.
bool hasPendingRangedFollowUp(const battle::Unit * unit);
}
