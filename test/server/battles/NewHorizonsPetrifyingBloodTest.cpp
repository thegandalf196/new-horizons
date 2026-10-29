/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPetrifyingBloodFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/serializer/CMemorySerializer.h"

TEST_F(NewHorizonsPetrifyingBloodBattleTest, FirstDamagingMeleeUsesNativePetrificationWithoutRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80); // Native20 plus explicit test80; no guaranteed production coefficient.
	const auto round = battle()->getRound();
	const auto ownMana = attackerSideHero->getNormalSpellPoints(), enemyMana = defenderSideHero->getNormalSpellPoints();
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_EQ(basilisk->petrifyingBloodRound, round);
	EXPECT_TRUE(petrified(*enemy));
	EXPECT_FALSE(std::ranges::any_of(server.attacks, [](const auto & hit) { return hit.counter(); }));
	EXPECT_TRUE(enemy->hasBonusOfType(BonusType::NO_RETALIATION));
	EXPECT_EQ(enemy->valOfBonuses(BonusType::GENERAL_DAMAGE_REDUCTION), 50);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), ownMana);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), enemyMana);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, GreaterBasiliskUsesSameCapturedPolicyAndNativeChance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	EXPECT_EQ(newHorizonsPetrifyingBlood::chanceBasisPoints(*battle(), *basilisk, *enemy), 2000);
	addChance(80);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_TRUE(petrified(*enemy));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, FailedFirstRollSpendsAttemptUntilNextRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(-20);
	const int round = battle()->getRound();
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_EQ(basilisk->petrifyingBloodRound, round);
	addChance(100);
	battle()->activeStack = enemy->unitId();
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(petrified(*enemy));
	nextRound();
	ASSERT_GT(battle()->getRound(), round);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_TRUE(petrified(*enemy));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, ImmuneFirstAttackerSpendsAttemptWithoutPetrification)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::STONE_GAZE))));
	EXPECT_EQ(newHorizonsPetrifyingBlood::chanceBasisPoints(*battle(), *basilisk, *enemy), 0);
	const int round = battle()->getRound();
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_EQ(basilisk->petrifyingBloodRound, round);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, SeparateBasiliskStacksKeepIndependentRoundAttempts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(-20);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	auto * second = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:greaterBasilisk")), BattleHex(3, 4), 10);
	auto first = basilisk->acquireState();
	EXPECT_FALSE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *first, 1, false, true));
	auto secondState = second->acquireState();
	EXPECT_TRUE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *secondState, 1, false, true));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, ZeroRangedMagicalAndFriendlyDamageDoNotSpend)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = basilisk->acquireState();
	EXPECT_FALSE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *state, 0, false, true));
	EXPECT_FALSE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *state, 1, true, true));
	EXPECT_FALSE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *state, 1, false, false));
	EXPECT_FALSE(newHorizonsPetrifyingBlood::qualifies(*battle(), basilisk, *state, 1, false, true));
	EXPECT_EQ(state->petrifyingBloodRound, -1);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, ActualRangedShotDoesNotConsumeRoundAttempt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	auto * shooter = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::ARCHER), BattleHex(12, 6), 10);
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(shooter, basilisk)));
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
	EXPECT_FALSE(petrified(*shooter));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, HistoricalAbsenceKeepsGenericRiderAndDoesNotProc)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasPetrifyingBlood(attackerSideHero));
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
		{ return bonus && bonus->stacking.find("creature-line-specialty") != std::string::npos; }));
	addChance(80);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, DifferentHeroCannotUseCapturedBronIdentity)
{
	bron = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, NewCapturedIdentityReplacesGenericCreatureRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsHeroes::hasPetrifyingBlood(attackerSideHero));
	EXPECT_FALSE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
		{ return bonus && bonus->stacking.find("creature-line-specialty") != std::string::npos; }));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, BloodWaitsForAllOrdinaryFollowupHits)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_EQ(std::ranges::count_if(server.attacks, [](const auto & hit) { return !hit.counter() && !hit.shot(); }), 2);
	EXPECT_TRUE(petrified(*enemy));
	EXPECT_EQ(server.castsOf(SpellID(SpellID::STONE_GAZE)).size(), 1u);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, OrdinaryRetaliationPetrificationIsNotRefreshedByBlood)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->removeUnit(enemy->unitId());
	enemy = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 400);
	forceMaximumDamage(enemy);
	// Let the ordinary Basilisk counter attack. Its native proc stays unchanged.
	basilisk->removeBonuses(Selector::type()(BonusType::NO_RETALIATION));
	addChance(80);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	ASSERT_TRUE(std::ranges::any_of(server.attacks, [](const auto & hit) { return hit.counter(); }));
	EXPECT_TRUE(petrified(*enemy));
	EXPECT_EQ(server.castsOf(SpellID(SpellID::STONE_GAZE)).size(), 1u);
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, SourceKilledByLaterBlowCannotResolveQueuedBlood)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	const auto range = battle()->calculateDmgRange(BattleAttackInfo(enemy, basilisk, 0, false));
	const int64_t survivesFirstHit = range.damage.max + 1;
	ASSERT_GT(range.damage.max, 0);
	ASSERT_LT(survivesFirstHit, basilisk->getAvailableHealth());
	auto state = basilisk->acquireState();
	int64_t preparationDamage = state->getAvailableHealth() - survivesFirstHit;
	state->damage(preparationDamage);
	BattleUnitsChanged prepared;
	prepared.battleID = battle()->getBattleID();
	UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	prepared.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(prepared);
	const int round = battle()->getRound();
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	EXPECT_FALSE(basilisk->alive());
	EXPECT_EQ(basilisk->petrifyingBloodRound, round);
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_TRUE(server.castsOf(SpellID(SpellID::STONE_GAZE)).empty());
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, LaterOrdinaryDamageUsesNativeWakeCleanup)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addChance(80);
	ASSERT_TRUE(attack(enemy, basilisk->getPosition()));
	ASSERT_TRUE(petrified(*enemy));
	battle()->moveUnit(enemy->unitId(), BattleHex(12, 6));
	auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::ARCHER), BattleHex(3, 6), 1);
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, enemy)));
	EXPECT_FALSE(petrified(*enemy));
	EXPECT_FALSE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::STONE_GAZE)))));
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, NativeImmuneRoleDoesNotInventAnEarlierDamageReceiptExclusion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	auto state = basilisk->acquireState();
	EXPECT_TRUE(newHorizonsPetrifyingBlood::qualifies(*battle(), enemy, *state, 1, false, true));
	EXPECT_EQ(newHorizonsPetrifyingBlood::chanceBasisPoints(*battle(), *basilisk, *enemy), 0);
	EXPECT_EQ(state->petrifyingBloodRound, -1); // The read-only qualifier itself never spends.
}

TEST_F(NewHorizonsPetrifyingBloodBattleTest, IndependentDescriptorRoundTripPreservesCurrentAndDefaultsOldReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CStackBasicDescriptor base(basilisk->unitType()->getId(), basilisk->unitBaseAmount());
	CStack descriptor(&base, basilisk->unitOwner(), static_cast<int>(basilisk->unitId()), basilisk->unitSide());
	descriptor.petrifyingBloodRound = battle()->getRound();
	CMemorySerializer current;
	current.oser & descriptor;
	CStack restored;
	current.iser.cb = gameState().get();
	current.iser & restored;
	EXPECT_EQ(restored.petrifyingBloodRound, descriptor.petrifyingBloodRound);
	descriptor.petrifyingBloodRound = -1;
	CMemorySerializer historical;
	historical.oser.version = historical.iser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	historical.oser & descriptor;
	CStack oldRestored;
	historical.iser.cb = gameState().get();
	historical.iser & oldRestored;
	EXPECT_EQ(oldRestored.petrifyingBloodRound, -1);
}
