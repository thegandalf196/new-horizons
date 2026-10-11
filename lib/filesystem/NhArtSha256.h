/* Native SHA-256 for NHART integrity, GPL-2.0-or-later; FIPS PUB 180-4. */
#pragma once
#include <array>
#include <span>
#include <istream>

namespace nhart
{
using Digest = std::array<ui8, 32>;
DLL_LINKAGE Digest sha256(std::span<const ui8> bytes);
DLL_LINKAGE Digest sha256(std::istream & stream, ui64 length);
DLL_LINKAGE std::string hex(const Digest & digest);
}
