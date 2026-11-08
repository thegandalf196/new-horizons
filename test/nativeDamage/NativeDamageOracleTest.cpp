/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "NativeDamageBenchmark.h"
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <fstream>
#include <iostream>
#include <random>
#include <type_traits>

namespace
{
#define BONUS_NAME(x) #x,
constexpr const char * bonusNames[] = { BONUS_LIST };
#undef BONUS_NAME

struct Writer
{
	lua_State * state;
	template<class T> void push(const T & value)
	{
		if constexpr(std::is_same_v<T, bool>) lua_pushboolean(state, value);
		else if constexpr(std::is_arithmetic_v<T>) lua_pushnumber(state, static_cast<double>(value));
		else if constexpr(std::is_same_v<T, std::string>) lua_pushlstring(state, value.data(), value.size());
		else if constexpr(std::is_same_v<T, std::vector<int>> || std::is_same_v<T, std::vector<double>>)
		{
			lua_newtable(state);
			for(size_t i = 0; i < value.size(); ++i) { push(value[i]); lua_rawseti(state, -2, static_cast<int>(i + 1)); }
		}
		else lua_pushnil(state); // Unit pointers, hexes and presence maps are supplied by the adapter.
	}
	template<class T> void operator()(const char * key, const T & value, const char *)
	{
		push(value);
		lua_setfield(state, -2, key);
	}
};

void pushUnit(lua_State * state, const nativeDamage::UnitFacts & unit)
{
	lua_newtable(state);
	Writer write{state};
#define FIELD(name) write(#name, unit.name, "")
	FIELD(minDamage); FIELD(maxDamage); FIELD(count); FIELD(attack); FIELD(defense);
	FIELD(firstHealth); FIELD(maxHealth); FIELD(phantomIntegrity); FIELD(baseAmount);
	FIELD(availableHealth); FIELD(shooter); FIELD(turret); FIELD(creatureKey);
#undef FIELD
	lua_newtable(state);
	for(size_t i = 0; i < nativeDamage::bonusTypeCount; ++i) write(bonusNames[i], unit.values[i], "");
	lua_setfield(state, -2, "values");
	lua_newtable(state);
	for(size_t i = 0; i < nativeDamage::bonusTypeCount; ++i) write(bonusNames[i], unit.bonuses[i], "");
	lua_setfield(state, -2, "bonuses");
}

void pushFacts(lua_State * state, const nativeDamage::Facts & facts)
{
	lua_newtable(state);
	Writer write{state};
	pushUnit(state, facts.attacker); lua_setfield(state, -2, "attacker");
	pushUnit(state, facts.defender); lua_setfield(state, -2, "defender");
#define FIELD(name) write(#name, facts.name, "")
	FIELD(attackerDefenseReduction); FIELD(defenderDefenseReduction);
	FIELD(defenderAttackReduction); FIELD(attackerAttackReduction); FIELD(damageBoost);
	FIELD(doubleDamageBonus); FIELD(hatedCreature); FIELD(petrification); FIELD(hatedTraits);
	FIELD(heroArtifactAttack); FIELD(heroBaseAttack); FIELD(hasSlayer); FIELD(slayerMastery); FIELD(slayerValue);
	FIELD(distancePenalty); FIELD(wallPenalty); FIELD(toReverse);
#undef FIELD
	lua_newtable(state);
#define FIELD(name) write(#name, facts.physicalProtection.name, "")
	FIELD(allDamageSourceValues); FIELD(subtypeSourceValues); FIELD(physicalBasisSourceValues);
	FIELD(legacyGeneralReduction); FIELD(magicShieldReduction);
#undef FIELD
	lua_setfield(state, -2, "physicalProtection");
}

void checked(lua_State * state, int result)
{
	if(result != 0) throw std::runtime_error(lua_tostring(state, -1));
}

int load(lua_State * state, const std::string & path, int base)
{
	checked(state, luaL_loadfile(state, path.c_str()));
	if(base != LUA_NOREF)
	{
		lua_newtable(state);
		lua_rawgeti(state, LUA_REGISTRYINDEX, base); lua_setfield(state, -2, "Base");
		lua_newtable(state);
		lua_pushvalue(state, LUA_GLOBALSINDEX); lua_setfield(state, -2, "__index");
		lua_setmetatable(state, -2);
		lua_setfenv(state, -2);
	}
	checked(state, lua_pcall(state, 0, 1, 0));
	return luaL_ref(state, LUA_REGISTRYINDEX);
}

DamageRange range(lua_State * state, const char * name)
{
	lua_getfield(state, -1, name);
	lua_getfield(state, -1, "min"); const auto low = static_cast<int64_t>(lua_tonumber(state, -1)); lua_pop(state, 1);
	lua_getfield(state, -1, "max"); const auto high = static_cast<int64_t>(lua_tonumber(state, -1)); lua_pop(state, 2);
	return {low, high};
}

struct RandomPayload
{
	std::mt19937 & random;
	template<class T> void operator()(const char *, T & value, const char *)
	{
		if constexpr(std::is_same_v<T, bool>) value = random() % 2 != 0;
		else if constexpr(std::is_integral_v<T>) value = static_cast<T>(random() % 151);
		else if constexpr(std::is_same_v<T, std::vector<int>>) value = {static_cast<int>(50 + random() % 101), 100};
	}
};

nativeDamage::UnitFacts randomUnit(std::mt19937 & random)
{
	nativeDamage::UnitFacts unit;
	unit.minDamage = 1 + random() % 50;
	unit.maxDamage = unit.minDamage + random() % 60;
	unit.count = 1 + random() % 500;
	unit.attack = random() % 80; unit.defense = random() % 80;
	unit.maxHealth = 1 + random() % 500; unit.firstHealth = 1 + random() % static_cast<unsigned>(unit.maxHealth);
	unit.baseAmount = unit.count + random() % 40;
	unit.availableHealth = (unit.count - 1) * unit.maxHealth + unit.firstHealth;
	unit.phantomIntegrity = random() % 8 == 0 ? unit.availableHealth : 0;
	unit.shooter = random() % 2 != 0; unit.turret = random() % 20 == 0;
	const std::array keys{"core:magicElemental", "core:psychicElemental", "core:pikeman"};
	unit.creatureKey = keys[random() % keys.size()];
	for(size_t i = 0; i < nativeDamage::bonusTypeCount; ++i)
		if(random() % 8 == 0) { unit.bonuses.set(i); unit.values[i] = random() % 101; }
	return unit;
}

nativeDamage::Facts randomFacts(std::mt19937 & random)
{
	nativeDamage::Facts facts;
	facts.attacker = randomUnit(random); facts.defender = randomUnit(random);
	facts.attackerDefenseReduction = facts.attacker.value(BonusType::ENEMY_DEFENCE_REDUCTION);
	facts.defenderDefenseReduction = facts.defender.value(BonusType::ENEMY_DEFENCE_REDUCTION);
	facts.attackerAttackReduction = facts.attacker.value(BonusType::GENERAL_ATTACK_REDUCTION);
	facts.defenderAttackReduction = facts.defender.value(BonusType::ENEMY_ATTACK_REDUCTION);
	facts.damageBoost = facts.attacker.value(BonusType::PERCENTAGE_DAMAGE_BOOST);
	facts.doubleDamageBonus = facts.attacker.value(BonusType::BONUS_DAMAGE_PERCENTAGE);
	facts.hatedCreature = facts.attacker.value(BonusType::HATE);
	facts.hatedTraits = facts.attacker.value(BonusType::HATES_TRAIT);
	facts.hasSlayer = facts.attacker.has(BonusType::SLAYER);
	facts.slayerMastery = random() % 4; facts.slayerValue = random() % 20;
	facts.heroArtifactAttack = random() % 10; facts.heroBaseAttack = random() % 20;
	facts.distancePenalty = random() % 2; facts.wallPenalty = random() % 2; facts.toReverse = random() % 2;
	if(facts.defender.has(BonusType::GENERAL_DAMAGE_REDUCTION))
	{
		facts.petrification = random() % 51;
		facts.physicalProtection.allDamageSourceValues = {static_cast<double>(random() % 101), 15};
		facts.physicalProtection.subtypeSourceValues = {static_cast<double>(random() % 51), 10};
		facts.physicalProtection.legacyGeneralReduction = random() % 51;
		facts.physicalProtection.magicShieldReduction = random() % 51;
	}
	if(facts.defender.has(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS))
		facts.physicalProtection.physicalBasisSourceValues = {static_cast<double>(random() % 10001), 1500};
	return facts;
}

void directed(int scenario, DamageAttackInfo & info, nativeDamage::Facts & facts)
{
	info = DamageAttackInfo{};
	info.attackFactorPerPoint = 0.05; info.attackFactorCap = 3.0;
	info.defenseFactorPerPoint = 0.025; info.defenseFactorCap = 0.7;
	facts = nativeDamage::Facts{};
	for(auto * unit : {&facts.attacker, &facts.defender})
	{
		unit->minDamage = 5; unit->maxDamage = 9; unit->count = 10;
		unit->maxHealth = 20; unit->firstHealth = 10; unit->baseAmount = 10;
		unit->availableHealth = 190; unit->creatureKey = "core:pikeman";
	}
	const auto bonus = [](auto & unit, BonusType type, double value)
	{
		unit.bonuses.set(static_cast<size_t>(type)); unit.values[static_cast<size_t>(type)] = value;
	};
	switch(scenario)
	{
	case 0: // Contradictory Bless/Curse takes precedence over Deadeye.
		bonus(facts.attacker, BonusType::ALWAYS_MINIMUM_DAMAGE, 3);
		bonus(facts.attacker, BonusType::ALWAYS_MAXIMUM_DAMAGE, 7);
		info.archeryMaximumCreatureDamage = true;
		break;
	case 1: case 2: // Exact Slayer mastery boundary, then just below it.
		bonus(facts.defender, BonusType::KING, 3);
		facts.hasSlayer = true; facts.slayerMastery = scenario == 1 ? 3 : 2; facts.slayerValue = 8;
		break;
	case 3: // The damage cap can floor to zero despite the ordinary minimum of one.
		bonus(facts.defender, BonusType::DAMAGE_RECEIVED_CAP, 1); facts.defender.maxHealth = 2;
		break;
	case 4: // Independent physical reductions near the integer-floor boundary.
		info.physicalDamageReductionCapPercent = 95;
		facts.attacker.minDamage = facts.attacker.maxDamage = 60;
		bonus(facts.defender, BonusType::GENERAL_DAMAGE_REDUCTION, 50);
		facts.physicalProtection.allDamageSourceValues = {20, 30};
		break;
	case 5:
		info.coordinatedPhysicalProtectionIgnorePercent = 75;
		bonus(facts.defender, BonusType::GENERAL_DAMAGE_REDUCTION, 70);
		facts.physicalProtection.legacyGeneralReduction = 70;
		break;
	case 6: // A negative Revenge radicand produces a NaN factor, ignored by both comparisons.
		bonus(facts.attacker, BonusType::REVENGE, 1); facts.attacker.availableHealth = 1000;
		break;
	case 7:
		info.physicalDamageReductionCapPercent = 100;
		bonus(facts.defender, BonusType::GENERAL_DAMAGE_REDUCTION, 100);
		facts.physicalProtection.allDamageSourceValues = {-10, 0, 100, 120};
		break;
	}
}

int prepareInvocation(lua_State * state, int script, int adapter, DamageAttackInfo & info, const nativeDamage::Facts & facts)
{
	lua_rawgeti(state, LUA_REGISTRYINDEX, adapter);
	lua_rawgeti(state, LUA_REGISTRYINDEX, script);
	lua_newtable(state); Writer write{state}; info.serializeScript(write);
	pushFacts(state, facts);
	lua_pushboolean(state, true);
	checked(state, lua_pcall(state, 4, 1, 0));
	if(!lua_isfunction(state, -1)) throw std::runtime_error("Oracle did not prepare a calculator invocation");
	return luaL_ref(state, LUA_REGISTRYINDEX);
}
}

