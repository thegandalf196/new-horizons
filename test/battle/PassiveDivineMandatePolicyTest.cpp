/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/battle/PassiveDivineMandateVanguard.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/json/JsonNode.h"
#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>

namespace
{
using namespace newHorizonsPassiveDivineMandate;

Policy policy(uint8_t rank, std::initializer_list<Perk> perks = std::initializer_list<Perk>{})
{
	Policy result{rank};
	for(const auto perk : perks)
		result.perks |= static_cast<uint16_t>(perk);
	return result;
}

TargetChoice target(uint32_t id) { return {id, {id}}; }
}

TEST(PassiveDivineMandatePolicy, RankScalesOnlyAttributeEfficiencyAndCombatCapacity)
{
	for(uint8_t rank = 0; rank <= 3; ++rank)
	{
		const auto ordinary = policy(rank);
		EXPECT_EQ(orderEfficiencyPercent(ordinary), rank * 5);
		EXPECT_EQ(activationLimit(ordinary), rank);
		EXPECT_EQ(orderRiders(ordinary), OrderRiders{});
	}
	EXPECT_EQ(activationLimit(policy(3, {Perk::MANDATE_OF_HEAVEN})), 5);
	// External perk grants do not invent a second action currency or force rank3.
	EXPECT_EQ(activationLimit(policy(1, {Perk::MANDATE_OF_HEAVEN})), 3);
}

TEST(PassiveDivineMandatePolicy, AcceptedCastsSpendOnceAndExhaustedUsesCannotAdvance)
{
	const auto hero = policy(3, {Perk::MANDATE_OF_HEAVEN, Perk::CHAPLAINS_RESERVE});
	Usage used;
	for(uint8_t count = 0; count < 5; ++count)
	{
		EXPECT_EQ(remainingActivations(hero, used), 5 - count);
		const auto next = afterAcceptedCast(hero, used);
		ASSERT_TRUE(next);
		EXPECT_EQ(next->completedCasts, count + 1);
		EXPECT_EQ(manaRecovery(hero), 3); // Every cast, not only the first one.
		used = *next;
	}
	EXPECT_EQ(remainingActivations(hero, used), 0);
	EXPECT_FALSE(afterAcceptedCast(hero, used));
	EXPECT_THROW(remainingActivations(policy(1), used), std::runtime_error);
}

TEST(PassiveDivineMandatePolicy, IneligibleMassVersionDoesNotHideItsUsefulSingleVersion)
{
	std::vector<SpellChoice> choices{
		{SpellID(1), SpellID(1), false, {target(10)}},
		{SpellID(2), SpellID(1), true, {}},
		{SpellID(3), SpellID(3), false, {target(11)}}
	};
	const auto pool = finalizeSpellPool(choices, {});
	ASSERT_EQ(pool.size(), 2);
	EXPECT_EQ(pool[0].spell, SpellID(1));
	EXPECT_EQ(pool[1].spell, SpellID(3));
}

TEST(PassiveDivineMandatePolicy, UsefulMassVersionSupersedesOnlyItsOwnSingleFamily)
{
	std::vector<SpellChoice> choices{
		{SpellID(1), SpellID(1), false, {target(10)}},
		{SpellID(2), SpellID(1), true, {{10, {10, 11}}}},
		{SpellID(3), SpellID(3), false, {target(12)}}
	};
	const auto pool = finalizeSpellPool(choices, {11});
	ASSERT_EQ(pool.size(), 2);
	EXPECT_EQ(pool[0].spell, SpellID(2));
	EXPECT_EQ(pool[0].targets.front().recipientUnitIds, (std::vector<uint32_t>{10, 11}));
	EXPECT_EQ(pool[1].spell, SpellID(3));
	EXPECT_EQ(selectSpell(pool, 0, 0).target.recipientUnitIds, (std::vector<uint32_t>{10, 11}));
	EXPECT_THROW(selectSpell(pool, 0, 1), std::runtime_error);
}

TEST(PassiveDivineMandatePolicy, OrderPreferenceChangesRecipientPoolWithoutRemovingOtherSpells)
{
	std::vector<SpellChoice> choices{
		{SpellID(1), SpellID(1), false, {target(10), target(11), target(12)}},
		{SpellID(2), SpellID(2), false, {target(20), target(21)}}
	};
	const auto pool = finalizeSpellPool(choices, {11, 12});
	ASSERT_EQ(pool.size(), 2);
	EXPECT_EQ(pool[0].targets, (std::vector<TargetChoice>{target(11), target(12)}));
	EXPECT_EQ(pool[1].targets, (std::vector<TargetChoice>{target(20), target(21)}));
	EXPECT_EQ(selectSpell(pool, 1, 1).target.aimUnitId, 21);
	EXPECT_THROW(selectSpell(pool, 2, 0), std::runtime_error);
	EXPECT_THROW(selectSpell(pool, 1, 2), std::runtime_error);
}

