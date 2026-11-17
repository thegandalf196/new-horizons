/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "ESerializationVersion.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace gameConnectionProtocol
{
inline constexpr std::byte MAGIC[]{std::byte{'N'}, std::byte{'H'}, std::byte{'P'}, std::byte{1}};
inline constexpr size_t HEADER_SIZE = sizeof(MAGIC) + sizeof(uint32_t);

/// The framing boundary is checked before polymorphic packet interpretation,
/// including the first lobby packet. Both transport implementations use it.
inline void appendHeader(std::vector<std::byte> & bytes, ESerializationVersion version)
{
	if(version < ESerializationVersion::MINIMAL || version > ESerializationVersion::CURRENT)
		throw std::runtime_error("Incompatible New Horizons protocol version");
	bytes.insert(bytes.end(), std::begin(MAGIC), std::end(MAGIC));
	const auto value = static_cast<uint32_t>(version);
	for(unsigned shift = 0; shift < 32; shift += 8)
		bytes.push_back(static_cast<std::byte>((value >> shift) & 0xff));
}

inline void validateHeader(const std::vector<std::byte> & bytes, ESerializationVersion expected)
{
	if(bytes.size() < HEADER_SIZE)
		throw std::runtime_error("Missing New Horizons protocol header");
	for(size_t index = 0; index < sizeof(MAGIC); ++index)
		if(bytes[index] != MAGIC[index])
			throw std::runtime_error("Incompatible New Horizons protocol header");
	uint32_t value = 0;
	for(unsigned index = 0; index < sizeof(uint32_t); ++index)
		value |= std::to_integer<uint32_t>(bytes[sizeof(MAGIC) + index]) << (index * 8);
	if(value < static_cast<uint32_t>(ESerializationVersion::MINIMAL)
		|| value > static_cast<uint32_t>(ESerializationVersion::CURRENT)
		|| value != static_cast<uint32_t>(expected))
		throw std::runtime_error("Incompatible New Horizons protocol version");
}
}
