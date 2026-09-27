/*
 * NewHorizonsCleaveTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsOffense.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
class CleaveEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CleaveEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsCleaveTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & offensePerks = perkRules["skills"][newHorizonsOffense::SKILL]["perks"].Vector();
		const auto cleave = std::find_if(offensePerks.begin(), offensePerks.end(), [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsOffense::CLEAVE;
		});
		if(cleave == offensePerks.end())
			throw std::runtime_error("Cleave is missing from the New Horizons perk registry");
		(*cleave)["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void startBattleWithCleave(BattleSide side)
	{
		startGame();
		auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		const int decodedOffense = SecondarySkill::decode(newHorizonsOffense::SKILL);
		ASSERT_GE(decodedOffense, 0);
		hero->setSecSkillLevel(SecondarySkill(decodedOffense), MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE});
		ASSERT_TRUE(hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE));
		startBattle();
	}

	void setActiveStack(const CStack * stack, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = reason;
		gameHandler->sendAndApply(activation);
	}

	void addTwoHexBreath(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	}

	void markAsClone(CStack * stack)
	{
		BattleUnitsChanged cloneUpdate;
		cloneUpdate.battleID = BattleID(0);
		cloneUpdate.changedStacks.emplace_back(stack->unitId(), UnitChanges::EOperation::UPDATE);
		cloneUpdate.changedStacks.back().data = stack->acquireState()->save();
		cloneUpdate.changedStacks.back().data["state"]["cloned"].Bool() = true;
		gameHandler->sendAndApply(cloneUpdate);
	}

	void giveGuaranteedRebirth(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	}

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}
};
}

TEST_F(NewHorizonsCleaveTest, LethalMeleeTriggersOneHalfDamageStrikeUsingHealthThenHexOrder)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	auto * lowerHealthAtLowerHex = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(75), 10);
	auto * equalHealthAtLowerHex = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 20);
	auto * equalHealthAtHigherHex = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 20);
	forceMaximumDamage(attacker);
	beginCombat();

	ASSERT_EQ(equalHealthAtLowerHex->getAvailableHealth(), equalHealthAtHigherHex->getAvailableHealth());
	ASSERT_GT(equalHealthAtLowerHex->getAvailableHealth(), lowerHealthAtLowerHex->getAvailableHealth());
	const int64_t tiedTargetHealth = equalHealthAtLowerHex->getAvailableHealth();
	const int64_t normalDamage = battle()->calculateDmgRange(
		BattleAttackInfo(attacker, equalHealthAtLowerHex, 0, false)).damage.max;
	ASSERT_GT(normalDamage, 0);
	ASSERT_LT(normalDamage / 2, equalHealthAtLowerHex->getAvailableHealth());

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, destroyed->getPosition()));
	ASSERT_EQ(destroyed->getCount(), 0);
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[0].bsa.front().stackAttacked, destroyed->unitId());
	ASSERT_EQ(server.attacks[1].stackAttacking, attacker->unitId());
	ASSERT_EQ(server.attacks[1].bsa.size(), 1u);
	EXPECT_EQ(server.attacks[1].bsa.front().stackAttacked, equalHealthAtLowerHex->unitId());
	EXPECT_EQ(server.attacks[1].bsa.front().damageAmount, normalDamage / 2);
	EXPECT_GT(equalHealthAtLowerHex->getAvailableHealth(), 0);
	EXPECT_EQ(equalHealthAtHigherHex->getAvailableHealth(), tiedTargetHealth);
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);

	const auto copiedState = attacker->acquireState();
	ASSERT_TRUE(copiedState->cleaveUsedThisActivation);
	const auto serializedState = copiedState->save();
	ASSERT_TRUE(serializedState["state"]["cleaveUsedThisActivation"].Bool());

	BattleUnitsChanged roundTrip;
	roundTrip.battleID = BattleID(0);
	roundTrip.changedStacks.emplace_back(attacker->unitId(), UnitChanges::EOperation::UPDATE);
	roundTrip.changedStacks.back().data = serializedState;
	gameHandler->sendAndApply(roundTrip);
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, BattleAndBattleStartRoundTripCleaveStateAndRejectOlderWriters)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 40);
	forceMaximumDamage(attacker);
	beginCombat();
	ASSERT_TRUE(attack(attacker, destroyed->getPosition()));
	ASSERT_TRUE(attacker->cleaveUsedThisActivation);
	ASSERT_TRUE(battle()->hasCleaveState());

	const auto restoredBattle = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_TRUE(restoredBattle->hasCleaveState());
	const auto * restoredStack = restoredBattle->getStack(attacker->unitId(), false);
	ASSERT_NE(restoredStack, nullptr);
	EXPECT_TRUE(restoredStack->cleaveUsedThisActivation);

	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), gameState().get());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & outgoing;
	current.iser.cb = gameState().get();
	BattleStart incoming;
	current.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	ASSERT_TRUE(incoming.info->hasCleaveState());
	const auto * restoredPacketStack = incoming.info->getStack(attacker->unitId(), false);
	ASSERT_NE(restoredPacketStack, nullptr);
	EXPECT_TRUE(restoredPacketStack->cleaveUsedThisActivation);

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty());

	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT;
	EXPECT_THROW(oldPacketWriter.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty());
}

TEST_F(NewHorizonsCleaveTest, ContinuationPreservesUseAndNewActivationResetsIt)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * firstKill = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	auto * firstTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 40);
	auto * secondKill = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(91), 1);
	auto * secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(74), 40);
	auto * nextKill = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(108), 1);
	auto * nextTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(125), 40);
	forceMaximumDamage(attacker);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, firstKill->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, firstTarget->unitId());
	ASSERT_TRUE(attacker->cleaveUsedThisActivation);

	setActiveStack(attacker, BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, secondKill->getPosition()));
	EXPECT_EQ(secondKill->getCount(), 0);
	EXPECT_EQ(server.attacks.size(), 1u);
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);

	setActiveStack(attacker, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(attacker->cleaveUsedThisActivation);
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, nextKill->getPosition()));
	EXPECT_EQ(nextKill->getCount(), 0);
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, nextTarget->unitId());
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, FollowupKillDoesNotTriggerCleaveRecursively)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	auto * highestHealth = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 10);
	auto * nextHighestHealth = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 5);
	forceMaximumDamage(attacker);
	beginCombat();

	ASSERT_GT(highestHealth->getAvailableHealth(), nextHighestHealth->getAvailableHealth());
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, destroyed->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, highestHealth->unitId());
	EXPECT_EQ(highestHealth->getCount(), 0);
	EXPECT_GT(nextHighestHealth->getAvailableHealth(), 0);
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, AttackPossibilityProjectsCleaveWithoutMutatingLiveBattleAndReplayCarriesIt)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(75), 10);
	auto * lowerHexTie = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 40);
	auto * higherHexTie = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 40);
	forceMaximumDamage(attacker);
	beginCombat();

	const auto attackerHealth = attacker->getAvailableHealth();
	const auto destroyedHealth = destroyed->getAvailableHealth();
	const auto cleaveTargetHealth = lowerHexTie->getAvailableHealth();
	const auto otherTargetHealth = higherHexTie->getAvailableHealth();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	CleaveEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(
		BattleAttackInfo(attacker, destroyed, 0, false), attacker->getPosition(), cache, model);

	ASSERT_TRUE(prediction.defenderDead);
	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_NE(prediction.attackerState, nullptr);
	ASSERT_TRUE(prediction.attackerState->cleaveUsedThisActivation);
	ASSERT_GE(prediction.fortuneStrikes.size(), 2u);
	const auto & cleaveStrike = prediction.fortuneStrikes[1];
	EXPECT_EQ(cleaveStrike.cleaveDamagePercent, newHorizonsOffense::CLEAVE_DAMAGE_PERCENT);
	ASSERT_EQ(cleaveStrike.hits.size(), 1u);
	EXPECT_EQ(cleaveStrike.defenderId, lowerHexTie->unitId());
	EXPECT_EQ(cleaveStrike.hits.front().first, lowerHexTie->unitId());
	EXPECT_GT(cleaveStrike.hits.front().second, 0);
	EXPECT_LT(prediction.effectPreview->battleGetUnitByID(lowerHexTie->unitId())->getAvailableHealth(), cleaveTargetHealth);
	EXPECT_EQ(prediction.effectPreview->battleGetUnitByID(higherHexTie->unitId())->getAvailableHealth(), otherTargetHealth);
	EXPECT_GT(prediction.defenderDamageReduce, 0);

	// Evaluating a candidate owns all changes in its preview; even the mutable
	// hypothetical battle passed to evaluate and the authoritative battle stay pristine.
	EXPECT_EQ(attacker->getAvailableHealth(), attackerHealth);
	EXPECT_EQ(destroyed->getAvailableHealth(), destroyedHealth);
	EXPECT_EQ(lowerHexTie->getAvailableHealth(), cleaveTargetHealth);
	EXPECT_FALSE(attacker->cleaveUsedThisActivation);
	EXPECT_FALSE(model->getForUpdate(attacker->unitId())->cleaveUsedThisActivation);
	EXPECT_EQ(model->getForUpdate(lowerHexTie->unitId())->getAvailableHealth(), cleaveTargetHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	EXPECT_TRUE(model->getForUpdate(attacker->unitId())->cleaveUsedThisActivation);
	EXPECT_EQ(model->getForUpdate(lowerHexTie->unitId())->getAvailableHealth(),
		prediction.effectPreview->battleGetUnitByID(lowerHexTie->unitId())->getAvailableHealth());
	EXPECT_EQ(lowerHexTie->getAvailableHealth(), cleaveTargetHealth);
	EXPECT_FALSE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, AttackPossibilityRecomputesRetaliationFromPostCleaveDefenderCount)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 30);
	auto * primaryDefender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 100);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1);
	addTwoHexBreath(attacker);
	forceMaximumDamage(attacker);
	beginCombat();

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	CleaveEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto initialPrimaryCount = primaryDefender->getCount();
	const auto retaliationBeforeCleave = model->battleExpectedLuckDamage(
		BattleAttackInfo(primaryDefender, attacker, 0, false));
	const auto prediction = AttackPossibility::evaluate(
		BattleAttackInfo(attacker, primaryDefender, 0, false), attacker->getPosition(), cache, model);

	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_FALSE(prediction.defenderDead);
	const auto * projectedCollateral = prediction.effectPreview->battleGetUnitByID(collateral->unitId());
	ASSERT_NE(projectedCollateral, nullptr);
	EXPECT_FALSE(projectedCollateral->alive());
	ASSERT_EQ(primaryDefender->getCount(), initialPrimaryCount);
	ASSERT_EQ(model->getForUpdate(primaryDefender->unitId())->getCount(), initialPrimaryCount);
	ASSERT_GE(prediction.fortuneStrikes.size(), 3u);
	ASSERT_EQ(prediction.fortuneStrikes[1].cleaveDamagePercent, newHorizonsOffense::CLEAVE_DAMAGE_PERCENT);
	EXPECT_EQ(prediction.fortuneStrikes[1].defenderId, primaryDefender->unitId());

	const auto retaliation = std::find_if(prediction.fortuneStrikes.begin(), prediction.fortuneStrikes.end(),
		[](const FortuneStrikeProjection & strike) { return strike.retaliation; });
	ASSERT_NE(retaliation, prediction.fortuneStrikes.end());
	ASSERT_EQ(retaliation->hits.size(), 1u);
	EXPECT_EQ(retaliation->hits.front().first, attacker->unitId());

	const auto * projectedPrimary = prediction.effectPreview->battleGetUnitByID(primaryDefender->unitId());
	const auto * projectedAttacker = prediction.effectPreview->battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedPrimary, nullptr);
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_LT(projectedPrimary->getCount(), initialPrimaryCount);
	BattleAttackInfo expectedPostCleaveRetaliation(projectedPrimary, projectedAttacker, 0, false);
	expectedPostCleaveRetaliation.retaliation = true;
	const auto retaliationFromCurrentState = prediction.effectPreview->battleExpectedLuckDamage(
		expectedPostCleaveRetaliation);
	EXPECT_LT(retaliationFromCurrentState, retaliationBeforeCleave);
	EXPECT_EQ(retaliation->hits.front().second, retaliationFromCurrentState);
	EXPECT_LT(retaliation->hits.front().second, retaliationBeforeCleave);
	EXPECT_EQ(primaryDefender->getCount(), initialPrimaryCount);
	EXPECT_EQ(collateral->getCount(), 1);
}

TEST_F(NewHorizonsCleaveTest, CollateralCloneDestroyedBySubAggregateHitCanEnableCleave)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * primaryDefender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	auto * clone = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 20);
	addTwoHexBreath(attacker);
	blockRetaliation(attacker);
	forceMaximumDamage(attacker);
	giveGuaranteedRebirth(clone);
	markAsClone(clone);
	beginCombat();

	ASSERT_TRUE(clone->isClone());
	BattleAttackInfo cloneCollateralAttack(attacker, clone, 0, false);
	cloneCollateralAttack.secondaryAttack = true;
	cloneCollateralAttack.defenderPos = clone->getPosition();
	const auto incomingDamage = battle()->calculateDmgRange(cloneCollateralAttack).damage.max;
	ASSERT_GT(incomingDamage, 0);
	ASSERT_LT(incomingDamage, clone->getTotalHealth());

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	CleaveEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(
		BattleAttackInfo(attacker, primaryDefender, 0, false), attacker->getPosition(), cache, model);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto * projectedClone = preview.effectPreview->battleGetUnitByID(clone->unitId());
	ASSERT_NE(projectedClone, nullptr);
	EXPECT_FALSE(projectedClone->alive());
	ASSERT_GE(preview.fortuneStrikes.size(), 2u);
	ASSERT_NE(preview.attackerState, nullptr);
	EXPECT_EQ(preview.fortuneStrikes[1].cleaveDamagePercent, newHorizonsOffense::CLEAVE_DAMAGE_PERCENT);
	EXPECT_EQ(preview.fortuneStrikes[1].defenderId, primaryDefender->unitId());
	EXPECT_TRUE(preview.attackerState->cleaveUsedThisActivation);
	EXPECT_TRUE(clone->alive()); // Candidate evaluation must not alter the live clone.

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, primaryDefender->getPosition()));
	EXPECT_FALSE(clone->alive());
	EXPECT_TRUE(attacker->cleaveUsedThisActivation);
	ASSERT_EQ(server.attacks.size(), 2u);
	const auto cloneHit = std::find_if(server.attacks.front().bsa.begin(), server.attacks.front().bsa.end(),
		[clone](const BattleStackAttacked & hit) { return hit.stackAttacked == clone->unitId(); });
	ASSERT_NE(cloneHit, server.attacks.front().bsa.end());
	EXPECT_TRUE(cloneHit->cloneKilled());
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, primaryDefender->unitId());
}

TEST_F(NewHorizonsCleaveTest, CollateralRebirthDoesNotQualifyAsCleaveOrigin)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 5);
	auto * primaryDefender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	auto * rebirthing = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1);
	addTwoHexBreath(attacker);
	blockRetaliation(attacker);
	forceMaximumDamage(attacker);
	giveGuaranteedRebirth(rebirthing);
	beginCombat();

	BattleAttackInfo rebirthCollateralAttack(attacker, rebirthing, 0, false);
	rebirthCollateralAttack.secondaryAttack = true;
	rebirthCollateralAttack.defenderPos = rebirthing->getPosition();
	const auto incomingDamage = battle()->calculateDmgRange(rebirthCollateralAttack).damage.max;
	ASSERT_GE(incomingDamage, rebirthing->getAvailableHealth());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	CleaveEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(
		BattleAttackInfo(attacker, primaryDefender, 0, false), attacker->getPosition(), cache, model);
	ASSERT_NE(preview.effectPreview, nullptr);
	EXPECT_TRUE(std::none_of(preview.fortuneStrikes.begin(), preview.fortuneStrikes.end(),
		[](const FortuneStrikeProjection & strike)
		{
			return strike.cleaveDamagePercent > 0;
		}));

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, primaryDefender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	const auto rebirthHit = std::find_if(server.attacks.front().bsa.begin(), server.attacks.front().bsa.end(),
		[rebirthing](const BattleStackAttacked & hit) { return hit.stackAttacked == rebirthing->unitId(); });
	ASSERT_NE(rebirthHit, server.attacks.front().bsa.end());
	EXPECT_TRUE(rebirthHit->killed());
	EXPECT_TRUE(rebirthHit->willRebirth());
	EXPECT_TRUE(rebirthing->alive());
	EXPECT_FALSE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, NonlethalMeleeDoesNotTriggerCleave)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 100);
	blockRetaliation(attacker);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().killedAmount, 0);
	EXPECT_FALSE(attacker->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, LethalPhysicalRangedAttackDoesNotTriggerCleave)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(110), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(111), 1000);
	forceMaximumDamage(shooter);
	beginCombat();
	ASSERT_TRUE(battle()->battleCanShoot(shooter, destroyed->getPosition()));

	server.attacks.clear();
	ASSERT_TRUE(shoot(shooter, destroyed));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_TRUE(server.attacks.front().shot());
	EXPECT_EQ(destroyed->getCount(), 0);
	EXPECT_FALSE(shooter->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, SpellLikeMagogShotDoesNotTriggerCleave)
{
	startBattleWithCleave(BattleSide::ATTACKER);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(70), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(110), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(111), 1000);
	forceMaximumDamage(shooter);
	beginCombat();
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, destroyed->getPosition()));

	server.attacks.clear();
	ASSERT_TRUE(shoot(shooter, destroyed));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_TRUE(server.attacks.front().shot());
	EXPECT_TRUE(server.attacks.front().spellLike());
	EXPECT_EQ(destroyed->getCount(), 0);
	EXPECT_FALSE(shooter->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, CounterattackKillDoesNotTriggerCleave)
{
	startBattleWithCleave(BattleSide::DEFENDER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 100);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(75), 100);
	forceMaximumDamage(defender);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(attacker->getCount(), 0);
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_TRUE(server.attacks.back().counter());
	EXPECT_EQ(server.attacks.back().stackAttacking, defender->unitId());
	EXPECT_FALSE(defender->cleaveUsedThisActivation);
}

TEST_F(NewHorizonsCleaveTest, BulwarkPreemptiveKillDoesNotTriggerCleave)
{
	startBattleWithCleave(BattleSide::DEFENDER);
	const int decodedBulwark = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	ASSERT_GE(decodedBulwark, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(decodedBulwark), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 100);
	auto * cleaveCandidate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(75), 100);
	defender->defending = true;
	forceMaximumDamage(defender);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(attacker->getCount(), 0);
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	EXPECT_EQ(server.attacks.front().bsa.front().stackAttacked, attacker->unitId());
	EXPECT_GT(cleaveCandidate->getAvailableHealth(), 0);
	EXPECT_FALSE(defender->cleaveUsedThisActivation);
}
