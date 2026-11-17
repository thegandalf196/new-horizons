/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../constants/EntityIdentifiers.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace newHorizonsHeroes
{
class ValidatedPerkRules;
class CompiledPerkCatalogue;

/// Frequent numeric combat queries. This key is resolved against each actual
/// captured catalogue; it never supplies effect status, rank or installed defaults.
enum class BuiltinCombatPerk : uint8_t
{
	Executioner, ArmorPiercer, Breakthrough, ShockAssault, LuckyAim, ElvenPrecision, Count
};

/// Runtime-only handle. Its identity belongs to one immutable catalogue and
/// cannot be serialized or accidentally applied to another captured catalogue.
class DLL_LINKAGE PerkId
{
	std::shared_ptr<const uint8_t> identity;
	uint32_t index = 0;
	friend class CompiledPerkCatalogue;
	PerkId(std::shared_ptr<const uint8_t> identity, uint32_t index)
		: identity(std::move(identity)), index(index) {}
public:
	PerkId() = default;
	bool operator==(const PerkId &) const = default;
	explicit operator bool() const { return bool(identity); }
};

struct DLL_LINKAGE CompiledPerkDefinition
{
	uint32_t denseIndex;
	SecondarySkill skill;
	int requiredRank;
	bool active;
};

/// Immutable projection of an admitted catalogue, shared by all its heroes.
/// Only cold string boundaries use maps; typed queries index the definition array.
class DLL_LINKAGE CompiledPerkCatalogue
{
	std::shared_ptr<const uint8_t> identity = std::make_shared<const uint8_t>(0);
	std::map<std::string, SecondarySkill, std::less<>> skills;
	std::map<std::string, std::map<std::string, uint32_t, std::less<>>, std::less<>> indices;
	std::vector<CompiledPerkDefinition> definitions;
	std::array<PerkId, static_cast<size_t>(BuiltinCombatPerk::Count)> combatPerks;
public:
	explicit CompiledPerkCatalogue(const ValidatedPerkRules & admitted);
	PerkId find(std::string_view skill, std::string_view perk) const;
	const PerkId & find(BuiltinCombatPerk perk) const;
	std::optional<SecondarySkill> skill(std::string_view skill) const;
	const CompiledPerkDefinition * definition(const PerkId & id) const;
	size_t size() const { return definitions.size(); }
};
}
