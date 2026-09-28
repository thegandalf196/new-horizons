/*
 * NewHorizonsSpellLockAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_spells_Mechanics.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "lib/GameLibrary.h"
#include "lib/modding/CModHandler.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/NewHorizonsSorcery.h"

namespace
{
using namespace testing;

SpellID spellId(const char * key)
{
	return SpellID(SpellID::decode(key));
}

std::shared_ptr<Bonus> spellBonus(SpellID source, BonusType type, int32_t value = 1)
{
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
		value, BonusSourceID(source));
	result->turnsRemain = 2;
	return result;
}

using SpellLockStackMock = test::battle::UnitFake;
}

class NewHorizonsSpellLockAITest : public ::testing::Test
{
protected:
	test::battle::BattleFake battle;
	spells::MechanicsMock mechanics;
	SpellLockStackMock friendly;
	SpellLockStackMock enemy;
	SpellID spell;

	void SetUp() override
	{
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		spell = spellId(newHorizonsSorcery::SPELL_LOCK_SPELL);
		ASSERT_TRUE(spell.hasValue());
		ASSERT_NE(spell.toSpell(), nullptr);

		ON_CALL(friendly, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
		ON_CALL(enemy, unitSide()).WillByDefault(Return(BattleSide::DEFENDER));
		friendly.redirectBonusesToFake();
		enemy.redirectBonusesToFake();
		for(auto * stack : {&friendly, &enemy})
		{
			ON_CALL(*stack, alive()).WillByDefault(Return(true));
			ON_CALL(*stack, isInvincible()).WillByDefault(Return(false));
			ON_CALL(*stack, getAvailableHealth()).WillByDefault(Return(100));
			ON_CALL(*stack, isValidTarget(_)).WillByDefault(Return(true));
			ON_CALL(*stack, canCast()).WillByDefault(Return(false));
			ON_CALL(*stack, isGhost()).WillByDefault(Return(false));
			ON_CALL(*stack, isHypnotized()).WillByDefault(Return(false));
			ON_CALL(*stack, creatureIndex()).WillByDefault(Return(0));
		}

		ON_CALL(battle, getUnitsIf(_)).WillByDefault([&](const battle::UnitFilter & filter)
		{
			::battle::Units result;
			for(auto * unit : {&friendly, &enemy})
				if(filter(unit))
					result.push_back(unit);
			return result;
		});
		ON_CALL(battle, getSidePlayer(BattleSide::ATTACKER)).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(battle, getSidePlayer(BattleSide::DEFENDER)).WillByDefault(Return(PlayerColor(1)));

		ON_CALL(mechanics, battle()).WillByDefault(Return(&battle));
		ON_CALL(mechanics, getSpell()).WillByDefault(Return(spell.toSpell()));
		ON_CALL(mechanics, getCasterColor()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(mechanics, getTargetTypes()).WillByDefault(Return(
			std::vector<spells::AimType>{spells::AimType::CREATURE}));
		ON_CALL(mechanics, getEffectDuration()).WillByDefault(Return(3));
		ON_CALL(mechanics, canBeCastAt(_, _)).WillByDefault(Return(true));
		ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	}
};

TEST_F(NewHorizonsSpellLockAITest, TargetEnumerationSkipsAlreadyLockedStacks)
{
	enemy.addNewBonus(spellBonus(spell, BonusType::MAGIC_RESISTANCE, 100));
	const auto targets = SpellTargetEvaluator::getViableTargets(&mechanics);
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), 1u);
	EXPECT_EQ(targets.front().front().unitValue, &friendly);
}

TEST_F(NewHorizonsSpellLockAITest, ValuesCleansingOrPreservingRelevantEffects)
{
	const auto slow = spellId("core:slow");
	const auto haste = spellId("core:haste");
	friendly.addNewBonus(spellBonus(slow, BonusType::STACKS_SPEED, -3));
	enemy.addNewBonus(spellBonus(haste, BonusType::STACKS_SPEED, 3));

	const auto friendlyValue = SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&friendly)});
	const auto enemyValue = SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)});
	EXPECT_GT(friendlyValue, 0.0f);
	EXPECT_GT(enemyValue, 0.0f);

	friendly.addNewBonus(spellBonus(spellId("core:deathCloud"), BonusType::STACKS_SPEED, -3));
	EXPECT_FLOAT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&friendly)}), friendlyValue);

	enemy.addNewBonus(spellBonus(spell, BonusType::MAGIC_RESISTANCE, 100));
	EXPECT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)}), 0.0f);
}

TEST_F(NewHorizonsSpellLockAITest, DoesNotValueAnUnbuffedEnemyCaster)
{
	ON_CALL(enemy, canCast()).WillByDefault(Return(true));
	EXPECT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)}), 0.0f);
}