int main(int argc, char ** argv)
{
	try
	{
		if(argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--benchmark"))
			throw std::runtime_error("Usage: NativeDamageOracleTest <source-root> [--benchmark]");
		const std::string root = argv[1];
		const auto deleter = [](lua_State * state) { lua_close(state); };
		std::unique_ptr<lua_State, decltype(deleter)> state(luaL_newstate(), deleter);
		luaL_openlibs(state.get());
		int script = LUA_NOREF;
		for(const auto name : {"damageCalculator", "physicalProtection", "siegeWeapon", "magicElemental",
			"psychicElemental", "vulnerableFromBack", "hatesTrait", "revenge", "enemyAttackReduction", "damageReceivedCap"})
			script = load(state.get(), root + "/scripts/damage/" + name + ".lua", script);
		const int adapter = load(state.get(), root + "/test/nativeDamage/OracleFixture.lua", LUA_NOREF);
		std::mt19937 random(0x4e48444du);
		for(int iteration = 0; iteration < 12008; ++iteration)
		{
			DamageAttackInfo info;
			RandomPayload randomize{random}; info.serializeScript(randomize);
			info.attackFactorPerPoint = 0.05; info.attackFactorCap = 3.0;
			info.defenseFactorPerPoint = 0.025; info.defenseFactorCap = 0.7;
			const std::array caps{-1, 0, 50, 95, 100}; info.physicalDamageReductionCapPercent = caps[iteration % caps.size()];
			auto facts = randomFacts(random);
			if(iteration < 8) directed(iteration, info, facts);
			const auto native = nativeDamage::calculate(info, facts);
			lua_rawgeti(state.get(), LUA_REGISTRYINDEX, adapter);
			lua_rawgeti(state.get(), LUA_REGISTRYINDEX, script);
			lua_newtable(state.get()); Writer write{state.get()}; info.serializeScript(write);
			pushFacts(state.get(), facts);
			checked(state.get(), lua_pcall(state.get(), 3, 1, 0));
			const auto damage = range(state.get(), "damage");
			const auto kills = range(state.get(), "kills");
			const auto before = range(state.get(), "damageBeforeDefense"); lua_pop(state.get(), 1);
			if(native.damage.min != damage.min || native.damage.max != damage.max
				|| native.kills.min != kills.min || native.kills.max != kills.max
				|| native.damageBeforeDefense.min != before.min || native.damageBeforeDefense.max != before.max)
			{
				std::cerr << "Mismatch case " << iteration << " native=" << native.damage.min << ',' << native.damage.max
					<< " Lua=" << damage.min << ',' << damage.max << " before=" << native.damageBeforeDefense.min
					<< ',' << native.damageBeforeDefense.max << '/' << before.min << ',' << before.max << '\n';
				return 1;
			}
		}
		std::cout << "Native damage: 8 directed and 12000 seeded complete Lua-chain numerical comparisons passed\n";
		if(argc == 3) nativeDamageOracle::benchmark(state.get(), script, adapter, prepareInvocation);
		return 0;
	}
	catch(const std::exception & error) { std::cerr << error.what() << '\n'; return 1; }
}
