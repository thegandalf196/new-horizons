/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

namespace aerialSupremacyPresentation
{
/// Facing belongs to every typed melee blow, including a point-blank zero-hop.
/// Only actual travel queues flight and return stages.
template<typename Position, typename StageFacing>
bool stageExcursionFacing(const Position & origin, const Position & landing, StageFacing stageFacing)
{
	stageFacing();
	return origin != landing;
}

/// Animation state is checked when a queued stage executes, after hit/death stages.
/// In particular the logical CStack may still be alive until its packet applies.
template<typename Animation, typename Play>
void animateReturn(Animation * animation, Play play)
{
	if(animation && !animation->isDeadOrDying())
		play();
}

/// A dead visual never flies back: reconcile its sprite with the unchanged
/// authoritative footprint. The callbacks create animations only for survivors.
template<typename Animation, typename Position, typename Play>
void finishReturn(Animation * animation, const Position & origin, Play play)
{
	if(!animation)
		return;
	if(animation->isDeadOrDying())
		animation->pos.moveTo(origin);
	else
		play();
}
}
