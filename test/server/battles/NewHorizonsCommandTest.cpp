/*
 * NewHorizonsCommandTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
struct CommandRankCase
{
	int rank;
	int chargePercent;
	int focusFirePercent;
};

class NewHorizonsCommandTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<CommandRankCase>
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

	SecondarySkill command() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:command");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void prepareRank(int rank, int attack)
	{
		startGame();
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, attack, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(command(), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:command"), rank);
		startBattle();
		beginCombat();
	}
};
}

TEST_P(NewHorizonsCommandTest, ScalesOnlyTheAttributeDerivedPartOfOrderFormulas)
{
	prepareRank(GetParam().rank, 50);
	const auto & commands = battle()->getHeroCommandRules()["commands"];

	// Charge is 10 constant + 20% of Attack. At 50 Attack its attribute
	// component is exactly 10, making the 110/120/130% rank scaling visible
	// without a rounding ambiguity. The base-only Flank component must stay 4.
	EXPECT_EQ(heroCommands::coefficient(commands["charge"]["effects"]["meleeDamagePercent"],
		*attackerSideHero), GetParam().chargePercent);
	EXPECT_EQ(heroCommands::coefficient(commands["flank"]["effects"]["additionalSidePercent"],
		*attackerSideHero), 4);
}

TEST_P(NewHorizonsCommandTest, AuthoritativeFocusFireStateUsesTheRankedCoefficient)
{
	prepareRank(GetParam().rank, 100);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(100), 100);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
			target->unitId())));
	const auto state = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->rangedDamagePercent, GetParam().focusFirePercent);
}

TEST_F(NewHorizonsCommandTest, RanksNeverIncreaseLeadershipOrLeadershipCapacity)
{
	startGame();
	const auto before = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(before);
	const auto leadershipBefore = attackerSideHero->valOfBonuses(BonusType::LEADERSHIP);

	for(const int rank : {MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(command(), rank, ChangeValueMode::ABSOLUTE);
		const auto after = attackerSideHero->getLeadershipCapacity();
		ASSERT_TRUE(after);
		EXPECT_EQ(after->capacity, before->capacity);
		EXPECT_EQ(after->used, before->used);
		EXPECT_EQ(after->movementPercent, before->movementPercent);
		EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LEADERSHIP), leadershipBefore);
	}
}

TEST_F(NewHorizonsCommandTest, SecondWindScalesOnlyLeadershipDerivedComponent)
{
	startGame();
	const auto capacity = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(capacity);

	for(const int rank : {0, static_cast<int>(MasteryLevel::BASIC),
		static_cast<int>(MasteryLevel::ADVANCED), static_cast<int>(MasteryLevel::EXPERT)})
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(command(), rank, ChangeValueMode::ABSOLUTE);
		const int efficiency = 100 + rank * 10;
		const int expected = std::clamp(50 + static_cast<int>(std::lround(
			0.015 * static_cast<double>(capacity->capacity) * efficiency / 100.0)), 0, 100);
		EXPECT_EQ(heroCommands::secondWindPercent(*attackerSideHero), expected);
	}
}

INSTANTIATE_TEST_SUITE_P(NoneBasicAdvancedExpert, NewHorizonsCommandTest,
	::testing::Values(
		CommandRankCase{0, 20, 20},
		CommandRankCase{MasteryLevel::BASIC, 21, 22},
		CommandRankCase{MasteryLevel::ADVANCED, 22, 23},
		CommandRankCase{MasteryLevel::EXPERT, 23, 25}));
