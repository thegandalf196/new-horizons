/*
 * StackHotStats.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/UnitHotStats.h"
#include <cstdint>
#include <thread>

/// Every value a memoised answer of one projected stack can read besides the
/// bonus records themselves: the branch and bearer bonus-tree versions (the
/// BonusCache key), the branch's unit-state epoch (advanced by every roster
/// and bonus mutator), the branch round and active stack, the view's scalars,
/// and the shape and record identities of the public overlay containers of
/// this view and of every projected layer below it. Those containers may be
/// edited without a version bump, and queries must see such edits at once.
struct StackHotStatsStamp
{
	const void * bearer = nullptr;
	uint64_t branchEpoch = 0;
	/// Mixed record identities and sizes of all projected layers' overlays.
	uint64_t overlays = 0;
	int32_t branchVersion = 0;
	int32_t bearerVersion = 0;
	int32_t localVersion = 0;
	int32_t formRevision = 0;
	int32_t round = 0;
	int32_t activeStack = 0;
	int32_t count = 0;
	int32_t firstHitPoints = 0;
	uint32_t layers = 0;
	int16_t position = 0;
	uint16_t flags = 0;
	bool operator==(const StackHotStatsStamp &) const = default;
};

/// Slots of one StackWithBonuses and their key: a generation number that
/// advances whenever the stamp differs from the last one seen. A projected
/// view belongs to the AI thread that built it; other threads bypass it, so
/// the stamp needs no synchronisation. Copies and assignments start empty.
class StackHotStats
{
public:
	StackHotStats() = default;
	StackHotStats(const StackHotStats &) noexcept {}
	StackHotStats & operator=(const StackHotStats &) noexcept
	{
		invalidate();
		return *this;
	}

	bool ownedByThisThread() const noexcept { return std::this_thread::get_id() == thread; }
	/// Current key for `stamp`: unchanged while the stamp is, advanced otherwise.
	uint32_t keyFor(const StackHotStatsStamp & stamp) const noexcept
	{
		if(valid && stamp == current)
			return generation;
		current = stamp;
		valid = true;
		if(++generation == 0)
		{
			slots.clear(); // A wrapped key must not meet an answer from 2^32 keys ago.
			generation = 1;
		}
		return generation;
	}
	/// Discard the key after a whole-state assignment.
	void invalidate() noexcept { valid = false; }

	battle::UnitHotStats slots;

private:
	mutable StackHotStatsStamp current;
	mutable uint32_t generation = 0;
	mutable bool valid = false;
	std::thread::id thread = std::this_thread::get_id();
};
