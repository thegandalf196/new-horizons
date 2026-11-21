/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"
#include <set>
#include <vector>

class CGHeroInstance;
namespace vstd { class RNG; }

namespace newHorizonsHeroes
{
/// Cuthbert's Pariah: for each Light spell in `learned` that the hero does not
/// already know, one uniformly random unknown Shadow spell of the same level that
/// this world lets heroes learn (none when he knows them all). Learning ignores
/// the Shadow rank; casting still needs it. Empty for every other hero.
/// Draws only when a pool has more than one candidate, in ascending spell order.
/// `learnedAlreadyKnown` is set for a starting spellbook, already in the book.
DLL_LINKAGE std::vector<SpellID> pariahMirrorSpells(const CGHeroInstance & hero,
	const std::set<SpellID> & learned, vstd::RNG & rng, bool learnedAlreadyKnown = false);
}
