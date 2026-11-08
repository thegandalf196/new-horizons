/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../lib/bonuses/BonusEnum.h"

#include <array>
#include <bitset>
#include <string>
#include <unordered_map>

namespace scripting
{
/// Script-owned declarations, populated by call_once and read thereafter.
/// The dense index borrows stable unordered_map elements; no unit data survives
/// collection. Unknown enum values keep the same map lookup as before.
class DeclaredBonusTypes final
{
	static constexpr size_t typeCount = static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT);
	std::unordered_map<BonusType, std::string> names;
	std::array<const std::string *, typeCount> indexed{};

public:
	DeclaredBonusTypes() = default;
	DeclaredBonusTypes(const DeclaredBonusTypes &) = delete;
	DeclaredBonusTypes & operator=(const DeclaredBonusTypes &) = delete;
	DeclaredBonusTypes(DeclaredBonusTypes &&) = delete;
	DeclaredBonusTypes & operator=(DeclaredBonusTypes &&) = delete;

	void add(BonusType type, const std::string & name)
	{
		const auto entry = names.try_emplace(type, name).first;
		const auto index = static_cast<size_t>(type);
		if(index < indexed.size())
			indexed[index] = &entry->second;
	}

	bool empty() const { return names.empty(); }

	/// Preserve the source traversal and first insertion order, including aliases
	/// that name one type more than once or give multiple types the same name.
	template<class Range, class TypeOf>
	std::unordered_map<std::string, bool> collect(const Range & bonuses, TypeOf typeOf) const
	{
		std::unordered_map<std::string, bool> result;
		std::bitset<typeCount> inserted;
		for(const auto & bonus : bonuses)
		{
			const auto type = typeOf(bonus);
			const auto index = static_cast<size_t>(type);
			if(index < indexed.size())
			{
				const auto * name = indexed[index];
				if(name && !inserted[index])
				{
					result.try_emplace(*name, true);
					inserted[index] = true;
				}
			}
			else if(const auto entry = names.find(type); entry != names.end())
				result.try_emplace(entry->second, true);
		}
		return result;
	}
};
}
