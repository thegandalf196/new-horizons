/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OverkillRandomPrayerRules.h"
#include "../PerkStateFixture.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
#include <string_view>

namespace
{
using namespace newHorizonsHeroes;
constexpr const char * SKILL = "new-horizons:offense";

PerkState state(JsonNode rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks")))
{
	PerkState result;
	rules.Struct().erase("unifiedSkillProgressionVersion");
	rules["fourOfferProgressionVersion"].Integer() = 1;
	rules["perkProgressionPolicyVersion"].Integer() = 2;
	result.replaceRules(std::move(rules));
	return result;
}

// Pin the complete v4 roster rather than relabeling installed v6 perks.
// Source: config/newHorizonsPerks.json at d0647f2e5d (v5 adds only Overflow).
JsonNode historicalMetamagicPerks()
{
	static constexpr std::string_view json = R"json([
		{
		  "id": "new-horizons:metamagic.conservedFormula",
		  "name": "Conserved Formula",
		  "requires": "basic",
		  "description": "Refund the actual Mana paid for a valid single-target spell completely resisted by its sole recipient. The Hero Action remains spent.",
		  "effect": {
		    "status": "active",
		    "description": "Refund the actual Mana paid for a valid single-target spell completely resisted by its sole recipient. The Hero Action remains spent."
		  },
		  "details": "No refund for cancellation, invalid targets, partial effects, absorption or damage reduced to zero. Combined refunds never exceed actual payment."
		},
		{
		  "id": "new-horizons:metamagic.manaRecycling",
		  "name": "Mana Recycling",
		  "requires": "basic",
		  "description": "Recover Mana from magical effects actually removed during your cast.",
		  "effect": {
		    "status": "active",
		    "description": "Recover Mana from magical effects actually removed during your cast."
		  },
		  "details": "Once per original cast, refund min(floor(total normalized removed Mana / 4), floor(actual Mana paid / 2)), respecting Mana capacity and the combined payment cap. Free casts refund zero."
		},
		{
		  "id": "new-horizons:metamagic.disruptiveCasting",
		  "name": "Disruptive Casting",
		  "requires": "basic",
		  "description": "Actual spell damage removes the target's ordinary Defend bonus after the hit.",
		  "effect": {
		    "status": "active",
		    "description": "Actual spell damage removes the target's ordinary Defend bonus after the hit."
		  },
		  "details": "Retaliation, spent actions, Wait and unrelated Orders remain unchanged."
		},
		{
		  "id": "new-horizons:metamagic.purgingFormula",
		  "name": "Purging Formula",
		  "requires": "basic",
		  "description": "A successful buff also removes one physical affliction from each actual friendly recipient.",
		  "effect": {
		    "status": "active",
		    "description": "A successful buff also removes one physical affliction from each actual friendly recipient."
		  },
		  "details": "Physical removal releases no Metamagic energy. No additional target or selector is introduced."
		},
		{
		  "id": "new-horizons:metamagic.spellSculpting",
		  "name": "Spell Sculpting",
		  "requires": "advanced",
		  "description": "Your damaging hero spells deal no damage to friendly creatures.",
		  "effect": {
		    "status": "active",
		    "description": "Your damaging hero spells deal no damage to friendly creatures."
		  },
		  "details": "Includes area, chain and global damage and your owned spell-created hazards; unrelated terrain, enemy spells and ordinary attacks are unchanged."
		},
		{
		  "id": "new-horizons:metamagic.hexWeaving",
		  "name": "Hex Weaving",
		  "requires": "advanced",
		  "description": "Successful non-damaging hostile hero spells also unravel temporary enemy enchantments.",
		  "effect": {
		    "status": "active",
		    "description": "Successful non-damaging hostile hero spells also unravel temporary enemy enchantments."
		  },
		  "details": "Uses the same rank-based chance, one attempt per logical effect and separate 10 damage per removed Mana discharge. It cannot recurse."
		},
		{
		  "id": "new-horizons:metamagic.widenSpell",
		  "name": "Widen Spell",
		  "requires": "advanced",
		  "description": "Eligible centrally aimed area spells affect one additional hex ring, without extra Mana cost.",
		  "effect": {
		    "status": "active",
		    "description": "Eligible centrally aimed area spells affect one additional hex ring, without extra Mana cost."
		  },
		  "details": "Excludes single-target spells, lines, cones, chains, global or mass spells, and persistent battlefield fields. Force Lance remains single-target."
		},
		{
		  "id": "new-horizons:metamagic.matterbreaker",
		  "name": "Matterbreaker",
		  "requires": "advanced",
		  "description": "Damaging area spells destroy ordinary physical obstacles intersecting their affected area.",
		  "effect": {
		    "status": "active",
		    "description": "Damaging area spells destroy ordinary physical obstacles intersecting their affected area."
		  },
		  "details": "Adds no radius, fortification damage, magical-object removal, explosion or permission to target illegal empty hexes."
		},
		{
		  "id": "new-horizons:metamagic.magicalMemory",
		  "name": "Magical Memory",
		  "requires": "expert",
		  "description": "A spell family actually removed by Metamagic cannot be reapplied to that same recipient for the rest of combat.",
		  "effect": {
		    "status": "active",
		    "description": "A spell family actually removed by Metamagic cannot be reapplied to that same recipient for the rest of combat."
		  },
		  "details": "Applies to cleansed allied curses and stripped enemy enchantments, including their single and mass variants, regardless of later caster. Does not block unrelated damage or extend durations."
		},
		{
		  "id": "new-horizons:metamagic.arcaneRupture",
		  "name": "Arcane Rupture",
		  "requires": "expert",
		  "description": "Metamagic's separate discharge bypasses magical protection and damage reduction.",
		  "effect": {
		    "status": "active",
		    "description": "Metamagic's separate discharge bypasses magical protection and damage reduction."
		  },
		  "details": "The discharge remains 10 damage per removed Mana. Original spell admission, resistance and damage remain unchanged; a fully resisted spell cannot earn a discharge."
		}
	])json";
	return JsonNode(json.data(), json.size(), "historical Metamagic fixture");
}

