/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include "NativeDamageBenchmark.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace nativeDamageOracle
{
namespace
{
using Output = std::array<int64_t, 6>;
using Digest = std::array<uint64_t, 6>;
constexpr size_t caseCount = 8;
constexpr size_t iterations = 32768;
constexpr size_t samples = 8;
constexpr uint64_t offset = 14695981039346656037ULL;
constexpr uint64_t prime = 1099511628211ULL;

struct Case
{
	DamageAttackInfo info;
	nativeDamage::Facts facts;
	int invocation = LUA_NOREF;
	Output expected{};
};

Output nativeOutput(const Case & item)
{
	const auto value = nativeDamage::calculate(item.info, item.facts);
	return {value.damage.min, value.damage.max, value.kills.min, value.kills.max,
		value.damageBeforeDefense.min, value.damageBeforeDefense.max};
}

Output luaOutput(lua_State * state, const Case & item)
{
	lua_rawgeti(state, LUA_REGISTRYINDEX, item.invocation);
	if(lua_pcall(state, 0, 1, 0) != 0) throw std::runtime_error(lua_tostring(state, -1));
	Output result{};
	size_t index = 0;
	for(const auto * field : {"damage", "kills", "damageBeforeDefense"})
	{
		lua_getfield(state, -1, field);
		for(const auto * bound : {"min", "max"})
		{
			lua_getfield(state, -1, bound);
			result[index++] = static_cast<int64_t>(lua_tonumber(state, -1));
			lua_pop(state, 1);
		}
		lua_pop(state, 1);
	}
	lua_pop(state, 1);
	return result;
}

void digest(Digest & result, const Output & value)
{
	for(size_t index = 0; index < value.size(); ++index)
		result[index] = (result[index] ^ static_cast<uint64_t>(value[index])) * prime;
}

Digest initialDigest()
{
	Digest result;
	result.fill(offset);
	return result;
}

std::string formatted(const Digest & value)
{
	std::ostringstream result;
	result << std::hex;
	for(size_t index = 0; index < value.size(); ++index)
	{
		if(index != 0) result << ':';
		result << value[index];
	}
	return result.str();
}

void bonus(nativeDamage::UnitFacts & unit, BonusType type, double value)
{
	unit.bonuses.set(static_cast<size_t>(type));
	unit.values[static_cast<size_t>(type)] = value;
}

Case makeCase(size_t group, int variant)
{
	Case result;
	auto & info = result.info;
	auto & facts = result.facts;
	info.attackFactorPerPoint = 0.05; info.attackFactorCap = 3.0;
	info.defenseFactorPerPoint = 0.025; info.defenseFactorCap = 0.7;
	for(auto * unit : {&facts.attacker, &facts.defender})
	{
		unit->minDamage = 5 + variant; unit->maxDamage = 12 + variant;
		unit->count = 17 + variant * 3; unit->baseAmount = unit->count + 4;
		unit->maxHealth = 35 + variant; unit->firstHealth = 19 + variant;
		unit->availableHealth = (unit->count - 1) * unit->maxHealth + unit->firstHealth;
		unit->attack = 13 + variant; unit->defense = 19 - variant;
		unit->creatureKey = "core:pikeman";
	}
	info.shooting = variant % 2 != 0;
	facts.attacker.shooter = info.shooting;
	info.physicalDamageReductionCapPercent = variant % 2 == 0 ? -1 : 95;
	if(group == 0) return result;

	bonus(facts.attacker, BonusType::PERCENTAGE_DAMAGE_BOOST, 25);
	bonus(facts.attacker, BonusType::GENERAL_DAMAGE_PREMY, 10);
	bonus(facts.attacker, BonusType::ENEMY_DEFENCE_REDUCTION, 15);
	bonus(facts.attacker, BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE, 10);
	bonus(facts.defender, BonusType::GENERAL_DAMAGE_REDUCTION, 20);
	bonus(facts.defender, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 1500);
	facts.damageBoost = 25; facts.attackerDefenseReduction = 15;
	facts.physicalProtection.allDamageSourceValues = {20, 15, 10};
	facts.physicalProtection.subtypeSourceValues = {12, 7};
	facts.physicalProtection.physicalBasisSourceValues = {1500, 800};
	facts.physicalProtection.legacyGeneralReduction = 20;
	facts.physicalProtection.magicShieldReduction = 15;
	info.newHorizonsArmorerReductionPercent = 10;
	info.heroOrderDamageReductionPercents = {10, 15};
	info.heroOrderFinalDamageMultipliers = {110, 85};
	info.sylvanAttackMultipliers = {105, 110};
	info.sylvanDefenseMultipliers = {90};
	info.combinedArmsDamagePercent = 7.5;
	info.arcaneInspirationDamageBonusPercent = 15;
	info.bulwarkDamageReductionBasisPoints = 1250;
	info.intrinsicPhysicalDamageReductionPercent = 10;
	info.physicalDamageReductionCapPercent = 95;
	info.physicalDamageReductionIgnorePercent = variant * 5;
	info.newHorizonsFinalLuck = true;
	info.luckyStrike = variant % 3 == 0;
	facts.distancePenalty = variant % 2 != 0;
	facts.wallPenalty = variant % 3 == 0;
	if(group == 1)
	{
		if(variant >= 4) bonus(facts.defender, BonusType::DAMAGE_RECEIVED_CAP, 75);
		return result;
	}
	if(group == 2)
	{
		// Exercise saved historical protection and ranged precedence policies.
		info.physicalDamageReductionCapPercent = -1;
		info.coordinatedPhysicalProtectionIgnorePercent = variant % 2 == 0 ? 0 : 35;
		info.legacyPhysicalDamageReductionIgnorePercent = 20;
		info.runicEnduranceReductionPercent = 15;
		info.ironHideReductionPercent = 10;
		info.newHorizonsFinalLuck = false;
		info.heroOrderFinalDamageMultipliers.clear();
		info.heroOrderFinalDamageMultiplier = 90;
		info.shooting = true; facts.attacker.shooter = true;
		info.archeryAdjacentRangedTarget = true;
		info.archeryIgnoreAdjacentRangedPenalty = true;
		info.targetedRangedCommand = true;
		info.pointBlankPreservesAdjacentExemption = variant >= 4;
		return result;
	}
	if(variant < 4)
	{
		bonus(facts.attacker, BonusType::SIEGE_WEAPON, 1);
		facts.attacker.creatureKey = "core:ballista";
		facts.attacker.count = facts.attacker.baseAmount = 1;
		facts.attacker.availableHealth = facts.attacker.firstHealth;
		facts.attacker.shooter = info.shooting = true;
		facts.heroArtifactAttack = 4; facts.heroBaseAttack = 9;
		info.machineBaseDamage = variant >= 2 ? 225 + variant * 10 : 0;
		info.siegeSkillMultiplier = variant % 2 == 0 ? 0 : 3;
	}
	else
	{
		facts.defender.phantomIntegrity = 175 + variant * 10;
		info.physicalDamage = variant % 2 == 0;
	}
	return result;
}

struct Measurement { double nanoseconds; Digest checksum; };
template<class Calculation> Measurement measure(Calculation calculate, const std::array<Case, caseCount> & cases)
{
	auto checksum = initialDigest();
	const auto start = std::chrono::steady_clock::now();
	for(size_t index = 0; index < iterations; ++index) digest(checksum, calculate(cases[index % caseCount]));
	const auto elapsed = std::chrono::steady_clock::now() - start;
	return {std::chrono::duration<double, std::nano>(elapsed).count() / iterations, checksum};
}

Digest verify(lua_State * state, const std::array<Case, caseCount> & cases)
{
	auto checksum = initialDigest();
	for(size_t index = 0; index < cases.size(); ++index)
	{
		if(nativeOutput(cases[index]) != cases[index].expected || luaOutput(state, cases[index]) != cases[index].expected)
			throw std::runtime_error("Benchmark output mismatch at case " + std::to_string(index));
		digest(checksum, cases[index].expected);
	}
	return checksum;
}

double median(std::array<double, samples> values)
{
	std::sort(values.begin(), values.end());
	return (values[samples / 2 - 1] + values[samples / 2]) / 2;
}
}

void benchmark(lua_State * state, int script, int adapter, PrepareInvocation prepare)
{
	std::cout << "BENCHMARK boundary=prepared numerical damage; native=production calculate(info,Facts); "
		"Lua=actual unchanged 10-layer calculator plus mock observation methods, C API invocation/result extraction.\n"
		"EXCLUDES fixture/input construction, live fact capture, C++ proxy/bonus-filter bridge, AI and whole-game work. "
		"Includes six-output digest overhead on both paths; Lua GC remains enabled. "
		"Cases are synthetic policy coverage, not a measured battle distribution or battle speedup estimate.\n";
	lua_getglobal(state, "jit");
	if(lua_istable(state, -1))
	{
		lua_getfield(state, -1, "version");
		std::cout << "Lua backend=" << lua_tostring(state, -1) << '\n';
		lua_pop(state, 1);
	}
	lua_pop(state, 1);
	std::cout << "group,sample,first,cases,calls,native_ns_per_call,lua_ns_per_call,lua_div_native,native_digest,lua_digest\n";
	const std::array names{"low_bonus", "layered_capped", "saved_legacy_policy", "siege_phantom"};
	for(size_t group = 0; group < names.size(); ++group)
	{
		std::array<Case, caseCount> cases;
		for(size_t index = 0; index < cases.size(); ++index)
		{
			cases[index] = makeCase(group, static_cast<int>(index));
			cases[index].expected = nativeOutput(cases[index]);
			cases[index].invocation = prepare(state, script, adapter, cases[index].info, cases[index].facts);
		}
		const auto before = verify(state, cases);
		// Warm both paths, including JIT traces and ordinary garbage collection.
		for(size_t index = 0; index < 1024; ++index)
		{
			const auto & item = cases[index % caseCount];
			if(nativeOutput(item) != luaOutput(state, item)) throw std::runtime_error("Benchmark warmup mismatch");
		}
		auto expected = initialDigest();
		for(size_t index = 0; index < iterations; ++index) digest(expected, cases[index % caseCount].expected);
		std::array<double, samples> nativeTimes{}, luaTimes{};
		for(size_t sample = 0; sample < samples; ++sample)
		{
			Measurement native{}, lua{};
			const auto runNative = [&] { native = measure(nativeOutput, cases); };
			const auto runLua = [&] { lua = measure([&](const Case & item) { return luaOutput(state, item); }, cases); };
			const bool nativeFirst = (group + sample) % 2 == 0;
			if(nativeFirst) { runNative(); runLua(); } else { runLua(); runNative(); }
			if(native.checksum != expected || lua.checksum != expected) throw std::runtime_error("Benchmark timed digest mismatch");
			nativeTimes[sample] = native.nanoseconds; luaTimes[sample] = lua.nanoseconds;
			std::cout << names[group] << ',' << sample << ',' << (nativeFirst ? "native" : "lua") << ',' << caseCount << ',' << iterations
				<< ',' << std::fixed << std::setprecision(3) << native.nanoseconds << ',' << lua.nanoseconds
				<< ',' << lua.nanoseconds / native.nanoseconds << ',' << formatted(native.checksum) << ',' << formatted(lua.checksum) << '\n';
		}
		const auto after = verify(state, cases);
		if(before != after) throw std::runtime_error("Benchmark before/after digest mismatch");
		std::cout << "SUMMARY " << names[group] << " native_median_ns=" << median(nativeTimes) << " lua_median_ns=" << median(luaTimes)
			<< " lua_div_native=" << median(luaTimes) / median(nativeTimes) << " verified_before=" << formatted(before)
			<< " verified_after=" << formatted(after) << '\n';
		for(const auto & item : cases) luaL_unref(state, LUA_REGISTRYINDEX, item.invocation);
	}
}
}
