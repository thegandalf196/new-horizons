/*
 * NewHorizonsNaturesWrathTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsNaturesWrathFixture.h"
#include "../../../lib/serializer/CMemorySerializer.h"

using NewHorizonsNaturesWrathTest = NewHorizonsNaturesWrathFixture;

TEST(NewHorizonsNaturesWrathPowerTest, CanonicalFractionalAttenuationAndWorldroot)
{
	const std::array<int64_t, 3> ordinary{310, 288, 268};
	const std::array<int64_t, 3> worldroot{330, 306, 285};
	for(int hop = 0; hop < 3; ++hop)
	{
		EXPECT_EQ(newHorizonsNaturesWrath::hopPower(100, 10000, 0, 0, false, hop), ordinary[hop]);
		EXPECT_EQ(newHorizonsNaturesWrath::hopPower(100, 10000, 0, 0, true, hop), worldroot[hop]);
	}
	EXPECT_EQ(newHorizonsNaturesWrath::hopPower(100, 15000, 0, 0, false, 0), 410);
	EXPECT_EQ(newHorizonsNaturesWrath::hopPower(100, 15000, 20, 50, true, 0), 704);
	EXPECT_THROW(newHorizonsNaturesWrath::hopPower(100, 10000, 0, 0, false, 17), std::invalid_argument);
	EXPECT_NO_THROW(newHorizonsNaturesWrath::hopPower(100, 10000, 0, 0, true, 18));
}

TEST_F(NewHorizonsNaturesWrathTest, PaidExpertCastAlternatesDamageAndSurvivorOnlyHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damage(friendly, 3 * friendly->getMaxHealth() - 1); // Two casualties and a wounded survivor.
	const auto survivors = friendly->getCount();
	const auto beforeFirst = first->getAvailableHealth();
	const auto beforeThird = third->getAvailableHealth();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell());
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(beforeFirst - first->getAvailableHealth(), newHorizonsNaturesWrath::hopPower(100, coefficient, 0, 0, false, 0));
	EXPECT_EQ(beforeThird - third->getAvailableHealth(), newHorizonsNaturesWrath::hopPower(100, coefficient, 0, 0, false, 2));
	EXPECT_EQ(friendly->getCount(), survivors);
	EXPECT_EQ(friendly->getAvailableHealth(), survivors * friendly->getMaxHealth());
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(reserve->getAvailableHealth(), reserve->getCount() * reserve->getMaxHealth());
	auto copy = third->acquireState();
	copy->load(copy->save());
	EXPECT_EQ(copy->getAvailableHealth(), third->getAvailableHealth());
}

TEST_F(NewHorizonsNaturesWrathTest, HealthyAndImmuneConductorsKeepHopIndex)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell())));
	const auto route = newHorizonsNaturesWrath::route(*battle(), first, 17);
	ASSERT_EQ(route.size(), 4u);
	EXPECT_EQ(route[0].unitValue, first);
	EXPECT_EQ(route[1].unitValue, friendly);
	EXPECT_EQ(route[2].unitValue, third);
	const auto health = first->getAvailableHealth();
	const auto later = third->getAvailableHealth();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(battle()->getMagicRules(), attackerSideHero, spell());
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(first->getAvailableHealth(), health);
	EXPECT_EQ(later - third->getAvailableHealth(), newHorizonsNaturesWrath::hopPower(100, coefficient, 0, 0, false, 2));
}

TEST_F(NewHorizonsNaturesWrathTest, ResistantEnemyConductsWithoutDamageOrRerouting)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto health = first->getAvailableHealth();
	const auto later = third->getAvailableHealth();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(first->getAvailableHealth(), health);
	EXPECT_LT(third->getAvailableHealth(), later);
}

TEST_F(NewHorizonsNaturesWrathTest, LockedInitialTargetRejectsWithoutManaButLockedIntermediateContinues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	lock(friendly);
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	damage(friendly, 9);
	const auto health = friendly->getAvailableHealth();
	const auto later = third->getAvailableHealth();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(friendly->getAvailableHealth(), health);
	EXPECT_LT(third->getAvailableHealth(), later);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsNaturesWrathTest, DeadStackCannotConductOrResurrect)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damage(friendly, friendly->getAvailableHealth());
	EXPECT_FALSE(newHorizonsNaturesWrath::validConductor(friendly));
	const auto route = newHorizonsNaturesWrath::route(*battle(), first, 17);
	for(const auto & destination : route)
		EXPECT_NE(destination.unitValue, friendly);
	ASSERT_TRUE(cast(first));
	EXPECT_FALSE(friendly->alive());
	EXPECT_EQ(friendly->getAvailableHealth(), 0);
}

TEST_F(NewHorizonsNaturesWrathTest, NoManaRejectsWithoutHealthMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	setTestSpellPointTotal(attackerSideHero, 0);
	const auto before = first->getAvailableHealth();
	EXPECT_FALSE(cast(first));
	EXPECT_EQ(first->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
}

TEST_F(NewHorizonsNaturesWrathTest, TimeStoppedStackCannotConduct)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(friendly->isTimeStopped());
	EXPECT_FALSE(newHorizonsNaturesWrath::validConductor(friendly));
	const auto route = newHorizonsNaturesWrath::route(*battle(), first, 17);
	for(const auto & destination : route)
		EXPECT_NE(destination.unitValue, friendly);
}

TEST_F(NewHorizonsNaturesWrathTest, StableUnitIdBreaksNearestTie)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * lower = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 4), 1000);
	const auto * higher = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 6), 1000);
	ASSERT_NE(lower, nullptr);
	ASSERT_NE(higher, nullptr);
	ASSERT_LT(lower->unitId(), higher->unitId());
	const auto route = newHorizonsNaturesWrath::route(*battle(), first, 17);
	ASSERT_GE(route.size(), 2u);
	EXPECT_EQ(route[1].unitValue, lower);
}

TEST_F(NewHorizonsNaturesWrathTest, NearestUsesWholeDoubleWideFootprint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * wide = addStack(BattleSide::DEFENDER, creatureByName("core:behemoth"), BattleHex(2, 5), 100);
	ASSERT_NE(wide, nullptr);
	ASSERT_EQ(wide->getHexes().size(), 2u);
	// Defender's second occupied hex (3,5) is adjacent to first(4,5),
	// while primary (2,5) ties friendly(6,5). Whole-footprint routing chooses wide.
	const auto route = newHorizonsNaturesWrath::route(*battle(), first, 17);
	ASSERT_GE(route.size(), 2u);
	EXPECT_EQ(route[1].unitValue, wide);
}

TEST_F(NewHorizonsNaturesWrathTest, InvincibleConductsButDoesNotTakeDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(newHorizonsNaturesWrath::validConductor(first));
	const auto before = first->getAvailableHealth();
	const auto later = third->getAvailableHealth();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(first->getAvailableHealth(), before);
	EXPECT_LT(third->getAvailableHealth(), later);
}

TEST_F(NewHorizonsNaturesWrathTest, PaidCastUpdatePacketRoundTripsExistingHealthWithoutNewState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(first));
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks.emplace_back(first->unitId(), UnitChanges::EOperation::UPDATE);
	update.changedStacks.back().data = first->acquireState()->save();
	CMemorySerializer writer;
	writer.oser & update;
	CMemorySerializer reader(writer.extractBuffer());
	BattleUnitsChanged restored;
	reader.iser & restored;
	ASSERT_EQ(restored.changedStacks.size(), 1u);
	EXPECT_EQ(restored.changedStacks.front().data, update.changedStacks.front().data);
	auto detached = first->acquireState();
	detached->load(restored.changedStacks.front().data);
	EXPECT_EQ(detached->getAvailableHealth(), first->getAvailableHealth());
}

TEST_F(NewHorizonsNaturesWrathTest, WorldrootUsesNineteenDistinctRecipientsAndActualExpertPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	for(int x = 2; x <= 13; ++x)
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(x, 2), 1000), nullptr);
	for(int x = 2; x <= 7; ++x)
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(x, 8), 1000), nullptr);
	EXPECT_EQ(newHorizonsNaturesWrath::route(*battle(), first, 17).size(), 17u);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	const auto route = newHorizonsNaturesWrath::route(*mechanics, first);
	ASSERT_EQ(route.size(), 19u);
	std::set<uint32_t> seen;
	for(const auto & destination : route)
		EXPECT_TRUE(seen.insert(destination.unitValue->unitId()).second);
	const auto expected = newHorizonsNaturesWrath::hopPower(*mechanics, 0);
	const auto before = first->getAvailableHealth();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(before - first->getAvailableHealth(), expected);
}
