/*
 * NewHorizonsRecoverySpecialtyRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode recoveryRules()
{
	auto rules = testHeroRules();
	auto & table = rules["recoverySpecialties"];
	table["version"].Integer() = 1;
	for(const auto key : {"infernalWellspring", "ritualReserve", "deepTrance"})
	{
		table[key]["dailyPercentPerLevel"].Integer() = 5;
		table[key]["dailyMaximumPercent"].Integer() = 100;
	}
	table["infernalWellspring"]["victoryPercent"].Integer() = 25;
	table["ritualReserve"]["lowNormalPercent"].Integer() = 50;
	table["deepTrance"]["firstCombatPercent"].Integer() = 50;
	return rules;
}
}
TEST(NewHorizonsRecoverySpecialtyRules, CaptureCopiesOnlyPresentRowsAndPreservesHistoricalAbsence)
{
	auto source = recoveryRules();
	source["recoverySpecialties"].Struct().erase("deepTrance");
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(recoverySpecialtyRules(captured, RecoverySpecialtyKind::INFERNAL_WELLSPRING));
	EXPECT_EQ(recoverySpecialtyRules(captured, RecoverySpecialtyKind::INFERNAL_WELLSPRING)->victoryPercent, 25);
	EXPECT_FALSE(recoverySpecialtyRules(captured, RecoverySpecialtyKind::DEEP_TRANCE));
	source["recoverySpecialties"]["infernalWellspring"]["victoryPercent"].Integer() = 80;
	EXPECT_EQ(recoverySpecialtyRules(captured, RecoverySpecialtyKind::INFERNAL_WELLSPRING)->victoryPercent, 25);
	auto legacy = captured;
	legacy.Struct().erase("recoverySpecialties");
	const auto before = legacy;
	EXPECT_NO_THROW(validateResolvedHeroRules(legacy));
	EXPECT_FALSE(recoverySpecialtyRules(legacy, RecoverySpecialtyKind::RITUAL_RESERVE));
	EXPECT_EQ(legacy, before);
}
TEST(NewHorizonsRecoverySpecialtyRules, StrictVersionedRowsRejectMalformedUnknownAndUnsafeValues)
{
	const auto valid = resolveHeroRules(recoveryRules(), HeroClassID(0));
	std::vector<JsonNode> bad(9, valid);
	bad[0]["recoverySpecialties"] = JsonNode();
	bad[1]["recoverySpecialties"]["version"].Integer() = 2;
	bad[2]["recoverySpecialties"]["deepTrance"] = JsonNode();
	bad[3]["recoverySpecialties"]["ritualReserve"].Struct().erase("dailyMaximumPercent");
	bad[4]["recoverySpecialties"]["deepTrance"]["dailyPercentPerLevel"].Integer() = 1001;
	bad[5]["recoverySpecialties"]["deepTrance"]["dailyMaximumPercent"].Integer() = 201;
	bad[6]["recoverySpecialties"]["infernalWellspring"]["victoryPercent"].Integer() = 1001;
	bad[7]["recoverySpecialties"]["ritualReserve"]["lowNormalPercent"].Float() = 0.5;
	bad[8]["recoverySpecialties"]["deepTrance"]["skipFirstFullCombat"].Bool() = true;
	for(const auto & rules : bad)
	{
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
		EXPECT_THROW(recoverySpecialtyRules(rules, RecoverySpecialtyKind::INFERNAL_WELLSPRING), std::runtime_error);
	}
}
TEST(NewHorizonsRecoverySpecialtyRules, ExplicitZeroIsCapturedAndOlderCapabilityRejectsPresentPolicy)
{
	auto source = recoveryRules();
	for(const auto key : {"infernalWellspring", "ritualReserve", "deepTrance"})
	{
		source["recoverySpecialties"][key]["dailyPercentPerLevel"].Integer() = 0;
		source["recoverySpecialties"][key]["dailyMaximumPercent"].Integer() = 0;
	}
	source["recoverySpecialties"]["infernalWellspring"]["victoryPercent"].Integer() = 0;
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(recoverySpecialtyRules(captured, RecoverySpecialtyKind::INFERNAL_WELLSPRING));
	EXPECT_EQ(recoverySpecialtyRules(captured, RecoverySpecialtyKind::INFERNAL_WELLSPRING)->victoryPercent, 0);
	EXPECT_THROW(validateRecoverySpecialtiesSerialization(captured, false), std::runtime_error);
	auto absent = captured;
	absent.Struct().erase("recoverySpecialties");
	EXPECT_NO_THROW(validateRecoverySpecialtiesSerialization(absent, false));
}

TEST(NewHorizonsRecoverySpecialtyRules, ReplayPredecessorPolicyGuardRejectsBeforePrefixAndCurrentRetainsCapture)
{
	const auto rules = recoveryRules();
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	current.oser.version = current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(source.serialize(current.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	auto absent = rules;
	absent.Struct().erase("recoverySpecialties");
	GameSettings historical;
	historical.addOverride(EGameSettings::HEROES_NEW_HORIZONS, absent);
	CMemorySerializer prior;
	prior.oser.version = prior.iser.version = old.oser.version;
	ASSERT_NO_THROW(historical.serialize(prior.oser));
	ASSERT_NO_THROW(decoded.serialize(prior.iser));
	const auto & loaded = decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_TRUE(loaded["recoverySpecialties"].isNull());
	EXPECT_FALSE(recoverySpecialtyRules(loaded, RecoverySpecialtyKind::DEEP_TRANCE));
}
