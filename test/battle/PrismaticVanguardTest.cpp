/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../server/battles/PrismaticVanguardFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/ElementalRebirthLineage.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace vanguard = newHorizonsPrismaticVanguard;
class PrismaticVanguardTest : public PrismaticVanguardFixture {};
TEST_F(PrismaticVanguardTest, DeploymentCapturesBothBodiesAndPreActivationRetaliationDoesNotBenefit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto * unit : {actor, upgraded})
	{
		const auto entries = markers(*unit);
		ASSERT_EQ(entries->size(), 1u);
		EXPECT_EQ(entries->front()->duration, BonusDuration::ONE_BATTLE);
		EXPECT_EQ(entries->front()->val, 20);
		BattleAttackInfo attack(unit, victim, 0, false);
		attack.retaliation = true;
		EXPECT_EQ(vanguard::activeDamagePercent(attack), 0);
	}
	EXPECT_TRUE(markers(*victim)->empty());
}
TEST_F(PrismaticVanguardTest, FirstActualActivationAndAcceptedWaitConsumeOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(vanguard::activeDamagePercent(attack), 20);
	attack.physicalDamage = false;
	EXPECT_EQ(vanguard::activeDamagePercent(attack), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeWait(actor)));
	EXPECT_TRUE(markers(*actor)->empty());
	EXPECT_TRUE(vanguard::activationPlan(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE).add.empty());
	EXPECT_EQ(markers(*upgraded)->size(), 1u);
}
TEST_F(PrismaticVanguardTest, ContinuationsAndRejectedActionsPreservePendingEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto reason : {BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION, BattleUnitTurnReason::HERO_SPELLCAST,
		BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
	{
		const auto plan = vanguard::activationPlan(*battle(), *actor, reason);
		EXPECT_TRUE(plan.remove.empty()); EXPECT_TRUE(plan.add.empty());
	}
	EXPECT_EQ(markers(*actor)->size(), 1u);
}
TEST_F(PrismaticVanguardTest, FrozenFollowsExistingGenuineActivationClock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect frozen;
	frozen.battleID = BattleID(0);
	frozen.toAdd.push_back({actor->unitId(), {newHorizonsFrozen::marker(BonusSourceID(), battle()->getRound())}});
	gameHandler->sendAndApply(frozen);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*actor));
	ASSERT_TRUE(battle()->battleBeginsActivation(actor, BattleUnitTurnReason::TURN_QUEUE));
	const auto plan = vanguard::activationPlan(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(plan.remove.size(), 1u);
	EXPECT_EQ(plan.add.size(), 1u);
}
TEST_F(PrismaticVanguardTest, FreshAdditionArmsBothFormsAndSameIdUpdateCannotRearmSpentEntry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const char * key : {"new-horizons:wisp", "new-horizons:wispUpgrade"})
	{
		auto * fresh = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode(key)), BattleHex(12, 4), 3);
		ASSERT_NE(fresh, nullptr);
		ASSERT_EQ(markers(*fresh)->size(), 1u);
		battle()->nextTurn(fresh->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		ASSERT_EQ(markers(*fresh)->front()->duration, BonusDuration::STACK_ACTIVATION);
		fresh->removeBonuses(CSelector(vanguard::isEffect)); // Model the existing accepted-action expiry, isolated from queue order.
		const JsonNode sameBody = fresh->save();
		battle()->updateUnit(fresh->unitId(), sameBody, 0);
		EXPECT_TRUE(markers(*fresh)->empty());
		battle()->removeUnit(fresh->unitId());
	}
}
TEST_F(PrismaticVanguardTest, RebirthProducerCurrentlyCannotChooseCoreWisps)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pool = newHorizonsElementalRebirth::legalCandidatePool(battle()->getCreatureCategoryRules(),
		battle()->getAccessibility(), BattleHex(10, 5), BattleSide::ATTACKER);
	ASSERT_FALSE(pool.empty());
	for(const char * key : {"new-horizons:wisp", "new-horizons:wispUpgrade"})
	{
		const auto creature = CreatureID(CreatureID::decode(key));
		EXPECT_FALSE(newHorizonsElementalRebirth::isCanonicalElemental(creature));
		EXPECT_EQ(std::find(pool.begin(), pool.end(), creature), pool.end());
		const auto category = battle()->getCreatureCategoryRules().lookup(key);
		ASSERT_TRUE(category);
		EXPECT_EQ(category->category, newHorizonsCreatures::CreatureCategory::CORE);
	}
}
class PrismaticVanguardCaptureTest : public PrismaticVanguardFixture,
	public ::testing::WithParamInterface<int> {};
