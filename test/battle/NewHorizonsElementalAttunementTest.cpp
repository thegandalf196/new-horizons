/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/combatScripts/NativeDamageCalculator.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <limits>

namespace attunement = newHorizonsElementalAttunement;

TEST(ElementalAttunementPolicyTest, StrictPolicyDoesNotAdoptAbsentOrMalformedCapturedRules)
{
	JsonNode rules;
	EXPECT_FALSE(attunement::usesRules(rules));
	rules[attunement::POLICY_KEY]["version"].Integer() = 1;
	EXPECT_TRUE(attunement::usesRules(rules));
	EXPECT_NO_THROW(attunement::validatePolicy(rules));
	EXPECT_THROW(attunement::validatePolicySerialization(rules, false), std::runtime_error);
	rules[attunement::POLICY_KEY]["extra"].Bool() = true;
	EXPECT_THROW(attunement::validatePolicy(rules), std::runtime_error);
}

TEST(ElementalAttunementPolicyTest, TagsAreIndependentOfSchoolsAndAreaRequiresExplicitTag)
{
	JsonNode rules;
	rules["spells"]["test:fire"]["elementalTag"].String() = "fire";
	rules["spells"]["test:fire"]["elementalAreaDamage"].Bool() = true;
	EXPECT_NO_THROW(attunement::validateSpellMetadata(rules));
	EXPECT_TRUE(attunement::hasSpellMetadata(rules));
	EXPECT_THROW(attunement::validateSpellMetadataSerialization(rules, false), std::runtime_error);
	rules["spells"]["test:fire"]["elementalTag"].String() = "havoc";
	EXPECT_THROW(attunement::validateSpellMetadata(rules), std::runtime_error);
	rules["spells"]["test:fire"].Struct().erase("elementalTag");
	EXPECT_THROW(attunement::validateSpellMetadata(rules), std::runtime_error);
}

TEST(ElementalAttunementPolicyTest, FourFlagsAreIdempotentAndBounded)
{
	uint8_t state = 0;
	for(const uint8_t bit : {1, 2, 4, 8})
	{
		state = attunement::activatedMask(state, bit);
		EXPECT_EQ(attunement::activatedMask(state, bit), state);
	}
	EXPECT_EQ(state, 15);
	EXPECT_EQ(attunement::activatedMask(state, 0), state);
	EXPECT_THROW(attunement::activatedMask(16, 1), std::invalid_argument);
	EXPECT_THROW(attunement::activatedMask(0, 16), std::invalid_argument);
}

TEST(ElementalAttunementPolicyTest, StoneboundUsesExactBasisPointsAndFixedFifteenPercentCap)
{
	for(const auto [power, expected] : {std::pair{10, 250}, {20, 500}, {30, 750},
		{40, 1000}, {50, 1250}, {60, 1500}, {80, 1500}})
		EXPECT_EQ(attunement::stoneboundReductionBasisPoints(power), expected);
	EXPECT_EQ(attunement::stoneboundReductionBasisPoints(-1), 0);
	EXPECT_EQ(attunement::stoneboundReductionBasisPoints(std::numeric_limits<int>::max()), 1500);
}

TEST(ElementalAttunementPolicyTest, PhysicalProtectionMultipliesWithIndependentReduction)
{
	DamageAttackInfo input;
	input.physicalDamageReductionCapPercent = 80;
	input.stoneboundPhysicalReductionBasisPoints = 1500;
	input.newHorizonsArmorerReductionPercent = 20;
	nativeDamage::Facts facts;
	facts.attacker.count = 1;
	facts.attacker.minDamage = facts.attacker.maxDamage = 100;
	facts.defender.count = 100;
	facts.defender.firstHealth = facts.defender.maxHealth = 100;
	EXPECT_EQ(nativeDamage::calculate(input, facts).damage.min, 68);
	input.physicalDamage = false;
	input.newHorizonsArmorerReductionPercent = 0;
	EXPECT_EQ(nativeDamage::calculate(input, facts).damage.min, 100);
}

