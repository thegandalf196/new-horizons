/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../../Global.h"
#include "../../luascript/DeclaredBonusTypes.h"
#include "BonusPresenceLegacyReference.h"

#include <iostream>
#include <random>
#include <stdexcept>

namespace
{
constexpr size_t typeCount = static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT);
using Presence = std::unordered_map<std::string, bool>;

void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

void compare(const scripting::DeclaredBonusTypes & indexed,
	const bonusPresenceLegacy::Declarations & legacy, const std::vector<BonusType> & bonuses)
{
	std::vector<BonusType> indexedReads, legacyReads;
	const auto actual = indexed.collect(bonuses, [&](BonusType type)
	{
		indexedReads.push_back(type);
		return type;
	});
	const auto expected = bonusPresenceLegacy::collect(legacy, bonuses, [&](BonusType type)
	{
		legacyReads.push_back(type);
		return type;
	});
	require(indexedReads == legacyReads && indexedReads == bonuses, "bonus read sequence changed");
	require(actual == expected, "presence map changed");
	require(actual.bucket_count() == expected.bucket_count(), "presence map allocation layout changed");
	require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()),
		"presence map iteration order changed");
}

void explicitCases()
{
	scripting::DeclaredBonusTypes indexed;
	bonusPresenceLegacy::Declarations legacy;
	const auto add = [&](BonusType type, const std::string & name)
	{
		indexed.add(type, name);
		legacy.try_emplace(type, name);
	};
	require(indexed.empty(), "new declarations must be empty");
	compare(indexed, legacy, {BonusType::LUCK, BonusType::LUCK});
	add(BonusType::LUCK, "first-name");
	add(BonusType::LUCK, "ignored-alias");
	add(BonusType::MORALE, "first-name");
	add(static_cast<BonusType>(typeCount), "future-type");
	add(static_cast<BonusType>(65535), "maximum-type");
	add(BonusType::NONE, "");
	require(!indexed.empty(), "added declarations must not be empty");
	std::vector<BonusType> bonuses{BonusType::MORALE, BonusType::LUCK, BonusType::LUCK,
		static_cast<BonusType>(65535), static_cast<BonusType>(typeCount), BonusType::NONE};
	compare(indexed, legacy, bonuses);
	// Rehash the declaration map after establishing borrowed dense entries.
	for(size_t type = 0; type < typeCount; ++type)
		add(static_cast<BonusType>(type), "bonus-" + std::to_string(type));
	compare(indexed, legacy, bonuses);
	// Presence is rebuilt from every current returned list, never retained.
	bonuses.erase(bonuses.begin(), bonuses.begin() + 3);
	compare(indexed, legacy, bonuses);
	compare(indexed, legacy, {});
	for(size_t type = 0; type < typeCount; ++type)
		bonuses.push_back(static_cast<BonusType>(type));
	compare(indexed, legacy, bonuses);
	std::reverse(bonuses.begin(), bonuses.end());
	compare(indexed, legacy, bonuses);
}

void randomizedCases()
{
	std::mt19937 generator(0xB0A05u);
	for(size_t sample = 0; sample < 2000; ++sample)
	{
		scripting::DeclaredBonusTypes indexed;
		bonusPresenceLegacy::Declarations legacy;
		const auto declarations = generator() % (typeCount + 40);
		for(size_t i = 0; i < declarations; ++i)
		{
			const auto type = static_cast<BonusType>(generator() % (typeCount + 20));
			const auto name = "declared-bonus-name-" + std::to_string(generator() % 80);
			indexed.add(type, name);
			legacy.try_emplace(type, name);
		}
		std::vector<BonusType> bonuses(generator() % 700);
		for(auto & type : bonuses)
			type = static_cast<BonusType>(generator() % (typeCount + 30));
		compare(indexed, legacy, bonuses);
	}
}
}

int main()
{
	try
	{
		explicitCases();
		randomizedCases();
		std::cout << "Lua declaration index: explicit and 2000 seeded parity cases passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