PerkState historicalMetamagicStateV4()
{
	auto result = state();
	perkStateFixture::editRules(result, [](JsonNode & rules)
	{
		auto & policy = rules["metamagicPolicy"];
		policy.Struct().clear();
		policy["version"].Integer() = 4;
		policy["removalChancePercent"].Vector() = {JsonNode(0), JsonNode(20), JsonNode(30), JsonNode(40)};
		policy["damagePerMana"].Integer() = 10;
		policy["restorationPerMana"].Integer() = 15;
		policy["halonRemovalChanceBonusPercent"].Integer() = 5;
		rules["skills"]["new-horizons:metamagic"]["perks"] = historicalMetamagicPerks();
	});
	return result;
}

void expectMetamagicRoster(const PerkState & value, const std::array<const char *, 10> & expected)
{
	const auto options = perkOptions(value.getRules(), "new-horizons:metamagic");
	ASSERT_EQ(options.size(), expected.size());
	for(size_t index = 0; index < expected.size(); ++index)
	{
		EXPECT_EQ(options[index].id, std::string("new-horizons:metamagic.") + expected[index]);
		EXPECT_EQ(options[index].requiredRank, index < 4 ? "basic" : index < 8 ? "advanced" : "expert");
		EXPECT_EQ(options[index].effect["status"].String(), "active");
	}
}

std::vector<PerkOfferCandidate> pool(const PerkState & state, int rank)
{
	return PerkSelectionOperation(state).eligibleCandidates(SKILL, rank);
}

void selectFirst(PerkState & value, int rank)
{
	const auto choices = pool(value, rank);
	ASSERT_FALSE(choices.empty());
	value.select(SKILL, choices.front().selection.perkId, rank);
}
}

