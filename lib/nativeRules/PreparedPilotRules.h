/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "MechanicsAuthoring.h"
#include <array>
#include <map>
namespace nativeRules
{
/// Cold captured definitions only. No selection facts or mutable battle state.
class DLL_LINKAGE PreparedPilotPerks
{
	std::map<std::pair<SecondarySkill, int>, std::vector<std::shared_ptr<const Bonus>>> ranks;
	std::array<std::optional<PreparedMechanics>, 2> timed;
	friend DLL_LINKAGE void validatePilotRankRow(const std::string &, const std::string &, const JsonNode &, PreparedPilotPerks *);
	friend DLL_LINKAGE void validatePilotPerkRow(const std::string &, const JsonNode &, PreparedPilotPerks *);
public:
	std::optional<std::vector<std::shared_ptr<Bonus>>> rank(SecondarySkill skill, int level) const;
	const PreparedMechanics * addition(TimedSourceSlot slot) const;
};
class DLL_LINKAGE PreparedPilotCreature
{
	Bonus prototype;
public:
	explicit PreparedPilotCreature(const Bonus & prototype) : prototype(prototype) {}
	const Bonus & bonus() const { return prototype; }
	std::shared_ptr<Bonus> attach() const { return std::make_shared<Bonus>(prototype); }
};
/// Structural admission can run before the native identifier registries finish loading.
DLL_LINKAGE void validatePilotPerks(const JsonNode & rules, bool supported = true);
DLL_LINKAGE void validatePilotCreatures(const JsonNode & development, bool supported = true);
DLL_LINKAGE void validatePilotRankRow(const std::string & skill, const std::string & rank, const JsonNode & row, PreparedPilotPerks * collector = nullptr);
DLL_LINKAGE void validatePilotPerkRow(const std::string & skill, const JsonNode & row, PreparedPilotPerks * collector = nullptr);
/// Saved settings overlays may omit inherited siblings; no defaults are read here.
DLL_LINKAGE void validatePilotOverrides(const JsonNode & overrides, bool supported);
/// Resolves identifiers once after content load, exclusively from captured rows.
DLL_LINKAGE std::shared_ptr<const PreparedPilotPerks> preparePilotPerks(const JsonNode & rules);
DLL_LINKAGE std::shared_ptr<const PreparedPilotCreature> preparePilotCreature(const JsonNode & development, HeroTypeID hero);
DLL_LINKAGE bool hasPreparedCreatureRow(const JsonNode & development, std::string_view hero);
}
