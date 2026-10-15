/*
 * NewHorizonsPerkStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <stdexcept>
#include <array>
#include <limits>
#include <cmath>
#include <boost/multiprecision/cpp_int.hpp>

namespace
{
newHorizonsHeroes::PerkState state()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	// Historical pure-state tests exercise the captured legacy offer policy.
	// Weighted-policy fixtures explicitly opt back into version1 below.
	result.rules.Struct().erase("offerWeightPolicyVersion");
	result.rules.Struct().erase("perkOfferNormalization");
	return result;
}

const std::string SKILL = "new-horizons:sorceryMagic";
// This identity is used by an explicitly synthetic planned snapshot below;
// its current authored pool is active and must not be treated as planned.
const std::string PLANNED_SKILL = "new-horizons:armorer";

newHorizonsHeroes::PerkState plannedState()
{
	auto result = state();
	for(auto & perk : result.rules["skills"][PLANNED_SKILL]["perks"].Vector())
		perk["effect"]["status"].String() = "planned";
	return result;
}
constexpr auto HISTORICAL_PERK_SOURCE_SHA256 =
	"d0aa9c0017967e85120b4e63e04df3d58441d606ce9c496d29117515330654ce";
constexpr auto CURRENT_DESIGN_SOURCE_SHA256 =
	"7fd38c3b12386e62f511d66bfbd1ea615f301d1e2bdf57a9c70b831ff3ff81e4";
constexpr auto UNKNOWN_SOURCE_SHA256 =
	"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

struct LegacyHeroProcText
{
	const char * skill;
	const char * perk;
	const char * name;
	const char * description;
};

const std::array<LegacyHeroProcText, 4> LEGACY_HERO_PROC_TEXT = {{
	{"new-horizons:luck", "new-horizons:luck.luckyRecovery", "Lucky Recovery",
		"When a melee attack triggers positive Luck, surviving creatures in the attacker recover HP equal to 10% of actual damage dealt."},
	{"new-horizons:luck", "new-horizons:luck.gambler", "Gambler",
		"The first friendly attack each round gains +3 Luck for that attack. If positive Luck does not trigger, the attacking stack suffers -2 Luck until its next activation."},
	{"new-horizons:luck", "new-horizons:luck.twistOfFate", "Twist of Fate",
		"The first random combat roll each combat that produces a negative result for the hero's army is automatically rerolled once. Deterministic effects cannot be rerolled."},
	{"new-horizons:discipline", "new-horizons:discipline.heroicSpirit", "Heroic Spirit",
		"When positive Morale triggers, the affected stack gains one additional retaliation until its next activation."}
}};

JsonNode & savedPerk(JsonNode & rules, const char * skill, const char * id)
{
	for(auto & perk : rules["skills"][skill]["perks"].Vector())
		if(perk["id"].String() == id)
			return perk;
	throw std::runtime_error("Missing fixture perk");
}

newHorizonsHeroes::PerkState legacyHeroProcState()
{
	auto result = state();
	result.select("new-horizons:luck", "new-horizons:luck.secondChance", 1);
	result.select("new-horizons:luck", "new-horizons:luck.luckyRecovery", 2);
	result.select("new-horizons:luck", "new-horizons:luck.twistOfFate", 3);
	result.select("new-horizons:discipline", "new-horizons:discipline.steadfast", 1);
	result.select("new-horizons:discipline", "new-horizons:discipline.holdFast", 2);
	result.select("new-horizons:discipline", "new-horizons:discipline.heroicSpirit", 3);
	for(const auto & entry : LEGACY_HERO_PROC_TEXT)
	{
		auto & perk = savedPerk(result.rules, entry.skill, entry.perk);
		perk["name"].String() = entry.name;
		perk["description"].String() = entry.description;
		perk["effect"]["description"].String() = entry.description;
	}
	// Preserve saved policy and unrelated text rather than adopting today's
	// full catalogue. The unselected Gambler row can remain planned in a save.
	savedPerk(result.rules, "new-horizons:luck", "new-horizons:luck.gambler")["effect"]["status"].String() = "planned";
	result.rules["sourceSha256"].String() = UNKNOWN_SOURCE_SHA256;
	result.rules["skills"][SKILL]["ranks"]["basic"]["description"].String() = "Saved unrelated coefficient: +42%.";
	result.validate();
	return result;
}

JsonNode expectedHeroProcText(const JsonNode & saved)
{
	auto result = saved;
	const auto canonical = state();
	for(const auto & entry : LEGACY_HERO_PROC_TEXT)
	{
		const auto current = newHorizonsHeroes::perkDefinition(canonical.rules, entry.skill, entry.perk);
		if(!current)
			throw std::runtime_error("Missing canonical fixture perk");
		auto & perk = savedPerk(result, entry.skill, entry.perk);
		perk["name"].String() = current->name;
		perk["description"].String() = current->description;
		perk["effect"]["description"] = current->effect["description"];
	}
	return result;
}
}

TEST(NewHorizonsPerkState, LegacyHeroProcTextMigratesFromCanonicalOnJsonLoadWithoutChangingSavedPolicy)
{
	const auto legacy = legacyHeroProcState();
	const auto expected = expectedHeroProcText(legacy.rules);
	const auto restored = newHorizonsHeroes::PerkState::fromJson(legacy.toJson());
	EXPECT_EQ(restored.rules, expected);
	EXPECT_EQ(restored.selected, legacy.selected);
	EXPECT_EQ(restored.rules["skills"]["new-horizons:sylvanLuck"], legacy.rules["skills"]["new-horizons:sylvanLuck"]);
	EXPECT_EQ(restored.rules["sourceSha256"], legacy.rules["sourceSha256"]);
}

TEST(NewHorizonsPerkState, LegacyHeroProcTextMigratesOnBinaryLoadAndIsIdempotent)
{
	const auto legacy = legacyHeroProcState();
	CMemorySerializer memory;
	memory.oser & legacy;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.rules, expectedHeroProcText(legacy.rules));
	EXPECT_EQ(restored.selected, legacy.selected);
	const auto jsonAgain = newHorizonsHeroes::PerkState::fromJson(restored.toJson());
	EXPECT_EQ(jsonAgain.rules, restored.rules);
	EXPECT_EQ(jsonAgain.selected, restored.selected);
	CMemorySerializer again;
	again.oser & restored;
	newHorizonsHeroes::PerkState binaryAgain;
	again.iser & binaryAgain;
	EXPECT_EQ(binaryAgain.rules, restored.rules);
	EXPECT_EQ(binaryAgain.selected, restored.selected);
}

TEST(NewHorizonsPerkState, CurrentAndUnrecognizedHeroProcTextRemainExactlyCaptured)
{
	for(bool customLegacy : {false, true})
	{
		auto captured = state();
		if(customLegacy)
		{
			auto & perk = savedPerk(captured.rules, "new-horizons:luck", "new-horizons:luck.gambler");
			perk["name"].String() = "Gambler";
			perk["description"].String() = "Unrecognized saved custom semantics.";
			perk["effect"]["description"].String() = "Unrecognized saved custom semantics.";
		}
		const auto json = newHorizonsHeroes::PerkState::fromJson(captured.toJson());
		EXPECT_EQ(json.rules, captured.rules);
		EXPECT_EQ(json.selected, captured.selected);
		CMemorySerializer memory;
		memory.oser & captured;
		newHorizonsHeroes::PerkState binary;
		memory.iser & binary;
		EXPECT_EQ(binary.rules, captured.rules);
		EXPECT_EQ(binary.selected, captured.selected);
	}
}

TEST(NewHorizonsPerkState, LegacyStateRemainsEmptyAndCannotSelect)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_NO_THROW(legacy.validate());
	EXPECT_TRUE(legacy.toJson().isNull());
	EXPECT_THROW(legacy.select(SKILL, "new-horizons:offense.shockAssault", 3), std::runtime_error);
}

TEST(NewHorizonsPerkState, SelectionEnforcesExactlyOnePerTierAndThreePerSkillCap)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	ASSERT_EQ(options.size(), 10u);
	EXPECT_THROW(saved.select(SKILL, options[0].id, -1), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 4), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[4].id, 1), std::runtime_error);
	saved.select(SKILL, options[0].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[1].id, 3), std::runtime_error);
	saved.select(SKILL, options[4].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[5].id, 3), std::runtime_error);
	saved.select(SKILL, options[8].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[9].id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, "new-horizons:offense.unknown", 3), std::runtime_error);
	EXPECT_EQ(saved.selected.size(), 3u);
}

TEST(NewHorizonsPerkState, OrdinaryRanksAndPerksAlternateWhilePerkTiersStayOrdered)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	const auto noRank = [](const std::string &) { return 0; };
	const auto expert = [](const std::string & skillId) { return skillId == SKILL ? 3 : 0; };

	EXPECT_TRUE(saved.canAdvanceSkillNormally(SKILL, 0));
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 1));

	// Exceptional teacher advancement can raise rank without granting a perk.
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 2));
	auto offer = saved.prepareOffer(expert, 11);
	ASSERT_EQ(offer.size(), 2u);
	for(const auto & candidate : offer)
		EXPECT_EQ(candidate.requiredRank, 1);
	EXPECT_THROW(saved.select(SKILL, options[4].id, 3), std::runtime_error);

	saved.acceptOffer(offer, 0, expert, 11);
	EXPECT_TRUE(saved.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 2));
	offer = saved.prepareOffer(expert, 12);
	ASSERT_EQ(offer.size(), 2u);
	for(const auto & candidate : offer)
		EXPECT_EQ(candidate.requiredRank, 2);

	saved.acceptOffer(offer, 0, expert, 12);
	offer = saved.prepareOffer(expert, 13);
	ASSERT_EQ(offer.size(), 2u);
	for(const auto & candidate : offer)
		EXPECT_EQ(candidate.requiredRank, 3);
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 3));
	EXPECT_TRUE(saved.prepareOffer(noRank, 14).empty());
}

TEST(NewHorizonsPerkState, SpellPenetrationOpensNormalAdvancedSpellcraftProgression)
{
	auto saved = state();
	constexpr auto SKILL = "new-horizons:spellcraft";
	constexpr auto PERK = "new-horizons:spellcraft.spellPenetration";

	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 1));
	saved.select(SKILL, PERK, 1);
	EXPECT_TRUE(saved.hasSelection(SKILL, PERK));
	EXPECT_TRUE(saved.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 2));
}

TEST(NewHorizonsPerkState, EmpowerSpellOpensNormalExpertSpellcraftProgression)
{
	auto saved = state();
	constexpr auto SKILL = "new-horizons:spellcraft";
	constexpr auto BASIC_PERK = "new-horizons:spellcraft.spellPenetration";
	constexpr auto ADVANCED_PERK = "new-horizons:spellcraft.empowerSpell";

	saved.select(SKILL, BASIC_PERK, 1);
	EXPECT_TRUE(saved.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 2));
	saved.select(SKILL, ADVANCED_PERK, 2);
	EXPECT_TRUE(saved.hasSelection(SKILL, ADVANCED_PERK));
	EXPECT_TRUE(saved.canAdvanceSkillNormally(SKILL, 2));
}

TEST(NewHorizonsPerkState, CommanderPerksOpenNormalAdvancedAndExpertCommandProgression)
{
	constexpr auto COMMAND = "new-horizons:command";
	constexpr auto VETERAN = "new-horizons:command.veteranCommander";
	for(const auto * basic : {"new-horizons:command.aggressiveCommander",
		"new-horizons:command.defensiveCommander"})
	{
		SCOPED_TRACE(basic);
		auto saved = state();
		EXPECT_FALSE(saved.canAdvanceSkillNormally(COMMAND, 1));
		saved.select(COMMAND, basic, 1);
		EXPECT_TRUE(saved.canAdvanceSkillNormally(COMMAND, 1));
		EXPECT_FALSE(saved.canAdvanceSkillNormally(COMMAND, 2));
		saved.select(COMMAND, VETERAN, 2);
		EXPECT_TRUE(saved.canAdvanceSkillNormally(COMMAND, 2));
		EXPECT_EQ(saved.selected.size(), 2u);
	}
}

TEST(NewHorizonsPerkState, OccupiedTierOnlyBlocksAlternativesFromTheSameSkill)
{
	auto saved = state();
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	const std::string otherSkill = "new-horizons:discipline";
	ASSERT_GE(sorceryOptions.size(), 2u);
	const std::string disciplinePerk = "new-horizons:discipline.inspirationalLeader";

	saved.select(SKILL, sorceryOptions[0].id, 1);
	EXPECT_THROW(saved.select(SKILL, sorceryOptions[1].id, 1), std::runtime_error);
	EXPECT_NO_THROW(saved.select(otherSkill, disciplinePerk, 1));
	EXPECT_TRUE(saved.hasSelection(SKILL, sorceryOptions[0].id));
	EXPECT_TRUE(saved.hasSelection(otherSkill, disciplinePerk));
}

TEST(NewHorizonsPerkState, OfferKeepsSameTierPerksFromOtherSkillsEligible)
{
	auto saved = state();
	// Isolate occupied-tier admission from the growing authored Discipline pool.
	for(auto & perk : saved.rules["skills"]["new-horizons:discipline"]["perks"].Vector())
		if(perk["id"].String() != "new-horizons:discipline.inspirationalLeader")
			perk["effect"]["status"].String() = "planned";
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	ASSERT_GE(sorceryOptions.size(), 2u);
	saved.select(SKILL, sorceryOptions[0].id, 1);

	const auto offer = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == SKILL || skillId == "new-horizons:discipline" ? 1 : 0;
	}, 17);
	ASSERT_EQ(offer.size(), 1u);
	EXPECT_EQ(offer.front().selection.skillId, "new-horizons:discipline");
	EXPECT_EQ(offer.front().selection.perkId, "new-horizons:discipline.inspirationalLeader");
	EXPECT_EQ(offer.front().requiredRank, 1);
}

TEST(NewHorizonsPerkState, PlannedAndRankLockedEffectsNeverProjectAsEnabled)
{
	auto saved = plannedState();
	const auto planned = newHorizonsHeroes::perkOptions(saved.rules, PLANNED_SKILL);
	ASSERT_GE(planned.size(), 2u);
	const auto & perk = planned.front();
	EXPECT_THROW(saved.select(PLANNED_SKILL, planned[1].id, 3), std::runtime_error);
	// Existing saves may contain a planned selection from an earlier registry;
	// keep loading it, but never allow a new one to be authored.
	saved.selected.push_back({PLANNED_SKILL, perk.id});
	saved.validate();
	auto projected = saved.project([](const std::string &) { return 3; });
	ASSERT_EQ(projected.size(), 1u);
	EXPECT_FALSE(projected.front().enabled);
	EXPECT_EQ(projected.front().effect["status"].String(), "planned");
	saved.rules["skills"][PLANNED_SKILL]["perks"].Vector()[0]["effect"]["status"].String() = "active";
	projected = saved.project([](const std::string &) { return 0; });
	EXPECT_FALSE(projected.front().enabled);
	projected = saved.project([](const std::string &) { return 1; });
	EXPECT_TRUE(projected.front().enabled);
}

TEST(NewHorizonsPerkState, OverchargerActivationComesFromTheSavedRegistrySnapshot)
{
	auto active = state();
	constexpr auto SKILL = "new-horizons:sorceryMagic";
	constexpr auto PERK = "new-horizons:sorceryMagic.overcharger";
	for(auto & perk : active.rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == PERK)
			perk["effect"]["status"].String() = "active";
	active.select(SKILL, PERK, 1);
	const auto activeProjection = active.project([](const std::string &) { return 1; });
	ASSERT_EQ(activeProjection.size(), 1u);
	EXPECT_TRUE(activeProjection.front().enabled);

	auto oldSave = active;
	for(auto & perk : oldSave.rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == PERK)
			perk["effect"]["status"].String() = "planned";
	const auto oldProjection = oldSave.project([](const std::string &) { return 1; });
	ASSERT_EQ(oldProjection.size(), 1u);
	EXPECT_FALSE(oldProjection.front().enabled);
}

TEST(NewHorizonsPerkState, JsonAndBinaryRoundTripsPreserveSavedRegistrySnapshot)
{
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.rules, SKILL).front();
	original.select(SKILL, perk.id, 3);
	const auto legacyPlanned = newHorizonsHeroes::perkOptions(original.rules, PLANNED_SKILL).front();
	original.selected.push_back({PLANNED_SKILL, legacyPlanned.id});
	original.validate();
	const auto savedJson = original.toJson();
	auto fromJson = newHorizonsHeroes::PerkState::fromJson(savedJson);
	ASSERT_EQ(fromJson.selected, original.selected);
	EXPECT_EQ(fromJson.rules, original.rules);

	CMemorySerializer memory;
	memory.oser & original;
	newHorizonsHeroes::PerkState fromBinary;
	memory.iser & fromBinary;
	EXPECT_EQ(fromBinary.selected, original.selected);
	EXPECT_EQ(fromBinary.rules, original.rules);

	original.rules["skills"].Struct().clear();
	EXPECT_TRUE(fromJson.hasSelection(SKILL, perk.id));
	EXPECT_TRUE(fromBinary.hasSelection(SKILL, perk.id));
	EXPECT_TRUE(fromJson.hasSelection(PLANNED_SKILL, legacyPlanned.id));
	EXPECT_TRUE(fromBinary.hasSelection(PLANNED_SKILL, legacyPlanned.id));
}

TEST(NewHorizonsPerkState, HistoricalRetiredSpellBufferMigratesToSpellEchoAcrossJsonAndBinaryLoads)
{
	auto legacy = state();
	legacy.rules["rulesetVersion"].Integer() = 1;
	legacy.rules["sourceSha256"].String() = HISTORICAL_PERK_SOURCE_SHA256;
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto retired = "new-horizons:metamagic.spellBuffer";
	constexpr auto replacement = "new-horizons:metamagic.spellEcho";
	for(auto & perk : legacy.rules["skills"][skill]["perks"].Vector())
	{
		if(perk["id"].String() != replacement && perk["id"].String() != retired)
			continue;
		perk["id"].String() = retired;
		perk["name"].String() = "Spell Buffer";
		perk["description"].String() = "Retired countered-spell rule.";
		perk["effect"]["description"].String() = "Retired countered-spell rule.";
	}
	legacy.selected.push_back({skill, retired});
	legacy.validate();

	const auto restoredJson = newHorizonsHeroes::PerkState::fromJson(legacy.toJson());
	EXPECT_TRUE(restoredJson.hasSelection(skill, replacement));
	EXPECT_FALSE(restoredJson.hasSelection(skill, retired));
	const auto jsonDefinition = newHorizonsHeroes::perkDefinition(
		restoredJson.rules, skill, replacement);
	ASSERT_TRUE(jsonDefinition);
	EXPECT_EQ(jsonDefinition->name, "Spell Echo");
	EXPECT_NE(jsonDefinition->description.find("repeats the first Spell"), std::string::npos);

	CMemorySerializer memory;
	memory.oser & legacy;
	newHorizonsHeroes::PerkState restoredBinary;
	memory.iser & restoredBinary;
	EXPECT_TRUE(restoredBinary.hasSelection(skill, replacement));
	EXPECT_FALSE(restoredBinary.hasSelection(skill, retired));
	const auto binaryDefinition = newHorizonsHeroes::perkDefinition(
		restoredBinary.rules, skill, replacement);
	ASSERT_TRUE(binaryDefinition);
	EXPECT_EQ(binaryDefinition->name, "Spell Echo");

	// A v1 save that has already migrated must retain Echo on subsequent loads.
	auto migrated = restoredBinary;
	const auto reloadedJson = newHorizonsHeroes::PerkState::fromJson(migrated.toJson());
	EXPECT_EQ(reloadedJson.rules, migrated.rules);
	EXPECT_EQ(reloadedJson.selected, migrated.selected);
	CMemorySerializer migratedMemory;
	migratedMemory.oser & migrated;
	newHorizonsHeroes::PerkState reloadedBinary;
	migratedMemory.iser & reloadedBinary;
	EXPECT_EQ(reloadedBinary.rules, migrated.rules);
	EXPECT_EQ(reloadedBinary.selected, migrated.selected);
}

TEST(NewHorizonsPerkState, RevivedSpellBufferSurvivesJsonAndBinaryLoadsForNewOrUnknownSnapshots)
{
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto revived = "new-horizons:metamagic.spellBuffer";
	constexpr auto replacement = "new-horizons:metamagic.spellEcho";
	constexpr auto revivedDescription = "An unused Metamagic Spell Action grants 6 buffer Mana when it expires.";

	for(const auto & [version, sourceSha256] : std::vector<std::pair<int, const char *>>{
		{1, CURRENT_DESIGN_SOURCE_SHA256}, {1, UNKNOWN_SOURCE_SHA256},
		{2, HISTORICAL_PERK_SOURCE_SHA256}, {2, CURRENT_DESIGN_SOURCE_SHA256}, {2, UNKNOWN_SOURCE_SHA256}})
	{
		auto current = state();
		current.rules["rulesetVersion"].Integer() = version;
		current.rules["sourceSha256"].String() = sourceSha256;
		for(auto & perk : current.rules["skills"][skill]["perks"].Vector())
		{
			if(perk["id"].String() != replacement && perk["id"].String() != revived)
				continue;
			perk["id"].String() = revived;
			perk["name"].String() = "Spell Buffer";
			perk["description"].String() = revivedDescription;
			perk["effect"]["description"].String() = revivedDescription;
		}
		current.selected.push_back({skill, revived});
		current.validate();

		const auto restoredJson = newHorizonsHeroes::PerkState::fromJson(current.toJson());
		EXPECT_TRUE(restoredJson.hasSelection(skill, revived));
		EXPECT_FALSE(restoredJson.hasSelection(skill, replacement));
		EXPECT_EQ(restoredJson.rules, current.rules);
		EXPECT_EQ(restoredJson.selected, current.selected);
		const auto jsonDefinition = newHorizonsHeroes::perkDefinition(restoredJson.rules, skill, revived);
		ASSERT_TRUE(jsonDefinition);
		EXPECT_EQ(jsonDefinition->name, "Spell Buffer");
		EXPECT_EQ(jsonDefinition->description, revivedDescription);

		CMemorySerializer memory;
		memory.oser & current;
		newHorizonsHeroes::PerkState restoredBinary;
		memory.iser & restoredBinary;
		EXPECT_TRUE(restoredBinary.hasSelection(skill, revived));
		EXPECT_FALSE(restoredBinary.hasSelection(skill, replacement));
		EXPECT_EQ(restoredBinary.rules, current.rules);
		EXPECT_EQ(restoredBinary.selected, current.selected);
		const auto binaryDefinition = newHorizonsHeroes::perkDefinition(restoredBinary.rules, skill, revived);
		ASSERT_TRUE(binaryDefinition);
		EXPECT_EQ(binaryDefinition->name, "Spell Buffer");
		EXPECT_EQ(binaryDefinition->description, revivedDescription);
	}
}

TEST(NewHorizonsPerkState, RetiredSelectionWithoutMatchingHistoricalDefinitionIsRejected)
{
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto retired = "new-horizons:metamagic.spellBuffer";
	constexpr auto currentPerk = "new-horizons:metamagic.spellEcho";
	auto currentRules = state().rules;
	currentRules["rulesetVersion"].Integer() = 1;
	currentRules["sourceSha256"].String() = HISTORICAL_PERK_SOURCE_SHA256;
	for(auto & perk : currentRules["skills"][skill]["perks"].Vector())
		if(perk["id"].String() == retired)
			perk["id"].String() = currentPerk;

	JsonNode invalidJson;
	invalidJson["stateVersion"].Integer() = 1;
	invalidJson["rules"] = currentRules;
	invalidJson["selected"].Vector();
	JsonNode savedSelection;
	savedSelection["skillId"].String() = skill;
	savedSelection["perkId"].String() = retired;
	invalidJson["selected"].Vector().push_back(savedSelection);
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(invalidJson), std::runtime_error);

	std::vector<newHorizonsHeroes::PerkSelection> orphanSelection = {{skill, retired}};
	CMemorySerializer memory;
	memory.oser & currentRules;
	memory.oser & orphanSelection;
	newHorizonsHeroes::PerkState restoredBinary;
	EXPECT_THROW(memory.iser & restoredBinary, std::runtime_error);
	EXPECT_FALSE(restoredBinary.hasSelection(skill, currentPerk));
	EXPECT_TRUE(restoredBinary.hasSelection(skill, retired));
}

TEST(NewHorizonsPerkState, CrossoverRejectsUnknownFieldsAndForgedSelections)
{
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.rules, SKILL).front();
	original.select(SKILL, perk.id, 3);
	auto invalid = original.toJson();
	invalid["unexpected"].Bool() = true;
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(invalid), std::runtime_error);
	invalid = original.toJson();
	invalid["selected"].Vector()[0]["perkId"].String() = "new-horizons:offense.forged";
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(invalid), std::runtime_error);
}

TEST(NewHorizonsPerkState, LegacySameTierSelectionsKeepTheEarliestChoiceOnLoad)
{
	auto original = state();
	const auto options = newHorizonsHeroes::perkOptions(original.rules, SKILL);
	JsonNode legacy;
	legacy["stateVersion"].Integer() = 1;
	legacy["rules"] = original.rules;
	legacy["selected"].Vector();
	for(const auto index : {0, 1, 4, 5, 8})
	{
		JsonNode selection;
		selection["skillId"].String() = SKILL;
		selection["perkId"].String() = options[index].id;
		legacy["selected"].Vector().push_back(std::move(selection));
	}

	const auto restored = newHorizonsHeroes::PerkState::fromJson(legacy);
	ASSERT_EQ(restored.selected.size(), 3u);
	EXPECT_TRUE(restored.hasSelection(SKILL, options[0].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[4].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[8].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[1].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[5].id));
}

TEST(NewHorizonsPerkState, OfferIsDeterministicBoundedAndUsesOnlyLearnedEligibleSkills)
{
	auto saved = state();
	const auto ranks = [](const std::string & skillId)
	{
		if(skillId == SKILL)
			return 1;
		return 0;
	};
	const auto first = saved.prepareOffer(ranks, 42);
	const auto repeated = saved.prepareOffer(ranks, 42);
	ASSERT_EQ(first, repeated);
	ASSERT_EQ(first.size(), 2u);
	for(const auto & candidate : first)
	{
		EXPECT_EQ(candidate.selection.skillId, SKILL);
		EXPECT_LE(candidate.requiredRank, ranks(candidate.selection.skillId));
		EXPECT_FALSE(candidate.name.empty());
		EXPECT_FALSE(candidate.description.empty());
		const auto definition = newHorizonsHeroes::perkDefinition(saved.rules,
			candidate.selection.skillId, candidate.selection.perkId);
		ASSERT_TRUE(definition);
		EXPECT_EQ(definition->effect["status"].String(), "active");
	}
	for(uint64_t seed = 43; seed < 48; ++seed)
	{
		const auto varied = saved.prepareOffer(ranks, seed);
		ASSERT_EQ(varied.size(), 2u);
		for(const auto & candidate : varied)
			EXPECT_LE(candidate.requiredRank, ranks(candidate.selection.skillId));
	}
	EXPECT_TRUE(saved.prepareOffer([](const std::string &) { return 0; }, 42).empty());
}

TEST(NewHorizonsPerkState, HavocDamagePerksAreAcceptedBasicChoices)
{
	constexpr auto havoc = "new-horizons:havocMagic";
	for(const auto * perk : {"new-horizons:havocMagic.pyromancer", "new-horizons:havocMagic.cryomancer"})
	{
		SCOPED_TRACE(perk);
		auto saved = state();
		for(auto & candidate : saved.rules["skills"][havoc]["perks"].Vector())
			if(candidate["id"].String() == perk)
				candidate["effect"]["status"].String() = "active";
		const auto rank = [](const std::string & skillId)
		{
			return skillId == "new-horizons:havocMagic" ? 1 : 0;
		};
		bool accepted = false;
		for(uint32_t seed = 0; seed < 64 && !accepted; ++seed)
		{
			const auto offer = saved.prepareOffer(rank, seed);
			for(size_t index = 0; index < offer.size(); ++index)
				if(offer[index].selection.perkId == perk)
				{
					EXPECT_EQ(offer[index].requiredRank, 1);
					saved.acceptOffer(offer, index, rank, seed);
					accepted = true;
					break;
				}
		}
		ASSERT_TRUE(accepted);
		EXPECT_TRUE(saved.hasSelection(havoc, perk));
		EXPECT_TRUE(saved.canAdvanceSkillNormally(havoc, 1));
	}
}

TEST(NewHorizonsPerkState, ExpertHavocWithBasicStormcallerOffersOnlyAdvancedTier)
{
	auto saved = state();
	constexpr auto havoc = "new-horizons:havocMagic";
	constexpr auto stormcaller = "new-horizons:havocMagic.stormcaller";
	saved.select(havoc, stormcaller, 1);

	const auto offer = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == "new-horizons:havocMagic" ? 3 : 0;
	}, 0xC0D0C7u);
	ASSERT_EQ(offer.size(), 2u);
	for(const auto & candidate : offer)
	{
		EXPECT_EQ(candidate.selection.skillId, havoc);
		EXPECT_EQ(candidate.requiredRank, 2);
	}
}

TEST(NewHorizonsPerkState, OfferExcludesPlannedPerksAndCanBeEmpty)
{
	auto saved = plannedState();
	const auto plannedOnly = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == PLANNED_SKILL ? 3 : 0;
	}, 9);
	EXPECT_TRUE(plannedOnly.empty());

	const auto mixed = saved.prepareOffer([](const std::string & skillId)
	{
		if(skillId == PLANNED_SKILL)
			return 3;
		if(skillId == SKILL)
			return 1;
		return 0;
	}, 9);
	ASSERT_FALSE(mixed.empty());
	for(const auto & candidate : mixed)
		EXPECT_EQ(candidate.selection.skillId, SKILL);
}

TEST(NewHorizonsPerkState, AcceptedOfferRevalidatesSnapshotRankDuplicateAndCap)
{
	auto saved = state();
	const auto expert = [](const std::string & skillId)
	{
		return skillId == SKILL ? 3 : 0;
	};
	auto offer = saved.prepareOffer(expert, 9);
	ASSERT_EQ(offer.size(), 2u);
	auto forged = offer;
	forged[0].description += " forged";
	EXPECT_THROW(saved.acceptOffer(forged, 0, expert, 9), std::runtime_error);
	auto duplicate = offer;
	duplicate[1] = duplicate[0];
	EXPECT_THROW(saved.acceptOffer(duplicate, 0, expert, 9), std::runtime_error);
	auto oversized = offer;
	oversized.push_back(offer[0]);
	EXPECT_THROW(saved.acceptOffer(oversized, 0, expert, 9), std::runtime_error);
	EXPECT_THROW(saved.acceptOffer(offer, 2, expert, 9), std::runtime_error);
	EXPECT_THROW(saved.acceptOffer(offer, 0, [](const std::string &) { return 0; }, 9), std::runtime_error);
	uint64_t differentSeed = 10;
	while(differentSeed < 1000 && saved.prepareOffer(expert, differentSeed) == offer)
		++differentSeed;
	ASSERT_LT(differentSeed, 1000u);
	EXPECT_THROW(saved.acceptOffer(offer, 0, expert, differentSeed), std::runtime_error);

	saved.acceptOffer(offer, 0, expert, 9);
	EXPECT_EQ(saved.selected.size(), 1u);
	EXPECT_THROW(saved.acceptOffer(offer, 1, expert, 9), std::runtime_error);
	for(uint64_t seed = 10; saved.selected.size() < 3; ++seed)
	{
		auto next = saved.prepareOffer(expert, seed);
		ASSERT_FALSE(next.empty());
		saved.acceptOffer(next, 0, expert, seed);
	}
	EXPECT_TRUE(saved.prepareOffer(expert, 99).empty());
}

TEST(NewHorizonsPerkState, OfferNeverRepeatsAnOccupiedTierForTheSameSkill)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	saved.select(SKILL, options[0].id, 3);
	const auto offer = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == SKILL ? 3 : 0;
	}, 42);
	ASSERT_FALSE(offer.empty());
	for(const auto & candidate : offer)
	{
		EXPECT_EQ(candidate.selection.skillId, SKILL);
		EXPECT_NE(candidate.requiredRank, 1);
	}
}

TEST(NewHorizonsPerkState, LegacyRulesNeverInventAnOffer)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_TRUE(legacy.prepareOffer([](const std::string &) { return 3; }, 1).empty());
	EXPECT_THROW(legacy.acceptOffer({}, 0, [](const std::string &) { return 3; }, 1), std::runtime_error);
}

namespace
{
newHorizonsHeroes::PerkState weightedOfferState()
{
	auto result = state();
	result.rules["offerWeightPolicyVersion"].Integer() = 1;
	for(auto & [skill, definition] : result.rules["skills"].Struct())
		for(auto & perk : definition["perks"].Vector())
		{
			const auto & id = perk["id"].String();
			const bool active = id == "new-horizons:discipline.inspirationalLeader"
				|| id == "new-horizons:luck.opportunist" || id == "new-horizons:luck.secondChance";
			perk["effect"]["status"].String() = active ? "active" : "planned";
		}
	return result;
}

int weightedOfferRank(const std::string & skill)
{
	return skill == "new-horizons:luck" || skill == "new-horizons:discipline" ? 1 : 0;
}

std::optional<int> parentOfferWeight(const std::string & skill)
{
	return skill == "new-horizons:luck" ? 20 : 60;
}

newHorizonsHeroes::PerkState::OfferWeightContext capturedWeights(
	newHorizonsHeroes::PerkState::OfferWeightLookup lookup)
{
	return {newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable, std::move(lookup)};
}
}

TEST(NewHorizonsPerkState, ParentWeightIsPerPerkNotNormalizedByFamily)
{
	auto saved = weightedOfferState();
	const auto offer = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	ASSERT_EQ(offer.size(), 2u);
	// SplitMix64(seed4) gives first draw78 out of100: Discipline60,
	// Luck Opportunist20, Luck SecondChance20. Normalizing the two Luck
	// perks to10 each would instead select Discipline (draw58 out of80).
	EXPECT_EQ(offer.front().selection.perkId, "new-horizons:luck.opportunist");
	EXPECT_NE(offer[0].selection, offer[1].selection);
	EXPECT_EQ(offer, saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight)));
}

TEST(NewHorizonsPerkState, WeightedIntegerDrawHasStableGoldenSequence)
{
	auto saved = weightedOfferState();
	const auto offer = saved.prepareOffer(weightedOfferRank, 0, capturedWeights(parentOfferWeight));
	ASSERT_EQ(offer.size(), 2u);
	EXPECT_EQ(offer[0].selection.perkId, "new-horizons:discipline.inspirationalLeader");
	EXPECT_EQ(offer[1].selection.perkId, "new-horizons:luck.secondChance");
}

TEST(NewHorizonsPerkState, ZeroParentWeightExcludesAllItsPerks)
{
	auto saved = weightedOfferState();
	const auto weights = [](const std::string & skill) -> std::optional<int>
	{
		return skill == "new-horizons:luck" ? 0 : 60;
	};
	const auto offer = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(weights));
	ASSERT_EQ(offer.size(), 1u);
	EXPECT_EQ(offer[0].selection.skillId, "new-horizons:discipline");
	EXPECT_TRUE(saved.prepareOffer(weightedOfferRank, 4,
		capturedWeights([](const std::string &) -> std::optional<int> { return 0; })).empty());
}

TEST(NewHorizonsPerkState, ExplicitLegacyWeightContextRetainsExactUniformOrdering)
{
	auto saved = weightedOfferState();
	auto uniform = saved;
	uniform.rules.Struct().erase("offerWeightPolicyVersion");
	const newHorizonsHeroes::PerkState::OfferWeightContext legacyContext{
		newHorizonsHeroes::PerkState::OfferWeightMode::IntentionalLegacyNoCapturedTable, {}};
	EXPECT_EQ(saved.prepareOffer(weightedOfferRank, 42, legacyContext),
		uniform.prepareOffer(weightedOfferRank, 42));
	// Captured absence overrides newly installed callbacks, preserving queries.
	EXPECT_EQ(uniform.prepareOffer(weightedOfferRank, 42, capturedWeights(parentOfferWeight)),
		uniform.prepareOffer(weightedOfferRank, 42));
}

TEST(NewHorizonsPerkState, PartialMissingOrInvalidParentWeightsRefuse)
{
	auto saved = weightedOfferState();
	const auto partial = [](const std::string & skill) -> std::optional<int>
	{
		return skill == "new-horizons:luck" ? std::optional<int>(20) : std::nullopt;
	};
	EXPECT_THROW(saved.prepareOffer(weightedOfferRank, 1, capturedWeights(partial)), std::runtime_error);
	for(int invalid : {-1, 101})
	{
		EXPECT_THROW(saved.prepareOffer(weightedOfferRank, 1,
			capturedWeights([invalid](const std::string &) -> std::optional<int> { return invalid; })), std::runtime_error);
	}
}

TEST(NewHorizonsPerkState, WeightedAcceptanceRevalidatesWeightsRankAndTier)
{
	auto saved = weightedOfferState();
	const auto offer = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	ASSERT_EQ(offer.size(), 2u);
	EXPECT_THROW(saved.acceptOffer(offer, 0, weightedOfferRank, 4,
		capturedWeights([](const std::string & skill) -> std::optional<int>
		{
			return skill == "new-horizons:luck" ? 0 : 60;
		})), std::runtime_error);
	EXPECT_THROW(saved.acceptOffer(offer, 0, [](const std::string &) { return 0; },
		4, capturedWeights(parentOfferWeight)), std::runtime_error);
	auto forged = offer;
	forged[0].description += " forged";
	EXPECT_THROW(saved.acceptOffer(forged, 0, weightedOfferRank, 4, capturedWeights(parentOfferWeight)), std::runtime_error);
	saved.acceptOffer(offer, 0, weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	EXPECT_TRUE(saved.hasSelection(offer[0].selection.skillId, offer[0].selection.perkId));
	const auto next = saved.prepareOffer(weightedOfferRank, 5, capturedWeights(parentOfferWeight));
	for(const auto & candidate : next)
		EXPECT_NE(candidate.selection.skillId, offer[0].selection.skillId);
}

TEST(NewHorizonsPerkState, CapturedPolicyAndLegacyPendingOfferSurviveSaveRoundtrip)
{
	auto saved = weightedOfferState();
	const auto weighted = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	CMemorySerializer memory;
	memory.oser & saved;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight)), weighted);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).rules, saved.rules);
	auto legacy = saved;
	legacy.rules.Struct().erase("offerWeightPolicyVersion");
	const auto pending = legacy.prepareOffer(weightedOfferRank, 42);
	CMemorySerializer legacyMemory;
	legacyMemory.oser & legacy;
	newHorizonsHeroes::PerkState legacyRestored;
	legacyMemory.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.prepareOffer(weightedOfferRank, 42, capturedWeights(parentOfferWeight)), pending);
	legacyRestored.acceptOffer(pending, 0, weightedOfferRank, 42, capturedWeights(parentOfferWeight));
}
TEST(NewHorizonsPerkState, WeightedCapturedPolicyRefusesOlderBinaryWriter)
{
	auto saved = weightedOfferState();
	CMemorySerializer older;
	older.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	EXPECT_THROW(older.oser & saved, std::runtime_error);
}

TEST(NewHorizonsPerkState, WeightedCapturedPolicyRefusesOlderRawBinaryReader)
{
	auto saved = weightedOfferState();
	CMemorySerializer current;
	current.oser & saved;
	current.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	newHorizonsHeroes::PerkState restored;
	EXPECT_THROW(current.iser & restored, std::runtime_error);
}

TEST(NewHorizonsPerkState, AbsentWeightedPolicyRetainsOlderBinaryPendingOffer)
{
	auto legacy = weightedOfferState();
	legacy.rules.Struct().erase("offerWeightPolicyVersion");
	const auto pending = legacy.prepareOffer(weightedOfferRank, 42);
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	CMemorySerializer older;
	older.oser.version = oldVersion;
	older.iser.version = oldVersion;
	older.oser & legacy;
	newHorizonsHeroes::PerkState restored;
	older.iser & restored;
	EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 42, capturedWeights(parentOfferWeight)), pending);
	restored.acceptOffer(pending, 0, weightedOfferRank, 42, capturedWeights(parentOfferWeight));
}

TEST(NewHorizonsPerkState, AbsentWeightedPolicyBinarySaveDoesNotMutateCapturedRules)
{
	const auto priorEpoch = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	for(const bool older : {false, true})
	{
		SCOPED_TRACE(older);
		auto saved = weightedOfferState();
		saved.rules.Struct().erase("offerWeightPolicyVersion");
		const JsonNode originalRules = saved.rules;
		const auto pending = saved.prepareOffer(weightedOfferRank, 42);
		CMemorySerializer memory;
		if(older)
		{
			memory.oser.version = priorEpoch;
			memory.iser.version = priorEpoch;
		}
		ASSERT_NO_THROW(memory.oser & saved);
		EXPECT_EQ(saved.rules, originalRules);
		EXPECT_FALSE(saved.rules.Struct().contains("offerWeightPolicyVersion"));
		newHorizonsHeroes::PerkState restored;
		ASSERT_NO_THROW(memory.iser & restored);
		EXPECT_EQ(restored.rules, originalRules);
		EXPECT_FALSE(restored.rules.Struct().contains("offerWeightPolicyVersion"));
		EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 42), pending);
	}
}

TEST(NewHorizonsPerkState, NullWeightedPolicyRemainsInvalidBeforeBinaryWrite)
{
	auto saved = state();
	saved.rules["offerWeightPolicyVersion"] = JsonNode();
	ASSERT_TRUE(saved.rules.Struct().contains("offerWeightPolicyVersion"));
	const JsonNode originalRules = saved.rules;
	EXPECT_THROW(saved.validate(), std::runtime_error);
	CMemorySerializer memory;
	EXPECT_THROW(memory.oser & saved, std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
	EXPECT_EQ(saved.rules, originalRules);
}

TEST(NewHorizonsPerkState, ActiveWeightedPolicyRefusesMissingContextOrLookup)
{
	auto saved = weightedOfferState();
	EXPECT_THROW(saved.prepareOffer(weightedOfferRank, 42), std::runtime_error);
	const newHorizonsHeroes::PerkState::OfferWeightContext forgotten{
		newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable, {}};
	EXPECT_THROW(saved.prepareOffer(weightedOfferRank, 42, forgotten), std::runtime_error);
	const auto allMissing = [](const std::string &) -> std::optional<int> { return std::nullopt; };
	EXPECT_THROW(saved.prepareOffer(weightedOfferRank, 42, capturedWeights(allMissing)), std::runtime_error);
	const auto offer = saved.prepareOffer(weightedOfferRank, 42, capturedWeights(parentOfferWeight));
	EXPECT_THROW(saved.acceptOffer(offer, 0, weightedOfferRank, 42), std::runtime_error);
}

namespace
{
newHorizonsHeroes::PerkState r2State(double ratio = 2.0)
{
	auto result = weightedOfferState();
	result.rules["offerWeightPolicyVersion"].Integer() = 2;
	result.rules["perkOfferNormalization"]["maximumPreferenceRatio"].Float() = ratio;
	result.rules["perkOfferNormalization"]["mathVersion"].Integer() = 1;
	return result;
}

newHorizonsHeroes::PerkState::OfferWeightContext r2Weights(const newHorizonsHeroes::PerkState & saved,
	int luck = 1, int discipline = 10)
{
	std::map<std::string, int> row;
	for(const auto & [id, definition] : saved.rules["skills"].Struct())
		row[id] = id == "new-horizons:luck" ? luck : id == "new-horizons:discipline" ? discipline : 1;
	return {newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable,
		[row](const std::string & id) -> std::optional<int>
		{
			const auto found = row.find(id);
			return found == row.end() ? std::nullopt : std::optional<int>(found->second);
		}, row};
}
}

TEST(NewHorizonsPerkState, R2DrawsDistinctParentsInsteadOfWeightingEachPerk)
{
	const auto saved = r2State();
	const auto context = r2Weights(saved);
	for(uint64_t seed = 0; seed < 64; ++seed)
	{
		const auto offer = saved.prepareOffer(weightedOfferRank, seed, context);
		ASSERT_EQ(offer.size(), 2u);
		EXPECT_NE(offer[0].selection.skillId, offer[1].selection.skillId);
		EXPECT_EQ(offer, saved.prepareOffer(weightedOfferRank, seed, context));
	}
}

TEST(NewHorizonsPerkState, R2LearnedZeroParentHasOneQuantumAndSoleParentHasTwoUniformChoices)
{
	const auto saved = r2State();
	const auto context = r2Weights(saved, 0, 10);
	const auto both = saved.prepareOffer(weightedOfferRank, 4, context);
	ASSERT_EQ(both.size(), 2u);
	EXPECT_NE(both[0].selection.skillId, both[1].selection.skillId);
	const auto luckOnly = [](const std::string & id) { return id == "new-horizons:luck" ? 1 : 0; };
	for(uint64_t seed = 0; seed < 32; ++seed)
	{
		const auto offer = saved.prepareOffer(luckOnly, seed, context);
		ASSERT_EQ(offer.size(), 2u);
		EXPECT_EQ(offer[0].selection.skillId, "new-horizons:luck");
		EXPECT_EQ(offer[1].selection.skillId, "new-horizons:luck");
		EXPECT_NE(offer[0].selection, offer[1].selection);
	}
}

TEST(NewHorizonsPerkState, R2FullReferenceAndNumericalCacheDoNotDependOnEligiblePool)
{
	const auto saved = r2State();
	auto context = r2Weights(saved);
	ASSERT_FALSE(saved.prepareOffer(weightedOfferRank, 1, context).empty());
	const auto first = context.normalizedWeights;
	ASSERT_NE(first, nullptr);
	const auto luckOnly = [](const std::string & id) { return id == "new-horizons:luck" ? 1 : 0; };
	ASSERT_FALSE(saved.prepareOffer(luckOnly, 1, context).empty());
	EXPECT_EQ(context.normalizedWeights, first);
	context.referenceWeights.erase(context.referenceWeights.begin());
	EXPECT_THROW(saved.prepareOffer(luckOnly, 1, context), std::runtime_error);
}

TEST(NewHorizonsPerkState, R2AdditionalBasicChoicesSurviveJsonAndBinaryWithoutLegacyDeduplication)
{
	auto saved = r2State();
	saved.select("new-horizons:luck", "new-horizons:luck.opportunist", 1);
	const auto onlyLuck = [](const std::string & id) { return id == "new-horizons:luck" ? 1 : 0; };
	const auto offer = saved.prepareOffer(onlyLuck, 5, r2Weights(saved));
	ASSERT_EQ(offer.size(), 1u);
	saved.acceptOffer(offer, 0, onlyLuck, 5, r2Weights(saved));
	ASSERT_EQ(saved.selected.size(), 2u);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).selected, saved.selected);
	CMemorySerializer memory;
	memory.oser & saved;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.selected, saved.selected);
	EXPECT_TRUE(restored.canAdvanceSkillNormally("new-horizons:luck", 1));
	EXPECT_FALSE(restored.canAdvanceSkillNormally("new-horizons:luck", 2));
}

TEST(NewHorizonsPerkState, R2RequiresItsCodecButPolicyOneRetainsPreviousWriter)
{
	auto saved = r2State();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	EXPECT_THROW(old.oser & saved, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	auto prior = weightedOfferState();
	CMemorySerializer policyOne;
	policyOne.oser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	EXPECT_NO_THROW(policyOne.oser & prior);
}

TEST(NewHorizonsPerkState, R2FirstAndEitherSlotStatisticsRespectConfiguredParentRatio)
{
	for(double ratio : {1.0, 1.5, 2.0, 3.0})
	{
		SCOPED_TRACE(ratio);
		auto saved = r2State(ratio);
		const std::array<std::string, 3> ids = {"new-horizons:luck", "new-horizons:discipline", "new-horizons:offense"};
		bool enabled = false;
		for(auto & perk : saved.rules["skills"][ids[2]]["perks"].Vector())
			if(!enabled && perk["requires"].String() == "basic")
			{
				perk["effect"]["status"].String() = "active";
				enabled = true;
			}
		ASSERT_TRUE(enabled);
		auto context = r2Weights(saved);
		context.referenceWeights[ids[2]] = 5;
		const auto row = context.referenceWeights;
		context.lookup = [row](const std::string & id) -> std::optional<int> { return row.at(id); };
		const auto ranks = [ids](const std::string & id)
		{
			return std::find(ids.begin(), ids.end(), id) != ids.end() ? 1 : 0;
		};
		std::array<size_t, 3> first{};
		std::array<size_t, 3> either{};
		constexpr size_t samples = 4000;
		for(size_t seed = 0; seed < samples; ++seed)
		{
			const auto offer = saved.prepareOffer(ranks, seed, context);
			ASSERT_EQ(offer.size(), 2u);
			EXPECT_NE(offer[0].selection.skillId, offer[1].selection.skillId);
			for(size_t i = 0; i < ids.size(); ++i)
			{
				first[i] += offer[0].selection.skillId == ids[i];
				either[i] += offer[0].selection.skillId == ids[i] || offer[1].selection.skillId == ids[i];
			}
		}
		const std::array<double, 3> weights = {1.0, ratio, std::pow(5.0, std::log(ratio) / std::log(10.0))};
		const double total = weights[0] + weights[1] + weights[2];
		for(size_t i = 0; i < ids.size(); ++i)
		{
			SCOPED_TRACE(ids[i]);
			const double expectedFirst = weights[i] / total;
			double expectedEither = expectedFirst;
			for(size_t j = 0; j < ids.size(); ++j)
				if(j != i)
					expectedEither += weights[j] / total * weights[i] / (total - weights[j]);
			EXPECT_NEAR(static_cast<double>(first[i]) / samples, expectedFirst, 0.035);
			EXPECT_NEAR(static_cast<double>(either[i]) / samples, expectedEither, 0.035);
			const auto prefix = "R" + std::to_string(static_cast<int>(ratio * 10)) + "_parent" + std::to_string(i);
			RecordProperty(prefix + "_firstCount", std::to_string(first[i]));
			RecordProperty(prefix + "_eitherCount", std::to_string(either[i]));
			RecordProperty(prefix + "_samples", std::to_string(samples));
		}
	}
}

TEST(NewHorizonsPerkState, R2SupportsFiniteLargeRatiosWithoutUint64Overflow)
{
	const auto saved = r2State(std::numeric_limits<double>::max());
	const auto context = r2Weights(saved);
	const auto offer = saved.prepareOffer(weightedOfferRank, 42, context);
	ASSERT_EQ(offer.size(), 2u);
	EXPECT_EQ(offer.front().selection.skillId, "new-horizons:discipline");
	EXPECT_NE(offer[0].selection.skillId, offer[1].selection.skillId);
}

TEST(NewHorizonsPerkState, R2WizardFourParentsUseFullClassBoundsRegardlessOfPerkPoolCardinality)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	std::map<std::string, int> row;
	for(const auto & [id, value] : development["skillOfferWeights"]["core:wizard"].Struct())
		row[id] = static_cast<int>(value.Integer());
	ASSERT_EQ(row.size(), 31u);
	const std::array<std::string, 4> ids = {"new-horizons:sorceryMagic", "new-horizons:spellcraft",
		"new-horizons:havocMagic", "new-horizons:offense"};
	const std::array<int, 4> authoredWeights = {10, 9, 8, 1};
	for(size_t i = 0; i < ids.size(); ++i)
		ASSERT_EQ(row.at(ids[i]), authoredWeights[i]);
	const auto ranks = [ids](const std::string & id)
	{
		return std::find(ids.begin(), ids.end(), id) != ids.end() ? 1 : 0;
	};
	constexpr size_t samples = 4000;
	for(double ratio : {1.0, 1.5, 2.0, 3.0})
	{
		SCOPED_TRACE(ratio);
		std::array<std::array<size_t, 4>, 2> first{};
		std::array<std::array<size_t, 4>, 2> either{};
		for(size_t pool = 0; pool < 2; ++pool)
		{
			// Only pool cardinality is synthetic: the class row is the entire
			// current authored Wizard row, including unlearned/zero parents.
			auto saved = r2State(ratio);
			std::array<size_t, 4> active{};
			for(auto & [id, definition] : saved.rules["skills"].Struct())
			{
				const auto parent = std::find(ids.begin(), ids.end(), id);
				for(auto & perk : definition["perks"].Vector())
				{
					bool enabled = false;
					if(parent != ids.end() && perk["requires"].String() == "basic")
					{
						const auto index = static_cast<size_t>(std::distance(ids.begin(), parent));
						enabled = active[index] < (pool == 0 ? 1u : index + 1);
						active[index] += enabled;
					}
					perk["effect"]["status"].String() = enabled ? "active" : "planned";
				}
			}
			for(size_t i = 0; i < active.size(); ++i)
			{
				ASSERT_EQ(active[i], pool == 0 ? 1u : i + 1);
			}
			const newHorizonsHeroes::PerkState::OfferWeightContext context{
				newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable,
				[row](const std::string & id) -> std::optional<int> { return row.at(id); }, row};
			for(size_t seed = 0; seed < samples; ++seed)
			{
				const auto offer = saved.prepareOffer(ranks, seed, context);
				ASSERT_EQ(offer.size(), 2u);
				ASSERT_NE(offer[0].selection.skillId, offer[1].selection.skillId);
				for(size_t i = 0; i < ids.size(); ++i)
				{
					first[pool][i] += offer[0].selection.skillId == ids[i];
					either[pool][i] += offer[0].selection.skillId == ids[i] || offer[1].selection.skillId == ids[i];
				}
			}
		}
		std::array<double, 4> weights{};
		double total = 0;
		for(size_t i = 0; i < ids.size(); ++i)
		{
			weights[i] = std::pow(static_cast<double>(authoredWeights[i]), std::log(ratio) / std::log(10.0));
			total += weights[i];
		}
		for(size_t i = 0; i < ids.size(); ++i)
		{
			const double expectedFirst = weights[i] / total;
			double expectedEither = expectedFirst;
			for(size_t j = 0; j < ids.size(); ++j)
				if(j != i)
					expectedEither += weights[j] / total * weights[i] / (total - weights[j]);
			for(size_t pool = 0; pool < 2; ++pool)
			{
				EXPECT_NEAR(static_cast<double>(first[pool][i]) / samples, expectedFirst, 0.035);
				EXPECT_NEAR(static_cast<double>(either[pool][i]) / samples, expectedEither, 0.035);
				const auto prefix = "wizard_R" + std::to_string(static_cast<int>(ratio * 10))
					+ "_pool" + std::to_string(pool) + "_parent" + std::to_string(i);
				RecordProperty(prefix + "_firstCount", std::to_string(first[pool][i]));
				RecordProperty(prefix + "_eitherCount", std::to_string(either[pool][i]));
				RecordProperty(prefix + "_samples", std::to_string(samples));
				RecordProperty(prefix + "_expectedFirst", std::to_string(expectedFirst));
				RecordProperty(prefix + "_expectedEither", std::to_string(expectedEither));
			}
			EXPECT_EQ(first[0][i], first[1][i]);
			EXPECT_NEAR(static_cast<double>(either[0][i]) / samples, static_cast<double>(either[1][i]) / samples, 0.05);
		}
	}
}

TEST(NewHorizonsPerkState, R2QuantizedWeightsHaveExactEndpointsRescalingAndStableFullBounds)
{
	const auto saved = r2State();
	auto context = r2Weights(saved);
	context.referenceWeights["new-horizons:offense"] = 5;
	auto row = context.referenceWeights;
	context.lookup = [row](const std::string & id) -> std::optional<int> { return row.at(id); };
	const auto weights = saved.quantizedParentWeights(context);
	EXPECT_EQ(weights.at("new-horizons:luck"), "4294967296");
	EXPECT_EQ(weights.at("new-horizons:discipline"), "8589934592");
	EXPECT_EQ(weights.at("new-horizons:offense"), "6972215446");
	for(auto & [id, value] : context.referenceWeights)
		value *= 2;
	row = context.referenceWeights;
	context.lookup = [row](const std::string & id) -> std::optional<int> { return row.at(id); };
	EXPECT_EQ(saved.quantizedParentWeights(context), weights);
	const std::array<double, 4> ratios = {1.0, 1.5, 2.0, 3.0};
	const std::array<std::string, 4> intermediate = {"4294967296", "5702198997", "6972215446", "9256638568"};
	for(size_t i = 0; i < ratios.size(); ++i)
	{
		const auto variant = r2State(ratios[i]);
		EXPECT_EQ(variant.quantizedParentWeights(context).at("new-horizons:offense"), intermediate[i]);
	}
}

TEST(NewHorizonsPerkState, R2QuantizedWeightsAreMonotonicAcrossEveryPositiveClassWeight)
{
	using Integer = boost::multiprecision::cpp_int;
	for(double ratio : {1.0, 1.5, 2.0, 3.0})
	{
		SCOPED_TRACE(ratio);
		const auto saved = r2State(ratio);
		Integer previous = 0;
		for(int value = 1; value <= 10; ++value)
		{
			// Luck and Discipline pin full-reference bounds at1 and10 while
			// a third parent's weight traverses the entire positive interval.
			auto context = r2Weights(saved, 1, 10);
			context.referenceWeights["new-horizons:offense"] = value;
			const auto row = context.referenceWeights;
			context.lookup = [row](const std::string & id) -> std::optional<int> { return row.at(id); };
			const auto weights = saved.quantizedParentWeights(context);
			const Integer current(weights.at("new-horizons:offense"));
			EXPECT_GE(current, previous);
			if(value > 1 && ratio > 1)
			{
				EXPECT_GT(current, previous);
			}
			EXPECT_GE(current, Integer(weights.at("new-horizons:luck")));
			EXPECT_LE(current, Integer(weights.at("new-horizons:discipline")));
			previous = current;
		}
	}
}

TEST(NewHorizonsPerkState, R2UniformAllZeroAndLearnedZeroUseSameMinimumQuantum)
{
	const auto saved = r2State(3);
	auto context = r2Weights(saved);
	for(int value : {0, 7})
	{
		for(auto & [id, weight] : context.referenceWeights)
			weight = value;
		const auto row = context.referenceWeights;
		context.lookup = [row](const std::string & id) -> std::optional<int> { return row.at(id); };
		for(const auto & [id, weight] : saved.quantizedParentWeights(context))
			EXPECT_EQ(weight, "4294967296");
		EXPECT_EQ(saved.prepareOffer(weightedOfferRank, 123, context).size(), 2u);
	}
	const auto zero = r2Weights(saved, 0, 10);
	EXPECT_EQ(saved.quantizedParentWeights(zero).at("new-horizons:luck"), "4294967296");
}

TEST(NewHorizonsPerkState, R2QuantizationKeepsLargestFiniteDoubleEndpointExact)
{
	const auto saved = r2State(std::numeric_limits<double>::max());
	const auto actual = saved.quantizedParentWeights(r2Weights(saved));
	using Integer = boost::multiprecision::cpp_int;
	const Integer expected = ((Integer(1) << std::numeric_limits<double>::digits) - 1)
		<< (std::numeric_limits<double>::max_exponent - std::numeric_limits<double>::digits
			+ newHorizonsHeroes::PERK_OFFER_WEIGHT_FRACTION_BITS);
	EXPECT_EQ(actual.at("new-horizons:discipline"), expected.convert_to<std::string>());
}

TEST(NewHorizonsPerkState, R2StillEnforcesThreeTotalUniquePerksAndEarlierTierPrerequisites)
{
	auto saved = r2State();
	auto missingBasic = saved;
	bool testedAdvanced = false;
	for(auto & perk : missingBasic.rules["skills"]["new-horizons:luck"]["perks"].Vector())
		if(!testedAdvanced && perk["requires"].String() == "advanced")
		{
			perk["effect"]["status"].String() = "active";
			EXPECT_THROW(missingBasic.select("new-horizons:luck", perk["id"].String(), 3), std::runtime_error);
			testedAdvanced = true;
		}
	ASSERT_TRUE(testedAdvanced);
	std::vector<std::string> basic;
	for(auto & perk : saved.rules["skills"]["new-horizons:luck"]["perks"].Vector())
		if(perk["requires"].String() == "basic")
		{
			perk["effect"]["status"].String() = "active";
			basic.push_back(perk["id"].String());
		}
	ASSERT_EQ(basic.size(), 4u);
	for(size_t i = 0; i < 3; ++i)
		saved.select("new-horizons:luck", basic[i], 1);
	EXPECT_THROW(saved.select("new-horizons:luck", basic[3], 1), std::runtime_error);
	EXPECT_THROW(saved.select("new-horizons:luck", basic[0], 1), std::runtime_error);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).selected, saved.selected);
}
