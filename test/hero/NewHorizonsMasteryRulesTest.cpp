/*
 * NewHorizonsMasteryRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsMasteryRules.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/entities/hero/NewHorizonsMasteryState.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
struct MasterySequenceWire
{
	uint64_t sequence = 0;
	template<typename Handler> void serialize(Handler & h)
	{
		newHorizonsHeroes::serializeMasterySequence(h, sequence);
	}
};

template<typename T> void expectSequenceRoundtrip(T original, uint64_t sequence)
{
	original.sequence = sequence;
	CMemorySerializer memory;
	memory.oser & original;
	T restored;
	memory.iser & restored;
	EXPECT_EQ(restored.sequence, sequence);
}

newHorizonsHeroes::MasteryOffer offer()
{
	using namespace newHorizonsHeroes;
	MasteryOffer result;
	result.hero = ObjectInstanceID(7);
	result.player = PlayerColor(0);
	result.skill = SecondarySkill::ARTILLERY;
	result.level = 2;
	result.sequence = 9;
	result.options = {{
		{{"new-horizons:artilleryVolley"}, MasteryEffect::ARTILLERY_VOLLEY, 1,
			"volley.name", "volley.description", "NH_mastery_artilleryVolley"},
		{{"new-horizons:artilleryPrecision"}, MasteryEffect::ARTILLERY_PRECISION, 1,
			"precision.name", "precision.description", "NH_mastery_artilleryPrecision"},
		{{"new-horizons:artilleryRepair"}, MasteryEffect::ARTILLERY_REPAIR, 50,
			"repair.name", "repair.description", "NH_mastery_artilleryRepair"}
	}};
	return result;
}
}

TEST(NewHorizonsMasteryRules, EveryOfferSelectionAndReplySequencePreservesAllUnsignedBits)
{
	using namespace newHorizonsHeroes;
	MasterySelection selection;
	selection.skill = SecondarySkill::ARTILLERY;
	selection.option = offer().options.front();
	selection.level = 2;
	HeroMasteryChosen chosen;
	chosen.hero = ObjectInstanceID(7);
	chosen.choice = 0;
	HeroMasteryReply reply;
	reply.hero = ObjectInstanceID(7);
	reply.player = PlayerColor(0);
	reply.qid = QueryID(8);
	reply.choice = 0;
	for(uint64_t sequence : {uint64_t(0), uint64_t(1), uint64_t(1) << 32,
		(uint64_t(1) << 63) - 1, uint64_t(1) << 63, std::numeric_limits<uint64_t>::max()})
	{
		SCOPED_TRACE(sequence);
		expectSequenceRoundtrip(offer(), sequence);
		expectSequenceRoundtrip(selection, sequence);
		expectSequenceRoundtrip(chosen, sequence);
		expectSequenceRoundtrip(reply, sequence);
	}
}

TEST(NewHorizonsMasteryRules, MalformedSequenceHalvesRejectBeforeAssignment)
{
	constexpr int64_t overflow = int64_t(std::numeric_limits<uint32_t>::max()) + 1;
	for(const auto & halves : std::array<std::pair<int64_t, int64_t>, 4>{{{-1, 0}, {0, -1}, {overflow, 0}, {0, overflow}}})
	{
		CMemorySerializer memory;
		memory.oser & halves.first;
		memory.oser & halves.second;
		MasterySequenceWire restored;
		restored.sequence = 99;
		EXPECT_THROW(memory.iser & restored, std::runtime_error);
		EXPECT_EQ(restored.sequence, 99);
	}
}

TEST(NewHorizonsMasteryRules, SavedCounterPreservesExistingCrossoverLimitAndRejectsExhaustion)
{
	using namespace newHorizonsHeroes;
	MasteryState original;
	original.rules = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	original.captureBeforeLevel(2, 3);
	original.applyOffer(*original.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	original.lastSequence = std::numeric_limits<int64_t>::max();
	original.pending->sequence = original.lastSequence;
	CMemorySerializer memory;
	memory.oser & original;
	MasteryState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.lastSequence, original.lastSequence);
	ASSERT_TRUE(restored.pending);
	EXPECT_EQ(restored.pending->sequence, original.lastSequence);
	EXPECT_THROW(restored.prepareOffer(ObjectInstanceID(8), PlayerColor(1), 2), std::runtime_error);
}

TEST(NewHorizonsMasteryRules, CanonicalNamedSchemaAndSavedOptionsAreIndependentOfDefaults)
{
	using namespace newHorizonsHeroes;
	JsonNode rules(JsonPath::builtin("config/newHorizonsMasteries"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsMasteries", "canonical mastery rules"));
	ASSERT_NO_THROW(validateMasteryRules(rules));
	const auto saved = masteryOptions(rules, SecondarySkill::ARTILLERY);
	ASSERT_TRUE(saved);
	EXPECT_EQ((*saved)[0].id.value, "new-horizons:artilleryVolley");
	EXPECT_EQ((*saved)[1].effect, MasteryEffect::ARTILLERY_PRECISION);
	EXPECT_EQ((*saved)[2].magnitude, 50);
	rules["skills"].Struct().clear();
	EXPECT_EQ((*saved)[0].magnitude, 1);
	EXPECT_NO_THROW(validateMasteryOptions(*saved));
	EXPECT_FALSE(masteryOptions(JsonNode(), SecondarySkill::ARTILLERY));
	EXPECT_NO_THROW(validateMasteryRules(JsonNode()));
	EXPECT_NO_THROW(validateMasteryRules(JsonNode(JsonMap{})));
	EXPECT_TRUE(JsonUtils::validate(JsonNode(JsonMap{}), "vcmi:newHorizonsMasteries", "empty mastery rules"));
}

TEST(NewHorizonsMasteryRules, RealSettingsWrapperRejectsInvalidMagnitudesAndShape)
{
	using namespace newHorizonsHeroes;
	const JsonNode original(JsonPath::builtin("config/newHorizonsMasteries"));
	const auto wrapped = [](const JsonNode & node)
	{
		JsonNode settings;
		settings["heroes"]["newHorizonsMasteries"] = node;
		return JsonUtils::validate(settings, "vcmi:gameSettings", "actual mastery settings wrapper");
	};
	EXPECT_TRUE(wrapped(original));
	EXPECT_TRUE(wrapped(JsonNode(JsonMap{})));
	std::vector<JsonNode> invalid(5, original);
	invalid[0]["schemaVersion"].Integer() = 2;
	invalid[1]["skills"]["core:artillery"]["options"].Vector().pop_back();
	invalid[2]["skills"]["core:artillery"]["options"].Vector()[0]["magnitude"].Integer() = 9;
	invalid[3]["skills"]["core:artillery"]["options"].Vector()[1]["magnitude"].Integer() = 2;
	invalid[4]["skills"]["core:artillery"]["options"].Vector()[2]["magnitude"].Float() = 50.5;
	for(const auto & node : invalid)
	{
		EXPECT_FALSE(wrapped(node));
		EXPECT_FALSE(JsonUtils::validate(node, "vcmi:newHorizonsMasteries", "bad mastery rules"));
		EXPECT_THROW(validateMasteryRules(node), std::runtime_error);
	}
}

TEST(NewHorizonsMasteryRules, RuntimeRejectsDuplicateIdsEffectsAndForeignScopesBeyondSchema)
{
	using namespace newHorizonsHeroes;
	const JsonNode original(JsonPath::builtin("config/newHorizonsMasteries"));
	std::vector<JsonNode> invalid(3, original);
	auto & ids = invalid[0]["skills"]["core:artillery"]["options"].Vector();
	ids[1]["id"] = ids[0]["id"];
	auto & effects = invalid[1]["skills"]["core:artillery"]["options"].Vector();
	effects[1]["effect"].String() = "repair";
	effects[1]["magnitude"].Integer() = 50;
	invalid[2]["skills"]["core:artillery"]["options"].Vector()[0]["id"].String() = "other-mod:volley";
	for(const auto & node : invalid)
	{
		EXPECT_TRUE(JsonUtils::validate(node, "vcmi:newHorizonsMasteries", "structural schema is not semantic validation"));
		EXPECT_THROW(validateMasteryRules(node), std::runtime_error);
	}
}

TEST(NewHorizonsMasteryRules, DescriptionUsesSavedMagnitudeNotLaterDefaults)
{
	using namespace newHorizonsHeroes;
	auto current = offer();
	const auto saved = current.options[2];
	current.options[2].magnitude = 99;
	EXPECT_EQ(formatMasteryDescription(saved, "Repair {magnitude} HP; at most {magnitude}."),
		"Repair 50 HP; at most 50.");
	EXPECT_EQ(formatMasteryDescription(current.options[1], "Ignore distance and wall penalties."),
		"Ignore distance and wall penalties.");
}

TEST(NewHorizonsMasteryRules, EachOfferedChoiceRequiresExactSavedIdentity)
{
	using namespace newHorizonsHeroes;
	const auto pending = offer();
	ASSERT_NO_THROW(validateMasteryOffer(pending));
	for(int index = 0; index < 3; ++index)
		EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 9, 2, 3, false, index), MasteryReplyError::NONE);
	EXPECT_EQ(validateMasteryReply(pending, ObjectInstanceID(8), pending.player, 9, 2, 3, false, 0), MasteryReplyError::WRONG_HERO);
	EXPECT_EQ(validateMasteryReply(pending, pending.hero, PlayerColor(1), 9, 2, 3, false, 0), MasteryReplyError::WRONG_PLAYER);
	EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 8, 2, 3, false, 0), MasteryReplyError::STALE_OFFER);
	EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 9, 3, 3, false, 0), MasteryReplyError::STALE_OFFER);
}

TEST(NewHorizonsMasteryRules, InvalidDuplicateAndFictionalRankFourRepliesCannotConsumeChoices)
{
	using namespace newHorizonsHeroes;
	const auto pending = offer();
	for(int index : {-1, 3})
		EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 9, 2, 3, false, index), MasteryReplyError::INVALID_CHOICE);
	for(int rank : {0, 1, 2, 4})
		EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 9, 2, rank, false, 0), MasteryReplyError::NOT_EXPERT);
	EXPECT_EQ(validateMasteryReply(pending, pending.hero, pending.player, 9, 2, 3, true, 0), MasteryReplyError::ALREADY_CHOSEN);
	EXPECT_EQ(pending.sequence, 9);
	EXPECT_EQ(pending.options[0].magnitude, 1);
}

TEST(NewHorizonsMasteryRules, MalformedOffersRejectDuplicateEffectsAndInertPrecision)
{
	using namespace newHorizonsHeroes;
	auto invalid = offer();
	invalid.options[1].id = invalid.options[0].id;
	EXPECT_THROW(validateMasteryOffer(invalid), std::runtime_error);
	invalid = offer();
	invalid.options[1].effect = invalid.options[0].effect;
	EXPECT_THROW(validateMasteryOffer(invalid), std::runtime_error);
	invalid = offer();
	invalid.options[1].magnitude = 0;
	EXPECT_THROW(validateMasteryOffer(invalid), std::runtime_error);
	invalid = offer();
	invalid.sequence = 0;
	EXPECT_THROW(validateMasteryOffer(invalid), std::runtime_error);
}
