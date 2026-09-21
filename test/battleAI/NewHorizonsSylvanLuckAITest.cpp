/*
 * NewHorizonsSylvanLuckAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

namespace
{
JsonNode certainLuck()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

class SylvanEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SylvanEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSylvanLuckAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(true));
	}

	static void luck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
			BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

TEST_F(NewHorizonsSylvanLuckAITest, CertainStrikeCommitsRecoveryAndSharedCascadeOnlyToTheModel)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.forestsFavor = true;
	fortune.luckyRecovery = true;
	fortune.sharedFortune = true;
	fortune.cascadingFortune = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, fortune);

	auto projectedSource = model->getForUpdate(source->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	int64_t wound = 40;
	projectedSource->damage(wound);
	const auto before = projectedSource->getAvailableHealth();
	BattleAttackInfo attack(projectedSource.get(), projectedTarget.get(), 0, false);
	ASSERT_TRUE(model->fortuneStrikeIsCertain(attack));

	// A child made before the commit is a stable snapshot; later parent
	// transitions must not leak backwards into it.
	HypotheticBattle pristine(environment.get(), model);
	model->projectFortuneStrike(attack, {{target->unitId(), 100}}, projectedSource.get(), true);
	const auto & committed = model->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(committed.positiveLuckUnits.contains(source->unitId()));
	EXPECT_EQ(committed.speedBonus(source->unitId()), 2);
	EXPECT_TRUE(committed.cascadingPending);
	EXPECT_TRUE(committed.sharedUnits.contains(ally->unitId()));
	EXPECT_EQ(model->battleGetAttackLuck(model->getForUpdate(ally->unitId()).get(), projectedTarget.get(), false), 1);
	EXPECT_EQ(projectedSource->getAvailableHealth(), before + 10);

	EXPECT_FALSE(pristine.getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.contains(source->unitId()));
	EXPECT_FALSE(pristine.getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
	EXPECT_EQ(source->getAvailableHealth(), static_cast<int64_t>(source->getMaxHealth()) * source->getCount());

	// A child made after the commit receives the pending gift by value.  Its
	// activation consumes only the child copy, leaving the parent armed.
	HypotheticBattle child(environment.get(), model);
	child.nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(child.getSylvanLuckState(BattleSide::ATTACKER).temporaryLuck(source->unitId()), 3);
	EXPECT_EQ(child.battleGetAttackLuck(child.getForUpdate(source->unitId()).get(),
		child.getForUpdate(target->unitId()).get(), false), 4);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
}

TEST_F(NewHorizonsSylvanLuckAITest, PerfectMomentProjectsFirstShotOnlyAndCommitsUseOnlyToSelectedModel)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	battle()->activeStack = source->unitId();
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.perfectMoment = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	BattleAttackInfo attack(source, target, 0, true);
	const auto ordinary = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model);
	const auto declared = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model, true);
	ASSERT_TRUE(declared.perfectMoment);
	ASSERT_EQ(declared.fortuneStrikes.size(), 2u);
	ASSERT_EQ(ordinary.fortuneStrikes.size(), 2u);
	EXPECT_TRUE(declared.fortuneStrikes[0].perfectMoment);
	EXPECT_FALSE(declared.fortuneStrikes[1].perfectMoment);
	EXPECT_GT(declared.fortuneStrikes[0].hits.front().second, ordinary.fortuneStrikes[0].hits.front().second);
	EXPECT_EQ(declared.fortuneStrikes[1].hits.front().second, ordinary.fortuneStrikes[1].hits.front().second);
	EXPECT_FALSE(model->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
	PotentialTargets candidates(source, cache, model);
	EXPECT_TRUE(candidates.bestAction().perfectMoment);
	BattleExchangeVariant exchange;
	exchange.trackAttack(declared, model, cache);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.contains(source->unitId()));
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
}

TEST_F(NewHorizonsSylvanLuckAITest, PerfectMomentLethalPreviewAvoidsRetaliationAndDoesNotForceEnemy)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	battle()->activeStack = source->unitId();
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.perfectMoment = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false), source->getPosition(), cache, model, true);
	ASSERT_TRUE(preview.perfectMoment);
	EXPECT_TRUE(preview.defenderDead);
	EXPECT_EQ(preview.attackerState->getAvailableHealth(), source->getAvailableHealth());
	for(const auto & strike : preview.fortuneStrikes)
	{
		if(strike.retaliation)
		{
			EXPECT_FALSE(strike.perfectMoment);
		}
	}
}

TEST_F(NewHorizonsSylvanLuckAITest, PerfectMomentEndsSerendipityBeforeSecondProjectedShot)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuck());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(source, -1);
	battle()->activeStack = source->unitId();
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.perfectMoment = fortune.serendipity = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	BattleAttackInfo attack(source, target, 0, true);
	const auto ordinary = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model);
	const auto declared = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model, true);
	ASSERT_EQ(declared.fortuneStrikes.size(), 2u);
	ASSERT_EQ(ordinary.fortuneStrikes.size(), 2u);
	EXPECT_LT(declared.fortuneStrikes[1].hits.front().second, ordinary.fortuneStrikes[1].hits.front().second);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.empty());
	EXPECT_FALSE(fortune.perfectMomentUsed);
}

TEST_F(NewHorizonsSylvanLuckAITest, RecoveryUsesOnlyPrimaryNonOverkillDamageInClassicMode)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(false));
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 1);
	luck(source, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.luckyRecovery = true;
	fortune.cascadingFortune = true;
	model.setSylvanLuckState(BattleSide::ATTACKER, fortune);
	auto projectedSource = model.getForUpdate(source->unitId());
	auto projectedTarget = model.getForUpdate(target->unitId());
	auto projectedCollateral = model.getForUpdate(collateral->unitId());
	int64_t wound = 40;
	projectedSource->damage(wound);
	const auto before = projectedSource->getAvailableHealth();
	BattleAttackInfo attack(projectedSource.get(), projectedTarget.get(), 0, false);
	model.projectFortuneStrike(attack,
		{{target->unitId(), 19}, {collateral->unitId(), 1000}}, projectedSource.get(), false);
	EXPECT_EQ(projectedSource->getAvailableHealth(), before + 1);
	EXPECT_FALSE(model.getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
}

TEST_F(NewHorizonsSylvanLuckAITest, SharedFortuneUsesProjectedAttackPosition)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex + 7), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 1);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::RETURN_AFTER_STRIKE, BonusSource::OTHER, 1, BonusSourceID()));

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.sharedFortune = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, fortune);

	const BattleHex movedFrom(leftHex + 6);
	model->getForUpdate(ally->unitId())->setPosition(leftHex + 7);
	AttackPossibility ap(movedFrom, target->getPosition(), BattleAttackInfo(source, target, 0, false));
	ap.attackerState = model->getForUpdate(source->unitId())->acquireState();
	ap.attackerState->setPosition(movedFrom);
	ap.affectedUnits.push_back(model->getForUpdate(target->unitId())->acquireState());
	FortuneStrikeProjection strike;
	strike.attackerId = source->unitId();
	strike.defenderId = target->unitId();
	strike.hits.emplace_back(target->unitId(), 10);
	ap.fortuneStrikes.push_back(strike);

	BattleExchangeVariant exchange;
	DamageCache cache;
	exchange.trackAttack(ap, model, cache);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).sharedUnits.contains(ally->unitId()));
	EXPECT_EQ(model->getForUpdate(source->unitId())->getPosition(), source->getPosition());
}

TEST_F(NewHorizonsSylvanLuckAITest, AttackPreviewReplaysDefenderRetaliationFortunePerStrike)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	luck(source, 1);
	luck(target, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState attackerFortune;
	attackerFortune.forestsFavor = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, attackerFortune);
	SylvanLuckState defenderFortune;
	defenderFortune.forestsFavor = true;
	model->setSylvanLuckState(BattleSide::DEFENDER, defenderFortune);

	AttackPossibility ap(source->getPosition(), target->getPosition(), BattleAttackInfo(source, target, 0, false));
	ap.attackerState = model->getForUpdate(source->unitId())->acquireState();
	const auto sourceBefore = ap.attackerState->getAvailableHealth();
	const auto targetBefore = model->getForUpdate(target->unitId())->getAvailableHealth();
	int64_t attackerDamage = 10;
	ap.attackerState->damage(attackerDamage);
	auto targetState = model->getForUpdate(target->unitId())->acquireState();
	int64_t targetDamage = 10;
	targetState->damage(targetDamage);
	ap.affectedUnits.push_back(targetState);
	FortuneStrikeProjection first;
	first.attackerId = source->unitId();
	first.defenderId = target->unitId();
	first.hits.emplace_back(target->unitId(), 10);
	FortuneStrikeProjection retaliation;
	retaliation.attackerId = target->unitId();
	retaliation.defenderId = source->unitId();
	retaliation.retaliation = true;
	retaliation.hits.emplace_back(source->unitId(), 10);
	ap.fortuneStrikes = {first, retaliation};

	BattleExchangeVariant exchange;
	DamageCache cache;
	exchange.trackAttack(ap, model, cache);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER).speedBonus(source->unitId()), 2);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::DEFENDER).speedBonus(target->unitId()), 2);
	EXPECT_EQ(model->getForUpdate(source->unitId())->getAvailableHealth(), sourceBefore - 10);
	EXPECT_EQ(model->getForUpdate(target->unitId())->getAvailableHealth(), targetBefore - 10);
}
