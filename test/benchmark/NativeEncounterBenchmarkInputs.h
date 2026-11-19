/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "NativeEncounterBenchmarkSupport.h"
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace nativeBenchmark
{
inline int benchmarkSeed()
{
	const char * selected = std::getenv("NH_BENCHMARK_SEED");
	const char * historical = std::getenv("NH_EXP_SEED");
	if(selected && historical && std::string_view(selected) != historical)
		throw std::invalid_argument("Conflicting benchmark seed controls");
	const std::string_view text(selected ? selected : historical ? historical : "1337");
	int result = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
	if(text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < 0)
		throw std::invalid_argument("Benchmark seed must be an integer from zero through INT_MAX");
	return result;
}
inline CreatureID troop(const char * id) { return CreatureID(CreatureID::decode(id)); }
using Army = std::vector<std::pair<CreatureID, uint16_t>>;
inline Army rangedArmy()
{
	return {{troop("core:halberdier"), 14}, {troop("core:marksman"), 13},
		{troop("core:gremlin"), 16}, {troop("core:marksman"), 13},
		{troop("core:halberdier"), 9}, {troop("core:marksman"), 13}, {troop("core:halberdier"), 7}};
}
inline Army knightArmy()
{
	Army result(7, {troop("core:dreadKnight"), 2});
	result.back().second = 1;
	return result;
}
inline Army mirrorCastleArmy()
{
	return {{troop("core:pikeman"), 30}, {troop("core:archer"), 20}, {troop("core:griffin"), 12},
		{troop("core:swordsman"), 10}, {troop("core:monk"), 6}, {troop("core:cavalier"), 3}, {troop("core:angel"), 1}};
}
inline Army mirrorRampartArmy()
{
	return {{troop("core:centaur"), 20}, {troop("core:woodElf"), 12}, {troop("core:dwarf"), 15},
		{troop("core:pegasus"), 8}, {troop("core:dendroidGuard"), 6}, {troop("core:unicorn"), 3}, {troop("core:greenDragon"), 1}};
}
inline Army mirrorNecropolisArmy()
{
	return {{troop("core:skeleton"), 40}, {troop("core:walkingDead"), 20}, {troop("core:wight"), 10},
		{troop("core:vampire"), 8}, {troop("core:lich"), 5}, {troop("core:blackKnight"), 3}, {troop("core:boneDragon"), 2}};
}
}