TEST(ElementalAttunementPolicyTest, ProfileAndFourFlagsRoundTripWithoutRefillOrNewCounters)
{
	attunement::Profile original;
	original.rank = 3;
	original.magmaWeapons = true;
	original.primalVigilance = true;
	original.stoneboundMagic = true;
	original.stoneboundBasisPoints = 1500;
	uint8_t mask = 6;
	CMemorySerializer memory;
	memory.oser & original;
	memory.oser & mask;
	attunement::Profile restored;
	uint8_t restoredMask = 0;
	memory.iser & restored;
	memory.iser & restoredMask;
	EXPECT_EQ(restored, original);
	EXPECT_EQ(restoredMask, mask);
	restored.rank = 0;
	EXPECT_THROW(restored.validate(), std::runtime_error);
}

TEST(ElementalAttunementPolicyTest, FullCurrentRegistryCannotSilentlyDropItsFactionPolicy)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_NO_THROW(attunement::validatePolicy(rules));
	rules.Struct().erase(attunement::POLICY_KEY);
	EXPECT_THROW(attunement::validatePolicy(rules), std::runtime_error);
	JsonNode authoredPartial;
	authoredPartial["skills"][attunement::SKILL_ID]["name"].String() = "Elemental Attunement";
	EXPECT_NO_THROW(attunement::validatePolicy(authoredPartial));
}

TEST(ElementalAttunementPolicyTest, DamageMagnitudeAddsConduitToExistingEffectivenessExactlyOnce)
{
	newHorizonsMetamagic::CastProfile cast;
	cast.perks.wisdomEffectivenessBonus = 20;
	cast.factionSpellDamageBonus = 15;
	EXPECT_EQ(newHorizonsMetamagic::outgoingDamageMagnitude(100, &cast, nullptr), 135);
	cast.factionSpellDamageBonus = 40;
	EXPECT_EQ(newHorizonsMetamagic::outgoingDamageMagnitude(100, &cast, nullptr), 160);
	cast.factionSpellDamageBonus = 0;
	EXPECT_EQ(newHorizonsMetamagic::outgoingDamageMagnitude(100, &cast, nullptr),
		newHorizonsMetamagic::outgoingMagnitude(100, &cast, nullptr));
}

TEST(ElementalAttunementPolicyTest, CapturedDeferredOffenseRoundTripsAndCannotCompoundStoredMagnitude)
{
	JsonNode snapshot;
	snapshot["factionSpellDamageBonusPercent"].Integer() = 15;
	auto captured = newHorizonsMetamagic::capturedOffensiveProfile(&snapshot);
	EXPECT_EQ(newHorizonsMetamagic::outgoingDamageMagnitude(100, &captured, nullptr), 115);
	// Native descriptor parameters retain the established integral-double form.
	snapshot["factionSpellDamageBonusPercent"] = JsonNode(15.0);
	EXPECT_EQ(newHorizonsMetamagic::capturedOffensiveProfile(&snapshot).factionSpellDamageBonus, 15);
	snapshot["factionSpellDamageCapturedInMagnitude"].Bool() = true;
	CMemorySerializer memory;
	memory.oser & snapshot;
	JsonNode restored;
	memory.iser & restored;
	captured = newHorizonsMetamagic::capturedOffensiveProfile(&restored);
	for(int tick = 0; tick < 3; ++tick)
		EXPECT_EQ(newHorizonsMetamagic::outgoingDamageMagnitude(115, &captured, nullptr), 115);
	snapshot["factionSpellDamageCapturedInMagnitude"].Integer() = 1;
	EXPECT_THROW(newHorizonsMetamagic::capturedOffensiveProfile(&snapshot), std::runtime_error);
}

TEST(ElementalAttunementPolicyTest, PhysicalPeriodicDamageUsesCappedFloorRoundingWithoutOverflow)
{
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(23, 1500), 19);
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(20, 1500), 17);
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(18, 1500), 15); // Existing 20% mitigation came first.
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(100, 2000), 85);
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(100, -1), 100);
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(1, 1500), 0);
	EXPECT_EQ(attunement::stoneboundPhysicalDamage(std::numeric_limits<int64_t>::max(), 1500), 7839866231326559435LL);
}
