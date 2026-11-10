/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandTestSupport.h"
#include "../../lib/entities/hero/NewHorizonsCommandAdmission.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
template<typename Packet> void rejectsBeforePrefix(Packet & packet)
{
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	EXPECT_THROW(packet.serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}
}

TEST(NewHorizonsCommandProtocol, AllDedicatedPacketsRejectThePreviousEpochBeforeTheirPrefix)
{
	HeroCommandProgression event;
	HeroCommandEnhancementDialog dialog;
	HeroCommandEnhancementChosen chosen;
	HeroCommandEnhancementReply reply;
	rejectsBeforePrefix(event);
	rejectsBeforePrefix(dialog);
	rejectsBeforePrefix(chosen);
	rejectsBeforePrefix(reply);
}

TEST(NewHorizonsCommandProtocol, MissingCrossoverHistoryAndUnsupportedCapturesCannotBeDiscarded)
{
	using namespace newHorizonsHeroes;
	JsonNode hero;
	hero["capabilityRules"] = commandTest::capabilities();
	EXPECT_THROW(validateCommandCrossover(hero, true), std::runtime_error);
	CommandState state;
	state.capture(commandTest::rules(), 16, 0);
	hero["commandProgression"] = state.toJson(commandTest::rules());
	EXPECT_NO_THROW(validateCommandCrossover(hero, true));
	EXPECT_THROW(validateCommandCrossover(hero, false), std::runtime_error);
	JsonNode warfare;
	warfare["warfarePolicyVersion"].Integer() = 1;
	EXPECT_THROW(validateCommandCaptures(JsonNode(), warfare, JsonNode(), false), std::runtime_error);
	EXPECT_THROW(validateCommandCaptures(JsonNode(), JsonNode(), warfare, false), std::runtime_error);
}

TEST(NewHorizonsCommandProtocol, UnsupportedCombatOverrideReadLeavesSettingsUnchanged)
{
	GameSettings settings;
	const auto before = settings.getFullConfig();
	JsonNode incoming;
	incoming["combat"]["heroCommands"]["warfarePolicyVersion"].Integer() = 1;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	memory.iser.version = memory.oser.version;
	memory.oser & incoming;
	EXPECT_THROW(settings.serialize(memory.iser), std::runtime_error);
	EXPECT_EQ(settings.getFullConfig(), before);
}

TEST(NewHorizonsCommandProtocol, NineValuedOptionsRetainAcquisitionLevelAfterLaterAdvancement)
{
	using namespace newHorizonsHeroes;
	CommandState state;
	const auto rules = commandTest::rules();
	state.capture(rules, 16, 0);
	const auto acquired = prepareCommandEnhancementOffer(ObjectInstanceID(3), PlayerColor(0), state, rules);
	ASSERT_TRUE(acquired);
	state.capture(rules, 31, 0);
	const auto deferred = prepareCommandEnhancementOffer(ObjectInstanceID(3), PlayerColor(0), state, rules);
	ASSERT_EQ(deferred, acquired);
	for(size_t i = 0; i < deferred->options.size(); ++i)
	{
		EXPECT_EQ(deferred->options[i].enhancement, static_cast<CommandEnhancement>(i));
		EXPECT_EQ(deferred->options[i].contribution, commandEnhancementValue(rules, static_cast<CommandEnhancement>(i), 16));
	}
	const auto before = state;
	EXPECT_FALSE(validCommandEnhancementReply(*deferred, ObjectInstanceID(3), PlayerColor(0), 7, 0, state, rules, 11));
	EXPECT_FALSE(validCommandEnhancementReply(*deferred, ObjectInstanceID(3), PlayerColor(0), 6, -1, state, rules, 11));
	EXPECT_FALSE(validCommandEnhancementReply(*deferred, ObjectInstanceID(3), PlayerColor(0), 6, 0, state, rules, 5));
	EXPECT_EQ(state, before);
}