TEST(ExclusivePerkProgression, BasicCompletedTierHasNoPerkGroupAndNextRankOffersAllAdvanced)
{
	auto value = state();
	ASSERT_EQ(pool(value, 1).size(), 4);
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 1));
	EXPECT_TRUE(pool(value, 1).empty());
	EXPECT_TRUE(value.candidatesForSkillTier(SKILL, 1).empty());
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 1));
	const auto advanced = pool(value, 2);
	ASSERT_EQ(advanced.size(), 4);
	for(const auto & choice : advanced) EXPECT_EQ(choice.requiredRank, 2);
	const std::vector<FourOfferSkillCandidate> candidates{{SecondarySkill(0), 1, 8, true, pool(value, 1)}};
	const auto offer = prepareFourOfferProgression(candidates, [](const auto &) { return size_t{0}; });
	EXPECT_TRUE(offer.perkSkills[0].empty());
	EXPECT_TRUE(offer.perkSkills[1].empty());
}

TEST(ExclusivePerkProgression, ExternalExpertRankAcquiresExactlyOnePerTierInOrder)
{
	auto value = state();
	for(int tier = 1; tier <= 3; ++tier)
	{
		const auto choices = pool(value, 3);
		ASSERT_EQ(choices.size(), tier == 3 ? 2 : 4);
		for(const auto & choice : choices) EXPECT_EQ(choice.requiredRank, tier);
		ASSERT_NO_FATAL_FAILURE(selectFirst(value, 3));
	}
	EXPECT_EQ(value.getSelected().size(), 3);
	EXPECT_TRUE(pool(value, 3).empty());
	EXPECT_NO_THROW(value.validate());
}

TEST(ExclusivePerkProgression, DuplicateTierAndSkippedPrerequisiteRejectWithoutPublishingChanges)
{
	auto value = state();
	const auto basic = pool(value, 1);
	ASSERT_GE(basic.size(), 2);
	const auto advancedDefinition = perkOptions(value.getRules(), SKILL)[4];
	EXPECT_FALSE(value.canAcquirePerk(SKILL, advancedDefinition.id, 3));
	EXPECT_THROW(value.select(SKILL, advancedDefinition.id, 3), std::runtime_error);
	EXPECT_TRUE(value.getSelected().empty());
	value.select(SKILL, basic.front().selection.perkId, 1);
	const auto before = value.toJson();
	EXPECT_FALSE(value.canAcquirePerk(SKILL, basic.back().selection.perkId, 3));
	EXPECT_THROW(value.select(SKILL, basic.back().selection.perkId, 3), std::runtime_error);
	EXPECT_EQ(value.toJson(), before);
}

TEST(ExclusivePerkProgression, BasicAndAdvancedPerksStillGateNormalRankUp)
{
	auto value = state();
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 2));
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 1));
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 2));
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 2));
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 2));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 3));
}

TEST(ExclusivePerkProgression, DevelopedHistoricalFourOfferChoicesKeepCapturedPolicyAndBytes)
{
	auto historical = state();
	perkStateFixture::editRules(historical, [&](JsonNode & rules) { rules["perkProgressionPolicyVersion"].Integer() = 1; });
	for(int count = 0; count < 3; ++count) ASSERT_NO_FATAL_FAILURE(selectFirst(historical, 1));
	EXPECT_TRUE(usesStrictPerkProgression(historical.getRules()));
	EXPECT_FALSE(usesExclusivePerkTiers(historical.getRules()));
	CMemorySerializer memory;
	memory.oser & historical;
	PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.getRules(), historical.getRules());
	EXPECT_EQ(restored.getSelected(), historical.getSelected());
	EXPECT_NO_THROW(restored.validate());
	CMemorySerializer reencoded;
	reencoded.oser & restored;
	EXPECT_EQ(reencoded.extractBuffer(), memory.extractBuffer());
}

