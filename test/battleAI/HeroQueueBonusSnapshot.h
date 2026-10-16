/*
 * HeroQueueBonusSnapshot.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct Bonus;
class BonusList;

namespace heroQueueTest
{
// CURRENT runtime Bonus wire, followed by owners in ordered depth-first graph traversal.
// Then full valid-cell membership for each UnitOnHexLimiter in the same traversal.
// Throws before writing for unsupported dynamic types or unbounded/cyclic graphs.
std::vector<std::byte> bonusSnapshotBytes(const Bonus & bonus);
std::vector<std::string> bonusSnapshots(const BonusList & bonuses);
}
