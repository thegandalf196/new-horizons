/*
 * FrozenAttackResolutionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FrozenAttackStageFixture.h"
#include "../../../server/battles/FrozenAttackResolution.h"

TEST_F(FrozenAttackStageFixture, PreparationMarksDirectHitsWithoutRemovingPreInjuryFrozen)
{
	freezeTarget();
	const auto health = target->getAvailableHealth();
	const auto receipt = target->frozenLastAppliedRound();
	BattleAttack pending;
	pending.bsa.push_back(pendingHit(ice->unitId()));
	pending.bsa.push_back(pendingHit(target->unitId()));
	const FrozenAttackContext context{*battle(), ice, target, 0, false, false};
	const auto result = prepareFrozenAttack(context, pending);
	ASSERT_EQ(result.shatter.toRemove.size(), 1u);
	EXPECT_EQ(result.shatter.toRemove.front().first, target->unitId());
	EXPECT_EQ(result.shatter.battleID, battle()->getBattleID());
	EXPECT_TRUE(pending.bsa.front().shattered());
	EXPECT_FALSE(pending.bsa.back().shattered());
	EXPECT_TRUE(result.primaryFrozenBeforeHit);
	EXPECT_TRUE(result.freezingTouch);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(target->frozenLastAppliedRound(), receipt);
}

TEST_F(FrozenAttackStageFixture, SecondaryAndForeignHitsDoNotCreateAPrimaryFreezingRoll)
{
	freezeTarget();
	BattleAttack pending;
	pending.bsa.push_back(pendingHit(target->unitId()));
	pending.bsa.push_back(pendingHit(ice->unitId(), BattleStackAttacked::SECONDARY));
	const FrozenAttackContext context{*battle(), ice, target, 0, false, true};
	const auto result = prepareFrozenAttack(context, pending);
	EXPECT_FALSE(result.freezingTouch);
	EXPECT_FALSE(pending.bsa.front().shattered());
	EXPECT_TRUE(pending.bsa.back().shattered());
	ASSERT_EQ(result.shatter.toRemove.size(), 1u);
}

TEST_F(FrozenAttackStageFixture, SpellLikePrimaryDoesNotShatterOrCreateAFreezingRoll)
{
	freezeTarget();
	BattleAttack pending;
	pending.flags = BattleAttack::SPELL_LIKE;
	pending.bsa.push_back(pendingHit(ice->unitId()));
	const FrozenAttackContext context{*battle(), ice, target, 0, false, false};
	const auto result = prepareFrozenAttack(context, pending);
	EXPECT_FALSE(result.freezingTouch);
	EXPECT_TRUE(result.shatter.toRemove.empty());
	EXPECT_FALSE(pending.bsa.front().shattered());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
}
