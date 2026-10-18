/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/gameState/ReplayLog.h"
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

class CGameHandler;

namespace saveParity
{
struct Evidence
{
	std::map<std::string, std::vector<std::byte>> channels;
	std::vector<std::byte> rawStatistics;
	std::vector<ReplayChapter> chapters;
	std::set<PlayerColor> actingPlayers;
	uint32_t day = 0;
};

/// Only statistics timestamps are normalized, on the detached owner with restoration.
Evidence capture(CGameHandler & handler);
/// Requires one complete ordinary day from the same exact starting replay prefix.
void requireTurnCoverage(const Evidence & start, const Evidence & finish);
/// Empty result means byte equality in every retained comparison channel.
std::string firstDifference(const Evidence & left, const Evidence & right);
}
