/*
 * HeroStartingProjection.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "CHero.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsCapabilityRules.h"
#include <set>

class IGameSettings;
class CGHeroInstance;

namespace newHorizonsHeroes
{
/// Explicit immutable creation context. Empty snapshots mean legacy, never
/// permission to consult installed defaults. Resolved contexts belong to a hero.
struct DLL_LINKAGE StartingHeroContext
{
	JsonNode development;
	JsonNode capabilities;
	JsonNode perks;
	JsonNode magic;
	bool resolved = false;
	/// Explicit legacy display bounds; NH bounds come from captured development.
	std::vector<int> minimumPrimary;
	std::vector<int> maximumPrimary;
};

/// Map-authored PRESET values are independent: absence requests the default.
struct DLL_LINKAGE StartingHeroOverrides
{
	std::optional<std::array<int, GameConstants::PRIMARY_SKILLS>> primary;
	std::optional<std::vector<std::pair<SecondarySkill, ui8>>> skills;
	std::optional<std::set<SpellID>> spells;
	std::optional<std::vector<CHero::InitialArmyStack>> army;
	std::optional<bool> spellBook;
	/// Only bonuses already present before initArmy, not eventual Skills/perks.
	int leadershipBonus = 0;
};

struct DLL_LINKAGE StartingHeroProjection
{
	std::array<int, GameConstants::PRIMARY_SKILLS> primary{};
	std::vector<std::pair<SecondarySkill, ui8>> skills;
	std::vector<PerkSelection> perks;
	std::set<SpellID> spells;
	std::vector<CHero::InitialArmyStack> army;
	bool spellBook = false;
};

DLL_LINKAGE StartingHeroContext startingHeroContext(const IGameSettings & settings);
/// Only for an uninitialized loader hero, not a saved/current hero's inventory.
DLL_LINKAGE StartingHeroOverrides authoredStartingHeroOverrides(const CGHeroInstance & hero);
DLL_LINKAGE StartingHeroProjection projectStartingHero(const CHero & hero,
	const StartingHeroContext & context, const StartingHeroOverrides & overrides = {});
/// Exact runtime inscription precedence; no cast, artifact allocation or RNG.
DLL_LINKAGE std::optional<SpellID> startingHeroSpellReplacement(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, SpellID source);
DLL_LINKAGE std::optional<SpellID> authoredRemainingSpellReplacement(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, SpellID source);
DLL_LINKAGE std::optional<SpellID> authoredDamageSpellReplacement(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, SpellID source);
DLL_LINKAGE std::optional<SpellID> authoredNonDamageSpellReplacement(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, SpellID source);
/// Shared clamp primitive, supplied the actual per-slot capacity by runtime.
DLL_LINKAGE int64_t clampStartingArmyCount(int64_t count, std::optional<int64_t> maximum);
DLL_LINKAGE void applyStartingLeadershipBonus(LeadershipSlotCapacity & capacity, int bonus);
DLL_LINKAGE void selectStartingHeroPerks(PerkState & state,
	const std::vector<PerkSelection> & profile, const std::vector<PerkSelection> & prototype,
	const std::function<int(const std::string &)> & rank);
}

