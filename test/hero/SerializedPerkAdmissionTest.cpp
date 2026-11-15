/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PerkStateWriteReference.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
using namespace newHorizonsHeroes;
PerkState canonicalState()
{
	PerkState state;
	state.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	return state;
}

std::vector<PerkSelection> activePrefix(const JsonNode & rules)
{
	for(const auto & [skill, row] : rules["skills"].Struct())
	{
		std::vector<PerkSelection> result;
		for(const auto * tier : {"basic", "advanced", "expert"})
		{
			for(const auto & perk : row["perks"].Vector())
				if(perk["requires"].String() == tier && perk["effect"]["status"].String() == "active")
				{
					result.push_back({skill, perk["id"].String()});
					break;
				}
		}
		if(result.size() == 3) return result;
	}
	throw std::runtime_error("Fixture needs one active Basic/Advanced/Expert prefix");
}

std::vector<std::byte> write(PerkState & state, ESerializationVersion version, bool reference)
{
	CMemorySerializer memory;
	memory.oser.version = version;
	if(reference) perkWriteReference::write(memory.oser, state);
	else memory.oser & state;
	return memory.extractBuffer();
}

std::vector<std::byte> rngBytes()
{
	CMemorySerializer memory;
	CRandomGenerator::getDefault().serialize(memory.oser);
	return memory.extractBuffer();
}
}

TEST(SerializedPerkAdmissionTest, CurrentWriterMatchesOriginalBytesForEverySelectedTierPrefix)
{
	auto state = canonicalState();
	const auto prefix = activePrefix(state.rules);
	const auto rngBefore = rngBytes();
	for(size_t count = 0; count <= prefix.size(); ++count)
	{
		SCOPED_TRACE(count);
		state.selected.assign(prefix.begin(), prefix.begin() + count);
		const auto rulesBefore = state.rules;
		const auto selectedBefore = state.selected;
		EXPECT_EQ(write(state, ESerializationVersion::CURRENT, false),
			write(state, ESerializationVersion::CURRENT, true));
		EXPECT_EQ(state.rules, rulesBefore);
		EXPECT_EQ(state.selected, selectedBefore);
	}
	EXPECT_EQ(rngBytes(), rngBefore);
}

TEST(SerializedPerkAdmissionTest, NullAndEmptyHistoricalRulesKeepCurrentAndEarlierCodecBytes)
{
	for(const bool emptyObject : {false, true})
		for(const auto version : {ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS})
		{
			PerkState state;
			if(emptyObject) state.rules.Struct();
			const auto before = state.rules;
			EXPECT_EQ(write(state, version, false), write(state, version, true));
			EXPECT_EQ(state.rules, before);
			EXPECT_TRUE(state.selected.empty());
		}
}

TEST(SerializedPerkAdmissionTest, UnrequestedMalformedRowAndIllegalSelectionsRefuseBeforeStatePrefix)
{
	for(const int failure : {0, 1, 2})
	{
		SCOPED_TRACE(failure);
		auto state = canonicalState();
		const auto prefix = activePrefix(state.rules);
		if(failure == 0)
			state.rules["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
		else if(failure == 1)
			state.selected = {prefix[0], prefix[0]};
		else
			state.selected = {{"missing:skill", "missing:perk"}};
		const auto rulesBefore = state.rules;
		const auto selectedBefore = state.selected;
		for(const bool reference : {false, true})
		{
			CMemorySerializer memory;
			if(reference) EXPECT_THROW(perkWriteReference::write(memory.oser, state), std::runtime_error);
			else EXPECT_THROW(memory.oser & state, std::runtime_error);
			EXPECT_TRUE(memory.extractBuffer().empty());
		}
		EXPECT_EQ(state.rules, rulesBefore);
		EXPECT_EQ(state.selected, selectedBefore);
	}
}

TEST(SerializedPerkAdmissionTest, OlderCodecRefusesCapturedCurrentPoliciesBeforeStatePrefix)
{
	auto state = canonicalState();
	for(const bool reference : {false, true})
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
		if(reference) EXPECT_THROW(perkWriteReference::write(memory.oser, state), std::runtime_error);
		else EXPECT_THROW(memory.oser & state, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	}
}

TEST(SerializedPerkAdmissionTest, RawWeightedAPIStillAdmitsEveryRowAndChecksEveryFeatureGate)
{
	auto state = canonicalState();
	EXPECT_NO_THROW(validateWeightedPerkSerialization(state.rules, true, true, true, true, true));
	{
		const ValidatedPerkRules admitted(state.rules);
		for(const int unsupported : {0, 1, 2, 3, 4})
		{
			SCOPED_TRACE(unsupported);
			EXPECT_THROW(admitted.validateWeightedSerialization(unsupported != 0, unsupported != 1,
				unsupported != 2, unsupported != 3, unsupported != 4), std::runtime_error);
			EXPECT_THROW(validateWeightedPerkSerialization(state.rules, unsupported != 0, unsupported != 1,
				unsupported != 2, unsupported != 3, unsupported != 4), std::runtime_error);
		}
	}
	state.rules["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
	EXPECT_THROW(validateWeightedPerkSerialization(state.rules, true, true, true, true, true), std::runtime_error);
}

TEST(SerializedPerkAdmissionTest, NewOperationAndEqualSecondHeroCannotReuseTheFirstAdmission)
{
	auto first = canonicalState();
	auto second = first;
	EXPECT_EQ(write(first, ESerializationVersion::CURRENT, false), write(second, ESerializationVersion::CURRENT, false));
	second.rules["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
	EXPECT_THROW(write(second, ESerializationVersion::CURRENT, false), std::runtime_error);
	EXPECT_NO_THROW(write(first, ESerializationVersion::CURRENT, false));
	first.rules = second.rules;
	EXPECT_THROW(write(first, ESerializationVersion::CURRENT, false), std::runtime_error);
}
