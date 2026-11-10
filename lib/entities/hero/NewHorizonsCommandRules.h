/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../json/JsonNode.h"
#include "../../constants/EntityIdentifiers.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace newHorizonsHeroes
{
enum class CommandSlotGrade : uint8_t { CORE, ELITE, CHAMPION };
enum class CommandRecipient : uint8_t { CORE_BLOCK, ELITE_1, ELITE_2, ELITE_3, CHAMPION };
enum class CommandEnhancement : uint8_t
{
	READINESS, MOBILITY, COUNTERATTACK, WEAPON_DRILL, GUARD_DRILL,
	FORTUNE, COURAGE, ENDURANCE, WARDING
};
enum class AuthorityPerk : uint8_t
{
	OFFENSIVE_DRILLS, DEFENSIVE_DRILLS, CONDITIONING, BATTLE_READINESS, OFFICER_CORPS,
	MANEUVER_TRAINING, COUNTERATTACK_TRAINING, PROTECTIVE_TRAINING, VETERAN_CORPS, GRAND_MARSHAL
};
inline constexpr std::array<std::string_view, 9> COMMAND_ENHANCEMENT_KEYS = {
	"readiness", "mobility", "counterattack", "weaponDrill", "guardDrill",
	"fortune", "courage", "endurance", "warding"};
inline constexpr std::array<std::string_view, 10> AUTHORITY_PERK_KEYS = {
	"offensiveDrills", "defensiveDrills", "conditioning", "battleReadiness", "officerCorps",
	"maneuverTraining", "counterattackTraining", "protectiveTraining", "veteranCorps", "grandMarshal"};

struct DLL_LINKAGE CommandEnhancementFormula
{
	uint32_t levelDivisor = 1;
	int64_t multiplier = 0;
	bool operator==(const CommandEnhancementFormula &) const = default;
};

/// Parsed only from an explicit resolved armyCommand capture, never installed defaults.
struct DLL_LINKAGE CommandRules
{
	uint32_t levelInterval = 1;
	std::array<int32_t, 4> authorityRankBonuses{};
	std::array<CommandEnhancementFormula, 9> enhancements{};
	std::array<int32_t, 10> authorityPerkValues{};
	void validate() const;
	bool operator==(const CommandRules &) const = default;
};
struct DLL_LINKAGE CommandValue
{
	int64_t natural = 0;
	int32_t authority = 0;
	int64_t effective() const { return natural + authority; }
};
struct DLL_LINKAGE CommandSlotView
{
	CommandSlotGrade grade;
	int64_t unlockCommand;
	bool unlocked;
	int64_t enhancementTier;
};

/// Raw contributions. HP and Warding awards add within their respective Command
/// sources; independent mitigation still combines multiplicatively downstream.
/// No final Luck/Morale, HP rounding or damage/retaliation eligibility is replaced.
struct DLL_LINKAGE CommandModifiers
{
	int64_t initiative = 0, speed = 0, retaliations = 0;
	int64_t attack = 0, defense = 0, luck = 0, morale = 0;
	int64_t maximumHealthPercent = 0, magicalReductionPercent = 0, directAttackDamagePercent = 0;
	void add(const CommandModifiers & other);
	bool operator==(const CommandModifiers &) const = default;
};

DLL_LINKAGE void validateArmyCommandPolicy(const JsonNode & policy, bool resolved);
DLL_LINKAGE JsonNode resolveArmyCommandPolicy(const JsonNode & policy, std::string_view classKey);
/// Missing policy returns nullopt; present null/unknown/malformed policies reject.
DLL_LINKAGE std::optional<CommandRules> capturedCommandRules(const JsonNode & capabilities);
DLL_LINKAGE void validateArmyCommandSerialization(const JsonNode & capabilities, bool supported);
DLL_LINKAGE CommandValue commandValue(const CommandRules & rules, uint32_t heroLevel, int authorityRank);
DLL_LINKAGE CommandSlotView commandSlotView(int64_t command, SlotID slot);
DLL_LINKAGE CommandRecipient commandMilestoneRecipient(int64_t milestone);
DLL_LINKAGE bool commandRecipientContains(CommandRecipient recipient, SlotID slot);
DLL_LINKAGE int64_t commandEnhancementValue(const CommandRules & rules, CommandEnhancement enhancement, uint32_t level);
DLL_LINKAGE CommandModifiers commandEnhancementModifiers(CommandEnhancement enhancement, int64_t value);
DLL_LINKAGE CommandModifiers authoritySlotModifiers(const CommandRules & rules, SlotID slot,
	const std::array<bool, 10> & activePerks);
}
