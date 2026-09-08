/* Part of VCMI; GPL v2 or later, see license.txt. */
#include "StdInc.h"
#include "NewHorizonsLogisticsMasteryFixture.h"
#include "../../lib/serializer/CMemorySerializer.h"

using namespace newHorizonsHeroes;

TEST(NewHorizonsLogisticsMasteryState, BothPreGainFamiliesOfferSequentiallyAndRoundtrip)
{
	MasteryState state;
	state.rules = logisticsMasteryRules();
	state.captureBeforeLevel(2, 3, 3);
	auto offer = state.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2);
	ASSERT_TRUE(offer);
	ASSERT_EQ(offer->skill, SecondarySkill::ARTILLERY);
	state.applyOffer(*offer);
	ASSERT_EQ(state.accept(offer->hero, offer->player, offer->sequence, 2, 3, 0), MasteryReplyError::NONE);
	offer = state.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2);
	ASSERT_TRUE(offer);
	ASSERT_EQ(offer->skill, SecondarySkill::LOGISTICS);
	ASSERT_EQ(offer->sequence, 2);
	state.applyOffer(*offer);
	CMemorySerializer memory;
	memory.oser & state;
	MasteryState loaded;
	memory.iser & loaded;
	ASSERT_TRUE(loaded.pending);
	EXPECT_EQ(loaded.pending->options, offer->options);
	EXPECT_TRUE(loaded.logisticsEligible);
	EXPECT_FALSE(loaded.artilleryEligible);
	loaded = MasteryState::fromJson(loaded.toJson());
	ASSERT_EQ(loaded.accept(offer->hero, offer->player, offer->sequence, 2, 3, 2), MasteryReplyError::NONE);
	ASSERT_EQ(loaded.selected.size(), 2);
	EXPECT_EQ(loaded.selected.back().option.effect, MasteryEffect::LOGISTICS_PATHFINDER);
	EXPECT_FALSE(loaded.prepareOffer(offer->hero, offer->player, 2));
	loaded.validate();
}

TEST(NewHorizonsLogisticsMasteryState, NewlyExpertLogisticsWaitsAndLegacyNeverInventsFamily)
{
	MasteryState state;
	state.rules = logisticsMasteryRules();
	state.captureBeforeLevel(2, 0, 2);
	EXPECT_FALSE(state.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	state.captureBeforeLevel(3, 0, 3);
	ASSERT_TRUE(state.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 3));
	MasteryState legacy;
	legacy.rules = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	legacy.captureBeforeLevel(2, 0, 3);
	EXPECT_FALSE(legacy.logisticsEligible);
	EXPECT_FALSE(legacy.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	EXPECT_EQ(MasteryState::fromJson(legacy.toJson()).rules, legacy.rules);
}

TEST(NewHorizonsLogisticsMasteryState, WrongFamilyAndUnsupportedVersionReject)
{
	auto rules = logisticsMasteryRules();
	rules["rulesetVersion"].Integer() = 1;
	EXPECT_THROW(validateMasteryRules(rules), std::runtime_error);
	rules = logisticsMasteryRules();
	rules["skills"]["core:logistics"] = rules["skills"]["core:artillery"];
	EXPECT_THROW(validateMasteryRules(rules), std::runtime_error);
	MasteryState state;
	state.rules = logisticsMasteryRules();
	state.captureBeforeLevel(2, 0, 3);
	auto offer = *state.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2);
	offer.skill = SecondarySkill::ARTILLERY;
	EXPECT_THROW(validateMasteryOffer(offer), std::runtime_error);
}

TEST(NewHorizonsLogisticsMasteryState, OldArtilleryBinaryLoadsWithoutInventingLogisticsAndNewStateCannotDowngrade)
{
	MasteryState artillery;
	artillery.rules = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	artillery.captureBeforeLevel(2, 3);
	artillery.applyOffer(*artillery.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	old.oser & artillery;
	MasteryState loaded;
	old.iser & loaded;
	ASSERT_TRUE(loaded.pending);
	EXPECT_EQ(loaded.pending->options, artillery.pending->options);
	EXPECT_FALSE(loaded.logisticsEligible);
	EXPECT_FALSE(masteryOptions(loaded.rules, SecondarySkill::LOGISTICS));
	MasteryState logistics;
	logistics.rules = logisticsMasteryRules();
	CMemorySerializer downgrade;
	downgrade.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	EXPECT_THROW(downgrade.oser & logistics, std::runtime_error);
	CMemorySerializer oldReader;
	oldReader.oser & logistics;
	oldReader.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	MasteryState rejected;
	EXPECT_THROW(oldReader.iser & rejected, std::runtime_error);
}

namespace
{
void checkOldRulesRoundtrip(const JsonNode & rules)
{
	MasteryState source;
	source.rules = rules;
	ASSERT_NO_THROW(source.validate());
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	ASSERT_NO_THROW(memory.oser & source);
	EXPECT_EQ(source.rules, rules);
	EXPECT_EQ(source.rules.getType(), rules.getType());
	const JsonNode & afterWrite = source.rules;
	EXPECT_EQ(afterWrite["rulesetVersion"].getType(), rules["rulesetVersion"].getType());
	MasteryState loaded;
	ASSERT_NO_THROW(memory.iser & loaded);
	EXPECT_EQ(loaded.rules, rules);
	EXPECT_EQ(loaded.rules.getType(), rules.getType());
	const JsonNode & afterRead = loaded.rules;
	EXPECT_EQ(afterRead["rulesetVersion"].getType(), rules["rulesetVersion"].getType());
	EXPECT_FALSE(loaded.logisticsEligible);
	ASSERT_NO_THROW(loaded.validate());
}
}

TEST(NewHorizonsLogisticsMasteryState, OldFormatNullRulesStayNullOnWriteAndRead)
{
	checkOldRulesRoundtrip(JsonNode());
}

TEST(NewHorizonsLogisticsMasteryState, OldFormatEmptyObjectRulesStayEmptyOnWriteAndRead)
{
	JsonNode empty;
	empty.Struct();
	checkOldRulesRoundtrip(empty);
}

TEST(NewHorizonsLogisticsMasteryState, OldFormatVersionInspectionPreservesIntegralFloatRepresentation)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMasteries"));
	rules["rulesetVersion"].Float() = 1.0;
	checkOldRulesRoundtrip(rules);
}

TEST(NewHorizonsLogisticsMasteryState, CrossoverPreservesFloatRulesAndLegacyEmptyEnvelopes)
{
	MasteryState source;
	source.rules = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	source.rules["rulesetVersion"].Float() = 1.0;
	const auto envelope = source.toJson();
	const auto loaded = MasteryState::fromJson(envelope);
	EXPECT_EQ(loaded.rules["rulesetVersion"].getType(), JsonNode::JsonType::DATA_FLOAT);
	EXPECT_EQ(loaded.rules, source.rules);
	for(bool emptyObject : {false, true})
	{
		auto legacy = envelope;
		legacy["rules"] = JsonNode();
		if(emptyObject)
			legacy["rules"].Struct();
		const auto expected = legacy;
		const auto absent = MasteryState::fromJson(legacy);
		EXPECT_EQ(legacy, expected);
		EXPECT_EQ(absent.rules, expected["rules"]);
		EXPECT_EQ(absent.rules.getType(), expected["rules"].getType());
		EXPECT_FALSE(absent.logisticsEligible);
		EXPECT_TRUE(absent.toJson().isNull());
	}
}
