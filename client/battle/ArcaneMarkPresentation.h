/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
class CStack;
class CGHeroInstance;
namespace arcaneMarkPresentation
{
inline constexpr const char * SMALL_ICON = "NH_metamagic_prism_basic_small.png";
inline constexpr const char * MEDIUM_ICON = "NH_metamagic_prism_basic_medium.png";
struct Readout
{
	std::string compact;
	std::string details;
};
std::array<uint8_t, 2> counts(const CStack & stack);
std::vector<Readout> forStack(const CStack & stack);
std::string combinedTooltip(const std::vector<Readout> & readouts);
std::string compactCounts(const std::array<uint8_t, 2> & marks);
std::string manaUpperBoundText(int ordinaryCost);
std::string targetIndependentEffectNote(const CGHeroInstance & hero);
}
