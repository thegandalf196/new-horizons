/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../../Global.h"
#include "../../luascript/DeclaredBonusTypes.h"
#include "../../test/scripting/BonusPresenceLegacyReference.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

namespace
{
constexpr size_t typeCount = static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT);
const auto typeOf = [](BonusType type) { return type; };
uint64_t checksum = 0;

template<class Collect>
double measure(Collect collect, const std::vector<std::vector<BonusType>> & inputs)
{
	std::array<double, 5> samples{};
	for(auto & elapsed : samples)
	{
		const auto start = std::chrono::steady_clock::now();
		for(size_t i = 0; i < 30000; ++i)
		{
			const auto result = collect(inputs[i % inputs.size()]);
			checksum += result.size();
			if(!result.empty()) checksum += result.begin()->first.size();
		}
		elapsed = std::chrono::duration<double, std::micro>(
			std::chrono::steady_clock::now() - start).count() / 30000;
	}
	std::sort(samples.begin(), samples.end());
	return samples[samples.size() / 2];
}

void run(const char * name, size_t declaredCount, size_t bonusCount, size_t distinctTypes,
	size_t firstType = 0, bool unique = false)
{
	scripting::DeclaredBonusTypes indexed;
	bonusPresenceLegacy::Declarations legacy;
	for(size_t i = 0; i < declaredCount; ++i)
	{
		const auto type = static_cast<BonusType>(i);
		const auto text = "CALCULATOR_BONUS_TYPE_" + std::to_string(i);
		indexed.add(type, text);
		legacy.try_emplace(type, text);
	}
	std::mt19937 generator(42);
	std::vector<std::vector<BonusType>> inputs(31, std::vector<BonusType>(bonusCount));
	for(auto & bonuses : inputs)
	{
		for(size_t i = 0; i < bonuses.size(); ++i)
			bonuses[i] = static_cast<BonusType>(firstType + (unique ? i : generator()) % distinctTypes);
		if(unique) std::shuffle(bonuses.begin(), bonuses.end(), generator);
	}
	const auto old = [&](const auto & input) { return bonusPresenceLegacy::collect(legacy, input, typeOf); };
	const auto current = [&](const auto & input) { return indexed.collect(input, typeOf); };
	for(const auto & input : inputs)
		if(old(input) != current(input)) throw std::runtime_error("benchmark output mismatch");
	const double legacyTime = measure(old, inputs);
	const double indexedTime = measure(current, inputs);
	std::cout << name << ',' << legacyTime << ',' << indexedTime << ',' << legacyTime / indexedTime << '\n';
}
}

int main()
{
	std::cout << "case,legacy_us,indexed_us,speedup\n" << std::fixed << std::setprecision(3);
	run("empty", 20, 0, 1);
	run("low_bonus", 20, 6, typeCount);
	run("typical", 20, 50, typeCount);
	run("duplicate_heavy", 20, 256, 24);
	run("mostly_undeclared", 20, 256, typeCount);
	run("all_undeclared", 20, 256, typeCount - 20, 20);
	run("unique_all_types", typeCount, typeCount, typeCount, 0, true);
	run("custom_all_types", typeCount + 15, 256, typeCount + 30);
	std::cout << "checksum," << checksum << '\n';
}
