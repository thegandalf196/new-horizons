/*
 * NewHorizonsFriendlyFire.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "ISpellMechanics.h"

#include "../constants/EntityIdentifiers.h"

#include <cstdint>
#include <vector>

class CStack;

namespace newHorizonsFriendlyFire
{

/// A read-only preview of friendly stacks which a hero damage effect may hurt.
/// Stack pointers are borrowed from the current battle and must not outlive it.
DLL_LINKAGE std::vector<const CStack *> potentialFriendlyDamageTargets(
	const spells::Mechanics & mechanics,
	const spells::Target & aimPoint);

/// Snapshot used by the client confirmation dialog. Unit IDs are sorted and
/// unique so an authoritative stack change invalidates the pending confirmation.
struct DLL_LINKAGE ConfirmationSnapshot
{
	BattleID battleID;
	SpellID spellID;
	ObjectInstanceID heroID;
	BattleSide casterSide = BattleSide::NONE;
	int32_t round = -1;
	uint64_t castingSession = 0;
	std::vector<uint32_t> friendlyUnitIDs;

	bool operator==(const ConfirmationSnapshot &) const = default;
};

/// One-shot modal decision state. A stale snapshot or repeated button callback
/// can never authorize a second request.
class DLL_LINKAGE ConfirmationGate
{
	ConfirmationSnapshot snapshot;
	bool completed = false;

public:
	explicit ConfirmationGate(ConfirmationSnapshot value);

	bool confirm(const ConfirmationSnapshot & current);
	bool cancel();
	bool isPending() const;
	const ConfirmationSnapshot & getSnapshot() const;
};

}