TEST(ExclusivePerkProgression, MetamagicV4RequiresExactCompleteCapturedProfile)
{
	const auto historical = historicalMetamagicStateV4();
	const auto profile = historical.getRules()["metamagicPolicy"];
	ASSERT_EQ(profile["version"].Integer(), 4);
	EXPECT_EQ(profile.Struct().size(), 5u);
	const std::vector<JsonNode> removal{JsonNode(0), JsonNode(20), JsonNode(30), JsonNode(40)};
	EXPECT_EQ(profile["removalChancePercent"].Vector(), removal);
	EXPECT_EQ(profile["damagePerMana"].Integer(), 10);
	EXPECT_EQ(profile["restorationPerMana"].Integer(), 15);
	EXPECT_EQ(profile["halonRemovalChanceBonusPercent"].Integer(), 5);
	constexpr std::array roster{"conservedFormula", "manaRecycling", "disruptiveCasting", "purgingFormula",
		"spellSculpting", "hexWeaving", "widenSpell", "matterbreaker", "magicalMemory", "arcaneRupture"};
	ASSERT_NO_FATAL_FAILURE(expectMetamagicRoster(historical, roster));
	EXPECT_NO_THROW(validatePassiveFactionPolicies(historical.getRules()));
	for(const auto * key : {"version", "removalChancePercent", "damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
	{
		auto missing = historical.getRules();
		missing["metamagicPolicy"].Struct().erase(key);
		EXPECT_THROW(validatePassiveFactionPolicies(missing), std::runtime_error);
	}
	for(const auto * key : {"damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
	{
		auto wrong = historical.getRules();
		++wrong["metamagicPolicy"][key].Integer();
		EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	}
	auto wrong = historical.getRules();
	wrong["metamagicPolicy"]["removalChancePercent"].Vector()[0].Integer() = 20;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	wrong = historical.getRules();
	wrong["metamagicPolicy"]["adeptAdditionalPoints"].Integer() = 1;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
}

TEST(ExclusivePerkProgression, CapturedMetamagicV6RequiresItsCompleteProfile)
{
	const auto current = state(overkillRandomPrayerRules());
	const auto & profile = current.getRules()["metamagicPolicy"];
	ASSERT_EQ(profile["version"].Integer(), 6);
	EXPECT_EQ(profile.Struct().size(), 8u);
	const std::vector<JsonNode> removal{JsonNode(0), JsonNode(20), JsonNode(30), JsonNode(40)};
	EXPECT_EQ(profile["removalChancePercent"].Vector(), removal);
	EXPECT_EQ(profile["damagePerMana"].Integer(), 10);
	EXPECT_EQ(profile["restorationPerMana"].Integer(), 15);
	EXPECT_EQ(profile["halonRemovalChanceBonusPercent"].Integer(), 5);
	constexpr std::array roster{"conservedFormula", "disruptiveCasting", "unravelingMagic", "reconstitutingMagic",
		"spellSculpting", "widenSpells", "levitation", "arcaneBallistics", "arcaneCascade", "empoweredSpells"};
	ASSERT_NO_FATAL_FAILURE(expectMetamagicRoster(current, roster));
	EXPECT_NO_THROW(validatePassiveFactionPolicies(current.getRules()));
	for(const auto * key : {"version", "removalChancePercent", "damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent",
		"arcaneOverflowPercentByRank", "friendlyDamageReductionPercentByRank", "debuffRetryChancePercentByRank"})
	{
		auto missing = current.getRules();
		missing["metamagicPolicy"].Struct().erase(key);
		EXPECT_THROW(validatePassiveFactionPolicies(missing), std::runtime_error);
	}
	for(const auto * key : {"damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
	{
		auto wrong = current.getRules();
		++wrong["metamagicPolicy"][key].Integer();
		EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	}
	const std::vector<JsonNode> expected{JsonNode(0), JsonNode(30), JsonNode(45), JsonNode(60)};
	for(const auto * key : {"arcaneOverflowPercentByRank", "friendlyDamageReductionPercentByRank", "debuffRetryChancePercentByRank"})
	{
		EXPECT_EQ(profile[key].Vector(), expected);
		auto wrong = current.getRules();
		wrong["metamagicPolicy"][key].Vector().pop_back();
		EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
		for(size_t rank = 0; rank < 4; ++rank)
		{
			wrong = current.getRules();
			++wrong["metamagicPolicy"][key].Vector()[rank].Integer();
			EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
		}
		wrong = current.getRules();
		wrong["metamagicPolicy"][key].Vector() = {JsonNode(0), JsonNode(25), JsonNode(50), JsonNode(75)};
		EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	}
	auto wrong = current.getRules();
	wrong["metamagicPolicy"]["removalChancePercent"].Vector()[0].Integer() = 20;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	wrong = current.getRules();
	wrong["metamagicPolicy"]["adeptAdditionalPoints"].Integer() = 1;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
}
