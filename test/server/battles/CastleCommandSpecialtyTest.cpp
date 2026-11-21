/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CUnitState.h"

namespace
{
using namespace consecratedArms;
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

class CastleCommandSpecialtyTest : public CastleSpecialtyTest
{
protected:
	CStack * archers = nullptr;
	CStack * crossbows = nullptr;
	CStack * target = nullptr;

	/// Orrin: two ready shooter stacks and a distant target; the pikeman acts first.
	void volleyArena(int level = 1, bool counterfire = false)
	{
		ASSERT_NO_FATAL_FAILURE(specialist("core:orrin", level));
		if(counterfire)
		{
			ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, "new-horizons:archery",
				"new-horizons:archery.counterfire", MasteryLevel::BASIC));
		}
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ATTACKER, "core:pikeman", BattleHex(2, 5), 5, 1000);
		archers = place(ATTACKER, "core:archer", BattleHex(1, 3), 20, 800);
		crossbows = place(ATTACKER, "core:marksman", BattleHex(1, 7), 20, 700);
		target = place(DEFENDER, counterfire ? "core:marksman" : "core:pikeman", BattleHex(14, 5), 200, 600);
		ASSERT_TRUE(active && archers && crossbows && target);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

	bool focusFire()
	{
		return submit(withPrayer(BattleAction::makeTargetedHeroCommand(ATTACKER, HeroCommand::FOCUS_FIRE, target->unitId())));
	}

	CStack * griffins = nullptr;
	CStack * firstShooter = nullptr;
	CStack * secondShooter = nullptr;
	CStack * thirdShooter = nullptr;

	/// Edric: Griffins far beyond their movement from three shooter stacks.
	void aerialArena(const char * griffin = "core:griffin")
	{
		ASSERT_NO_FATAL_FAILURE(specialist("core:edric", 10));
		ASSERT_NO_FATAL_FAILURE(emptyField());
		griffins = place(ATTACKER, griffin, BattleHex(1, 5), 30, 100);
		firstShooter = place(DEFENDER, "core:archer", BattleHex(15, 1), 30, 900);
		secondShooter = place(DEFENDER, "core:archer", BattleHex(15, 5), 30, 800);
		thirdShooter = place(DEFENDER, "core:archer", BattleHex(15, 9), 30, 700);
		ASSERT_TRUE(griffins && firstShooter && secondShooter && thirdShooter);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_LT(griffins->getMovementRange(0),
			BattleHex::getDistance(griffins->getPosition(), secondShooter->getPosition()));
	}

	int64_t shootGriffins(CStack * shooter)
	{
		const auto before = shooter->getAvailableHealth();
		activate(shooter);
		EXPECT_TRUE(submit(BattleAction::makeShotAttack(shooter, griffins), PlayerColor(1)));
		return before - shooter->getAvailableHealth();
	}
};
}

TEST_F(CastleCommandSpecialtyTest, VolleyCommandFiresEveryReadyShooterAndKeepsTheirTurn)
{
	ASSERT_NO_FATAL_FAILURE(volleyArena());
	const auto health = target->getAvailableHealth();
	const auto archerShots = archers->shots.available();
	const auto crossbowShots = crossbows->shots.available();
	ASSERT_TRUE(focusFire());
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_EQ(archers->shots.available(), archerShots - 1);
	EXPECT_EQ(crossbows->shots.available(), crossbowShots - 1);
	// The shooters keep their normal turn this round.
	EXPECT_TRUE(archers->willMove(0));
	EXPECT_TRUE(crossbows->willMove(0));
}

TEST_F(CastleCommandSpecialtyTest, VolleyShotsCannotProvokeCounterfireOrRepeat)
{
	// The target's hero has Counterfire, which answers an ordinary shot at a shooter.
	ASSERT_NO_FATAL_FAILURE(volleyArena(1, true));
	const auto archersHealth = archers->getAvailableHealth();
	const auto crossbowsHealth = crossbows->getAvailableHealth();
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(focusFire());
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_EQ(archers->getAvailableHealth(), archersHealth);
	EXPECT_EQ(crossbows->getAvailableHealth(), crossbowsHealth);
	// Focus Fire! is once per round, so a second volley this round is refused with it.
	EXPECT_FALSE(focusFire());
}