TEST(PassiveDivineMandatePolicy, EachOrderRiderHasIndependentSpecifiedMagnitude)
{
	const auto riders = orderRiders(policy(3, {
		Perk::SACRED_COMMAND, Perk::KNIGHTLY_VALOR, Perk::SHIELD_OF_FAITH,
		Perk::RIGHTEOUS_DISCIPLINE, Perk::UNWAVERING_STANDARD,
		Perk::CONSECRATED_ARMS, Perk::DIVINE_FURY}));
	EXPECT_EQ(riders.morale, 1);
	EXPECT_EQ(riders.luck, 1);
	EXPECT_EQ(riders.meleeDamagePercent, 10);
	EXPECT_EQ(riders.enemyAttackReductionPercent, 10);
	EXPECT_EQ(riders.extraRetaliations, 1);
	EXPECT_TRUE(riders.ignoreNegativeMorale);
	EXPECT_EQ(riders.meleeDefenseIgnorePercent, 10);
	EXPECT_TRUE(riders.fury);
	EXPECT_EQ(inspiringSpeed(policy(2, {Perk::INSPIRING_LEADERSHIP})), 2);
}

TEST(PassiveDivineMandatePolicy, FuryDoesNotAcceptSpellCollateralDeadFriendlyOrIllegalTargets)
{
	const auto hero = policy(3, {Perk::DIVINE_FURY});
	EXPECT_EQ(FURY_CHANCE_PERCENT, 10);
	EXPECT_TRUE(furyEligible(hero, true, true, true, true));
	EXPECT_FALSE(furyEligible(hero, false, true, true, true));
	EXPECT_FALSE(furyEligible(hero, true, false, true, true));
	EXPECT_FALSE(furyEligible(hero, true, true, false, true));
	EXPECT_FALSE(furyEligible(hero, true, true, true, false));
	EXPECT_FALSE(furyEligible(policy(3), true, true, true, true));
}

TEST(PassiveDivineMandatePolicy, VanguardMarkerCannotMasqueradeAsLegacyRoyalEntitlement)
{
	Bonus marker;
	marker.type = BonusType::NONE;
	marker.source = BonusSource::HERO_COMMAND;
	marker.valType = BonusValueType::BASE_NUMBER;
	marker.stacking = "vanguard";
	marker.duration = BonusDuration::N_TURNS;
	marker.turnsRemain = 3;
	marker.subtype = BonusSubtypeID(BonusCustomSubtype(1));
	marker.val = 1; // Sacred Command alone deliberately overlaps legacy Royal numeric value1.
	EXPECT_TRUE(isVanguardRiderEntitlement(marker));
	EXPECT_NO_THROW(validateVanguardRiderEntitlement(marker));
	marker.valType = BonusValueType::ADDITIVE_VALUE;
	EXPECT_FALSE(isVanguardRiderEntitlement(marker));
	marker.valType = BonusValueType::BASE_NUMBER;
	marker.val = 0;
	EXPECT_TRUE(isVanguardRiderEntitlement(marker));
	EXPECT_THROW(validateVanguardRiderEntitlement(marker), std::runtime_error);
	marker.val = 1;
	marker.subtype = BonusSubtypeID(BonusCustomSubtype(0));
	EXPECT_THROW(validateVanguardRiderEntitlement(marker), std::runtime_error);
	marker.subtype = BonusSubtypeID(BonusCustomSubtype(2));
	marker.turnsRemain = 4;
	EXPECT_THROW(validateVanguardRiderEntitlement(marker), std::runtime_error);
}

TEST(PassiveDivineMandatePolicy, OnlyExactCapturedIntegerVersionTwoEnablesNewRules)
{
	JsonNode rules;
	EXPECT_FALSE(enabled(rules));
	rules["divineMandatePolicy"]["version"].Integer() = 1;
	EXPECT_FALSE(enabled(rules));
	rules["divineMandatePolicy"]["version"].Integer() = 2;
	EXPECT_TRUE(enabled(rules));
	rules["divineMandatePolicy"]["version"].Float() = 2.0;
	EXPECT_FALSE(enabled(rules));
	rules["divineMandatePolicy"]["version"].String() = "2";
	EXPECT_FALSE(enabled(rules));
}
