/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedSpecialtySnapshotFixture.h"
#include "CurrentOrdinaryOwnerFixture.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include <array>
#include <tuple>
#include <string_view>

namespace
{
using Version = ESerializationVersion;
using capturedSpecialtySnapshotTest::constructedSnapshot;
using capturedSpecialtySnapshotTest::encode;
constexpr auto machineEpoch = Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES;
constexpr std::array<Version, 5> priorEpochs{{static_cast<Version>(1145), static_cast<Version>(1146), static_cast<Version>(1147), static_cast<Version>(1148), static_cast<Version>(1149)}};
struct MachinePolicy { const char * key; const char * hero; const char * name; int value; };
constexpr std::array<MachinePolicy, 1> machinePolicies{{
	{"heavyBatterySiegeTermBonusPercent", "core:gurnisson", "Heavy Battery", 25}}};
JsonNode & policy(JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
const JsonNode & policy(const JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
void install(JsonNode & rules, const MachinePolicy & row, int value)
{
	policy(rules)[row.key].Integer() = value;
}

JsonNode priorRules(bool includeFour = false)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	historicalHeroRulesTest::omitPost1152OrdinarySpecialties(rules);
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "undyingHealingHP", "cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
		policy(rules).Struct().erase(key);
	for(const auto & entry : machinePolicies) policy(rules).Struct().erase(entry.key);
	if(!includeFour)
		for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercentPerHex",
			"ridersExploitDamagePercent", "risingTideCreaturesPerRound"}) policy(rules).Struct().erase(key);
	// Redesigned fixed-value recipes are covered separately at CURRENT.
	for(const char * key : {"packPursuitSpeed", "undertowDefensePerAttack", "goblinFrenzyDamagePercent",
		"evasiveFlightReductionPercentPerHex", "risingTideCreaturesPerRound", "undyingHealingHP"})
		policy(rules).Struct().erase(key);
	rules.setOverrideFlag(true);
	return rules;
}
newHorizonsHeroes::SpecialtySerializationContext context(Version epoch)
{
	CMemorySerializer wire;
	wire.oser.version = epoch;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
}
template<class Decode> void expectMachineReadFailure(Decode decode)
{
	try { decode(); FAIL() << "Expected1150 captured policy rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Captured ordinary machine specialties require format1150");
	}
}
template<class T> void expectOldOwner(T & owner, bool reject)
{
	for(const auto epoch : priorEpochs)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		if(reject)
		{
			expectMachineReadFailure([&] { owner.serialize(wire.oser); });
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(owner.serialize(wire.oser));
			EXPECT_FALSE(wire.extractBuffer().empty());
		}
	}
}
TEST(OrdinaryMachineAdmission, NamedFlagAndExplicitZeroRemainStrictTo1150)
{
	static_assert(static_cast<int>(machineEpoch) == 1150);
	static_assert(Version::CURRENT >= machineEpoch);
	EXPECT_FALSE(newHorizonsHeroes::SpecialtySerializationContext{}.ordinaryMachineSpecialties);
	for(const auto epoch : priorEpochs) EXPECT_FALSE(context(epoch).ordinaryMachineSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryMachineSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1146)).ordinaryCreaturePolicyExtensions);
	for(const auto & row : machinePolicies)
		for(const int value : {0, row.value})
		{
			auto rules = priorRules();
			install(rules, row, value);
			const auto before = rules;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			for(const auto epoch : priorEpochs)
				expectMachineReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)); });
			EXPECT_EQ(rules, before);
		}
}

TEST(OrdinaryMachineAdmission, UnchangedPriorPoliciesKeepTheirOwnBoundaries)
{
	auto rules = priorRules();
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))), std::runtime_error);
	ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))));
	policy(rules)["prismaticScreenReductionPercent"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))), std::runtime_error);
	for(const auto epoch : {static_cast<Version>(1146), static_cast<Version>(1147), Version::CURRENT})
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)));
}

