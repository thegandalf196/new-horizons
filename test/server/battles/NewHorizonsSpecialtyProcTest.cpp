/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcFixture.h"
class NewHorizonsSpecialtyProcTest : public NewHorizonsSpecialtyProcFixture {};
TEST_F(NewHorizonsSpecialtyProcTest, BaseWyvernReceivesExactlyNativeMonarchChanceWithoutChangingPotency)
{
	prepare("core:broghild", "core:wyvern");
	const auto chance = newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *actor, SpellID(SpellID::POISON), false);
	ASSERT_TRUE(chance); EXPECT_EQ(*chance, 30);
	EXPECT_EQ(newHorizonsSpecialtyProcs::additionalAttackSpells(*battle(), *actor, false), std::vector<SpellID>{SpellID(SpellID::POISON)});
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*battle(), *actor), 1);
	passiveCast(SpellID(SpellID::POISON), *actor, *enemy);
	ASSERT_FALSE(server.casts.empty()); EXPECT_FALSE(server.casts.back().announcement.strongWyvernPoison);
	const auto cap = enemy->getFirstBonus(Selector::type()(BonusType::POISON));
	ASSERT_NE(cap, nullptr); EXPECT_EQ(cap->val, 30); EXPECT_EQ(cap->turnsRemain, 3);
}
TEST_F(NewHorizonsSpecialtyProcTest, MonarchNativePoisonDoublesInitialRampAndCapButPreservesDuration)
{
	prepare("core:broghild", "core:wyvernMonarch");
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*battle(), *actor), 2);
	passiveCast(SpellID(SpellID::POISON), *actor, *enemy);
	ASSERT_FALSE(server.casts.empty()); EXPECT_TRUE(server.casts.back().announcement.strongWyvernPoison);
	const auto cap = enemy->getFirstBonus(Selector::type()(BonusType::POISON));
	const auto health = enemy->getFirstBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::POISON)))
		.And(Selector::type()(BonusType::STACK_HEALTH)));
	ASSERT_NE(cap, nullptr); ASSERT_NE(health, nullptr);
	EXPECT_EQ(cap->val, 60); EXPECT_EQ(health->val, -20);
	EXPECT_EQ(cap->turnsRemain, 3); EXPECT_EQ(health->turnsRemain, 3);
	EXPECT_TRUE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(cap.get()));
	EXPECT_TRUE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(health.get()));
	auto ramp = *health;
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonRampValue(*enemy, ramp), -40);
	ramp.val = -40; EXPECT_EQ(newHorizonsSpecialtyProcs::poisonRampValue(*enemy, ramp), -60);
	ramp.val = -60; EXPECT_EQ(newHorizonsSpecialtyProcs::poisonRampValue(*enemy, ramp), -60);
	EXPECT_GE(enemy->getMaxHealth(), 1u);
}
TEST_F(NewHorizonsSpecialtyProcTest, OtherHeroMonarchRetainsNativePoisonAndDetachedPlanMatchesNativeMagnitude)
{
	prepare("core:orrin", "core:wyvernMonarch");
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*battle(), *actor), 1);
	const auto plan = newHorizonsSpecialtyProcs::poisonBonusPlan(*battle(), *actor, 3);
	ASSERT_EQ(plan.size(), 2);
	for(const auto & bonus : plan)
	{
		EXPECT_EQ(bonus.sid, BonusSourceID(SpellID(SpellID::POISON)));
		EXPECT_EQ(bonus.turnsRemain, 3);
		EXPECT_EQ(bonus.val, bonus.type == BonusType::POISON ? 30 : -10);
		EXPECT_FALSE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(&bonus));
	}
}
TEST_F(NewHorizonsSpecialtyProcTest, PoisonUpgradesExistingNativeStatusAndWeakReapplicationNeverDowngradesItsRamp)
{
	prepare("core:broghild", "core:wyvernMonarch");
	auto * base = addStack(BattleSide::ATTACKER, creatureByName("core:wyvern"), BattleHex(8, 5), 5);
	passiveCast(SpellID(SpellID::POISON), *base, *enemy);
	passiveCast(SpellID(SpellID::POISON), *actor, *enemy);
	BattleTriggerEffect ramp;
	ramp.battleID = BattleID(0); ramp.stackID = enemy->unitId(); ramp.effect = BonusType::POISON; ramp.val = -40;
	gameHandler->sendAndApply(ramp);
	passiveCast(SpellID(SpellID::POISON), *base, *enemy);
	const auto cap = enemy->getFirstBonus(Selector::type()(BonusType::POISON));
	const auto health = enemy->getFirstBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::POISON)))
		.And(Selector::type()(BonusType::STACK_HEALTH)));
	ASSERT_NE(cap, nullptr); ASSERT_NE(health, nullptr);
	EXPECT_EQ(cap->val, 60); EXPECT_EQ(health->val, -40);
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonRampValue(*enemy, *health), -60);
	EXPECT_TRUE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(health.get()));
	EXPECT_EQ(enemy->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::POISON))))->size(), 2);
	for(int round = 0; round < 4; ++round) advanceRound();
	EXPECT_FALSE(enemy->hasBonus(Selector::type()(BonusType::POISON)));
	passiveCast(SpellID(SpellID::POISON), *base, *enemy);
	const auto fresh = enemy->getFirstBonus(Selector::type()(BonusType::POISON));
	ASSERT_NE(fresh, nullptr); EXPECT_EQ(fresh->val, 30);
	EXPECT_FALSE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(fresh.get()));
}
TEST_F(NewHorizonsSpecialtyProcTest, NullifyingShotChanceIsTwentyOrFortyAndOnlyRanged)
{
	prepare("core:theodorus", "core:mage");
	const auto spell = SpellID(SpellID::DISPEL_HELPFUL_SPELLS);
	EXPECT_EQ(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *actor, spell, true), 20);
	EXPECT_FALSE(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *actor, spell, false));
	const auto * upgraded = addStack(BattleSide::ATTACKER, creatureByName("core:archMage"), BattleHex(8, 5), 5);
	EXPECT_EQ(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *upgraded, spell, true), 40);
	EXPECT_TRUE(newHorizonsSpecialtyProcs::additionalAttackSpells(*battle(), *actor, false).empty());
}
TEST_F(NewHorizonsSpecialtyProcTest, NullifyingNativeDispelRemovesEveryPositiveTemporaryMagicAndPreservesOtherSources)
{
	prepare("core:theodorus", "core:archMage");
	for(const auto id : {SpellID::BLESS, SpellID::HASTE})
	{
		Bonus bonus(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(id)));
		bonus.turnsRemain = 3; enemy->addNewBonus(std::make_shared<Bonus>(bonus));
	}
	Bonus permanent(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::FORTUNE)));
	Bonus negative(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(SpellID::SLOW)));
	negative.turnsRemain = 3;
	Bonus innate(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(enemy->creatureId()));
	for(const auto & bonus : {permanent, negative, innate}) enemy->addNewBonus(std::make_shared<Bonus>(bonus));
	EXPECT_EQ(newHorizonsSpecialtyProcs::dispelRemovalPlan(*enemy).size(), 2);
	passiveCast(SpellID(SpellID::DISPEL_HELPFUL_SPELLS), *actor, *enemy);
	EXPECT_TRUE(newHorizonsSpecialtyProcs::dispelRemovalPlan(*enemy).empty());
	EXPECT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::FORTUNE)))));
	EXPECT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)))));
	EXPECT_TRUE(enemy->hasBonus(Selector::sourceType()(BonusSource::CREATURE_ABILITY).And(Selector::type()(BonusType::LUCK))));
}
TEST_F(NewHorizonsSpecialtyProcTest, ConcussiveProcIncludesOrdinaryMeleeShotAndRetaliationButNotSecondaryOrPreemptive)
{
	prepare("core:yog", "core:cyclopKing");
	BattleAttackInfo strike(actor, enemy, 0, true);
	EXPECT_EQ(newHorizonsSpecialtyProcs::stunChance(*battle(), strike), 25);
	strike.shooting = false; strike.retaliation = true;
	EXPECT_EQ(newHorizonsSpecialtyProcs::stunChance(*battle(), strike), 25);
	strike.secondaryAttack = true; EXPECT_EQ(newHorizonsSpecialtyProcs::stunChance(*battle(), strike), 0);
	strike.secondaryAttack = false; strike.bracePreemptive = true;
	EXPECT_EQ(newHorizonsSpecialtyProcs::stunChance(*battle(), strike), 0);
	strike.bracePreemptive = false;
	BattleStackAttacked hit; hit.stackAttacked = enemy->unitId(); hit.damageAmount = 0;
	EXPECT_EQ(battlePrimaryStrikeAftermath::prepare(*battle(), strike, {hit}).stunChance, 25);
	EXPECT_EQ(battlePrimaryStrikeAftermath::prepare(*battle(), strike, {}).stunChance, 0);
}
TEST_F(NewHorizonsSpecialtyProcTest, ConcussiveResolutionUsesExistingFavorableAbilityStreamExactlyOnce)
{
	prepare("core:yog", "core:cyclop");
	CMemorySerializer copy; gameHandler->randomizer->serialize(copy.oser);
	GameRandomizer expected(*gameState()); expected.serialize(copy.iser);
	const int chance = expected.prepareFavorableCreatureAbilityChance(*actor, 25);
	const bool succeeds = expected.rollCombatAbility(battle()->getSideArmy(actor->unitSide())->id, chance);
	battleSpecialtyProcs::resolveStun(*gameHandler, *gameHandler->battles, *battle(), actor->unitId(), enemy->unitId(), 25);
	EXPECT_EQ(newHorizonsStun::isStunned(*enemy), succeeds);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), randomBytes(expected));
}
TEST_F(NewHorizonsSpecialtyProcTest, RejectedAndAlreadyPendingStunDoNotAdvanceOrRefreshTheRandomStream)
{
	prepare("core:yog", "core:cyclop");
	const auto before = randomBytes(*gameHandler->randomizer);
	battleSpecialtyProcs::resolveStun(*gameHandler, *gameHandler->battles, *battle(), actor->unitId(), enemy->unitId(), 0);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), before);
	status(*enemy, newHorizonsStun::marker());
	battleSpecialtyProcs::resolveStun(*gameHandler, *gameHandler->battles, *battle(), actor->unitId(), enemy->unitId(), 25);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), before);
	EXPECT_EQ(newHorizonsStun::removalPlan(*enemy).size(), 1);
	EXPECT_THROW(battleSpecialtyProcs::resolveStun(*gameHandler, *gameHandler->battles, *battle(), actor->unitId(), enemy->unitId(), 101), std::invalid_argument);
}
TEST_F(NewHorizonsSpecialtyProcTest, PendingStunBonusRoundtripsAndMalformedProvenanceRejects)
{
	prepare("core:yog", "core:cyclop");
	auto marker = newHorizonsStun::marker();
	CMemorySerializer wire; wire.oser & marker;
	Bonus restored; wire.iser & restored;
	EXPECT_TRUE(newHorizonsStun::isMarker(&restored));
	marker.turnsRemain = 1;
	EXPECT_THROW(newHorizonsStun::isMarker(&marker), std::runtime_error);
}
TEST(StrongWyvernPoisonReceipt, CurrentBinaryPreservesPositiveAndNegativeReceipt)
{
	for(bool strong : {false, true})
	{
		BattleSpellCast packet;
		packet.battleID = BattleID(0); packet.side = BattleSide::ATTACKER;
		packet.spellID = SpellID::POISON; packet.castByHero = false; packet.casterStack = 1; packet.activeCast = false;
		packet.strongWyvernPoison = strong;
		CMemorySerializer wire; wire.oser & packet;
		BattleSpellCast restored; wire.iser & restored;
		EXPECT_EQ(restored.strongWyvernPoison, strong);
		EXPECT_EQ(restored.spellID, packet.spellID);
	}
}
TEST(StrongWyvernPoisonReceipt, UnsupportedOrForgedReceiptRejectsBeforeAnyPacketPrefix)
{
	BattleSpellCast packet;
	packet.battleID = BattleID(0); packet.side = BattleSide::ATTACKER;
	packet.spellID = SpellID::POISON; packet.castByHero = false; packet.casterStack = 1; packet.activeCast = false;
	packet.strongWyvernPoison = true;
	CMemorySerializer old; old.oser.version = ESerializationVersion::NEW_HORIZONS_FACTION_SKILL_REVISIONS;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	packet.spellID = SpellID::MAGIC_ARROW;
	CMemorySerializer forged;
	EXPECT_THROW(forged.oser & packet, std::runtime_error);
	EXPECT_TRUE(forged.extractBuffer().empty());
	packet.strongWyvernPoison = false;
	CMemorySerializer ordinary;
	EXPECT_NO_THROW(ordinary.oser & packet);
}
