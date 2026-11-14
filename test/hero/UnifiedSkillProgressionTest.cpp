/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <algorithm>

namespace
{
constexpr auto SKILL = "new-horizons:authority";
newHorizonsHeroes::PerkState state()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	return result;
}
int expert(const std::string & skill) { return skill == SKILL ? 3 : 0; }
}

TEST(UnifiedSkillProgression, CompleteTierPoolsAreSequentialAndIndependentOfSeed)
{
	auto current = state();
	ASSERT_TRUE(newHorizonsHeroes::usesUnifiedSkillProgression(current.rules));
	for(const auto [tier, size] : {std::pair{1, 4u}, std::pair{2, 4u}, std::pair{3, 2u}})
	{
		const auto offer = current.nextRequiredPerkOffer(expert);
		ASSERT_EQ(offer.size(), size);
		EXPECT_EQ(offer, current.prepareOffer(expert, 0));
		EXPECT_EQ(offer, current.prepareOffer(expert, UINT64_MAX));
		for(const auto & candidate : offer) EXPECT_EQ(candidate.requiredRank, tier);
		current.acceptOffer(offer, offer.size() - 1, expert, 0);
		EXPECT_TRUE(current.hasPerkForTier(SKILL, tier));
		EXPECT_TRUE(current.candidatesForSkillTier(SKILL, tier).empty());
	}
	EXPECT_EQ(current.selected.size(), 3u);
	EXPECT_TRUE(current.nextRequiredPerkOffer(expert).empty());
}

TEST(UnifiedSkillProgression, SubsetsReorderingForgedMetadataAndDuplicateRepliesAreAtomic)
{
	auto current = state();
	const auto offer = current.nextRequiredPerkOffer(expert);
	ASSERT_EQ(offer.size(), 4u);
	const auto before = current.toJson();
	auto subset = offer;
	subset.pop_back();
	EXPECT_THROW(current.acceptOffer(subset, 0, expert, 0), std::runtime_error);
	auto reversed = offer;
	std::reverse(reversed.begin(), reversed.end());
	EXPECT_THROW(current.acceptOffer(reversed, 0, expert, 0), std::runtime_error);
	auto forged = offer;
	forged.front().description += " altered";
	EXPECT_THROW(current.acceptOffer(forged, 0, expert, 0), std::runtime_error);
	EXPECT_EQ(current.toJson(), before);
	current.acceptOffer(offer, 0, expert, 0);
	const auto accepted = current.toJson();
	EXPECT_THROW(current.acceptOffer(offer, 0, expert, 0), std::runtime_error);
	EXPECT_EQ(current.toJson(), accepted);
}

TEST(UnifiedSkillProgression, RankAdvancementRequiresPreviousTierAndOwnershipOfRank)
{
	auto current = state();
	EXPECT_TRUE(current.canAdvanceSkillNormally(SKILL, 0));
	EXPECT_FALSE(current.canAdvanceSkillNormally(SKILL, 1));
	const auto basic = current.candidatesForSkillTier(SKILL, 1);
	const auto absent = [](const std::string &) { return 0; };
	EXPECT_THROW(current.acceptOffer(basic, 0, absent, 0), std::runtime_error);
	EXPECT_TRUE(current.selected.empty());
	current.acceptOffer(basic, 0, expert, 0);
	EXPECT_TRUE(current.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(current.canAdvanceSkillNormally(SKILL, 2));
	current.acceptOffer(current.candidatesForSkillTier(SKILL, 2), 0, expert, 0);
	EXPECT_TRUE(current.canAdvanceSkillNormally(SKILL, 2));
	EXPECT_FALSE(current.canAdvanceSkillNormally(SKILL, 3));
}

TEST(UnifiedSkillProgression, AuthoredStartingPerkAndBinaryReloadRetainMandatoryTierDebt)
{
	auto current = state();
	const auto authored = current.candidatesForSkillTier(SKILL, 1).at(2);
	current.select(SKILL, authored.selection.perkId, 3);
	CMemorySerializer memory;
	memory.oser & current;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.selected, current.selected);
	const auto next = restored.nextRequiredPerkOffer(expert);
	ASSERT_EQ(next.size(), 4u);
	EXPECT_EQ(next.front().requiredRank, 2);
	restored.selectMissingPerksDeterministically(expert);
	ASSERT_EQ(restored.selected.size(), 3u);
	EXPECT_EQ(restored.selected.front(), authored.selection);
	const auto completed = restored.toJson();
	restored.selectMissingPerksDeterministically(expert);
	EXPECT_EQ(restored.toJson(), completed);
}

TEST(UnifiedSkillProgression, DowngradeRefusesBeforeWritingAnyPerkPayload)
{
	auto current = state();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	EXPECT_THROW(memory.oser & current, std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}
