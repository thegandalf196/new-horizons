/*
 * NewHorizonsCommandEfficiencyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"

namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER[] = "new-horizons:command.aggressiveCommander";
constexpr char DEFENSIVE_COMMANDER[] = "new-horizons:command.defensiveCommander";
constexpr char VETERAN_COMMANDER[] = "new-horizons:command.veteranCommander";

JsonNode commandFormula(double base, double attack, double defense)
{
	JsonNode formula;
	formula["base"].Float() = base;
	formula["attack"].Float() = attack;
	formula["defense"].Float() = defense;
	return formula;
}

JsonNode legacyCommandRules()
{
	const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
	auto rules = config["combat"]["heroCommands"];
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = 1;
	rules["commands"].Struct().erase("focusFire");
	return rules;
}

class NewHorizonsCommandEfficiencyTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill commandSkill() const
	{
		const int decoded = SecondarySkill::decode(COMMAND_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void prepareCommand(int rank, int attack = 10, int defense = 20)
	{
		startGame();
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, attack, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, defense, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(commandSkill(), rank, ChangeValueMode::ABSOLUTE);
		startBattle();
		beginCombat();
	}

	void setCommandRank(int rank)
	{
		attackerSideHero->setSecSkillLevel(commandSkill(), rank, ChangeValueMode::ABSOLUTE);
	}

	void selectCommandPerk(const char * perkId)
	{
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, perkId});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, perkId));
	}
};
}

TEST_F(NewHorizonsCommandEfficiencyTest, AggressiveCommanderScalesOnlyAttackDerivedTerms)
{
	prepareCommand(MasteryLevel::BASIC);
	const auto bothAttributes = commandFormula(11, 1, 1);
	const auto attackOnly = commandFormula(11, 1, 0);
	const auto defenseOnly = commandFormula(11, 0, 1);
	const auto flatOnly = commandFormula(37, 0, 0);

	selectCommandPerk(AGGRESSIVE_COMMANDER);
	EXPECT_EQ(heroCommands::coefficient(bothAttributes, *attackerSideHero), 46);
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), 24);
	EXPECT_EQ(heroCommands::coefficient(defenseOnly, *attackerSideHero), 33);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero), 37);
}

TEST_F(NewHorizonsCommandEfficiencyTest, DefensiveCommanderScalesOnlyDefenseDerivedTerms)
{
	prepareCommand(MasteryLevel::BASIC);
	const auto bothAttributes = commandFormula(11, 1, 1);
	const auto attackOnly = commandFormula(11, 1, 0);
	const auto defenseOnly = commandFormula(11, 0, 1);
	const auto flatOnly = commandFormula(37, 0, 0);

	selectCommandPerk(DEFENSIVE_COMMANDER);
	EXPECT_EQ(heroCommands::coefficient(bothAttributes, *attackerSideHero), 48);
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), 22);
	EXPECT_EQ(heroCommands::coefficient(defenseOnly, *attackerSideHero), 37);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero), 37);
}

TEST_F(NewHorizonsCommandEfficiencyTest, RankAndWarcastingPointsRemainAdditiveForAttackTerms)
{
	prepareCommand(MasteryLevel::BASIC);
	selectCommandPerk(AGGRESSIVE_COMMANDER);
	const auto attackOnly = commandFormula(11, 1, 0);

	for(const int rank : {static_cast<int>(MasteryLevel::BASIC),
		static_cast<int>(MasteryLevel::ADVANCED), static_cast<int>(MasteryLevel::EXPERT)})
	{
		SCOPED_TRACE(rank);
		setCommandRank(rank);
		const int rankEfficiency = 100 + rank * 10;
		const int expected = 11 + 10 * (rankEfficiency + 20) / 100;
		EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), expected);
		const int warcastExpected = 11 + 10 * (rankEfficiency + 20 + 10) / 100;
		EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero, 10), warcastExpected);
	}
}

TEST_F(NewHorizonsCommandEfficiencyTest, CommanderPerkIsDormantWhenItsBasicRankIsLost)
{
	prepareCommand(MasteryLevel::BASIC);
	selectCommandPerk(AGGRESSIVE_COMMANDER);
	const auto attackOnly = commandFormula(11, 1, 0);
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), 24);

	setCommandRank(0);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), 21);
}

TEST_F(NewHorizonsCommandEfficiencyTest, AttributeTermsUseOneFinalRoundingStep)
{
	prepareCommand(MasteryLevel::BASIC, 1, 1);
	selectCommandPerk(AGGRESSIVE_COMMANDER);
	const auto fractionalTerms = commandFormula(0, 0.3, 0.3);

	// Aggressive Commander produces 0.39 Attack plus 0.33 Defense; neither term
	// rounds independently, while their shared sum rounds once to one.
	EXPECT_EQ(heroCommands::coefficient(fractionalTerms, *attackerSideHero), 1);
}

TEST_F(NewHorizonsCommandEfficiencyTest, VeteranCommanderOnlyScalesSecondWindLeadershipAndUsesOrderSnapshot)
{
	prepareCommand(MasteryLevel::BASIC);
	const auto capacityBefore = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(capacityBefore);
	selectCommandPerk(AGGRESSIVE_COMMANDER);
	setCommandRank(MasteryLevel::ADVANCED);
	selectCommandPerk(VETERAN_COMMANDER);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));

	const auto capacityAfter = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(capacityAfter);
	EXPECT_EQ(capacityAfter->capacity, capacityBefore->capacity);
	EXPECT_EQ(capacityAfter->used, capacityBefore->used);
	EXPECT_EQ(capacityAfter->movementPercent, capacityBefore->movementPercent);

	const auto attackOnly = commandFormula(11, 1, 0);
	const auto defenseOnly = commandFormula(11, 0, 1);
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero), 25);
	EXPECT_EQ(heroCommands::coefficient(defenseOnly, *attackerSideHero), 35);

	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 1;
	order.warcastingBonusPercent = 10;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(memory.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(memory.iser & restored);
	EXPECT_EQ(restored.warcastingBonusPercent, 10);
	EXPECT_EQ(heroCommands::coefficient(attackOnly, *attackerSideHero, restored.warcastingBonusPercent), 26);

	const int expectedLeadershipEfficiency = 120 + 25 + restored.warcastingBonusPercent;
	const int expectedSecondWind = std::clamp(50 + static_cast<int>(std::lround(
		0.015 * static_cast<double>(capacityAfter->capacity) * expectedLeadershipEfficiency / 100.0)), 0, 100);
	EXPECT_EQ(heroCommands::secondWindPercent(*attackerSideHero, restored.warcastingBonusPercent), expectedSecondWind);
}

TEST_F(NewHorizonsCommandEfficiencyTest, LegacyRulesDoNotGainCanonicalCommanderPerks)
{
	prepareCommand(MasteryLevel::BASIC);
	selectCommandPerk(AGGRESSIVE_COMMANDER);
	const auto rules = legacyCommandRules();
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::AGGRESSIVE));

	const auto effects = heroCommands::bonuses(rules, HeroCommand::CHARGE, *attackerSideHero);
	ASSERT_EQ(effects.size(), 1u);
	// The v1 saved formula's base 20 and 0.5 * Attack term retain the historical
	// rank-only result (25.5 rounds to 26), even with the new perk active.
	EXPECT_EQ(effects.front().val, 26);
}
