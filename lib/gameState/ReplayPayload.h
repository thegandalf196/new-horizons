/*
 * ReplayPayload.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../serializer/ESerializationVersion.h"
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

/// Raw bytes alone carry no proven encoding. Only a canonical writer or explicit
/// trusted origin can supply a known version; NONE remains opaque storage.
struct DLL_LINKAGE ReplayPayload
{
	std::vector<std::byte> bytes;
	ESerializationVersion version = ESerializationVersion::NONE;
	ReplayPayload() = default;
	ReplayPayload(std::vector<std::byte> bytes, ESerializationVersion version = ESerializationVersion::NONE)
		: bytes(std::move(bytes)), version(version) {}
	ReplayPayload(std::initializer_list<std::byte> bytes) : bytes(bytes) {}
	bool operator==(const ReplayPayload &) const = default;
};
namespace replayPayload
{
DLL_LINKAGE void validateStoredVersion(ESerializationVersion version);
DLL_LINKAGE void requireKnownVersion(ESerializationVersion version);
}
