/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include <stdexcept>
#include "../server/battles/InfernoStrikeDebuffFixture.h"
#include "../../lib/bonuses/RecipientBonusOverlay.h"
class InfernoStrikeDebuffTest : public InfernoStrikeDebuffFixture {};
TEST_F(InfernoStrikeDebuffTest, CalhPhysicalGogAndMagicalMagogPrimaryShareTheSameEffect)
{
	prepare(true);
	BattleAttackInfo shot(actor, victim, 0, true);
	ASSERT_FALSE(shot.physicalDamage);
	const auto magical = newHorizonsOrdinaryStrike::plan(*battle(), shot, 100);
	ASSERT_EQ(magical.add.size(), 1u);
	EXPECT_EQ(magical.add.front().type, BonusType::GENERAL_ATTACK_REDUCTION);
	EXPECT_EQ(magical.add.front().val, 20);
	auto * gog = addStack(BattleSide::ATTACKER, creatureByName("core:gog"), BattleHex(2, 3), 10);
	shot.attacker = gog; shot.physicalDamage = true;
	const auto physical = newHorizonsOrdinaryStrike::plan(*battle(), shot, 100);
	ASSERT_EQ(physical.add.size(), 1u);
	EXPECT_EQ(physical.add.front().toJsonNode(), magical.add.front().toJsonNode());
}
TEST_F(InfernoStrikeDebuffTest, CalhReducesExistingOrdinaryAttackDamageWithoutAffectingDefense)
{
	prepare(true, false);
	const auto defense = victim->getDefense(false);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	publish(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, true), 100));
	const auto after = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	EXPECT_NEAR(after.damage.min, before.damage.min * 0.8, 1.0);
	EXPECT_EQ(victim->getDefense(false), defense);
}
TEST_F(InfernoStrikeDebuffTest, NymusFlatDefenseDebuffUsesBothPitFormsAndExistingDefenseArithmetic)
{
	prepare(false);
	const auto before = victim->getDefense(false);
	BattleAttackInfo hit(actor, victim, 0, false);
	const auto plan = newHorizonsOrdinaryStrike::plan(*battle(), hit, 100);
	ASSERT_EQ(plan.add.size(), 1u); EXPECT_EQ(plan.add.front().val, -3);
	publish(plan); EXPECT_EQ(victim->getDefense(false), std::max(0, before - 3));
	auto * fiend = addStack(BattleSide::ATTACKER, creatureByName("core:pitFiend"), BattleHex(2, 3), 10);
	hit.attacker = fiend; hit.retaliation = true;
	const auto counter = newHorizonsOrdinaryStrike::plan(*battle(), hit, 100);
	ASSERT_EQ(counter.add.size(), 1u); EXPECT_EQ(counter.add.front().val, -3);
}
TEST_F(InfernoStrikeDebuffTest, PrimaryRiderExcludesNativeCollateralAndTaggedAutomaticReactions)
{
	prepare(true);
	const BattleAttackInfo shot(actor, victim, 0, true);
	for(int excluded = 0; excluded < 4; ++excluded)
	{
		auto other = shot;
		if(excluded == 0) other.secondaryAttack = true;
		if(excluded == 1) other.bracePreemptive = true;
		if(excluded == 2) other.preemptiveDamagePercent = 50;
		if(excluded == 3) other.cleaveDamagePercent = 50;
		EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), other, 100).add.empty());
	}
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), shot, 0).add.empty());
	auto melee = shot; melee.shooting = false;
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), melee, 100).add.empty());
}
TEST_F(InfernoStrikeDebuffTest, NymusRejectsShotsMagicalDamageAndSecondaryVictims)
{
	prepare(false);
	const BattleAttackInfo hit(actor, victim, 0, false);
	for(int excluded = 0; excluded < 3; ++excluded)
	{
		auto other = hit;
		if(excluded == 0) other.shooting = true;
		if(excluded == 1) other.physicalDamage = false;
		if(excluded == 2) other.secondaryAttack = true;
		EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), other, 100).add.empty());
	}
}
TEST_F(InfernoStrikeDebuffTest, ReapplicationRefreshesOneStrongestEffectAndPreservesUnrelatedBonuses)
{
	prepare(true);
	const BattleAttackInfo shot(actor, victim, 0, true);
	auto first = newHorizonsOrdinaryStrike::plan(*battle(), shot, 100);
	ASSERT_EQ(first.add.size(), 1u); first.add.front().val = 40;
	first.add.front().description = MetaString::createFromRawString("Scorching Impact: ordinary attack damage -40% through the next accepted Creature Action (including Wait). Cure and Dispel do not remove this effect.");
	publish(first);
	auto unrelated = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_ATTACK_REDUCTION, BonusSource::OTHER, 5, BonusSourceID());
	victim->addNewBonus(unrelated);
	const auto again = newHorizonsOrdinaryStrike::plan(*battle(), shot, 100);
	ASSERT_EQ(again.remove.size(), 1u); ASSERT_EQ(again.add.size(), 1u);
	EXPECT_EQ(again.add.front().val, 40); publish(again);
	EXPECT_EQ(effects(*victim)->size(), 1u);
	EXPECT_TRUE(victim->hasBonus(CSelector([&](const Bonus * b) { return b == unrelated.get(); })));
}
TEST_F(InfernoStrikeDebuffTest, CalhMissingCaptureKeepsOldIdentity)
{
	captureMode = 1; prepare(true);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, true), 100).add.empty());
}
TEST_F(InfernoStrikeDebuffTest, ZeroCaptureKeepsCalhReplacementWithoutEffect)
{
	captureMode = 2; prepare(true);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, true), 100).add.empty());
}
TEST_F(InfernoStrikeDebuffTest, NymusMissingCaptureKeepsOldIdentity)
{
	captureMode = 1; prepare(false);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, false), 100).add.empty());
}
TEST_F(InfernoStrikeDebuffTest, ZeroNymusKeepsReplacementWithoutDefenseLoss)
{
	captureMode = 2; prepare(false);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, false), 100).add.empty());
}
TEST_F(InfernoStrikeDebuffTest, NativeMagogShotAddsOnePrimaryRiderAndNoSplashRider)
{
	prepare(true); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto ammo = actor->shots.available(); server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_GT(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(effects(*victim)->size(), 1u); EXPECT_TRUE(effects(*collateral)->empty());
	EXPECT_EQ(actor->shots.available(), ammo - 1);
}
TEST_F(InfernoStrikeDebuffTest, NativePitLordHitAddsDefenseDebuffAfterDamageWithoutExtraAttack)
{
	prepare(false); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto expected = battle()->calculateDmgRange(BattleAttackInfo(actor, victim, 0, false)).damage;
	const auto before = victim->getAvailableHealth(); server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	ASSERT_EQ(server.attacks.size(), 1u); EXPECT_EQ(effects(*victim)->size(), 1u);
	EXPECT_GE(before - victim->getAvailableHealth(), expected.min); EXPECT_LE(before - victim->getAvailableHealth(), expected.max);
}
TEST_F(InfernoStrikeDebuffTest, NextAcceptedWaitExpiresTheEffectThroughTheGenericActivationClock)
{
	prepare(true, false); beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	ASSERT_EQ(effects(*victim)->size(), 1u); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeWait(victim)));
	EXPECT_TRUE(effects(*victim)->empty());
}
TEST_F(InfernoStrikeDebuffTest, ReservedStatusesValidateSnapshotOwnershipAndDoNotConsumeRng)
{
	prepare(false);
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(rng, nullptr);
	const auto random = infernoRandomBytes(*rng), rolls = infernoRandomBytes(*gameHandler->randomizer);
	const auto plan = newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, false), 100);
	ASSERT_EQ(plan.add.size(), 1u); EXPECT_TRUE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&plan.add.front()));
	auto invalid = plan.add.front(); invalid.val = 1;
	EXPECT_THROW(newHorizonsInfernoStrikeDebuffs::isEffect(&invalid), std::runtime_error);
	EXPECT_EQ(infernoRandomBytes(*rng), random); EXPECT_EQ(infernoRandomBytes(*gameHandler->randomizer), rolls);
}
TEST_F(InfernoStrikeDebuffTest, PlayerCallbackCannotReadHiddenPolicyButPublishedEffectStaysVisible)
{
	prepare(true);
	const CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	const CPlayerBattleCallback own(battle(), PlayerColor(0));
	const BattleAttackInfo shot(actor, victim, 0, true);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(hidden, shot, 100).add.empty());
	EXPECT_EQ(newHorizonsOrdinaryStrike::plan(own, shot, 100).add.size(), 1u);
	publish(newHorizonsOrdinaryStrike::plan(*battle(), shot, 100));
	EXPECT_EQ(effects(*victim)->size(), 1u);
}
