/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../AI/BattleAI/ConsecratedArmsValue.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/NominalAbilityChance.h"
#include "../../lib/gameState/CGameState.h"
#include <limits>
#include <set>
#include <algorithm>
#include <array>

namespace
{
using namespace consecratedArms;
Policy policy(Perk perk)
{
	Policy result;
	result.rank = 3;
	result.perks = static_cast<uint16_t>(perk);
	return result;
}

TEST(ConsecratedArmsValueTest, SinglePrayerKeepsAllSixIncludingUselessResults)
{
	std::set<Prayer> seen;
	Policy basic; basic.rank = 1;
	const auto result = consecratedArmsAI::expectedPrayers(basic, [&](auto prayers) -> std::optional<float>
	{
		EXPECT_EQ(prayers.size(), 1u);
		seen.insert(prayers.front());
		return prayers.front() == Prayer::DIVINE_PURPOSE ? 60 : 0;
	});
	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(*result, 10);
	EXPECT_EQ(seen.size(), 6u);
}
TEST(ConsecratedArmsValueTest, PreventionChangesSuccessMassWithoutRestoringAnAdverseRoll)
{
	const nominalAbilityChance::Outcomes before{0.1, 0.2, 0.3, 0.4};
	const auto result = nominalAbilityChance::afterPrevention(before, 4000);
	EXPECT_NEAR(result[0], 0.18, 1e-12);
	EXPECT_NEAR(result[1], 0.12, 1e-12);
	EXPECT_NEAR(result[2], 0.46, 1e-12);
	EXPECT_NEAR(result[3], 0.24, 1e-12);
	EXPECT_DOUBLE_EQ(result[0] + result[1], before[0] + before[1]);
	EXPECT_EQ(nominalAbilityChance::afterPrevention(before, 0), before);
	const auto prevented = nominalAbilityChance::afterPrevention(before, 10000);
	EXPECT_DOUBLE_EQ(prevented[1], 0);
	EXPECT_DOUBLE_EQ(prevented[3], 0);
	EXPECT_DOUBLE_EQ(prevented[0] + prevented[2], 1);
}
TEST(ConsecratedArmsValueTest, AntiphonalVisitsFifteenPairsWithoutReplacementOrMaximization)
{
	std::set<std::pair<Prayer, Prayer>> seen;
	const auto result = consecratedArmsAI::expectedPrayers(policy(Perk::ANTIPHONAL_PRAYER),
		[&](auto prayers) -> std::optional<float>
		{
			EXPECT_EQ(prayers.size(), 2u);
			EXPECT_LT(prayers[0], prayers[1]);
			seen.emplace(prayers[0], prayers[1]);
			return std::ranges::find(prayers, Prayer::DIVINE_PURPOSE) != prayers.end() ? 60 : 0;
		});
	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(*result, 20);
	EXPECT_EQ(seen.size(), 15u);
}
TEST(ConsecratedArmsValueTest, PairSynergyUsesItsActualJointProbability)
{
	const auto result = consecratedArmsAI::expectedPrayers(policy(Perk::ANTIPHONAL_PRAYER),
		[](auto prayers) -> std::optional<float>
		{
			return prayers[0] == Prayer::DIVINE_PURPOSE && prayers[1] == Prayer::DAUNTLESS_ASSAULT ? 150 : 0;
		});
	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(*result, 10);
}
TEST(ConsecratedArmsValueTest, UnknownOrNonFiniteMassIsNotRenormalized)
{
	EXPECT_FALSE(consecratedArmsAI::expectedPrayers(policy(Perk::ANTIPHONAL_PRAYER),
		[](auto) -> std::optional<float> { return std::nullopt; }));
	EXPECT_FALSE(consecratedArmsAI::expectedPrayers(policy(Perk::ANTIPHONAL_PRAYER),
		[](auto) -> std::optional<float> { return std::numeric_limits<float>::quiet_NaN(); }));
}
TEST(ConsecratedArmsValueTest, ExistingFullDurationPrayersHaveNoDuplicateStrengthOrShieldRefillCredit)
{
	Policy basic; basic.rank = 1;
	consecratedArmsAI::Prayers before;
	for(size_t index = 0; index < before.size(); ++index)
		before[index] = ActivePrayer{static_cast<Prayer>(index), 200, duration(200, basic), basic};
	consecratedArmsAI::Exposure exposure;
	exposure.outgoing = exposure.incoming = exposure.retaliation = 100;
	exposure.count = 10; exposure.firstHP = exposure.maximumHP = 10;
	EXPECT_FLOAT_EQ(consecratedArmsAI::expectedRecipientValue(basic, 200, before, exposure), 0);
}
TEST(ConsecratedArmsValueTest, NewlyUsefulPurposeAndRetaliationProtectionHavePositiveExpectedValue)
{
	Policy basic; basic.rank = 1;
	consecratedArmsAI::Exposure exposure;
	exposure.outgoing = exposure.retaliation = 100;
	exposure.movement = 5;
	EXPECT_GT(consecratedArmsAI::expectedRecipientValue(basic, 200, {}, exposure), 0);
}
TEST(ConsecratedArmsValueTest, OneCreatureVowDoesNotInventSurvivalAgainstALethalHit)
{
	Policy basic; basic.rank = 1;
	consecratedArmsAI::Exposure exposure;
	exposure.incoming = 10; exposure.count = 1; exposure.firstHP = exposure.maximumHP = 10;
	EXPECT_FLOAT_EQ(consecratedArmsAI::expectedRecipientValue(basic, 200, {}, exposure), 0);
}
TEST(ConsecratedArmsValueTest, LivingLiturgyCreditsExistingPrayerDurationWithoutRecapturingItsFaith)
{
	const auto current = policy(Perk::LIVING_LITURGY);
	consecratedArmsAI::Prayers before;
	before[0] = ActivePrayer{Prayer::DIVINE_PURPOSE, 200, 1, current};
	consecratedArmsAI::Exposure exposure;
	exposure.outgoing = 100; exposure.movement = 5;
	const auto ordinary = consecratedArmsAI::expectedRecipientValue(Policy{3}, 200, before, exposure);
	EXPECT_GT(consecratedArmsAI::expectedRecipientValue(current, 200, before, exposure), ordinary);
}
}

