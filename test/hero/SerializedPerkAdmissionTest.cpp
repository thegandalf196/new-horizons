/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../PerkStateFixture.h"
#include "PerkStateWriteReference.h"
#include "../server/LegacyEstatesCapture.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
using namespace newHorizonsHeroes;
PerkState canonicalState()
{
	PerkState state;
	// The frozen reference represents the released unified-policy admission,
	// not the successor four-offer/Estates rules installed for fresh games.
	auto rules = estatesTest::legacyEstatesPerks();
	state.replaceRules(std::move(rules));
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

TEST(SerializedPerkAdmissionTest, CurrentWriterMatchesOriginalBytesForHistoricalCapturedTierPrefixes)
{
	auto state = canonicalState();
	const auto prefix = activePrefix(state.getRules());
	const auto rngBefore = rngBytes();
	for(size_t count = 0; count <= prefix.size(); ++count)
	{
		SCOPED_TRACE(count);
		perkStateFixture::editSelected(state, [&](auto & selected) { selected.assign(prefix.begin(), prefix.begin() + count); });
		const auto rulesBefore = state.getRules();
		const auto selectedBefore = state.getSelected();
		EXPECT_EQ(write(state, ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT, false),
			write(state, ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT, true));
		EXPECT_EQ(state.getRules(), rulesBefore);
		EXPECT_EQ(state.getSelected(), selectedBefore);
	}
	EXPECT_EQ(rngBytes(), rngBefore);
}

TEST(SerializedPerkAdmissionTest, NullAndEmptyHistoricalRulesKeepCurrentAndEarlierCodecBytes)
{
	for(const bool emptyObject : {false, true})
		for(const auto version : {ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT, ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS})
		{
			PerkState state;
			if(emptyObject) { JsonNode empty; empty.Struct(); state.replaceRules(std::move(empty)); }
			const auto before = state.getRules();
			EXPECT_EQ(write(state, version, false), write(state, version, true));
			EXPECT_EQ(state.getRules(), before);
			EXPECT_TRUE(state.getSelected().empty());
		}
}

TEST(SerializedPerkAdmissionTest, UnrequestedMalformedRowAndIllegalSelectionsRefuseBeforeStatePrefix)
{
	for(const int failure : {0, 1, 2})
	{
		SCOPED_TRACE(failure);
		auto state = canonicalState();
		const auto before = state.toJson();
		const auto prefix = activePrefix(state.getRules());
		auto payload = before;
		if(failure == 0)
		{
			auto malformed = state.getRules();
			malformed["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
			EXPECT_THROW(state.replaceRules(malformed), std::runtime_error);
			payload["rules"] = malformed;
		}
		else
		{
			const std::vector<PerkSelection> malformed = failure == 1
				? std::vector<PerkSelection>{prefix[0], prefix[0]}
				: std::vector<PerkSelection>{{"missing:skill", "missing:perk"}};
			EXPECT_THROW(state.replaceSelected(malformed), std::runtime_error);
			for(const auto & selection : malformed)
			{
				JsonNode row;
				row["skillId"].String() = selection.skillId;
				row["perkId"].String() = selection.perkId;
				payload["selected"].Vector().push_back(row);
			}
		}
		EXPECT_THROW(PerkState::fromJson(payload), std::runtime_error);
		EXPECT_EQ(state.toJson(), before);
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
	EXPECT_NO_THROW(validateWeightedPerkSerialization(state.getRules(), true, true, true, true, true));
	{
		const ValidatedPerkRules admitted(state.getRules());
		for(const int unsupported : {0, 1, 2, 3, 4})
		{
			SCOPED_TRACE(unsupported);
			EXPECT_THROW(admitted.validateWeightedSerialization(unsupported != 0, unsupported != 1,
				unsupported != 2, unsupported != 3, unsupported != 4), std::runtime_error);
			EXPECT_THROW(validateWeightedPerkSerialization(state.getRules(), unsupported != 0, unsupported != 1,
				unsupported != 2, unsupported != 3, unsupported != 4), std::runtime_error);
		}
	}
	auto malformed = state.getRules();
	malformed["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
	EXPECT_THROW(validateWeightedPerkSerialization(malformed, true, true, true, true, true), std::runtime_error);
}

TEST(SerializedPerkAdmissionTest, NewOperationAndEqualSecondHeroCannotReuseTheFirstAdmission)
{
	auto first = canonicalState();
	auto second = first;
	EXPECT_EQ(write(first, ESerializationVersion::CURRENT, false), write(second, ESerializationVersion::CURRENT, false));
	auto invalid = second.getRules();
	invalid["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid-tier";
	EXPECT_THROW(second.replaceRules(invalid), std::runtime_error);
	EXPECT_THROW(first.replaceRules(invalid), std::runtime_error);
	EXPECT_EQ(first.getRules(), second.getRules());
	EXPECT_NO_THROW(write(first, ESerializationVersion::CURRENT, false));
	EXPECT_NO_THROW(write(second, ESerializationVersion::CURRENT, false));
}
