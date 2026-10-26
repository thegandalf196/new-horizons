/*
 * TheChosenValueTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/TheChosenValue.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"

class TheChosenValueTest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * shooter = nullptr;
	std::shared_ptr<heroQueueTest::QueueEnvironment> environment;
	std::shared_ptr<HypotheticBattle> before, after;
	SpellID spell;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	Bonus doubleStrike(BonusSource source = BonusSource::SPELL_EFFECT) const
	{
		Bonus result(BonusDuration::N_TURNS, BonusType::ADDITIONAL_ATTACK, source, 1,
			source == BonusSource::SPELL_EFFECT ? BonusSourceID(spell) : BonusSourceID(),
			BonusSubtypeID(), BonusValueType::INDEPENDENT_MAX);
		result.turnsRemain = 3;
		return result;
	}
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		spell = SpellID(SpellID::decode("new-horizons:theChosen"));
		ASSERT_NE(spell, SpellID::NONE);
		BattleUnitsChanged clear;
		clear.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			clear.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(clear);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 3), 1);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 8), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(shooter, nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 8), 10000), nullptr);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
	}
	void stage()
	{
		auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		before = std::make_shared<HypotheticBattle>(environment.get(), live);
		after = std::make_shared<HypotheticBattle>(environment.get(), before);
	}
	float value()
	{
		DamageCache beforeCache;
		beforeCache.buildDamageCache(before, BattleSide::ATTACKER);
		DamageCache afterCache;
		afterCache.buildDamageCache(after, BattleSide::ATTACKER);
		return theChosenValue::marginalValue(BattleSide::ATTACKER, active->unitId(),
			before, after, spell, beforeCache, afterCache);
	}
};

TEST_F(TheChosenValueTest, AddedDoubleStrikeUsesNativeAttackSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	stage();
	after->getForUpdate(shooter->unitId())->addUnitBonus({doubleStrike()});
	EXPECT_GT(value(), 0.0f);
	EXPECT_EQ(before->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 1);
	EXPECT_EQ(after->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 2);
}

TEST_F(TheChosenValueTest, NaturalDoubleStrikeIsNotMultiplied)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	shooter->addNewBonus(std::make_shared<Bonus>(doubleStrike(BonusSource::CREATURE_ABILITY)));
	stage();
	after->getForUpdate(shooter->unitId())->addUnitBonus({doubleStrike()});
	EXPECT_EQ(before->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 2);
	EXPECT_EQ(after->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 2);
	EXPECT_FLOAT_EQ(value(), 0.0f);
}

TEST_F(TheChosenValueTest, RetargetingSubtractsOldRecipientAndExcludesActiveContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	shooter->addNewBonus(std::make_shared<Bonus>(doubleStrike()));
	stage();
	after->getForUpdate(shooter->unitId())->removeUnitBonus(
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	after->getForUpdate(active->unitId())->addUnitBonus({doubleStrike()});
	EXPECT_LT(value(), 0.0f);
	EXPECT_EQ(before->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 2);
	EXPECT_EQ(after->battleGetUnitByID(shooter->unitId())->getTotalAttacks(true), 1);
}
