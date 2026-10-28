/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/json/JsonNode.h"
#include <cstddef>
#include <optional>
class ReplayLog;
class CGameState;
struct CPack;
namespace battleActionExport
{
struct Limits
{
	size_t packets = 200000;
	size_t packetBytes = 4 * 1024 * 1024;
	size_t intervalPackets = 20000;
	size_t intervals = 1024;
	size_t outputBytes = 8 * 1024 * 1024;
};
/// Null selection lists retained intervals; an index requires a complete battle.
/// Historical context must be an exclusively owned detached world; reuses snapshot load +
/// recorded-packet apply. Null is decoder-fixture mode, never used by the CLI.
JsonNode inspect(const ReplayLog & replay, CGameState * historicalContext,
	std::optional<size_t> selected, const Limits & limits = {});
/// Read-only packet summaries. The caller supplies provenance and interval position.
JsonNode describe(const CPack & packet);
}
