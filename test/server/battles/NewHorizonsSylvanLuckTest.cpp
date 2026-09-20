/*
 * NewHorizonsSylvanLuckTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/AttackPossibility.h"

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

class NewHorizonsSylvanLuckTest : public BattleTestFixture
{
	JsonNode oldBadLuck;
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		oldBadLuck = LIBRARY->settingsHandler->getValue(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		// Original rules disable negative Luck entirely. Enable it only in this
		// fixture, including the static unit cap, and restore it after each test.
		LIBRARY->settingsHandler->addOverride(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuck());
		startGame();
	}
	void TearDown() override
	{
		LIBRARY->settingsHandler->addOverride(EGameSettings::COMBAT_BAD_LUCK_CHANCE, oldBadLuck);
		BattleTestFixture::TearDown();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}
	void perks()
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		ASSERT_GE(skill.getNum(), 0);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		for(const auto * id : {"serendipity", "natureSProvidence", "fortunateAim"})
			attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", std::string("new-horizons:sylvanLuck.") + id});
	}
	static void luck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

TEST(SylvanLuckRulesTest, ChanceOnlyHistoryAndRoundProtection)
{
	SylvanLuckState state;
	EXPECT_EQ(state.chanceLuck(1, 42, true), 1);
	EXPECT_FALSE(state.recordStrike(42, true, false));
	EXPECT_TRUE(state.positiveLuckUnits.empty());
	state.serendipity = state.naturesProvidence = state.fortunateAim = true;
	EXPECT_EQ(state.chanceLuck(1, 42, false), 2);
	EXPECT_EQ(state.chanceLuck(1, 42, true), 3);
	EXPECT_FALSE(state.recordStrike(42, true, false));
	EXPECT_EQ(state.chanceLuck(1, 42, false), 1);
	EXPECT_EQ(state.chanceLuck(1, 43, false), 2);
	EXPECT_TRUE(state.recordStrike(42, false, true));
	EXPECT_FALSE(state.recordStrike(43, false, true));
	state.nextRound();
	EXPECT_TRUE(state.recordStrike(43, false, true));
	EXPECT_EQ(state.positiveLuckUnits.size(), 1u);
}

TEST(SylvanLuckRulesTest, GiftsAreOncePerStackNonStackingAndActivationScoped)
{
	SylvanLuckState state;
	state.forestsFavor = state.sharedFortune = state.cascadingFortune = true;
	state.recordStrike(1, true, false);
	state.finishPositiveStrike({2, 3, 3}, true);
	state.finishPositiveStrike({2}, true);
	EXPECT_EQ(state.speedBonus(1), 2);
	EXPECT_EQ(state.temporaryLuck(2), 1);
	EXPECT_TRUE(state.cascadingPending);
	state.endActivation();
	EXPECT_EQ(state.speedBonus(1), 0);
	EXPECT_EQ(state.temporaryLuck(2), 1);
	state.beginActivation(9, false);
	EXPECT_TRUE(state.cascadingPending);
	state.beginActivation(2, true);
	EXPECT_FALSE(state.cascadingPending);
	EXPECT_EQ(state.temporaryLuck(2), 3);
	EXPECT_EQ(state.temporaryLuck(3), 1);
	state.recordStrike(1, true, false);
	EXPECT_EQ(state.speedBonus(1), 0);
	state.endActivation();
	EXPECT_EQ(state.temporaryLuck(2), 0);
	EXPECT_EQ(SylvanLuckState::recoveryAmount(19), 1);
	EXPECT_EQ(SylvanLuckState::recoveryAmount(-20), 0);
}

TEST_F(NewHorizonsSylvanLuckTest, WholeMeleeStrikeHealsNonOverkillDamageWithoutResurrection)
{
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(true));
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(leftHex - 17), 1);
	luck(source, 1);
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.forestsFavor = fortune.luckyRecovery = fortune.sharedFortune = fortune.cascadingFortune = true;
	int64_t wound = source->getMaxHealth() + 60;
	source->damage(wound);
	const auto count = source->getCount();
	const auto before = source->getAvailableHealth();
	const auto targetHealth = target->getAvailableHealth();
	const auto collateralHealth = collateral->getAvailableHealth();
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	const auto & strike = server.attacks.front();
	ASSERT_TRUE(strike.lucky());
	ASSERT_EQ(strike.bsa.size(), 2u);
	int64_t actual = 0;
	for(const auto & hit : strike.bsa)
		actual += std::min<int64_t>(hit.damageAmount, hit.newState.id == target->unitId() ? targetHealth : collateralHealth);
	EXPECT_EQ(source->getCount(), count);
	EXPECT_EQ(source->getAvailableHealth(), before + std::min<int64_t>(60, actual / 10));
	ASSERT_TRUE(strike.fortuneState);
	EXPECT_EQ(strike.fortuneState->speedBonus(source->unitId()), 2);
	EXPECT_TRUE(strike.fortuneState->cascadingPending);
	EXPECT_EQ(fortune.speedBonus(source->unitId()), 0); // accepted EndAction
}

TEST_F(NewHorizonsSylvanLuckTest, NonLuckyCollateralDoesNotHealOrArmCascading)
{
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(false));
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(leftHex - 17), 10);
	luck(source, 1);
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.luckyRecovery = fortune.cascadingFortune = true;
	int64_t wound = 100;
	source->damage(wound);
	const auto before = source->getAvailableHealth();
	const auto healCapacity = source->getMaxHealth() - source->getFirstHPleft();
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_FALSE(collateral->alive());
	ASSERT_TRUE(target->alive());
	ASSERT_FALSE(server.attacks.empty());
	const auto & hit = server.attacks.front();
	ASSERT_TRUE(hit.fortuneState);
	EXPECT_FALSE(hit.fortuneState->cascadingPending);
	const auto primary = std::find_if(hit.bsa.begin(), hit.bsa.end(), [target](const auto & victim) { return victim.newState.id == target->unitId(); });
	ASSERT_NE(primary, hit.bsa.end());
	EXPECT_EQ(source->getAvailableHealth(), before + std::min<int64_t>(healCapacity, primary->damageAmount / 10));
}

TEST_F(NewHorizonsSylvanLuckTest, DoubleWideAdjacencyUsesBothOccupiedHexesAndCurrentOwner)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 2), 1);
	auto * far = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	const auto adjacent = battle()->battleFortuneAdjacentFriends(source);
	EXPECT_TRUE(vstd::contains(adjacent, rear->unitId()));
	EXPECT_FALSE(vstd::contains(adjacent, source->unitId()));
	EXPECT_FALSE(vstd::contains(adjacent, far->unitId()));
	EXPECT_EQ(adjacent.size(), 1u);
}

TEST_F(NewHorizonsSylvanLuckTest, LuckyRetaliationHealsOnlyItsSurvivingCreatures)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	luck(target, 1);
	battle()->getSide(BattleSide::DEFENDER).sylvanLuck.luckyRecovery = true;
	const auto originalHealth = target->getAvailableHealth();
	const auto sourceHealth = source->getAvailableHealth();
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	const auto & primary = server.attacks.front();
	const auto & retaliation = server.attacks.back();
	ASSERT_TRUE(retaliation.counter());
	ASSERT_TRUE(retaliation.lucky());
	const auto remainingHealth = originalHealth - primary.bsa.front().damageAmount;
	const auto missingTopHealth = target->getCount() * target->getMaxHealth() - remainingHealth;
	const auto recovered = std::min<int64_t>(missingTopHealth, std::min<int64_t>(sourceHealth, retaliation.bsa.front().damageAmount) / 10);
	EXPECT_GT(recovered, 0);
	EXPECT_EQ(target->getAvailableHealth(), remainingHealth + recovered);
}

TEST_F(NewHorizonsSylvanLuckTest, RangedPositiveTriggerDoesNotRecoverHealth)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:monk"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(source, 1);
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.luckyRecovery = true;
	int64_t wound = 20;
	source->damage(wound);
	const auto health = source->getAvailableHealth();
	battle()->activeStack = source->unitId();
	const auto action = BattleAction::makeShotAttack(source, target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(source->unitSide()), action));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.front().lucky());
	EXPECT_TRUE(server.attacks.front().shot());
	EXPECT_EQ(source->getAvailableHealth(), health);
}

TEST_F(NewHorizonsSylvanLuckTest, GenuineActivationAndHypotheticalCopiesConsumeOnlyFriendlyPendingGift)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.forestsFavor = fortune.sharedFortune = fortune.cascadingFortune = true;
	fortune.recordStrike(source->unitId(), true, false);
	fortune.finishPositiveStrike({source->unitId()}, true);
	const auto speed = source->getMovementRange();
	for(auto reason : {BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::HERO_SPELLCAST,
		BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::ACTION_REJECTED})
	{
		battle()->nextTurn(source->unitId(), reason);
		EXPECT_TRUE(fortune.cascadingPending);
		EXPECT_EQ(fortune.temporaryLuck(source->unitId()), 1);
		EXPECT_EQ(source->getMovementRange(), speed);
	}
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SylvanEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	model->nextTurn(enemy->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
	model->nextTurn(source->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER).temporaryLuck(source->unitId()), 3);
	EXPECT_TRUE(fortune.cascadingPending);
	battle()->nextTurn(enemy->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(source->getMovementRange(), speed - 2);
	EXPECT_TRUE(fortune.cascadingPending);
	battle()->nextTurn(source->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_EQ(fortune, model->getSylvanLuckState(BattleSide::ATTACKER));
	fortune.finishPositiveStrike({}, true);
	auto stopped = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	source->addNewBonus(stopped);
	battle()->nextTurn(source->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(fortune.cascadingPending);
	source->removeBonus(stopped);
	HeroOrderState order;
	order.command = HeroCommand::SECOND_WIND;
	order.secondWindActive = true;
	order.primaryTargetUnitId = source->unitId();
	battle()->getSide(BattleSide::ATTACKER).orderState = order;
	battle()->nextTurn(source->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_FALSE(fortune.cascadingPending);
	EXPECT_EQ(fortune.temporaryLuck(source->unitId()), 3);
}

TEST_F(NewHorizonsSylvanLuckTest, ExtendedStateRoundTripAndPreviousVersionDefaults)
{
	startBattle();
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.forestsFavor = fortune.luckyRecovery = fortune.sharedFortune = fortune.cascadingFortune = true;
	fortune.recordStrike(42, true, false);
	fortune.finishPositiveStrike({43}, true);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(restored->getSylvanLuckState(BattleSide::ATTACKER), fortune);
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_LUCK;
	EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
	SylvanLuckState original;
	original.serendipity = true;
	original.recordStrike(42, true, false);
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_LUCK;
	old.oser & original;
	SylvanLuckState decoded = fortune;
	old.iser & decoded;
	EXPECT_EQ(decoded, original);
}

TEST_F(NewHorizonsSylvanLuckTest, FocusTargetAndChanceOnlyQuery)
{
	perks();
	startBattle();
	battle()->nextRound();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:monk"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 20);
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 20);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).fortunateAim);
	const int base = shooter->luckVal();
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), base + 1);
	FocusFireState mark;
	mark.targetUnitId = target->unitId();
	mark.issuedRound = battle()->getRound();
	mark.recipientUnitIds = {shooter->unitId()};
	battle()->getSide(BattleSide::ATTACKER).focusFire = mark;
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), base + 2);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, other, true), base + 1);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, false), base + 1);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, nullptr, true), base + 1);
	auto siegeMarker = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(siegeMarker);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), base + 1);
	shooter->removeBonus(siegeMarker);
	auto * siege = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(leftHex - 4), 1);
	auto * turret = addStack(BattleSide::ATTACKER, CreatureID::ARROW_TOWERS, BattleHex(leftHex + 34), 1);
	ASSERT_TRUE(turret->isTurret());
	EXPECT_EQ(battle()->battleGetAttackLuck(siege, target, true), battle()->battleGetAttackLuck(siege, other, true));
	EXPECT_EQ(battle()->battleGetAttackLuck(turret, target, true), battle()->battleGetAttackLuck(turret, other, true));
	EXPECT_EQ(shooter->luckVal(), base);
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.recordStrike(shooter->unitId(), true, false);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), base + 1);
	battle()->nextRound();
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), base);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), 0);
}

TEST_F(NewHorizonsSylvanLuckTest, AuthoritativeMultiTargetStrikeRecordsOnce)
{
	perks();
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(leftHex - 17), 100);
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	const auto & hit = server.attacks.front();
	ASSERT_TRUE(hit.lucky());
	ASSERT_GE(hit.bsa.size(), 2u);
	ASSERT_TRUE(hit.fortuneState);
	EXPECT_EQ(hit.fortuneState->positiveLuckUnits, std::set<uint32_t>{source->unitId()});
	EXPECT_EQ(battle()->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.size(), 1u);
	CMemorySerializer wire;
	wire.oser & hit;
	BattleAttack restored;
	wire.iser & restored;
	EXPECT_EQ(restored.fortuneState, hit.fortuneState);
	EXPECT_EQ(restored.fortuneSide, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsSylvanLuckTest, ProvidenceSuppressesOnlyFirstBadStrikePerRound)
{
	perks();
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	luck(source, -20);
	ASSERT_LT(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.front().unlucky());
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).negativeLuckIgnored);
	server.attacks.clear();
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.front().unlucky());
	battle()->nextRound();
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).negativeLuckIgnored);
}

TEST_F(NewHorizonsSylvanLuckTest, CurrentSaveAndHypotheticalCopyPreserveIsolatedHistory)
{
	perks();
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto & state = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	state.recordStrike(source->unitId(), true, false);
	state.recordStrike(source->unitId(), false, true);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(restored->getSylvanLuckState(BattleSide::ATTACKER), state);
	EXPECT_EQ(restored->getLuckRollRules(), battle()->getLuckRollRules());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SylvanEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER), state);
	EXPECT_EQ(model->getLuckRollRules(), battle()->getLuckRollRules());
	EXPECT_EQ(model->battleGetAttackLuck(source, nullptr, false), battle()->battleGetAttackLuck(source, nullptr, false));
	model->nextRound();
	EXPECT_FALSE(model->getSylvanLuckState(BattleSide::ATTACKER).negativeLuckIgnored);
	EXPECT_TRUE(state.negativeLuckIgnored);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits, state.positiveLuckUnits);
}

TEST_F(NewHorizonsSylvanLuckTest, LegacySaveIsInertAndCannotDiscardLiveHistory)
{
	startBattle();
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE;
	old.oser & *battle();
	BattleInfo restored(gameState().get());
	restored.getSide(BattleSide::ATTACKER).sylvanLuck.serendipity = true;
	old.iser & restored;
	EXPECT_EQ(restored.getSylvanLuckState(BattleSide::ATTACKER), SylvanLuckState{});
	EXPECT_EQ(restored.getLuckRollRules().diceSize, 0);
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.serendipity = true;
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE;
	EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
}

TEST_F(NewHorizonsSylvanLuckTest, ExpectedDamageUsesCapturedCurveWithoutAnyPerk)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	ASSERT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).active());
	const BattleAttackInfo ordinary(source, target, 0, false);
	luck(source, 1);
	auto rolled = ordinary;
	rolled.luckyStrike = true;
	// Fixture curve is 100%, while the installed NH global curve is 4%.
	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), battle()->calculateDmgRange(rolled).damage.min);
	luck(source, -2);
	rolled.luckyStrike = false;
	rolled.unluckyStrike = true;
	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), battle()->calculateDmgRange(rolled).damage.min);
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	rolled.luckyStrike = true;
	rolled.unluckyStrike = false;
	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), battle()->calculateDmgRange(rolled).damage.min);
	EXPECT_GT(battle()->battleExpectedLuckDamage(ordinary), battle()->calculateDmgRange(ordinary).damage.min * 2);
}

TEST_F(NewHorizonsSylvanLuckTest, GuaranteedLuckyKillHasNoPhantomRetaliation)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 1);
	ASSERT_LT(battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, false)).damage.max, target->getAvailableHealth());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SylvanEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto projected = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false), source->getPosition(), cache, model);
	EXPECT_TRUE(projected.defenderDead);
	EXPECT_EQ(projected.attackerState->getAvailableHealth(), source->getAvailableHealth());
}

TEST_F(NewHorizonsSylvanLuckTest, RetaliationIncludesItsOwnLuckyStrike)
{
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 4);
	luck(target, 1);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SylvanEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto projected = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false), source->getPosition(), cache, model);
	EXPECT_FALSE(projected.defenderDead);
	EXPECT_EQ(projected.attackerState->getAvailableHealth(), 200);
}
