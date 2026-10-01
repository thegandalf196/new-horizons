/*
 * NewHorizonsArmorerVeteranAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto armorerSkill = "new-horizons:armorer";
constexpr auto pavisePerk = "new-horizons:armorer.pavise";
constexpr auto veteranPerk = "new-horizons:armorer.veteran";
constexpr int64_t guardianBuffer = 40;

class VeteranAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit VeteranAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsArmorerVeteranAITest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void selectVeteran(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(armorerSkill);
		ASSERT_GE(decoded, 0);
		const auto armorer = SecondarySkill(decoded);
		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({armorerSkill, pavisePerk});
		ASSERT_TRUE(hero->hasActivePerk(armorerSkill, pavisePerk));
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({armorerSkill, veteranPerk});
		ASSERT_TRUE(hero->hasActivePerk(armorerSkill, veteranPerk));
	}

	void removeStartingStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void updateState(CStack * stack, const std::shared_ptr<battle::CUnitState> & state,
		int64_t healthDelta)
	{
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = healthDelta;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void applyDamage(CStack * stack, int64_t amount, battle::DamageProvenance provenance)
	{
		auto state = stack->acquireState();
		const auto healthBefore = state->getAvailableHealth();
		state->damage(amount, false, provenance);
		updateState(stack, state, state->getAvailableHealth() - healthBefore);
	}

	void addGuardianBuffer(CStack * stack, int64_t points)
	{
		Bonus marker(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::SPELL_EFFECT, points, BonusSourceID(SpellID(SpellID::HASTE)));
		marker.turnsRemain = 2;

		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effects);
		ASSERT_TRUE(stack->hasBonus(Selector::type()(BonusType::GUARDIAN_SPIRIT)));
	}

	void reduceToHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		const auto healthBefore = state->getAvailableHealth();
		int64_t damage = healthBefore - desiredHealth;
		state->damage(damage, false);
		updateState(stack, state, state->getAvailableHealth() - healthBefore);
	}
};
}

TEST_F(NewHorizonsArmorerVeteranAITest, DetachedActivationUsesTheOwningHeroAndLeavesLiveStacksUntouched)
{
	startGame();
	selectVeteran(attackerSideHero);
	startBattle();
	removeStartingStacks();
	auto * ownStack = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(92), 10);
	auto * enemyStack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(93), 10);
	ASSERT_NE(ownStack, nullptr);
	ASSERT_NE(enemyStack, nullptr);
	beginCombat();
	applyDamage(ownStack, 100, battle::DamageProvenance::PHYSICAL_CREATURE);
	applyDamage(enemyStack, 100, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto ownLiveHealth = ownStack->getAvailableHealth();
	const auto enemyLiveHealth = enemyStack->getAvailableHealth();

	VeteranAIEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	const auto projectedEnemy = projected.getForUpdate(enemyStack->unitId());
	projected.nextTurn(enemyStack->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(projectedEnemy->getAvailableHealth(), enemyLiveHealth)
		<< "An enemy stack must not borrow the AI player's Armorer Veteran";
	EXPECT_EQ(projectedEnemy->veteranPhysicalDamageSinceActivation, 0)
		<< "The detached activation consumes its observed interval even without the owner's perk";

	const auto projectedOwn = projected.getForUpdate(ownStack->unitId());
	projected.nextTurn(ownStack->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(projectedOwn->getAvailableHealth(), ownLiveHealth + 15);
	EXPECT_EQ(projectedOwn->veteranPhysicalDamageSinceActivation, 0);
	EXPECT_EQ(ownStack->getAvailableHealth(), ownLiveHealth);
	EXPECT_EQ(ownStack->veteranPhysicalDamageSinceActivation, 100);
	EXPECT_EQ(enemyStack->getAvailableHealth(), enemyLiveHealth);
	EXPECT_EQ(enemyStack->veteranPhysicalDamageSinceActivation, 100);
}

TEST_F(NewHorizonsArmorerVeteranAITest, AttackPreviewAndCommittedReplayAbsorbGuardianSpiritOnlyOnce)
{
	startGame();
	selectVeteran(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(92), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	beginCombat();
	addGuardianBuffer(target, guardianBuffer);
	ASSERT_EQ(target->guardianSpiritHitPoints, guardianBuffer);
	ASSERT_EQ(target->guardianSpiritRoundsRemaining, 2);
	const auto liveHealth = target->getAvailableHealth();

	VeteranAIEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	DamageCache damageCache;
	const auto physical = BattleAttackInfo(projectedAttacker.get(), projectedTarget.get(), 0, false);
	ASSERT_TRUE(physical.physicalDamage);
	const auto prediction = AttackPossibility::evaluate(physical, attacker->getPosition(), damageCache, model);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 1u);
	const auto hit = std::ranges::find_if(prediction.fortuneStrikes.front().hits,
		[target](const auto & entry) { return entry.first == target->unitId(); });
	ASSERT_NE(hit, prediction.fortuneStrikes.front().hits.end());
	EXPECT_GT(hit->second, guardianBuffer)
		<< "The replay record keeps the physical hit before Guardian Spirit absorption";

	const auto affectedTarget = std::ranges::find_if(prediction.affectedUnits,
		[target](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(affectedTarget, prediction.affectedUnits.end());
	auto previewedTarget = *affectedTarget;
	ASSERT_TRUE(previewedTarget->alive());
	EXPECT_EQ(previewedTarget->guardianSpiritHitPoints, 0);
	const auto previewHealth = previewedTarget->getAvailableHealth();
	const auto previewHistory = previewedTarget->veteranPhysicalDamageSinceActivation;
	EXPECT_EQ(previewHistory, liveHealth - previewHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, damageCache);
	auto replayedTarget = model->getForUpdate(target->unitId());
	EXPECT_EQ(replayedTarget->getAvailableHealth(), previewHealth);
	EXPECT_EQ(replayedTarget->guardianSpiritHitPoints, previewedTarget->guardianSpiritHitPoints);
	EXPECT_EQ(replayedTarget->veteranPhysicalDamageSinceActivation, previewHistory);
	EXPECT_EQ(target->getAvailableHealth(), liveHealth);
	EXPECT_EQ(target->guardianSpiritHitPoints, guardianBuffer);
	EXPECT_EQ(target->veteranPhysicalDamageSinceActivation, 0)
		<< "AI evaluation and replay never mutate authoritative battle state";
}

TEST_F(NewHorizonsArmorerVeteranAITest, BufferedPhysicalOverkillStillKillsTheLastSurvivor)
{
	startGame();
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(92), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(93), 1);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	reduceToHealth(target, 50);
	beginCombat();
	addGuardianBuffer(target, guardianBuffer);
	ASSERT_EQ(target->getAvailableHealth(), 50);
	ASSERT_EQ(target->guardianSpiritHitPoints, guardianBuffer);
	ASSERT_EQ(target->guardianSpiritRoundsRemaining, 2);
	ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage.max,
		50 + guardianBuffer)
		<< "The maximum raw hit must exceed both the survivor's HP and Guardian Spirit buffer";

	VeteranAIEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache damageCache;
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker.get(), projectedTarget.get(), 0, false), attacker->getPosition(), damageCache, model);
	const auto affectedTarget = std::ranges::find_if(prediction.affectedUnits,
		[target](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(affectedTarget, prediction.affectedUnits.end());
	EXPECT_FALSE((*affectedTarget)->alive());
	EXPECT_EQ((*affectedTarget)->guardianSpiritHitPoints, 0);

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, damageCache);
	EXPECT_FALSE(model->getForUpdate(target->unitId())->alive());
	EXPECT_EQ(model->getForUpdate(target->unitId())->guardianSpiritHitPoints, 0);
	EXPECT_TRUE(target->alive()) << "The detached overkill forecast must not kill the live stack";
}
