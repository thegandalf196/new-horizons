/*
 * NewHorizonsMarchLeft.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../GameConstants.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

class CArmedInstance;
class CGHeroInstance;
class CStackInstance;

/// Day's March removes same-day hero chaining. Each army stack records the share of
/// today's march it has left; a hero who receives troops keeps at most that share of
/// his own daily movement, taken from the received stack with the least march left.
namespace newHorizonsMarchLeft
{
/// March left is stored in parts of FULL; FULL is a fresh stack's whole day.
constexpr int FULL = 10000;

/// Share of the daily march left: remaining / full, rounded down and clamped to
/// [0, FULL]. Movement above the daily limit counts as a full day left.
DLL_LINKAGE int marchLeft(int remainingMovement, int fullDailyMovement);

/// Movement left after receiving troops with this march left:
/// min(remaining, march left x full), rounded up. Never raises movement.
DLL_LINKAGE int movementAfterReceiving(int remainingMovement, int fullDailyMovement, int marchLeft);

/// Day's March applies to heroes that use the New Horizons movement rules.
DLL_LINKAGE bool appliesTo(const CGHeroInstance & hero);

/// The hero's daily movement on his current layer (land or the boat's layer).
DLL_LINKAGE int fullDailyMovement(const CGHeroInstance & hero);

/// Lowers every army stack of a hero who has just spent movement to his share of
/// the day left. Called while applying movement packets.
DLL_LINKAGE void recordMarch(CGHeroInstance & hero, int day);

/// Creatures leaving the hero's Demonic Reserve take his march left: the reserve
/// travels with him. Call before the hero's reserve is replaced by nextReserve.
DLL_LINKAGE void recordReserveWithdrawal(CGHeroInstance & hero,
	const std::map<CreatureID, TQuantity> & nextReserve, int day);

/// Whether any stack of the hero has less than a full day's march left today, i.e.
/// he spent movement or took marched troops. A hero re-hired the same day keeps his movement.
DLL_LINKAGE bool hasMarchedToday(const CGHeroInstance & hero, int day);

/// Movement the hero keeps after holding a received stack with this march left,
/// or nullopt when the rule leaves his movement unchanged.
DLL_LINKAGE std::optional<int> limitedMovement(const CGHeroInstance & hero, int marchLeft);

/// Whole percent of the day's march left shown to the player, rounded to nearest.
DLL_LINKAGE int percentLeft(int marchLeft);

/// Garrison-slot hover text: a stack with less than a full day left gets
/// ": 20% of today's march left"; a fresh stack, an empty slot (nullptr) or another
/// day leaves the text unchanged.
DLL_LINKAGE std::string withMarchLeft(std::string hoverText, const CStackInstance * stack, int day);

/// One stack moving from one army to another, as a player's exchange would move it.
struct StackTransfer
{
	const CArmedInstance * from = nullptr;
	SlotID fromSlot;
	const CArmedInstance * to = nullptr;
	SlotID toSlot;
	bool swap = false; ///< exchanged with the stack at toSlot instead of merging into it
};

/// A receiving hero whose movement the transfers would lower.
struct TransferEffect
{
	const CGHeroInstance * hero = nullptr;
	int troopMarchLeft = FULL; ///< least march left among the stacks he receives (after merging)
	int movementBefore = 0;
	int movementAfter = 0;
};

/// Projects what the authoritative limit will do, for the exchange warning.
/// A received stack merging into one of his own keeps the lower march left.
DLL_LINKAGE std::vector<TransferEffect> projectTransfers(const std::vector<StackTransfer> & transfers, int day);
}