namespace heroQueueTest
{
class ConsecratedDamageProjectionTest : public HeroQueueSelectorTest
{
protected:
	void originalRedemptionArena()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(consecratedArms::SKILL));
		ASSERT_TRUE(skill.hasValue());
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_EQ(consecratedArms::capturePolicy(hero).rank, 1);
			hero->clearSlots();
		}
		// Redemption's immutable cap belongs to original army stacks captured
		// during localInit, not arena()'s later slotless ADD recipients.
		attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1);
		attackerSideHero->setCreature(SlotID(1), creatureByName("core:angel"), 100);
		attackerSideHero->setCreature(SlotID(2), creatureByName("core:pikeman"), 100);
		defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		for(const auto & stack : battle()->stacks)
		{
			if(stack->unitSide() == BattleSide::DEFENDER && stack->unitSlot() == SlotID(0)) enemy = stack.get();
			if(stack->unitSide() != BattleSide::ATTACKER) continue;
			if(stack->unitSlot() == SlotID(0)) active = stack.get();
			if(stack->unitSlot() == SlotID(1)) actor = stack.get();
			if(stack->unitSlot() == SlotID(2)) waiter = stack.get();
		}
		const std::array<CStack *, 4> units{active, actor, waiter, enemy};
		const std::array<BattleHex, 4> positions{BattleHex(1, 5), BattleHex(7, 5), BattleHex(3, 5), BattleHex(8, 5)};
		for(size_t index = 0; index < units.size(); ++index)
		{
			auto * unit = units[index];
			ASSERT_NE(unit, nullptr);
			unit->setPosition(positions[index]); // Initial scenario geometry before combat.
			const auto basis = static_cast<int64_t>(unit->unitBaseAmount()) * unit->getMaxHealth();
			ASSERT_GT(basis, 0);
			ASSERT_EQ(unit->getBattleStartMaximumAggregateHP(), basis);
			effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000 - static_cast<int>(index) * 200, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
			effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		}
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_EQ(battle()->battleActiveUnit(), active);
	}
	void prepare(bool originalRedemption = false)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		if(originalRedemption)
		{
			ASSERT_NO_FATAL_FAILURE(originalRedemptionArena());
		}
		else
		{
			ASSERT_NO_FATAL_FAILURE(arena());
		}
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		parent->useReferenceForecast();
	}
};
TEST_F(ConsecratedDamageProjectionTest, SharedPreviewPricesStoredRedemptionWithoutRefillOrMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto target = parent->getForUpdate(actor->unitId());
	consecratedArms::Policy policy; policy.rank = 1;
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::VICARIOUS_REDEMPTION,
		200, policy, 3, 1));
	ASSERT_GT(target->gainRedemptionShield(1000, 3000, false), 0);
	const auto shield = target->getRedemptionShieldHitPoints();
	const auto before = target->getAvailableHealth();
	const auto preview = battleAIProjectDamage(target.get(), shield + 37, battle::DamageProvenance::MAGICAL_CREATURE);
	EXPECT_EQ(preview.healthLoss, 37);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(target->getRedemptionShieldHitPoints(), shield);
	auto outcome = target->acquireState();
	auto damage = shield + 37;
	outcome->damage(damage, false, battle::DamageProvenance::MAGICAL_CREATURE);
	EXPECT_EQ(before - outcome->getAvailableHealth(), preview.healthLoss);
}
TEST_F(ConsecratedDamageProjectionTest, ExplicitSharedQuotaPreviewMatchesNativeCohortsWithoutSpendingQuota)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto target = parent->getForUpdate(actor->unitId());
	const auto quota = battle::VowCasualtyQuota::capture(target->unitId(), target->getCount(), 4000);
	auto previewQuota = quota;
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE, &previewQuota};
	const auto before = target->getAvailableHealth();
	const auto preview = battleAIProjectDamage(target.get(), before * 2, battle::DamageProvenance::PHYSICAL_CREATURE, &context);
	EXPECT_EQ(previewQuota.remainingCasualties, quota.remainingCasualties);
	EXPECT_LT(preview.healthLoss, before);
	auto outcome = target->acquireState();
	auto damage = before * 2;
	outcome->damageWithContext(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE, context);
	EXPECT_EQ(before - outcome->getAvailableHealth(), preview.healthLoss);
	EXPECT_TRUE(outcome->alive());
}
TEST_F(ConsecratedDamageProjectionTest, PhysicalBlowAndJudgmentShareOneQuotaAndNextBlowGetsANewQuota)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = parent->getForUpdate(actor->unitId());
	auto target = parent->getForUpdate(enemy->unitId());
	consecratedArms::Policy policy; policy.rank = 1;
	source->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::HEAVENS_JUDGMENT, 400, policy, 3, 1));
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::UNBROKEN_VOW, 400, policy, 3, 1));
	const auto before = target->getCount();
	const auto permitted = consecratedArms::casualtyLimit(before,
		consecratedArms::prayerValues(*target, consecratedArms::Prayer::UNBROKEN_VOW).casualtyCeilingBP);
	PhysicalStrikeProjection projection(parent.get());
	BattleAttackInfo attack(source.get(), target.get(), 0, false);
	auto amount = target->getAvailableHealth() * 2;
	projection.damage(*parent, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto afterBody = target->getAvailableHealth();
	projection.finish(source->unitId());
	EXPECT_EQ(target->getCount(), before - permitted);
	EXPECT_EQ(target->getAvailableHealth(), afterBody) << "Judgment cannot bypass the spent blow quota";
	amount = target->getAvailableHealth() * 2;
	projection.damage(*parent, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(source->unitId());
	EXPECT_LT(target->getCount(), before - permitted);
}
TEST_F(ConsecratedDamageProjectionTest, RedemptionWaitsForJudgmentAndRepeatedFinishCannotRefillShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto source = parent->getForUpdate(actor->unitId());
	auto target = parent->getForUpdate(enemy->unitId());
	consecratedArms::Policy policy; policy.rank = 1;
	source->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::HEAVENS_JUDGMENT, 400, policy, 3, 1));
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::VICARIOUS_REDEMPTION, 400, policy, 3, 1));
	const auto before = target->getAvailableHealth();
	const auto request = static_cast<int64_t>(target->getMaxHealth()) * 10;
	PhysicalStrikeProjection projection(parent.get());
	BattleAttackInfo attack(source.get(), target.get(), 0, false);
	auto amount = request;
	projection.damage(*parent, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(target->getRedemptionShieldHitPoints(), 0);
	projection.finish(source->unitId());
	EXPECT_LT(target->getAvailableHealth(), before - request) << "Judgment precedes casualty shield acquisition";
	ASSERT_GT(target->getRedemptionShieldHitPoints(), 0);
	const auto shield = target->getRedemptionShieldHitPoints();
	const auto health = target->getAvailableHealth();
	projection.finish(source->unitId());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(target->getRedemptionShieldHitPoints(), shield);
}
TEST_F(ConsecratedDamageProjectionTest, FearlessCounterCorrectionComposesWithExistingCounterFailureWithoutRolling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto target = parent->getForUpdate(actor->unitId());
	consecratedArms::Policy policy;
	policy.rank = 1; policy.perks = static_cast<uint16_t>(consecratedArms::Perk::FEARLESS_ADVANCE);
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::DAUNTLESS_ASSAULT, 200, policy, 3, 1));
	const auto before = target->save();
	EXPECT_FLOAT_EQ(consecratedArmsAI::fearlessCounterCorrection(*target, 100), 20);
	EXPECT_FLOAT_EQ(consecratedArmsAI::fearlessCounterCorrection(*target, 100, 0.4f), 8);
	EXPECT_EQ(target->save(), before);
}
TEST_F(ConsecratedDamageProjectionTest, ExistingAttackEvaluationCreditsObservedJudgmentWithoutChangingLiveWorldOrRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto random = wire(*gameHandler->randomizer);
	const auto worldBefore = gameState()->saveToMemory();
	auto source = parent->getForUpdate(actor->unitId());
	auto target = parent->getForUpdate(enemy->unitId());
	auto injury = source->getAvailableHealth() - source->getMaxHealth();
	source->damage(injury, false, battle::DamageProvenance::OTHER);
	ASSERT_EQ(source->getCount(), 1);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const BattleAttackInfo attack(source.get(), target.get(), 0, false);
	const auto ordinary = AttackPossibility::evaluate(attack, source->getPosition(), cache, parent);
	consecratedArms::Policy policy; policy.rank = 1;
	source->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::HEAVENS_JUDGMENT, 400, policy, 3, 1));
	const auto blessed = AttackPossibility::evaluate(attack, source->getPosition(), cache, parent);
	EXPECT_GT(blessed.defenderDamageReduce, ordinary.defenderDamageReduce);
	EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}