TEST_P(PrismaticVanguardCaptureTest, MissingAndZeroCaptureNeverCreateEntryMarkers)
{
	capture = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(markers(*actor)->empty()); EXPECT_TRUE(markers(*upgraded)->empty());
	beginCombat();
	EXPECT_TRUE(markers(*actor)->empty());
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(actor, victim, 0, false)), 0);
}
INSTANTIATE_TEST_SUITE_P(HistoricalAndExplicitZero, PrismaticVanguardCaptureTest, ::testing::Values(-1, 0));
TEST_F(PrismaticVanguardTest, PhysicalConsumerChangesDuringActivationAndSameBodyHealingNeverRearms)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleAttackInfo attack(actor, victim, 0, false);
	const auto before = battle()->battleEstimateDamage(attack);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto active = battle()->battleEstimateDamage(attack);
	EXPECT_GT(active.damage.min, before.damage.min);
	EXPECT_GT(active.damage.max, before.damage.max);
	actor->removeBonuses(CSelector(vanguard::isEffect));
	for(const bool resurrect : {false, true})
	{
		auto state = actor->acquireState();
		int64_t damage = resurrect ? actor->getAvailableHealth() : 1;
		state->damage(damage);
		const JsonNode injured = state->save();
		battle()->updateUnit(actor->unitId(), injured, -damage);
		auto restoration = actor->acquireState();
		int64_t healing = resurrect ? actor->getMaxHealth() : 1;
		const auto receipt = restoration->heal(healing,
			resurrect ? EHealLevel::RESURRECT : EHealLevel::HEAL, EHealPower::PERMANENT);
		ASSERT_GT(receipt.healedHealthPoints, 0);
		const JsonNode restored = restoration->save();
		battle()->updateUnit(actor->unitId(), restored, receipt.healedHealthPoints);
		ASSERT_TRUE(actor->alive());
		EXPECT_TRUE(markers(*actor)->empty());
	}
}
TEST_F(PrismaticVanguardTest, PendingMarkerValidationAndUnrelatedBonusRemainIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto entries = markers(*actor);
	ASSERT_EQ(entries->size(), 1u);
	const auto pending = *entries->front();
	for(int kind = 0; kind < 5; ++kind)
	{
		auto invalid = pending;
		if(kind == 0) invalid.source = BonusSource::OTHER;
		if(kind == 1) invalid.val = 0;
		if(kind == 2) invalid.duration = BonusDuration::PERMANENT;
		if(kind == 3) invalid.turnsRemain = 1;
		if(kind == 4) invalid.statusIdentity = "unrelated";
		EXPECT_THROW(vanguard::isEffect(&invalid), std::runtime_error);
	}
	auto unrelated = pending;
	unrelated.stacking = "unrelated-legacy-monere-none";
	unrelated.statusIdentity = "unrelated";
	actor->addNewBonus(std::make_shared<Bonus>(unrelated));
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(actor->hasBonus(CSelector([](const Bonus * bonus)
	{
		return bonus->stacking == "unrelated-legacy-monere-none";
	})));
	ASSERT_EQ(markers(*actor)->size(), 1u);
	EXPECT_EQ(markers(*actor)->front()->duration, BonusDuration::STACK_ACTIVATION);
}
TEST_F(PrismaticVanguardTest, CanonicalMetadataRetainsBothFormsHeroEffectSuppressionAndEffectiveFormGuard)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * unit : {actor, upgraded})
	{
		battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		ASSERT_TRUE(unit->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT, 2,
			BonusSourceID(SpellID(SpellID::FORGETFULNESS))));
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*unit), 2);
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(unit, victim, 0, false)), 20);
		unit->beginBattleForm(creatureByName("core:pikeman"), 2);
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(unit, victim, 0, false)), 0);
		unit->endBattleForm();
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(unit, victim, 0, false)), 20);
	}
}
TEST_F(PrismaticVanguardTest, FirstGenuineNonWispFormSpendsPendingWithoutManufacturingALaterEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); actor->beginBattleForm(creatureByName("core:pikeman"), 2);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*actor)->empty()); actor->endBattleForm();
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*actor)->empty());
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(actor, victim, 0, false)), 0);
}
