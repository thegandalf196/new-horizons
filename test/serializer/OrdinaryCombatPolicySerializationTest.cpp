/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedSpecialtySnapshotFixture.h"
#include "CurrentOrdinaryOwnerFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
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
constexpr auto combatEpoch = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
constexpr std::array<Version, 6> priorEpochs{{static_cast<Version>(1145), static_cast<Version>(1146), static_cast<Version>(1147), static_cast<Version>(1148), static_cast<Version>(1149), static_cast<Version>(1150)}};
struct CombatPolicy { const char * key; const char * hero; const char * name; int value; };
constexpr std::array<CombatPolicy, 1> combatPolicies{{
	{"shieldAndSwordPhysicalReductionPercent", "core:sorsha", "Sword and Shield", 40}}};
JsonNode & policy(JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
const JsonNode & policy(const JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
void install(JsonNode & rules, const CombatPolicy & row, int value)
{
	policy(rules)[row.key].Integer() = value;
}

JsonNode priorRules(bool includeFour = false, bool includeMachine = false)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	historicalHeroRulesTest::omitPost1152OrdinarySpecialties(rules);
	for(const auto & entry : combatPolicies) policy(rules).Struct().erase(entry.key);
	if(!includeMachine)
		for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
			policy(rules).Struct().erase(key);
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
template<class Decode> void expectCombatReadFailure(Decode decode)
{
	try { decode(); FAIL() << "Expected1151 captured policy rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Captured ordinary combat specialties require format1151");
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
			expectCombatReadFailure([&] { owner.serialize(wire.oser); });
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(owner.serialize(wire.oser));
			EXPECT_FALSE(wire.extractBuffer().empty());
		}
	}
}
TEST(OrdinaryCombatAdmission, NamedFlagAndExplicitZeroRemainStrictTo1151)
{
	static_assert(static_cast<int>(combatEpoch) == 1151);
	static_assert(Version::CURRENT >= combatEpoch);
	EXPECT_FALSE(newHorizonsHeroes::SpecialtySerializationContext{}.ordinaryCombatSpecialties);
	for(const auto epoch : priorEpochs) EXPECT_FALSE(context(epoch).ordinaryCombatSpecialties);
	EXPECT_TRUE(context(combatEpoch).ordinaryCombatSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryCombatSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1146)).ordinaryCreaturePolicyExtensions);
	for(const auto & row : combatPolicies)
		for(const int value : {0, row.value})
		{
			auto rules = priorRules();
			install(rules, row, value);
			const auto before = rules;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			for(const auto epoch : priorEpochs)
				expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)); });
			EXPECT_EQ(rules, before);
		}
}

TEST(OrdinaryCombatAdmission, UnchangedPriorPoliciesKeepTheirOwnBoundaries)
{
	auto rules = priorRules();
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))), std::runtime_error);
	ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))));
	policy(rules)["prismaticScreenReductionPercent"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))), std::runtime_error);
	for(const auto epoch : {static_cast<Version>(1146), static_cast<Version>(1147), Version::CURRENT})
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)));
}

TEST(OrdinaryCombatAdmission, Minimal1144RejectsUnchangedCombatKeysIncludingZero)
{
	for(const auto & row : combatPolicies)
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
			expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))); });
		}
}

TEST(OrdinaryCombatAdmission, CurrentStillRejectsBadCombatShapesAndUnknownKey)
{
	for(const auto & row : combatPolicies)
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
			case 1: bad.Integer() = 101; break;
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

TEST(OrdinaryCombatAdmission, SettingsCurrentKeepsZeroAndOldReadRejectsBeforeInstalling)
{
	for(const auto & row : combatPolicies)
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
		expectCombatReadFailure([&] { target.serialize(old.iser); });
		EXPECT_EQ(encode(target), before);
	}
}

TEST(OrdinaryCombatAdmission, UnchangedCombatKeysDoNotWidenPriorFamilyGates)
{
	for(const auto & row : combatPolicies)
	{
		auto rules = priorRules(true, true);
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1150))));
		install(rules, row, 0);
		expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1150))); });
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
	}
	EXPECT_TRUE(context(static_cast<Version>(1149)).ordinaryMaintenanceSpecialties);
	EXPECT_FALSE(context(static_cast<Version>(1149)).ordinaryMachineSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1150)).ordinaryMachineSpecialties);
	EXPECT_FALSE(context(static_cast<Version>(1150)).ordinaryCombatSpecialties);
}

constexpr CurrentOrdinaryPolicyCase currentCases[] = {
	{"core:sorsha", "shieldAndSwordPhysicalReductionPercent", "Sword and Shield", 40},
	{"core:straker", "undyingHealingHP", "Undying", 100},
	{"core:yog", "cyclopeanStunChancePercent", "Concussive Shot", 25},
	{"core:ufretin", "runicReflectionPolicyVersion", "Runic Endurance", 1},
};
class OrdinaryCombatPolicySerializationTest : public CurrentOrdinaryOwnerFixture, public ::testing::WithParamInterface<int> {};
TEST_P(OrdinaryCombatPolicySerializationTest, Current1157RoundtripsHeroPoolMapSettingsWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentRoundtrips());
}
TEST_P(OrdinaryCombatPolicySerializationTest, CompleteCurrentOwnersAndFileAdmissionRefuseEarlierFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentOwnersRefuseOlderFormats());
}
TEST_P(OrdinaryCombatPolicySerializationTest, MalformedCurrentHeroAndWorldReadersRejectWithoutChangingTheirSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(malformedCurrentOwnersRejectWithoutChangingTheSource());
}
TEST_P(OrdinaryCombatPolicySerializationTest, CurrentAuthoredOmissionDoesNotInventAnOrdinaryRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()], true));
	ASSERT_NO_FATAL_FAILURE(currentAuthoredOmissionSurvivesRoundtrip());
}
INSTANTIATE_TEST_SUITE_P(CurrentNativeProfiles, OrdinaryCombatPolicySerializationTest, ::testing::Range(0, 4));
}
