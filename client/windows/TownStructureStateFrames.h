/*
 * TownStructureStateFrames.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstddef>
#include <optional>

struct CStructure;
class CGTownInstance;

/// Frame range of a town structure whose animation frames are building states rather than an
/// animation. An unset bound keeps the structure's whole animation.
struct TownStructureStateFrames
{
	/// First frame of the range, also the frame shown first
	std::optional<size_t> first;
	/// Frame index the range ends before
	std::optional<size_t> last;
};

namespace townStructureStateFrames
{
/// Castle's shipyard and ship use frame 1 once the citadel's moat is built, frame 0 before.
/// Dungeon's Mana Vortex cycles frames 0-8 before Mage Guild 5 and shows frames from 10 after.
TownStructureStateFrames forStructure(const CGTownInstance & town, const CStructure & structure);
}
