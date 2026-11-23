/*
 * NewHorizonsPerkStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../PerkStateFixture.h"
#include "../server/LegacyEstatesCapture.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/nativeRules/CapturedRuleRef.h"

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
	auto rules = estatesTest::legacyEstatesPerks();
	// Historical pure-state tests exercise the captured legacy offer policy.
	// Weighted-policy fixtures explicitly opt back into version1 below.
	rules.Struct().erase("offerWeightPolicyVersion");
	rules.Struct().erase("perkOfferNormalization");
	rules.Struct().erase("perkProgressionPolicyVersion");
	rules.Struct().erase("unifiedSkillProgressionVersion");
	rules.Struct().erase("sylvanTacticsPolicy");
	rules.Struct().erase("necromancyPolicy");
	rules.Struct().erase("quickStudyPolicy");
	// Pure legacy offer states do not carry later faction or reroll captures.
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy", "connoisseurPolicy"})
		rules.Struct().erase(key);
	result.replaceRules(std::move(rules));
	return result;
}

const std::string SKILL = "new-horizons:sorceryMagic";
// This identity is used by an explicitly synthetic planned snapshot below;
// its current authored pool is active and must not be treated as planned.
const std::string PLANNED_SKILL = "new-horizons:armorer";

newHorizonsHeroes::PerkState plannedState()
{
	auto result = state();
	auto rules = result.getRules();
	for(auto & perk : rules["skills"][PLANNED_SKILL]["perks"].Vector())
		perk["effect"]["status"].String() = "planned";
	result.replaceRules(std::move(rules));
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

JsonNode legacyHeroProcJson()
{
	auto result = state();
	result.select("new-horizons:luck", "new-horizons:luck.secondChance", 1);
	result.select("new-horizons:luck", "new-horizons:luck.luckyRecovery", 2);
	result.select("new-horizons:luck", "new-horizons:luck.twistOfFate", 3);
	result.select("new-horizons:discipline", "new-horizons:discipline.steadfast", 1);
	result.select("new-horizons:discipline", "new-horizons:discipline.holdFast", 2);
	result.select("new-horizons:discipline", "new-horizons:discipline.heroicSpirit", 3);
	auto capturedRules = result.getRules();
	for(const auto & entry : LEGACY_HERO_PROC_TEXT)
	{
		auto & perk = savedPerk(capturedRules, entry.skill, entry.perk);
		perk["name"].String() = entry.name;
		perk["description"].String() = entry.description;
		perk["effect"]["description"].String() = entry.description;
	}
	// Author genuine legacy wire data, bypassing current capture admission.
	// replaceRules intentionally normalizes recognized text before publication.
	savedPerk(capturedRules, "new-horizons:luck", "new-horizons:luck.gambler")["effect"]["status"].String() = "planned";
	capturedRules["sourceSha256"].String() = UNKNOWN_SOURCE_SHA256;
	capturedRules["skills"][SKILL]["ranks"]["basic"]["description"].String() = "Saved unrelated coefficient: +42%.";
	auto json = result.toJson();
	json["rules"] = std::move(capturedRules);
	return json;
}

JsonNode expectedHeroProcText(const JsonNode & saved)
{
	auto result = saved;
	const auto canonical = state();
	for(const auto & entry : LEGACY_HERO_PROC_TEXT)
	{
		const auto current = newHorizonsHeroes::perkDefinition(canonical.getRules(), entry.skill, entry.perk);
		if(!current)
			throw std::runtime_error("Missing canonical fixture perk");
		auto & perk = savedPerk(result, entry.skill, entry.perk);
		perk["name"].String() = current->name;
		perk["description"].String() = current->description;
		perk["effect"]["description"].String() = current->effect["description"].String();
	}
	return result;
}
}

TEST(NewHorizonsPerkState, LegacyHeroProcTextMigratesFromCanonicalOnJsonLoadWithoutChangingSavedPolicy)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto legacy = legacyHeroProcJson();
	const auto expected = expectedHeroProcText(legacy["rules"]);
	const auto restored = newHorizonsHeroes::PerkState::fromJson(legacy);
	EXPECT_EQ(restored.getRules(), expected);
	EXPECT_EQ(restored.toJson()["selected"], legacy["selected"]);
	EXPECT_EQ(restored.getRules()["skills"]["new-horizons:sylvanLuck"], legacy["rules"]["skills"]["new-horizons:sylvanLuck"]);
	EXPECT_EQ(restored.getRules()["sourceSha256"], legacy["rules"]["sourceSha256"]);
}

TEST(NewHorizonsPerkState, LegacyHeroProcTextMigratesOnBinaryLoadAndIsIdempotent)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto legacy = legacyHeroProcJson();
	CMemorySerializer memory;
	const bool pooled = false;
	memory.oser & pooled;
	memory.oser & legacy["rules"];
	std::vector<newHorizonsHeroes::PerkSelection> selections;
	for(const auto & row : legacy["selected"].Vector())
		selections.push_back({row["skillId"].String(), row["perkId"].String()});
	memory.oser & selections;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_NE(legacy["rules"], expectedHeroProcText(legacy["rules"]));
	EXPECT_EQ(restored.getRules(), expectedHeroProcText(legacy["rules"]));
	EXPECT_EQ(restored.toJson()["selected"], legacy["selected"]);
	const auto jsonAgain = newHorizonsHeroes::PerkState::fromJson(restored.toJson());
	EXPECT_EQ(jsonAgain.getRules(), restored.getRules());
	EXPECT_EQ(jsonAgain.getSelected(), restored.getSelected());
	CMemorySerializer again;
	again.oser & restored;
	newHorizonsHeroes::PerkState binaryAgain;
	again.iser & binaryAgain;
	EXPECT_EQ(binaryAgain.getRules(), restored.getRules());
	EXPECT_EQ(binaryAgain.getSelected(), restored.getSelected());
}

TEST(NewHorizonsPerkState, AdmissionNormalizesLegacyPresentationWithoutLosingMetadataOrPolicy)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto legacy = legacyHeroProcJson();
	auto & row = savedPerk(legacy["rules"], "new-horizons:luck", "new-horizons:luck.luckyRecovery");
	for(const auto * key : {"name", "description"})
	{
		row[key].setModScope("legacy-presentation-scope", false);
		row[key].setOverrideFlag(true);
	}
	row["effect"]["description"].setModScope("legacy-effect-scope", false);
	row["effect"]["description"].setOverrideFlag(true);
	const auto originalWire = legacy;
	const auto expected = expectedHeroProcText(legacy["rules"]);
	ASSERT_NE(legacy["rules"], expected); // This fixture genuinely has obsolete text.
	newHorizonsHeroes::PerkState authored;
	authored.replaceRules(legacy["rules"]);
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(authored.getRules(), expected));
	EXPECT_EQ(authored.getRules()["sourceSha256"], legacy["rules"]["sourceSha256"]);
	EXPECT_EQ(authored.getRules()["skills"][SKILL], legacy["rules"]["skills"][SKILL]);
	const auto imported = newHorizonsHeroes::PerkState::fromJson(legacy);
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(imported.getRules(), expected));
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(legacy["rules"], originalWire["rules"]));
}

TEST(NewHorizonsPerkState, PooledLegacyPresentationAdmissionSharesOneOwnerAndIsIdempotent)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto legacy = legacyHeroProcJson();
	CMemorySerializer memory;
	int64_t count = 1;
	int64_t kind = static_cast<int64_t>(nativeRules::CapturedRuleKind::Perks);
	int64_t setCount = 0;
	memory.oser & count;
	memory.oser & kind;
	memory.oser & legacy["rules"];
	memory.oser & setCount;
	const bool pooled = true;
	const int64_t identity = 1;
	std::vector<newHorizonsHeroes::PerkSelection> selections;
	for(const auto & row : legacy["selected"].Vector())
		selections.push_back({row["skillId"].String(), row["perkId"].String()});
	for(int copy = 0; copy < 2; ++copy)
	{
		memory.oser & pooled;
		memory.oser & identity;
		memory.oser & selections;
	}
	nativeRules::CapturedRulePool decoded;
	memory.iser & decoded;
	newHorizonsHeroes::PerkState first, second;
	{
		nativeRules::CapturedRuleWireScope scope(memory.iser, decoded);
		memory.iser & first;
		memory.iser & second;
	}
	EXPECT_EQ(first.capturedOwner(), second.capturedOwner());
	EXPECT_EQ(first.capturedOwner(), decoded.document(1, nativeRules::CapturedRuleKind::Perks)->perks());
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(first.getRules(), expectedHeroProcText(legacy["rules"])));
	EXPECT_EQ(first.getSelected(), selections);
	const auto again = newHorizonsHeroes::PerkState::fromJson(first.toJson());
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(again.getRules(), first.getRules()));
	CMemorySerializer roundtrip;
	roundtrip.oser & decoded;
	nativeRules::CapturedRulePool restored;
	roundtrip.iser & restored;
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(restored.document(1, nativeRules::CapturedRuleKind::Perks)->document(),
		first.getRules()));
}

TEST(NewHorizonsPerkState, NormalizedDuplicateRawPoolRejectsWithoutReplacingItsOwner)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto legacy = legacyHeroProcJson();
	const auto normalized = expectedHeroProcText(legacy["rules"]);
	ASSERT_NE(legacy["rules"], normalized);
	CMemorySerializer memory;
	const int64_t count = 2;
	const int64_t kind = static_cast<int64_t>(nativeRules::CapturedRuleKind::Perks);
	memory.oser & count;
	memory.oser & kind;
	memory.oser & legacy["rules"];
	memory.oser & kind;
	memory.oser & normalized;
	nativeRules::CapturedRulePool destination;
	const auto id = destination.intern(nativeRules::CapturedRuleKind::Mastery, JsonNode());
	const auto original = destination.document(id, nativeRules::CapturedRuleKind::Mastery);
	EXPECT_THROW(memory.iser & destination, std::runtime_error);
	EXPECT_EQ(destination.documentCount(), 1u);
	EXPECT_EQ(destination.document(id, nativeRules::CapturedRuleKind::Mastery), original);
}

TEST(NewHorizonsPerkState, SharedLegacySelectionsNormalizeWhileStrictDuplicatesRejectAtomically)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(const bool strict : {false, true})
	{
		SCOPED_TRACE(strict);
		const auto rules = strict ? JsonNode(JsonPath::builtin("config/newHorizonsPerks")) : state().getRules();
		std::vector<newHorizonsHeroes::PerkSelection> selected;
		for(const auto & perk : rules["skills"][SKILL]["perks"].Vector())
			if(perk["requires"].String() == "basic" && selected.size() < 2)
				selected.push_back({SKILL, perk["id"].String()});
		ASSERT_EQ(selected.size(), 2u);
		CMemorySerializer memory;
		const bool pooled = false;
		memory.oser & pooled;
		memory.oser & rules;
		memory.oser & selected;
		auto destination = state();
		const auto before = destination.toJson();
		if(strict)
		{
			EXPECT_THROW(memory.iser & destination, std::runtime_error);
			EXPECT_EQ(destination.toJson(), before);
		}
		else
		{
			ASSERT_NO_THROW(memory.iser & destination);
			EXPECT_EQ(destination.getSelected(), (std::vector<newHorizonsHeroes::PerkSelection>{selected.front()}));
		}
	}
}

TEST(NewHorizonsPerkState, CurrentAndUnrecognizedHeroProcTextRemainExactlyCaptured)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(bool customLegacy : {false, true})
	{
		auto captured = state();
		if(customLegacy)
		{
			auto rules = captured.getRules();
			auto & perk = savedPerk(rules, "new-horizons:luck", "new-horizons:luck.gambler");
			perk["name"].String() = "Gambler";
			perk["description"].String() = "Unrecognized saved custom semantics.";
			perk["effect"]["description"].String() = "Unrecognized saved custom semantics.";
			captured.replaceRules(std::move(rules));
		}
		const auto json = newHorizonsHeroes::PerkState::fromJson(captured.toJson());
		EXPECT_EQ(json.getRules(), captured.getRules());
		EXPECT_EQ(json.getSelected(), captured.getSelected());
		CMemorySerializer memory;
		memory.oser & captured;
		newHorizonsHeroes::PerkState binary;
		memory.iser & binary;
		EXPECT_EQ(binary.getRules(), captured.getRules());
		EXPECT_EQ(binary.getSelected(), captured.getSelected());
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
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
	EXPECT_EQ(saved.getSelected().size(), 3u);
}

TEST(NewHorizonsPerkState, OrdinaryRanksAndPerksAlternateWhilePerkTiersStayOrdered)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
		EXPECT_EQ(saved.getSelected().size(), 2u);
	}
}

TEST(NewHorizonsPerkState, OccupiedTierOnlyBlocksAlternativesFromTheSameSkill)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	// Isolate occupied-tier admission from the growing authored Discipline pool.
	perkStateFixture::editRules(saved, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"]["new-horizons:discipline"]["perks"].Vector())
			if(perk["id"].String() != "new-horizons:discipline.inspirationalLeader")
				perk["effect"]["status"].String() = "planned";
	});
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = plannedState();
	const auto planned = newHorizonsHeroes::perkOptions(saved.getRules(), PLANNED_SKILL);
	ASSERT_GE(planned.size(), 2u);
	const auto & perk = planned.front();
	EXPECT_THROW(saved.select(PLANNED_SKILL, planned[1].id, 3), std::runtime_error);
	// Existing saves may contain a planned selection from an earlier registry;
	// keep loading it, but never allow a new one to be authored.
	perkStateFixture::editSelected(saved, [&](auto & selected) { selected.push_back({PLANNED_SKILL, perk.id}); });
	saved.validate();
	auto projected = saved.project([](const std::string &) { return 3; });
	ASSERT_EQ(projected.size(), 1u);
	EXPECT_FALSE(projected.front().enabled);
	EXPECT_EQ(projected.front().effect["status"].String(), "planned");
	perkStateFixture::editRules(saved, [&](JsonNode & rules) { rules["skills"][PLANNED_SKILL]["perks"].Vector()[0]["effect"]["status"].String() = "active"; });
	projected = saved.project([](const std::string &) { return 0; });
	EXPECT_FALSE(projected.front().enabled);
	projected = saved.project([](const std::string &) { return 1; });
	EXPECT_TRUE(projected.front().enabled);
}

TEST(NewHorizonsPerkState, OverchargerActivationComesFromTheSavedRegistrySnapshot)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto active = state();
	constexpr auto SKILL = "new-horizons:sorceryMagic";
	constexpr auto PERK = "new-horizons:sorceryMagic.overcharger";
	perkStateFixture::editRules(active, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"][SKILL]["perks"].Vector())
			if(perk["id"].String() == PERK)
				perk["effect"]["status"].String() = "active";
	});
	active.select(SKILL, PERK, 1);
	const auto activeProjection = active.project([](const std::string &) { return 1; });
	ASSERT_EQ(activeProjection.size(), 1u);
	EXPECT_TRUE(activeProjection.front().enabled);

	auto oldSave = active;
	perkStateFixture::editRules(oldSave, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"][SKILL]["perks"].Vector())
			if(perk["id"].String() == PERK)
				perk["effect"]["status"].String() = "planned";
	});
	const auto oldProjection = oldSave.project([](const std::string &) { return 1; });
	ASSERT_EQ(oldProjection.size(), 1u);
	EXPECT_FALSE(oldProjection.front().enabled);
}

TEST(NewHorizonsPerkState, JsonAndBinaryRoundTripsPreserveSavedRegistrySnapshot)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.getRules(), SKILL).front();
	original.select(SKILL, perk.id, 3);
	const auto legacyPlanned = newHorizonsHeroes::perkOptions(original.getRules(), PLANNED_SKILL).front();
	perkStateFixture::editSelected(original, [&](auto & selected) { selected.push_back({PLANNED_SKILL, legacyPlanned.id}); });
	original.validate();
	const auto savedJson = original.toJson();
	auto fromJson = newHorizonsHeroes::PerkState::fromJson(savedJson);
	ASSERT_EQ(fromJson.getSelected(), original.getSelected());
	EXPECT_EQ(fromJson.getRules(), original.getRules());

	CMemorySerializer memory;
	memory.oser & original;
	newHorizonsHeroes::PerkState fromBinary;
	memory.iser & fromBinary;
	EXPECT_EQ(fromBinary.getSelected(), original.getSelected());
	EXPECT_EQ(fromBinary.getRules(), original.getRules());

	const auto untouched = original.toJson();
	EXPECT_THROW(perkStateFixture::editRules(original, [&](JsonNode & rules) { rules["skills"].Struct().clear(); }), std::runtime_error);
	EXPECT_EQ(original.toJson(), untouched);
	EXPECT_TRUE(fromJson.hasSelection(SKILL, perk.id));
	EXPECT_TRUE(fromBinary.hasSelection(SKILL, perk.id));
	EXPECT_TRUE(fromJson.hasSelection(PLANNED_SKILL, legacyPlanned.id));
	EXPECT_TRUE(fromBinary.hasSelection(PLANNED_SKILL, legacyPlanned.id));
}

TEST(NewHorizonsPerkState, BinaryWriterMatchesCapturedFieldsForZeroOneAndMultipleSelections)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
	for(size_t count = 0; count <= 3; ++count)
	{
		SCOPED_TRACE(count);
		if(count != 0)
			saved.select(SKILL, options.at((count - 1) * 4).id, 3);
		ASSERT_NO_THROW(saved.validate());
		CMemorySerializer actual;
		CMemorySerializer fields;
		actual.oser.version = fields.oser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT;
		// Characterize the predecessor standalone wire layout, independently of PerkState's writer.
		ASSERT_NO_THROW(actual.oser & saved);
		fields.oser & saved.getRules();
		fields.oser & saved.getSelected();
		EXPECT_EQ(actual.oser.version, fields.oser.version);
		EXPECT_EQ(actual.extractBuffer(), fields.extractBuffer());
	}
}

TEST(NewHorizonsPerkState, SuccessfulSaveDoesNotHideLaterCatalogOrSelectionCorruption)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto perk = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL).front();
	saved.select(SKILL, perk.id, 3);
	CMemorySerializer successful;
	ASSERT_NO_THROW(successful.oser & saved);
	ASSERT_FALSE(successful.extractBuffer().empty());
	const auto before = saved.toJson();
	auto invalidRules = saved.getRules();
	invalidRules["skills"][PLANNED_SKILL]["ranks"]["basic"]["description"].String().clear();
	EXPECT_THROW(saved.replaceRules(invalidRules), std::runtime_error);
	auto invalidSelected = saved.getSelected();
	invalidSelected.front().perkId = "new-horizons:sorceryMagic.missing";
	EXPECT_THROW(saved.replaceSelected(invalidSelected), std::runtime_error);
	auto payload = before;
	payload["rules"] = invalidRules;
	payload["selected"].Vector().front()["perkId"] = JsonNode(invalidSelected.front().perkId);
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(payload), std::runtime_error);
	EXPECT_EQ(saved.toJson(), before);
}

TEST(NewHorizonsPerkState, HistoricalRetiredSpellBufferMigratesToSpellEchoAcrossJsonAndBinaryLoads)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto legacy = state();
	perkStateFixture::editRules(legacy, [&](JsonNode & rules)
	{
		rules["rulesetVersion"].Integer() = 1;
		rules["sourceSha256"].String() = HISTORICAL_PERK_SOURCE_SHA256;
	});
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto retired = "new-horizons:metamagic.spellBuffer";
	constexpr auto replacement = "new-horizons:metamagic.spellEcho";
	const JsonNode historical(JsonPath::builtin("test/newHorizonsHistoricalFactionPerks"));
	perkStateFixture::editRules(legacy, [&](JsonNode & rules) { rules["skills"][skill] = historical["skills"][skill]; });
	perkStateFixture::editRules(legacy, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"][skill]["perks"].Vector())
		{
			if(perk["id"].String() != replacement && perk["id"].String() != retired)
				continue;
			perk["id"].String() = retired;
			perk["name"].String() = "Spell Buffer";
			perk["description"].String() = "Retired countered-spell rule.";
			perk["effect"]["description"].String() = "Retired countered-spell rule.";
		}
	});
	perkStateFixture::editSelected(legacy, [&](auto & selected) { selected.push_back({skill, retired}); });
	legacy.validate();

	const auto restoredJson = newHorizonsHeroes::PerkState::fromJson(legacy.toJson());
	EXPECT_TRUE(restoredJson.hasSelection(skill, replacement));
	EXPECT_FALSE(restoredJson.hasSelection(skill, retired));
	const auto jsonDefinition = newHorizonsHeroes::perkDefinition(
		restoredJson.getRules(), skill, replacement);
	ASSERT_TRUE(jsonDefinition);
	EXPECT_EQ(jsonDefinition->name, "Spell Echo");
	EXPECT_NE(jsonDefinition->description.find("repeats the first Spell"), std::string::npos);

	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT;
	memory.oser & legacy;
	newHorizonsHeroes::PerkState restoredBinary;
	memory.iser & restoredBinary;
	EXPECT_TRUE(restoredBinary.hasSelection(skill, replacement));
	EXPECT_FALSE(restoredBinary.hasSelection(skill, retired));
	const auto binaryDefinition = newHorizonsHeroes::perkDefinition(
		restoredBinary.getRules(), skill, replacement);
	ASSERT_TRUE(binaryDefinition);
	EXPECT_EQ(binaryDefinition->name, "Spell Echo");

	// A v1 save that has already migrated must retain Echo on subsequent loads.
	auto migrated = restoredBinary;
	const auto reloadedJson = newHorizonsHeroes::PerkState::fromJson(migrated.toJson());
	EXPECT_EQ(reloadedJson.getRules(), migrated.getRules());
	EXPECT_EQ(reloadedJson.getSelected(), migrated.getSelected());
	CMemorySerializer migratedMemory;
	migratedMemory.oser & migrated;
	newHorizonsHeroes::PerkState reloadedBinary;
	migratedMemory.iser & reloadedBinary;
	EXPECT_EQ(reloadedBinary.getRules(), migrated.getRules());
	EXPECT_EQ(reloadedBinary.getSelected(), migrated.getSelected());
}

TEST(NewHorizonsPerkState, RevivedSpellBufferSurvivesJsonAndBinaryLoadsForNewOrUnknownSnapshots)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto revived = "new-horizons:metamagic.spellBuffer";
	constexpr auto replacement = "new-horizons:metamagic.spellEcho";
	constexpr auto revivedDescription = "An unused Metamagic Spell Action grants 6 buffer Mana when it expires.";

	for(const auto & [version, sourceSha256] : std::vector<std::pair<int, const char *>>{
		{1, CURRENT_DESIGN_SOURCE_SHA256}, {1, UNKNOWN_SOURCE_SHA256},
		{2, HISTORICAL_PERK_SOURCE_SHA256}, {2, CURRENT_DESIGN_SOURCE_SHA256}, {2, UNKNOWN_SOURCE_SHA256}})
	{
		auto current = state();
		perkStateFixture::editRules(current, [&](JsonNode & rules)
		{
			rules["rulesetVersion"].Integer() = version;
			rules["sourceSha256"].String() = sourceSha256;
		});
		const JsonNode historical(JsonPath::builtin("test/newHorizonsHistoricalFactionPerks"));
		perkStateFixture::editRules(current, [&](JsonNode & rules) { rules["skills"][skill] = historical["skills"][skill]; });
		perkStateFixture::editRules(current, [&](JsonNode & rules)
		{
			for(auto & perk : rules["skills"][skill]["perks"].Vector())
			{
				if(perk["id"].String() != replacement && perk["id"].String() != revived)
					continue;
				perk["id"].String() = revived;
				perk["name"].String() = "Spell Buffer";
				perk["description"].String() = revivedDescription;
				perk["effect"]["description"].String() = revivedDescription;
			}
		});
		perkStateFixture::editSelected(current, [&](auto & selected) { selected.push_back({skill, revived}); });
		current.validate();

		const auto restoredJson = newHorizonsHeroes::PerkState::fromJson(current.toJson());
		EXPECT_TRUE(restoredJson.hasSelection(skill, revived));
		EXPECT_FALSE(restoredJson.hasSelection(skill, replacement));
		EXPECT_EQ(restoredJson.getRules(), current.getRules());
		EXPECT_EQ(restoredJson.getSelected(), current.getSelected());
		const auto jsonDefinition = newHorizonsHeroes::perkDefinition(restoredJson.getRules(), skill, revived);
		ASSERT_TRUE(jsonDefinition);
		EXPECT_EQ(jsonDefinition->name, "Spell Buffer");
		EXPECT_EQ(jsonDefinition->description, revivedDescription);

		CMemorySerializer memory;
		memory.oser & current;
		newHorizonsHeroes::PerkState restoredBinary;
		memory.iser & restoredBinary;
		EXPECT_TRUE(restoredBinary.hasSelection(skill, revived));
		EXPECT_FALSE(restoredBinary.hasSelection(skill, replacement));
		EXPECT_EQ(restoredBinary.getRules(), current.getRules());
		EXPECT_EQ(restoredBinary.getSelected(), current.getSelected());
		const auto binaryDefinition = newHorizonsHeroes::perkDefinition(restoredBinary.getRules(), skill, revived);
		ASSERT_TRUE(binaryDefinition);
		EXPECT_EQ(binaryDefinition->name, "Spell Buffer");
		EXPECT_EQ(binaryDefinition->description, revivedDescription);
	}
}

TEST(NewHorizonsPerkState, RetiredSelectionWithoutMatchingHistoricalDefinitionIsRejected)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	constexpr auto skill = "new-horizons:metamagic";
	constexpr auto retired = "new-horizons:metamagic.spellBuffer";
	constexpr auto currentPerk = "new-horizons:metamagic.spellEcho";
	auto currentRules = state().getRules();
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
	memory.oser.version = memory.iser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT;
	memory.oser & currentRules;
	memory.oser & orphanSelection;
	newHorizonsHeroes::PerkState restoredBinary;
	EXPECT_THROW(memory.iser & restoredBinary, std::runtime_error);
	EXPECT_FALSE(restoredBinary.hasSelection(skill, currentPerk));
	EXPECT_FALSE(restoredBinary.hasSelection(skill, retired));
	EXPECT_TRUE(restoredBinary.getRules().isNull());
	EXPECT_TRUE(restoredBinary.getSelected().empty());
}

TEST(NewHorizonsPerkState, CrossoverRejectsUnknownFieldsAndForgedSelections)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.getRules(), SKILL).front();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto original = state();
	const auto options = newHorizonsHeroes::perkOptions(original.getRules(), SKILL);
	JsonNode legacy;
	legacy["stateVersion"].Integer() = 1;
	legacy["rules"] = original.getRules();
	legacy["selected"].Vector();
	for(const auto index : {0, 1, 4, 5, 8})
	{
		JsonNode selection;
		selection["skillId"].String() = SKILL;
		selection["perkId"].String() = options[index].id;
		legacy["selected"].Vector().push_back(std::move(selection));
	}

	const auto restored = newHorizonsHeroes::PerkState::fromJson(legacy);
	ASSERT_EQ(restored.getSelected().size(), 3u);
	EXPECT_TRUE(restored.hasSelection(SKILL, options[0].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[4].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[8].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[1].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[5].id));
}

TEST(NewHorizonsPerkState, OfferIsDeterministicBoundedAndUsesOnlyLearnedEligibleSkills)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
		const auto definition = newHorizonsHeroes::perkDefinition(saved.getRules(),
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	constexpr auto havoc = "new-horizons:havocMagic";
	for(const auto * perk : {"new-horizons:havocMagic.pyromancer", "new-horizons:havocMagic.cryomancer"})
	{
		SCOPED_TRACE(perk);
		auto saved = state();
		perkStateFixture::editRules(saved, [&](JsonNode & rules)
		{
			for(auto & candidate : rules["skills"][havoc]["perks"].Vector())
				if(candidate["id"].String() == perk)
					candidate["effect"]["status"].String() = "active";
		});
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	EXPECT_EQ(saved.getSelected().size(), 1u);
	EXPECT_THROW(saved.acceptOffer(offer, 1, expert, 9), std::runtime_error);
	for(uint64_t seed = 10; saved.getSelected().size() < 3; ++seed)
	{
		auto next = saved.prepareOffer(expert, seed);
		ASSERT_FALSE(next.empty());
		saved.acceptOffer(next, 0, expert, seed);
	}
	EXPECT_TRUE(saved.prepareOffer(expert, 99).empty());
}

TEST(NewHorizonsPerkState, OfferNeverRepeatsAnOccupiedTierForTheSameSkill)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
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
	auto rules = result.getRules();
	rules["offerWeightPolicyVersion"].Integer() = 1;
	for(auto & [skill, definition] : rules["skills"].Struct())
		for(auto & perk : definition["perks"].Vector())
		{
			const auto & id = perk["id"].String();
			const bool active = id == "new-horizons:discipline.inspirationalLeader"
				|| id == "new-horizons:luck.opportunist" || id == "new-horizons:luck.secondChance";
			perk["effect"]["status"].String() = active ? "active" : "planned";
		}
	result.replaceRules(std::move(rules));
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	const auto offer = saved.prepareOffer(weightedOfferRank, 0, capturedWeights(parentOfferWeight));
	ASSERT_EQ(offer.size(), 2u);
	EXPECT_EQ(offer[0].selection.perkId, "new-horizons:discipline.inspirationalLeader");
	EXPECT_EQ(offer[1].selection.perkId, "new-horizons:luck.secondChance");
}

TEST(NewHorizonsPerkState, ZeroParentWeightExcludesAllItsPerks)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	auto uniform = saved;
	perkStateFixture::editRules(uniform, [&](JsonNode & rules) { rules.Struct().erase("offerWeightPolicyVersion"); });
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	// The weighted sample must not also be a complete eligible tier pool.
	perkStateFixture::editRules(saved, [&](JsonNode & rules) { savedPerk(rules, "new-horizons:luck", "new-horizons:luck.luckyAim")["effect"]["status"].String() = "active"; });
	ASSERT_GE(saved.candidatesForSkillTier("new-horizons:luck", 1).size(), 3u);
	const auto offer = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	ASSERT_EQ(offer.size(), 2u);
	const auto tierPool = saved.candidatesForSkillTier(offer.front().selection.skillId, offer.front().requiredRank);
	ASSERT_NE(offer, tierPool);
	ASSERT_EQ(tierPool.size(), 1u); // This seed selects the sole Discipline candidate first.
	const auto before = saved.toJson();
	const auto offeredParent = offer.front().selection.skillId;
	EXPECT_THROW(saved.acceptOffer(offer, 0, weightedOfferRank, 4,
		capturedWeights([offeredParent](const std::string & skill) -> std::optional<int>
		{
			return skill == offeredParent ? std::optional<int>(0) : parentOfferWeight(skill);
		})), std::runtime_error);
	EXPECT_EQ(saved.toJson(), before);
	EXPECT_THROW(saved.acceptOffer(offer, 0, [](const std::string &) { return 0; },
		4, capturedWeights(parentOfferWeight)), std::runtime_error);
	EXPECT_EQ(saved.toJson(), before);
	auto forged = offer;
	forged[0].description += " forged";
	EXPECT_THROW(saved.acceptOffer(forged, 0, weightedOfferRank, 4, capturedWeights(parentOfferWeight)), std::runtime_error);
	EXPECT_EQ(saved.toJson(), before);
	saved.acceptOffer(offer, 0, weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	EXPECT_TRUE(saved.hasSelection(offer[0].selection.skillId, offer[0].selection.perkId));
	const auto next = saved.prepareOffer(weightedOfferRank, 5, capturedWeights(parentOfferWeight));
	for(const auto & candidate : next)
		EXPECT_NE(candidate.selection.skillId, offer[0].selection.skillId);
}

TEST(NewHorizonsPerkState, CapturedPolicyAndLegacyPendingOfferSurviveSaveRoundtrip)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	const auto weighted = saved.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight));
	CMemorySerializer memory;
	memory.oser & saved;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 4, capturedWeights(parentOfferWeight)), weighted);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).getRules(), saved.getRules());
	auto legacy = saved;
	perkStateFixture::editRules(legacy, [&](JsonNode & rules) { rules.Struct().erase("offerWeightPolicyVersion"); });
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	CMemorySerializer older;
	older.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	EXPECT_THROW(older.oser & saved, std::runtime_error);
}

TEST(NewHorizonsPerkState, WeightedCapturedPolicyRefusesOlderRawBinaryReader)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = weightedOfferState();
	CMemorySerializer current;
	current.oser & saved;
	current.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	newHorizonsHeroes::PerkState restored;
	EXPECT_THROW(current.iser & restored, std::runtime_error);
}

TEST(NewHorizonsPerkState, AbsentWeightedPolicyRefusesPriorCodecAndCurrentCodecRetainsPendingOffer)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto legacy = weightedOfferState();
	perkStateFixture::editRules(legacy, [&](JsonNode & rules) { rules.Struct().erase("offerWeightPolicyVersion"); });
	const auto pending = legacy.prepareOffer(weightedOfferRank, 42);
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	CMemorySerializer older;
	older.oser.version = oldVersion;
	older.iser.version = oldVersion;
	const auto before = legacy.toJson();
	EXPECT_THROW(older.oser & legacy, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
	EXPECT_EQ(legacy.toJson(), before);
	older.oser.version = older.iser.version = ESerializationVersion::CURRENT;
	older.oser & legacy;
	newHorizonsHeroes::PerkState restored;
	older.iser & restored;
	EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 42, capturedWeights(parentOfferWeight)), pending);
	restored.acceptOffer(pending, 0, weightedOfferRank, 42, capturedWeights(parentOfferWeight));
}

TEST(NewHorizonsPerkState, AbsentWeightedPolicyBinarySaveDoesNotMutateCapturedRules)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto priorEpoch = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES) - 1);
	for(const bool older : {false, true})
	{
		SCOPED_TRACE(older);
		auto saved = weightedOfferState();
		perkStateFixture::editRules(saved, [&](JsonNode & rules) { rules.Struct().erase("offerWeightPolicyVersion"); });
		const JsonNode originalRules = saved.getRules();
		const auto pending = saved.prepareOffer(weightedOfferRank, 42);
		CMemorySerializer memory;
		if(older)
		{
			memory.oser.version = priorEpoch;
			memory.iser.version = priorEpoch;
			EXPECT_THROW(memory.oser & saved, std::runtime_error);
			EXPECT_TRUE(memory.extractBuffer().empty());
			EXPECT_EQ(saved.getRules(), originalRules);
			memory.oser.version = memory.iser.version = ESerializationVersion::CURRENT;
		}
		ASSERT_NO_THROW(memory.oser & saved);
		EXPECT_EQ(saved.getRules(), originalRules);
		EXPECT_FALSE(saved.getRules().Struct().contains("offerWeightPolicyVersion"));
		newHorizonsHeroes::PerkState restored;
		ASSERT_NO_THROW(memory.iser & restored);
		EXPECT_EQ(restored.getRules(), originalRules);
		EXPECT_FALSE(restored.getRules().Struct().contains("offerWeightPolicyVersion"));
		EXPECT_EQ(restored.prepareOffer(weightedOfferRank, 42), pending);
	}
}

TEST(NewHorizonsPerkState, NullWeightedPolicyCannotReplaceAnAdmittedCapture)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = state();
	const auto before = saved.toJson();
	auto malformed = saved.getRules();
	malformed["offerWeightPolicyVersion"] = JsonNode();
	ASSERT_TRUE(malformed.Struct().contains("offerWeightPolicyVersion"));
	EXPECT_THROW(saved.replaceRules(malformed), std::runtime_error);
	auto payload = before;
	payload["rules"] = malformed;
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(payload), std::runtime_error);
	EXPECT_EQ(saved.toJson(), before);
}

TEST(NewHorizonsPerkState, ActiveWeightedPolicyRefusesMissingContextOrLookup)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	auto rules = result.getRules();
	// Explicit historical R2 capture: keep normalized weights and repeated tiers.
	rules.Struct().erase("perkProgressionPolicyVersion");
	rules["offerWeightPolicyVersion"].Integer() = 2;
	rules["perkOfferNormalization"]["maximumPreferenceRatio"].Float() = ratio;
	rules["perkOfferNormalization"]["mathVersion"].Integer() = 1;
	result.replaceRules(std::move(rules));
	return result;
}

newHorizonsHeroes::PerkState::OfferWeightContext r2Weights(const newHorizonsHeroes::PerkState & saved,
	int luck = 1, int discipline = 10)
{
	std::map<std::string, int> row;
	for(const auto & [id, definition] : saved.getRules()["skills"].Struct())
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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

TEST(NewHorizonsPerkState, HistoricalStrictProgressionUsesSixAlternatingRankAndPerkSteps)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	newHorizonsHeroes::PerkState saved;
	saved.replaceRules(estatesTest::legacyEstatesPerks());
	// Capture the ordered-tier policy before unified four-card offers.
	perkStateFixture::editRules(saved, [&](JsonNode & rules) { rules.Struct().erase("unifiedSkillProgressionVersion"); });
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(saved.getRules()));
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
	int rank = 0;
	const auto ranks = [&](const std::string & id) { return id == SKILL ? rank : 0; };
	const auto context = r2Weights(saved);
	for(int tier = 1; tier <= 3; ++tier)
	{
		SCOPED_TRACE(tier);
		ASSERT_TRUE(saved.canAdvanceSkillNormally(SKILL, rank));
		++rank; // The rank half of the normal six-step progression.
		EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, rank));
		const auto offer = saved.prepareOffer(ranks, tier, context);
		ASSERT_EQ(offer.size(), 1u); // Sole eligible parent never duplicates a card.
		EXPECT_EQ(offer.front().requiredRank, tier);
		ASSERT_TRUE(saved.canAcquirePerk(SKILL, offer.front().selection.perkId, rank));
		saved.acceptOffer(offer, 0, ranks, tier, context);
		ASSERT_EQ(saved.getSelected().size(), static_cast<size_t>(tier));
		for(const auto & option : options)
		{
			if(newHorizonsHeroes::perkRequiredRank(option.requiredRank) != tier)
				continue;
			EXPECT_FALSE(saved.canAcquirePerk(SKILL, option.id, rank));
			EXPECT_THROW(saved.select(SKILL, option.id, rank), std::runtime_error);
		}
	}
	EXPECT_FALSE(saved.canAdvanceSkillNormally(SKILL, 3));
	EXPECT_TRUE(saved.prepareOffer(ranks, 9, context).empty());
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).getSelected(), saved.getSelected());
}

TEST(NewHorizonsPerkState, HistoricalStrictExceptionalExpertStillRequiresBasicThenAdvancedThenExpert)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	newHorizonsHeroes::PerkState saved;
	saved.replaceRules(estatesTest::legacyEstatesPerks());
	// Capture the ordered-tier policy before unified four-card offers.
	perkStateFixture::editRules(saved, [&](JsonNode & rules) { rules.Struct().erase("unifiedSkillProgressionVersion"); });
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(saved.getRules()));
	const auto expert = [](const std::string & id) { return id == SKILL ? 3 : 0; };
	const auto context = r2Weights(saved);
	const auto options = newHorizonsHeroes::perkOptions(saved.getRules(), SKILL);
	EXPECT_THROW(saved.select(SKILL, options.at(4).id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options.at(8).id, 3), std::runtime_error);
	for(int tier = 1; tier <= 3; ++tier)
	{
		const auto offer = saved.prepareOffer(expert, tier + 10, context);
		ASSERT_EQ(offer.size(), 1u);
		EXPECT_EQ(offer.front().requiredRank, tier);
		saved.acceptOffer(offer, 0, expert, tier + 10, context);
	}
	EXPECT_EQ(saved.getSelected().size(), 3u);
	EXPECT_TRUE(saved.prepareOffer(expert, 20, context).empty());
}

TEST(NewHorizonsPerkState, StrictCaptureRejectsHistoricalDuplicateOrGappedPrefixesWithoutNormalization)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto historical = r2State();
	historical.select("new-horizons:luck", "new-horizons:luck.opportunist", 1);
	historical.select("new-horizons:luck", "new-horizons:luck.secondChance", 1);
	const auto oldJson = historical.toJson();
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(oldJson).getSelected(), historical.getSelected());
	auto forged = oldJson;
	forged["rules"]["perkProgressionPolicyVersion"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(forged), std::runtime_error);
	EXPECT_EQ(historical.toJson(), oldJson);

	newHorizonsHeroes::PerkState strict;
	strict.replaceRules(estatesTest::legacyEstatesPerks());
	const auto options = newHorizonsHeroes::perkOptions(strict.getRules(), SKILL);
	const auto before = strict.toJson();
	EXPECT_THROW(strict.replaceSelected({{SKILL, options.at(4).id}}), std::runtime_error); // Advanced without Basic.
	EXPECT_THROW(strict.replaceSelected({{SKILL, options.at(0).id}, {SKILL, options.at(8).id}}), std::runtime_error);
	EXPECT_EQ(strict.toJson(), before); // Expert without Advanced also refuses atomically.
}

TEST(NewHorizonsPerkState, HistoricalStrictEligibilityDoesNotRebaseFullClassRelativeParentWeights)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	newHorizonsHeroes::PerkState saved;
	saved.replaceRules(estatesTest::legacyEstatesPerks());
	// Capture the ordered-tier policy before unified four-card offers.
	perkStateFixture::editRules(saved, [&](JsonNode & rules) { rules.Struct().erase("unifiedSkillProgressionVersion"); });
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(saved.getRules()));
	const auto context = r2Weights(saved, 1, 10);
	const auto before = saved.quantizedParentWeights(context);
	const auto both = saved.prepareOffer(weightedOfferRank, 17, context);
	ASSERT_EQ(both.size(), 2u);
	EXPECT_NE(both.front().selection.skillId, both.back().selection.skillId);
	saved.select("new-horizons:luck", "new-horizons:luck.opportunist", 1);
	const auto remaining = saved.prepareOffer(weightedOfferRank, 17, context);
	ASSERT_EQ(remaining.size(), 1u);
	EXPECT_EQ(remaining.front().selection.skillId, "new-horizons:discipline");
	EXPECT_EQ(saved.quantizedParentWeights(context), before);
	const auto luckAdvanced = [](const std::string & id)
	{
		return id == "new-horizons:luck" ? 2 : id == "new-horizons:discipline" ? 1 : 0;
	};
	const auto reopened = saved.prepareOffer(luckAdvanced, 17, context);
	ASSERT_EQ(reopened.size(), 2u);
	for(const auto & candidate : reopened)
		EXPECT_EQ(candidate.requiredRank, candidate.selection.skillId == "new-horizons:luck" ? 2 : 1);
	EXPECT_EQ(saved.quantizedParentWeights(context), before);
}

TEST(NewHorizonsPerkState, StrictAndHistoricalR2Refuse1117BeforePrefixWhileCurrentCodecRoundTrips)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	newHorizonsHeroes::PerkState strict;
	strict.replaceRules(estatesTest::legacyEstatesPerks());
	CMemorySerializer refused;
	refused.oser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	EXPECT_THROW(refused.oser & strict, std::runtime_error);
	EXPECT_TRUE(refused.extractBuffer().empty());
	auto historical = r2State();
	historical.select("new-horizons:luck", "new-horizons:luck.opportunist", 1);
	historical.select("new-horizons:luck", "new-horizons:luck.secondChance", 1);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	const auto before = historical.toJson();
	EXPECT_THROW(wire.oser & historical, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
	EXPECT_EQ(historical.toJson(), before);
	wire.oser.version = wire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(wire.oser & historical);
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_EQ(restored.getRules(), historical.getRules());
	EXPECT_EQ(restored.getSelected(), historical.getSelected());
}

TEST(NewHorizonsPerkState, R2LearnedZeroParentHasOneQuantumAndSoleParentHasTwoUniformChoices)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = r2State();
	saved.select("new-horizons:luck", "new-horizons:luck.opportunist", 1);
	const auto onlyLuck = [](const std::string & id) { return id == "new-horizons:luck" ? 1 : 0; };
	const auto offer = saved.prepareOffer(onlyLuck, 5, r2Weights(saved));
	ASSERT_EQ(offer.size(), 1u);
	saved.acceptOffer(offer, 0, onlyLuck, 5, r2Weights(saved));
	ASSERT_EQ(saved.getSelected().size(), 2u);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).getSelected(), saved.getSelected());
	CMemorySerializer memory;
	memory.oser & saved;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.getSelected(), saved.getSelected());
	EXPECT_TRUE(restored.canAdvanceSkillNormally("new-horizons:luck", 1));
	EXPECT_FALSE(restored.canAdvanceSkillNormally("new-horizons:luck", 2));
}

TEST(NewHorizonsPerkState, R2AndPolicyOneRefusePriorWritersWhileCurrentCodecRetainsPolicyOne)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = r2State();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	EXPECT_THROW(old.oser & saved, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	auto prior = weightedOfferState();
	CMemorySerializer policyOne;
	policyOne.oser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	const auto before = prior.toJson();
	EXPECT_THROW(policyOne.oser & prior, std::runtime_error);
	EXPECT_TRUE(policyOne.extractBuffer().empty());
	EXPECT_EQ(prior.toJson(), before);
	policyOne.oser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(policyOne.oser & prior);
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(policyOne.iser & restored);
	EXPECT_EQ(restored.toJson(), before);
}

TEST(NewHorizonsPerkState, R2FirstAndEitherSlotStatisticsRespectConfiguredParentRatio)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(double ratio : {1.0, 1.5, 2.0, 3.0})
	{
		SCOPED_TRACE(ratio);
		auto saved = r2State(ratio);
		const std::array<std::string, 3> ids = {"new-horizons:luck", "new-horizons:discipline", "new-horizons:offense"};
		bool enabled = false;
		perkStateFixture::editRules(saved, [&](JsonNode & rules)
		{
			for(auto & perk : rules["skills"][ids[2]]["perks"].Vector())
				if(!enabled && perk["requires"].String() == "basic")
				{
					perk["effect"]["status"].String() = "active";
					enabled = true;
				}
		});
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto saved = r2State(std::numeric_limits<double>::max());
	const auto context = r2Weights(saved);
	const auto offer = saved.prepareOffer(weightedOfferRank, 42, context);
	ASSERT_EQ(offer.size(), 2u);
	EXPECT_EQ(offer.front().selection.skillId, "new-horizons:discipline");
	EXPECT_NE(offer[0].selection.skillId, offer[1].selection.skillId);
}

TEST(NewHorizonsPerkState, R2WizardFourParentsUseFullClassBoundsRegardlessOfPerkPoolCardinality)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	std::map<std::string, int> row;
	for(const auto & [id, value] : development["skillOfferWeights"]["core:wizard"].Struct())
		row[id] = static_cast<int>(value.Integer());
	ASSERT_EQ(row.size(), newHorizonsHeroes::PERK_CANONICAL_SKILL_COUNT);
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
			perkStateFixture::editRules(saved, [&](JsonNode & rules)
			{
				for(auto & [id, definition] : rules["skills"].Struct())
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
			});
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto saved = r2State();
	auto missingBasic = saved;
	bool testedAdvanced = false;
	std::string advancedId;
	perkStateFixture::editRules(missingBasic, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"]["new-horizons:luck"]["perks"].Vector())
			if(!testedAdvanced && perk["requires"].String() == "advanced")
			{
				perk["effect"]["status"].String() = "active";
				advancedId = perk["id"].String();
				testedAdvanced = true;
			}
	});
	ASSERT_TRUE(testedAdvanced);
	EXPECT_THROW(missingBasic.select("new-horizons:luck", advancedId, 3), std::runtime_error);
	std::vector<std::string> basic;
	perkStateFixture::editRules(saved, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"]["new-horizons:luck"]["perks"].Vector())
			if(perk["requires"].String() == "basic")
			{
				perk["effect"]["status"].String() = "active";
				basic.push_back(perk["id"].String());
			}
	});
	ASSERT_EQ(basic.size(), 4u);
	for(size_t i = 0; i < 3; ++i)
		saved.select("new-horizons:luck", basic[i], 1);
	EXPECT_THROW(saved.select("new-horizons:luck", basic[3], 1), std::runtime_error);
	EXPECT_THROW(saved.select("new-horizons:luck", basic[0], 1), std::runtime_error);
	EXPECT_EQ(newHorizonsHeroes::PerkState::fromJson(saved.toJson()).getSelected(), saved.getSelected());
}