TEST_F(ConsecratedDamageProjectionTest, RighteousSentenceUsesCompletedActionAndRoundResetRatherThanWait)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = parent->getForUpdate(actor->unitId());
	auto target = parent->getForUpdate(enemy->unitId());
	consecratedArms::Policy policy;
	policy.rank = 2; policy.perks = static_cast<uint16_t>(consecratedArms::Perk::RIGHTEOUS_SENTENCE);
	source->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::HEAVENS_JUDGMENT, 200, policy, 3, 1));
	const auto potency = consecratedArms::judgmentPotency(*source);
	target->waiting = true;
	const auto waiting = consecratedArms::judgmentRawDamage(potency, *target, 100);
	ASSERT_FALSE(target->hasCompletedActionThisRound());
	PhysicalStrikeProjection projection(parent.get());
	BattleAttackInfo counter(target.get(), source.get(), 0, false);
	counter.retaliation = true;
	int64_t amount = 1;
	projection.damage(*parent, counter, *source, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(target->unitId());
	EXPECT_FALSE(target->hasCompletedActionThisRound()) << "Retaliation is not a completed action";
	consecratedArmsAI::completeObservedAction(*parent, target->unitId());
	EXPECT_GT(consecratedArms::judgmentRawDamage(potency, *target, 100), waiting);
	parent->nextRound();
	EXPECT_FALSE(parent->battleGetUnitByID(target->unitId())->hasCompletedActionThisRound());
	EXPECT_EQ(consecratedArms::judgmentRawDamage(potency, *parent->battleGetUnitByID(target->unitId()), 100), waiting);
}
TEST_F(ConsecratedDamageProjectionTest, SelectedCounterJudgmentReplayDoesNotApplyResidualDamageTwice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = parent->getForUpdate(actor->unitId());
	auto target = parent->getForUpdate(enemy->unitId());
	auto injury = source->getAvailableHealth() - 10 * source->getMaxHealth();
	source->damage(injury, false, battle::DamageProvenance::OTHER);
	consecratedArms::Policy policy; policy.rank = 1;
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::HEAVENS_JUDGMENT, 400, policy, 3, 1));
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const BattleAttackInfo attack(source.get(), target.get(), 0, false);
	const auto projection = AttackPossibility::evaluate(attack, source->getPosition(), cache, parent);
	ASSERT_TRUE(projection.effectPreview);
	ASSERT_FALSE(projection.fortuneStrikes.empty());
	auto replay = std::make_shared<HypotheticBattle>(environment.get(), parent);
	BattleExchangeVariant exchange;
	exchange.trackAttack(projection, replay, cache);
	EXPECT_EQ(replay->battleGetUnitByID(source->unitId())->getAvailableHealth(), projection.attackerState->getAvailableHealth());
	EXPECT_EQ(replay->battleGetUnitByID(target->unitId())->getAvailableHealth(),
		projection.effectPreview->battleGetUnitByID(target->unitId())->getAvailableHealth());
}
TEST_F(ConsecratedDamageProjectionTest, CommunionPeerOutsideHitRecipientsReceivesOneOwnedRestorableShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto source = parent->getForUpdate(enemy->unitId());
	auto target = parent->getForUpdate(actor->unitId());
	auto peer = parent->getForUpdate(waiter->unitId());
	consecratedArms::Policy policy;
	policy.rank = 2; policy.perks = static_cast<uint16_t>(consecratedArms::Perk::COMMUNION_OF_SAINTS);
	peer->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::VICARIOUS_REDEMPTION, 200, policy, 3, 1));
	PhysicalStrikeProjection projection(parent.get());
	BattleAttackInfo attack(source.get(), target.get(), 0, false);
	auto amount = static_cast<int64_t>(target->getMaxHealth()) * 10;
	projection.damage(*parent, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(peer->getRedemptionShieldHitPoints(), 0);
	projection.finish(source->unitId());
	ASSERT_GT(peer->getRedemptionShieldHitPoints(), 0);
	auto restored = std::make_shared<HypotheticBattle>(environment.get(), live);
	restored->getForUpdate(peer->unitId())->addUnitBonus(consecratedArms::prayerBonuses(
		consecratedArms::Prayer::VICARIOUS_REDEMPTION, 200, policy, 3, 1));
	consecratedArmsAI::restoreShieldState(*restored, *parent);
	EXPECT_EQ(restored->battleGetUnitByID(peer->unitId())->getRedemptionShieldHitPoints(), peer->getRedemptionShieldHitPoints());
}
TEST_F(ConsecratedDamageProjectionTest, UnshakableUsesCapturedAfflictionFamilyAndPreservesFriendlyOrBeneficialEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = parent->getForUpdate(enemy->unitId());
	auto target = parent->getForUpdate(actor->unitId());
	EXPECT_EQ(consecratedArmsAI::afflictionResistanceBP(*parent, *source, *target, creatureAbility::Native::PHYSICAL_POISON), 0);
	consecratedArms::Policy policy;
	policy.rank = 1; policy.perks = static_cast<uint16_t>(consecratedArms::Perk::UNSHAKABLE_FAITH);
	target->addUnitBonus(consecratedArms::prayerBonuses(consecratedArms::Prayer::STEADFAST_SOUL, 250, policy, 3, 1));
	const auto before = target->save();
	const int expected = consecratedArms::prayerValues(*target, consecratedArms::Prayer::STEADFAST_SOUL).resistanceBP;
	EXPECT_EQ(consecratedArmsAI::afflictionResistanceBP(*parent, *source, *target, creatureAbility::Native::PHYSICAL_POISON), expected);
	EXPECT_EQ(consecratedArmsAI::afflictionResistanceBP(*parent, *target, *target, creatureAbility::Native::PHYSICAL_POISON), 0);
	EXPECT_EQ(consecratedArmsAI::afflictionResistanceBP(*parent, *source, *target, *SpellID(SpellID::BLESS).toSpell()), 0);
	EXPECT_EQ(target->save(), before);
	ASSERT_TRUE(parent->isMeleeAttackPossible(source.get(), target.get()));
	PhysicalStrikeProjection nominal(parent.get());
	BattleAttackInfo attack(source.get(), target.get(), 0, false);
	const auto health = target->getAvailableHealth();
	int64_t amount = 1;
	nominal.damage(*parent, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	nominal.finish(source->unitId());
	EXPECT_TRUE(nominal.uncertainAfflictions());
	EXPECT_EQ(target->getAvailableHealth(), health - 1);
}
}
