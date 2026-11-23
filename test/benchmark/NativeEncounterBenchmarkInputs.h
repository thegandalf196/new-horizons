/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "NativeEncounterBenchmarkSupport.h"
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
inline Army withSlot(Army army, size_t slot, const char * creature, uint16_t count)
{
	army[slot] = {troop(creature), count};
	return army;
}
inline Army strongholdArmy()
{
	return {{troop("core:goblin"), 40}, {troop("core:orc"), 15}, {troop("core:ogre"), 8},
		{troop("core:roc"), 6}, {troop("core:thunderbird"), 5}, {troop("core:cyclop"), 3}};
}
inline Army dungeonArmy()
{
	return {{troop("core:troglodyte"), 30}, {troop("core:harpy"), 15}, {troop("core:beholder"), 8},
		{troop("core:medusa"), 8}, {troop("core:minotaur"), 5}, {troop("core:manticore"), 4}, {troop("core:scorpicore"), 2}};
}
inline Army infernoArmy()
{
	return {{troop("core:imp"), 30}, {troop("core:gog"), 12}, {troop("core:magog"), 8}, {troop("core:demon"), 10},
		{troop("core:hornedDemon"), 8}, {troop("core:pitFiend"), 4}, {troop("core:efreet"), 3}};
}
inline Army fortressArmy()
{
	return {{troop("core:gnoll"), 30}, {troop("core:gnollMarauder"), 20}, {troop("core:lizardman"), 15},
		{troop("core:gorgon"), 4}, {troop("core:wyvern"), 3}};
}
inline Army confluxArmy()
{
	return {{troop("core:pixie"), 20}, {troop("new-horizons:wisp"), 14}, {troop("new-horizons:wispUpgrade"), 8},
		{troop("core:airElemental"), 6}, {troop("core:fireElemental"), 6}, {troop("core:energyElemental"), 4}};
}
/// A specialty hero against an unchanged hero. Native defaults keep the hero's
/// own book and Mana; otherwise it receives the fixture spells plus `spells`.
/// Armies are built at run time: identifiers resolve only after content loads.
struct SpecialtyEncounter
{
	using ArmyFactory = Army (*)();
	const char * name;
	const char * hero;
	ArmyFactory left;
	ArmyFactory right;
	const char * opponent;
	std::vector<std::string> spells;
	bool nativeHeroDefaults;
};
inline std::vector<SpecialtyEncounter> specialtyEncounters()
{
	using A = SpecialtyEncounter::ArmyFactory;
	const A castle = mirrorCastleArmy, inferno = infernoArmy, rampart = mirrorRampartArmy;
	return {
		{"Adela", "core:adela", castle, castle, "core:rion", {"core:bless", "core:shield"}, false},
		{"Loynis", "core:loynis", castle, castle, "core:rion", {"core:bless", "new-horizons:sanctuary"}, false},
		{"Orrin", "core:orrin", rangedArmy, castle, "core:rion", {"core:bless"}, false},
		{"Adelaide", "core:adelaide", castle, castle, "core:rion", {"core:frostRing"}, false},
		{"Sanya", "core:sanya", castle, castle, "core:rion", {"core:slow", "core:haste"}, false},
		{"Cuthbert", "core:cuthbert", castle, castle, "core:rion", {"core:slow"}, false},
		{"Edric", "core:edric", +[] { return withSlot(mirrorCastleArmy(), 2, "core:royalGriffin", 12); }, castle, "core:rion", {}, true},
		{"RionIvorControl", "core:rion", castle, castle, "core:ivor", {"core:bless", "core:slow"}, false},
		{"Tyris", "core:tyris", +[] { return withSlot(mirrorCastleArmy(), 5, "core:champion", 4); }, castle, "core:rion", {}, true},
		{"Ingham", "core:ingham", +[] { return withSlot(mirrorCastleArmy(), 4, "core:zealot", 6); }, castle, "core:rion", {}, true},
		{"Rashka", "core:rashka", +[] { return withSlot(infernoArmy(), 6, "core:efreetSultan", 3); }, inferno, "core:ivor", {}, true},
		{"Marius", "core:marius", inferno, inferno, "core:ivor", {}, true},
		{"Calh", "core:calh", inferno, inferno, "core:ivor", {}, true},
		{"Nymus", "core:nymus", +[] { return withSlot(infernoArmy(), 5, "core:pitLord", 4); }, inferno, "core:ivor", {}, true},
		{"Moandor", "core:moandor", +[] { return withSlot(mirrorNecropolisArmy(), 4, "core:powerLich", 5); },
			mirrorNecropolisArmy, "core:ivor", {}, true},
		{"Synca", "core:synca", dungeonArmy, dungeonArmy, "core:ivor", {}, true},
		{"Shiva", "core:shiva", strongholdArmy, strongholdArmy, "core:ivor", {}, true},
		{"Drakon", "core:drakon", fortressArmy, fortressArmy, "core:ivor", {}, true},
		{"Ignissa", "core:ignissa", confluxArmy, confluxArmy, "core:ivor", {}, true},
		{"Monere", "core:monere", confluxArmy, confluxArmy, "core:ivor", {}, true},
		{"Mephala", "core:mephala", rampart, castle, "core:rion", {}, true},
		{"Coronius", "core:coronius", rampart, castle, "core:rion", {}, true},
		{"Uland", "core:uland", rampart, castle, "core:rion", {"core:cure"}, false},
		{"Gem", "core:gem", rampart, castle, "core:rion", {"core:cure", "core:bless"}, false},
		{"Alagar", "core:alagar", rampart, castle, "core:rion", {"core:iceBolt", "core:frostRing"}, false},
		{"Melodia", "core:melodia", rampart, +[] { return withSlot(mirrorCastleArmy(), 6, "core:archangel", 2); },
			"core:rion", {}, true},
		{"Aeris", "core:aeris", +[] { return withSlot(mirrorRampartArmy(), 4, "core:silverPegasus", 8); }, castle,
			"core:rion", {}, true},
		{"Ryland", "core:ryland", +[] { return withSlot(mirrorRampartArmy(), 3, "core:dendroidSoldier", 6); }, castle,
			"core:rion", {}, true},
	};
}
}
