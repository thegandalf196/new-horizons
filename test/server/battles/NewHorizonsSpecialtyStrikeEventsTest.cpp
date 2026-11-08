/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyEventFixture.h"
#include "NewHorizonsMeleeSpecialtyFixture.h"
#include "../../../lib/battle/NewHorizonsShieldAndSword.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class NewHorizonsSpecialtyStrikeEventsTest : public NewHorizonsSpecialtyEventFixture {};
TEST_F(NewHorizonsSpecialtyStrikeEventsTest, UndertowAccumulatesOnAcceptedZeroDamageAndRetaliation)
{
	prepare("core:lacus", "core:iceElemental");
	const int before = actor->getDefense(false);
	BattleAttackInfo incoming(enemy, actor, 0, false);
	BattleStackAttacked accepted;
	accepted.stackAttacked = actor->unitId();
	accepted.damageAmount = 0;
	for(int strike = 1; strike <= 3; ++strike)
	{
		incoming.retaliation = strike == 2;
		const auto staged = battlePrimaryStrikeAftermath::prepare(*battle(), incoming, {accepted});
		battlePrimaryStrikeAftermath::apply(*gameHandler, *battle(), staged);
		EXPECT_EQ(actor->getDefense(false), before + strike);
	}
	const auto rejected = battlePrimaryStrikeAftermath::prepare(*battle(), incoming, {});
	battlePrimaryStrikeAftermath::apply(*gameHandler, *battle(), rejected);
	EXPECT_EQ(actor->getDefense(false), before + 3);
}
TEST_F(NewHorizonsSpecialtyStrikeEventsTest, GoblinFrenzyConsumesExactlyOneAcceptedAttackOrRetaliation)
{
	prepare("core:gretchin", "core:hobgoblin");
	BattleAttackInfo attack(actor, enemy, 0, false);
	attack.retaliation = true;
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 150);
	const auto receipt = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack);
	ASSERT_EQ(receipt.size(), 1);
	apply(receipt);
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 100);
	EXPECT_TRUE(newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack).empty());
	int64_t damage = actor->getAvailableHealth();
	actor->health.damage(damage);
	ASSERT_FALSE(actor->alive());
	int64_t restore = actor->getTotalHealth();
	actor->heal(restore, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(actor->alive());
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 100);
}
TEST_F(NewHorizonsSpecialtyStrikeEventsTest, GoblinFrenzyIgnoresPreemptiveAndCollateralAndMarkerSurvivesDeath)
{
	prepare("core:gretchin", "core:goblin");
	BattleAttackInfo attack(actor, enemy, 0, false);
	attack.bracePreemptive = true;
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 100);
	EXPECT_TRUE(newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack).empty());
	attack.bracePreemptive = false; attack.secondaryAttack = true;
	EXPECT_TRUE(newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack).empty());
	attack.secondaryAttack = false;
	const auto prepared = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack);
	int64_t damage = actor->getAvailableHealth(); actor->health.damage(damage);
	apply(prepared);
	EXPECT_TRUE(actor->hasBonus(CSelector(newHorizonsSpecialtyEvents::isGoblinFrenzySpent)));
}
TEST_F(NewHorizonsSpecialtyStrikeEventsTest, KorbacUsesExactMatchingRetaliationMovementNotGeometry)
{
	prepare("core:korbac", "core:dragonFly");
	BattleAttackInfo forward(actor, enemy, 99, false);
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*battle(), forward.reverse()), 0);
	forward.voluntaryTravelDistance = 7;
	auto counter = forward.reverse();
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*battle(), counter), 21);
	counter.bracePreemptive = true;
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*battle(), counter), 0);
	counter.bracePreemptive = false; counter.secondaryAttack = true;
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*battle(), counter), 0);
	counter.secondaryAttack = false; counter.physicalDamage = false;
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*battle(), counter), 0);
	EXPECT_TRUE(newHorizonsSpecialtyEvents::requiresTravelReceipt(*battle(), *actor));
}
class NewHorizonsSwordAndShieldSequenceTest : public NewHorizonsMeleeSpecialtyFixture {};
TEST_F(NewHorizonsSwordAndShieldSequenceTest, ProtectionStartsOnlyAfterAllCrusaderStrikesAndRetaliation)
{
	prepare("core:sorsha", 0, true);
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	ASSERT_GE(observed.shieldAtAttack.size(), 2);
	EXPECT_TRUE(std::ranges::none_of(observed.shieldAtAttack, [](bool value) { return value; }));
	EXPECT_EQ(observed.shieldPublications, 1);
	const auto protection = actor->getBonuses(CSelector(newHorizonsShieldAndSword::isProtection));
	ASSERT_EQ(protection->size(), 1);
	EXPECT_EQ(protection->front()->val, 40);
}

TEST_F(NewHorizonsSpecialtyStrikeEventsTest, GoblinFrenzyScalesTheNativeFinalDamageAndItsReceiptRoundtrips)
{
	prepare("core:gretchin", "core:goblin");
	BattleAttackInfo attack(actor, enemy, 0, false);
	const auto first = battle()->battleEstimateDamage(attack);
	const auto receipt = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack);
	ASSERT_EQ(receipt.size(), 1);
	ASSERT_EQ(receipt.front().add.size(), 1);
	Bonus saved = receipt.front().add.front();
	CMemorySerializer wire; wire.oser & saved;
	Bonus restored; wire.iser & restored;
	EXPECT_TRUE(newHorizonsSpecialtyEvents::isGoblinFrenzySpent(&restored));
	apply(receipt);
	const auto ordinary = battle()->battleEstimateDamage(attack);
	EXPECT_GT(first.damage.min, ordinary.damage.min);
	EXPECT_NEAR(first.damage.min, ordinary.damage.min * 1.5, 2);
	EXPECT_NEAR(first.damage.max, ordinary.damage.max * 1.5, 2);
}
TEST_F(NewHorizonsSpecialtyStrikeEventsTest, MalformedSpentReceiptCannotMasqueradeAsAValidOne)
{
	prepare("core:gretchin", "core:goblin");
	BattleAttackInfo attack(actor, enemy, 0, false);
	const auto receipt = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack);
	ASSERT_EQ(receipt.size(), 1);
	auto malformed = receipt.front().add.front();
	malformed.val = 2;
	EXPECT_THROW(newHorizonsSpecialtyEvents::isGoblinFrenzySpent(&malformed), std::runtime_error);
	malformed = receipt.front().add.front();
	malformed.bonusOwner = PlayerColor(0);
	EXPECT_THROW(newHorizonsSpecialtyEvents::isGoblinFrenzySpent(&malformed), std::runtime_error);
}
