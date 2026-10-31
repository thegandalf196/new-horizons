/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/NewHorizonsOrdinaryRangedSpecialtyRules.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#if ENABLE_BATTLE_AI
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
#include <stdexcept>
namespace
{
using newHorizonsOrdinaryRangedSpecialtyRules::orcCasualtyDamagePercent;
class NewHorizonsJabarkasSpecialtyRulesTest : public BattleTestFixture
{
protected:
	CStack * orc = nullptr;
	CStack * target = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:orcChieftain")), 10));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 3));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
			{
				if(unit->unitSide() == BattleSide::ATTACKER) orc = unit.get();
				else target = unit.get();
			}
		ASSERT_NE(orc, nullptr);
		ASSERT_NE(target, nullptr);
		orc->setPosition(BattleHex(3, 5));
		target->setPosition(BattleHex(6, 5));
	}
	CStack * added(const char * creature, int count = 3, int row = 7)
	{
		return addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode(creature)), BattleHex(12, row), count);
	}
	int premium(const battle::Unit & recipient, int percent = 20) const
	{
		return orcCasualtyDamagePercent(BattleAttackInfo(orc, &recipient, 0, true), percent);
	}
	static void loseOne(battle::CUnitState & recipient)
	{
		int64_t damage = recipient.getMaxHealth();
		recipient.damage(damage);
	}
	DamageEstimation consumer(const battle::Unit & source, const battle::Unit & recipient, int percent) const
	{
		const BattleAttackInfo attack(&source, &recipient, 0, true);
		DamageAttackInfo payload;
		payload.attacker = &source;
		payload.defender = &recipient;
		payload.attackerHex = source.getPosition();
		payload.defenderHex = recipient.getPosition();
		payload.shooting = true;
		payload.physicalDamageReductionCapPercent = 80;
		payload.intrinsicPhysicalDamageReductionPercent = 50;
		// Characterize the real shared calculator consumer, not hero entitlement.
		payload.ordinaryCreatureSpecialtyDamagePercent = 100 + orcCasualtyDamagePercent(attack, percent);
		return LIBRARY->scriptTypes()->getDamageCalculator()->calculate(*battle(), payload);
	}
};
#if ENABLE_BATTLE_AI
class RangedSpecialtyEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RangedSpecialtyEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, DeployedAndAddedUnitsKeepTheirOwnEntryCounts)
{
	EXPECT_EQ(target->unitBaseAmount(), 3);
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(premium(*target), 0);
	auto * reinforcement = added("core:pikeman", 7);
	EXPECT_EQ(reinforcement->unitBaseAmount(), 7);
	EXPECT_EQ(reinforcement->getCurrentOriginalCreatureCount(), 7);
	loseOne(*reinforcement);
	EXPECT_EQ(reinforcement->unitBaseAmount(), 7);
	EXPECT_EQ(reinforcement->getCurrentOriginalCreatureCount(), 6);
	EXPECT_EQ(premium(*reinforcement), 20);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, TopWoundsDoNotCountButCurrentCreatureLossDoes)
{
	ASSERT_GT(target->getMaxHealth(), 1u);
	int64_t damage = 1;
	target->damage(damage);
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(premium(*target), 0);
	loseOne(*target);
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), 2);
	EXPECT_EQ(premium(*target, 0), 0);
	EXPECT_EQ(premium(*target, 37), 37);
	EXPECT_THROW(premium(*target, -1), std::invalid_argument);
	EXPECT_THROW(premium(*target, 101), std::invalid_argument);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, BothOrcFormsQualifyWithoutInventingAPrimaryOnlyRestriction)
{
	loseOne(*target);
	const auto * base = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:orc")), BattleHex(3, 7), 3);
	for(const auto * source : {base, static_cast<const CStack *>(orc)})
	{
		BattleAttackInfo attack(source, target, 0, true);
		EXPECT_EQ(orcCasualtyDamagePercent(attack, 20), 20);
		attack.secondaryAttack = true;
		EXPECT_EQ(orcCasualtyDamagePercent(attack, 20), 20);
		attack.retaliation = true;
		attack.archeryRangedDamageMultiplierPercent = 50;
		EXPECT_EQ(orcCasualtyDamagePercent(attack, 20), 20);
		attack.physicalDamage = false;
		EXPECT_EQ(orcCasualtyDamagePercent(attack, 20), 0);
		attack.physicalDamage = true;
		attack.shooting = false;
		EXPECT_EQ(orcCasualtyDamagePercent(attack, 20), 0);
	}
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:archer")), BattleHex(3, 8), 3);
	EXPECT_EQ(orcCasualtyDamagePercent(BattleAttackInfo(archer, target, 0, true), 20), 0);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, UnhurtLargerFormCannotInventACombatEntryCasualty)
{
	target->beginBattleForm(CreatureID(CreatureID::decode("core:ancientBehemoth")), 2);
	ASSERT_LT(target->getCount(), target->unitBaseAmount());
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(premium(*target), 0);
	target->endBattleForm();
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(premium(*target), 0);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, HurtSmallerFormRetainsItsSourceSpeciesDeficit)
{
	auto * large = added("core:ancientBehemoth");
	loseOne(*large);
	ASSERT_EQ(large->getCurrentOriginalCreatureCount(), 2);
	large->beginBattleForm(CreatureID(CreatureID::decode("core:peasant")), 2);
	ASSERT_GT(large->getCount(), large->unitBaseAmount());
	EXPECT_EQ(large->getCurrentOriginalCreatureCount(), 2);
	EXPECT_EQ(premium(*large), 20);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, DamageAndBothResurrectionPowersUpdateProvenanceDuringForm)
{
	for(const auto power : {EHealPower::PERMANENT, EHealPower::ONE_BATTLE})
	{
		auto * source = added("core:pikeman", 3, power == EHealPower::PERMANENT ? 7 : 8);
		const auto originalMaximum = source->getMaxHealth();
		source->beginBattleForm(CreatureID(CreatureID::decode("core:peasant")), 2);
		int64_t damage = originalMaximum;
		source->damage(damage);
		ASSERT_EQ(source->getCurrentOriginalCreatureCount(), 2);
		EXPECT_EQ(premium(*source), 20);
		int64_t healing = originalMaximum;
		source->heal(healing, EHealLevel::RESURRECT, power);
		ASSERT_EQ(healing, originalMaximum);
		EXPECT_EQ(source->getCurrentOriginalCreatureCount(), 3);
		EXPECT_EQ(premium(*source), 0);
		EXPECT_EQ(source->getKilled(), power == EHealPower::ONE_BATTLE ? 1 : 0);
		source->endBattleForm();
		EXPECT_EQ(source->getCurrentOriginalCreatureCount(), 3);
		EXPECT_EQ(premium(*source), 0);
	}
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, FullTemporaryResurrectionOutsideFormAlsoRemovesThePremium)
{
	loseOne(*target);
	int64_t healing = target->getMaxHealth();
	target->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(target->getKilled(), 1);
	EXPECT_EQ(target->getCurrentOriginalCreatureCount(), target->unitBaseAmount());
	EXPECT_EQ(premium(*target), 0);
}
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, RealCalculatorScalesDamageAndDamageBeforeDefenseOnce)
{
	loseOne(*target);
	ASSERT_NE(LIBRARY->scriptTypes()->getDamageCalculator(), nullptr);
	const auto baseline = consumer(*orc, *target, 0);
	const auto boosted = consumer(*orc, *target, 20);
	EXPECT_EQ(boosted.damage.min, baseline.damage.min * 120 / 100);
	EXPECT_EQ(boosted.damage.max, baseline.damage.max * 120 / 100);
	EXPECT_EQ(boosted.damageBeforeDefense.min, baseline.damageBeforeDefense.min * 120 / 100);
	EXPECT_EQ(boosted.damageBeforeDefense.max, baseline.damageBeforeDefense.max * 120 / 100);
	EXPECT_GT(boosted.damageBeforeDefense.min, boosted.damage.min);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, HypotheticalCopiesKeepLocalCasualtyCountsThroughResurrectionAndFormExpiry)
{
	beginCombat();
	loseOne(*target);
	target->beginBattleForm(CreatureID(CreatureID::decode("core:peasant")), 1);
	RangedSpecialtyEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	// Ordinary projections are lazy: materialize both branch states before mutating either.
	const auto parentTarget = parent->getForUpdate(target->unitId());
	const auto childSnapshot = child->getForUpdate(target->unitId());
	ASSERT_NE(parentTarget.get(), childSnapshot.get());
	ASSERT_EQ(parentTarget->getCurrentOriginalCreatureCount(), 2);
	ASSERT_EQ(childSnapshot->getCurrentOriginalCreatureCount(), 2);
	ASSERT_EQ(premium(*childSnapshot), 20);
	int64_t healing = target->getTotalHealth();
	parentTarget->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(premium(*parentTarget), 0);
	EXPECT_EQ(premium(*child->battleGetUnitByID(target->unitId())), 20);
	EXPECT_EQ(premium(*target), 20);
	parent->nextRound();
	// Expiring the provider must not replace its descendant's still-active native body.
	EXPECT_EQ(parentTarget->unitType()->getId(), CreatureID(CreatureID::decode("core:pikeman")));
	EXPECT_EQ(parentTarget->getMaxHealth(), 10u);
	EXPECT_EQ(childSnapshot->unitType()->getId(), CreatureID(CreatureID::decode("core:peasant")));
	EXPECT_EQ(childSnapshot->getMaxHealth(), 1u);
	EXPECT_EQ(childSnapshot->getCurrentOriginalCreatureCount(), 2);
	child->nextRound();
	const auto * childTarget = child->battleGetUnitByID(target->unitId());
	EXPECT_EQ(parentTarget->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(childTarget->getCurrentOriginalCreatureCount(), 2);
	EXPECT_FALSE(parentTarget->hasBattleForm());
	const auto * childSource = child->battleGetUnitByID(orc->unitId());
	EXPECT_EQ(consumer(*childSource, *childTarget, 20).damage.min, consumer(*orc, *target, 20).damage.min);
	EXPECT_EQ(premium(*target), 20);
}
#endif
TEST_F(NewHorizonsJabarkasSpecialtyRulesTest, DetachedAndJsonCopiesOwnTheirSourceCountsThroughTemporaryResurrectionAndExpiry)
{
	loseOne(*target);
	target->beginBattleForm(CreatureID(CreatureID::decode("core:peasant")), 2);
	const auto liveBefore = target->save();
	auto detached = target->acquireState();
	auto restored = target->acquireState();
	const auto snapshot = detached->save();
	restored->load(snapshot);
	ASSERT_EQ(detached->getCurrentOriginalCreatureCount(), 2);
	ASSERT_EQ(restored->getCurrentOriginalCreatureCount(), 2);
	int64_t healing = target->getTotalHealth();
	detached->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(detached->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(detached->getKilled(), 1);
	EXPECT_EQ(premium(*detached), 0);
	EXPECT_EQ(premium(*restored), 20);
	EXPECT_EQ(premium(*target), 20);
	detached->endBattleForm();
	restored->endBattleForm();
	EXPECT_EQ(detached->getCurrentOriginalCreatureCount(), 3);
	EXPECT_EQ(restored->getCurrentOriginalCreatureCount(), 2);
	EXPECT_EQ(premium(*restored), 20);
	EXPECT_EQ(target->save(), liveBefore);
}
