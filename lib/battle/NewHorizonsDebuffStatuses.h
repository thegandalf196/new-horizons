/*
 * NewHorizonsDebuffStatuses.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <vcmi/scripting/ApiTags.h>

#include <cstddef>
#include <string>
#include <vector>

namespace battle { class Unit; }
struct Bonus;

namespace newHorizonsDebuffStatuses
{
inline constexpr const char * POISON_IDENTITY = "poison";

/// Apply the shared authored runtime-producer descriptor to a typed spell
/// component. Used by native factories; the same descriptors adapt old saves.
DLL_LINKAGE void applyProducerMetadata(Bonus & bonus);

/// Immutable, lexically ordered logical status identities. Several components,
/// sources and refreshed applications with the same identity count only once.
struct DLL_LINKAGE StatusSnapshot
{
	std::vector<std::string> identities;

	size_t count() const { return identities.size(); }
};

/// Read effective DEBUFF-tagged bonuses and the typed stored physical-Poison
/// state without mutating the battle. Duration and raw stat signs do not infer
/// classification: an ongoing PERMANENT-duration debuff is eligible, whereas
/// intrinsic creature characteristics must remain untagged.
/// New producers author a nonempty valid statusIdentity. Historical tag-only
/// records use registered component metadata or stable typed-source identity;
/// untagged spell components use current authored registered metadata. Invalid
/// explicit metadata still throws; absent historical identity is not an error.
/// Battlefield consumers choose their recipient set and capture all snapshots
/// before applying effects; this query does not impose damage-target legality.
DLL_LINKAGE StatusSnapshot snapshot(const battle::Unit & unit);
}