TEST(OrdinaryMachineAdmission, Minimal1144RejectsTheSurvivingMachineCoefficientIncludingZero)
{
	for(const auto & row : machinePolicies)
		for(const int value : {0, row.value})
		{
			JsonNode rules;
			rules["additionalSpecialties"]["version"].Integer() = 1;
			auto & minimal = policy(rules);
			minimal["version"].Integer() = 1;
			minimal["marshSharpshooter"].Integer() = 1;
			for(const char * key : {"retaliationDamagePercent", "maximumHealthPercent", "resistanceAuraPoints"})
				minimal[key].Integer() = 0;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))));
			install(rules, row, value);
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			expectMachineReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))); });
		}
}

TEST(OrdinaryMachineAdmission, CurrentStillRejectsBadMachineShapesAndUnknownKey)
{
	for(const auto & row : machinePolicies)
		for(int kind = 0; kind < 6; ++kind)
		{
			auto rules = priorRules();
			install(rules, row, row.value);
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			auto & bad = policy(rules)[row.key];
			bad = JsonNode();
			switch(kind)
			{
			case 0: bad.Integer() = -1; break;
			case 1: bad.Integer() = std::string_view(row.key) == "gunneryProfessorFollowUpBonusPoints" ? 41 : 101; break;
			case 2: bad.Bool() = false; break;
			case 3: break;
			case 4: bad.Float() = 1.5; break;
			case 5: bad.String() = "12"; break;
			}
			EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
		}
	auto unknown = priorRules();
	policy(unknown)["unreservedFutureRangedKey"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(unknown), std::runtime_error);
}

TEST(OrdinaryMachineAdmission, SettingsCurrentKeepsZeroAndOldReadRejectsBeforeInstalling)
{
	for(const auto & row : machinePolicies)
	{
		auto rules = priorRules();
		install(rules, row, 0);
		GameSettings source;
		source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
		expectOldOwner(source, true);
		const auto bytes = encode(source);
		CMemorySerializer good(bytes);
		GameSettings current;
		ASSERT_NO_THROW(current.serialize(good.iser));
		EXPECT_EQ(current.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
		GameSettings target;
		target.addOverride(EGameSettings::HEROES_NEW_HORIZONS, priorRules());
		const auto before = encode(target);
		CMemorySerializer old(bytes);
		old.iser.version = priorEpochs.back();
		expectMachineReadFailure([&] { target.serialize(old.iser); });
		EXPECT_EQ(encode(target), before);
	}
}

TEST(OrdinaryMachineAdmission, FieryShotCurrentFlagRequires1157AndRetiredGunneryPairIsRejected)
{
	auto rules = currentOrdinaryOwnerTest::leafRules();
	auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["incendiaryShotPolicyVersion"].Integer() = 1;
	EXPECT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS)), std::runtime_error);
	for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints"})
	{
		auto retired = currentOrdinaryOwnerTest::leafRules();
		retired["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 2;
		EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(retired), std::runtime_error);
	}
}

constexpr CurrentOrdinaryPolicyCase currentCases[] = {
	{"core:torosar", "incendiaryShotPolicyVersion", "Fiery Shot", 1},
	{"core:gurnisson", "heavyBatterySiegeTermBonusPercent", "Heavy Battery", 25},
};
class OrdinaryMachinePolicySerializationTest : public CurrentOrdinaryOwnerFixture, public ::testing::WithParamInterface<int> {};
TEST_P(OrdinaryMachinePolicySerializationTest, Current1157RoundtripsHeroPoolMapSettingsWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentRoundtrips());
}
TEST_P(OrdinaryMachinePolicySerializationTest, CompleteCurrentOwnersAndFileAdmissionRefuseEarlierFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentOwnersRefuseOlderFormats());
}
TEST_P(OrdinaryMachinePolicySerializationTest, MalformedCurrentHeroAndWorldReadersRejectWithoutChangingTheirSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(malformedCurrentOwnersRejectWithoutChangingTheSource());
}
TEST_P(OrdinaryMachinePolicySerializationTest, CurrentAuthoredOmissionDoesNotInventAnOrdinaryRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()], true));
	ASSERT_NO_FATAL_FAILURE(currentAuthoredOmissionSurvivesRoundtrip());
}
INSTANTIATE_TEST_SUITE_P(CurrentNativeProfiles, OrdinaryMachinePolicySerializationTest, ::testing::Range(0, 2));
}