TEST_F(CastleCommandSpecialtyTest, VolleyDamageScalesWithLevel)
{
	ASSERT_NO_FATAL_FAILURE(volleyArena(1));
	EXPECT_EQ(newHorizonsHeroes::volleyCommandDamagePercent(attackerSideHero), 30);
	attackerSideHero->level = 20;
	EXPECT_EQ(newHorizonsHeroes::volleyCommandDamagePercent(attackerSideHero), 49);
	attackerSideHero->level = 35;
	EXPECT_EQ(newHorizonsHeroes::volleyCommandDamagePercent(attackerSideHero), 50);
}

TEST_F(CastleCommandSpecialtyTest, HistoricalOrrinIssuesAPlainFocusFire)
{
	stripped = {"volleyCommand"};
	ASSERT_NO_FATAL_FAILURE(volleyArena());
	EXPECT_EQ(newHorizonsHeroes::volleyCommandDamagePercent(attackerSideHero), 0);
	const auto health = target->getAvailableHealth();
	const auto shots = archers->shots.available();
	ASSERT_TRUE(focusFire());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(archers->shots.available(), shots);
}

TEST_F(CastleCommandSpecialtyTest, RevisedAerialSupremacyCountersAtAnyRangeOncePerRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(aerialArena());
	ASSERT_TRUE(newHorizonsHeroes::revisedAerialSupremacyRule(attackerSideHero));
	// Ordinary Griffins have two retaliations: two counters, then none.
	EXPECT_GT(shootGriffins(firstShooter), 0);
	EXPECT_GT(shootGriffins(secondShooter), 0);
	EXPECT_EQ(shootGriffins(thirdShooter), 0);
	EXPECT_EQ(griffins->getPosition(), BattleHex(1, 5)) << "The Griffins fly back after each counter";
}

TEST_F(CastleCommandSpecialtyTest, RevisedAerialSupremacyRoyalGriffinsCounterEveryShot)
{
	ASSERT_NO_FATAL_FAILURE(aerialArena("core:royalGriffin"));
	EXPECT_GT(shootGriffins(firstShooter), 0);
	EXPECT_GT(shootGriffins(secondShooter), 0);
	EXPECT_GT(shootGriffins(thirdShooter), 0);
}

TEST_F(CastleCommandSpecialtyTest, RevisedAerialCounterDamageGrowsWithLevel)
{
	ASSERT_NO_FATAL_FAILURE(aerialArena());
	EXPECT_EQ(newHorizonsHeroes::aerialCounterDamagePercent(attackerSideHero), 18);
	attackerSideHero->level = 25;
	EXPECT_EQ(newHorizonsHeroes::aerialCounterDamagePercent(attackerSideHero), 40);
	BattleAttackInfo plain(griffins, secondShooter, 0, false);
	plain.retaliation = true;
	auto boosted = plain;
	boosted.aerialCounterDamagePercent = 40;
	const auto ordinary = battle()->calculateDmgRange(plain);
	const auto increased = battle()->calculateDmgRange(boosted);
	EXPECT_GT(increased.damage.max, ordinary.damage.max);
	EXPECT_NEAR(static_cast<double>(increased.damage.max), ordinary.damage.max * 1.4, ordinary.damage.max * 0.02 + 1);
}

TEST_F(CastleCommandSpecialtyTest, HistoricalAerialSupremacyKeepsItsMovementRange)
{
	stripped = {"aerialSupremacy"};
	ASSERT_NO_FATAL_FAILURE(aerialArena());
	EXPECT_FALSE(newHorizonsHeroes::revisedAerialSupremacyRule(attackerSideHero));
	EXPECT_EQ(shootGriffins(secondShooter), 0) << "Out of movement range under the historical rule";
}
