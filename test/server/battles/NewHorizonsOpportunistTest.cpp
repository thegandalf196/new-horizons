/*
 * NewHorizonsOpportunistTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include <algorithm>
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsOpportunist.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class OpportunistEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OpportunistEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsOpportunistTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		JsonNode curve;
		for(int i = 0; i < 10; ++i) curve.Vector().emplace_back(100);
		map->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, curve);
		map->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}
	void select(CGHeroInstance * hero, const std::string & skill, const std::string & perk)
	{
		const SecondarySkill id(SecondarySkill::decode(skill));
		hero->setSecSkillLevel(id, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(
				[hero](const std::string & name) { return hero->getPerkSkillRank(name); }, seed);
			for(size_t index = 0; index < offer.size(); ++index)
				if(offer[index].selection.skillId == skill && offer[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(hero, offer, index, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No shipped active legal offer for " << perk;
	}
	void prepare(bool perk = true, bool gunner = false)
	{
		startGame();
		if(perk) select(attackerSideHero, "new-horizons:luck", "new-horizons:luck.opportunist");
		if(gunner) select(attackerSideHero, "new-horizons:warMachines", "new-horizons:warMachines.masterGunner");
		startBattle();
		const auto capturedLuck = battle()->getLuckRollRules();
		ASSERT_EQ(capturedLuck.diceSize, 100);
		ASSERT_EQ(capturedLuck.goodChance.size(), 10u);
		ASSERT_TRUE(std::ranges::all_of(capturedLuck.goodChance, [](int chance) { return chance == 100; }));
		// Isolate the authored scenario from the map's token armies/commanders.
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}
	void lucky(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	}
	bool act(CStack * actor, const BattleAction & action)
	{
		if(battle()->getRound() == 0)
			beginCombat();
		battle()->activeStack = actor->unitId(); // Same accepted-action setup as existing Pursuit fixtures.
		if(action.actionType == EActionType::SHOOT)
		{
			const auto targets = action.getTarget(battle());
			EXPECT_EQ(targets.size(), 1u);
			if(targets.size() == 1)
				EXPECT_TRUE(battle()->battleCanShootAction(actor, targets.front().hexValue))
					<< "Accepted-shot fixture requires the real shooting predicate";
		}
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(actor), action);
	}
	CStack * target()
	{
		// Columns zero and sixteen are hero-only borders, not legal shot targets.
		const BattleHex position(14, 5);
		EXPECT_TRUE(position.isAvailable());
		return addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), position, 1000);
	}
	void expectLucky(const CStack * actor)
	{
		ASSERT_TRUE(std::ranges::any_of(server.attacks, [actor](const auto & attack)
			{ return attack.stackAttacking == actor->unitId() && attack.lucky(); }));
	}
};
}

TEST_F(NewHorizonsOpportunistTest, ActualLuckyNonlethalMeleeOpensOnlyBoundedMovement)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(82), 1000);
	lucky(actor); blockRetaliation(actor);
	ASSERT_TRUE(act(actor, BattleAction::makeMeleeAttack(actor, enemy->getPosition(), BattleHex(81))));
	expectLucky(actor);
	EXPECT_TRUE(enemy->alive());
	EXPECT_EQ(actor->pursuitMovementRemaining, 2);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(battle()->battleActiveUnit(), actor);
	BattleClientInterfaceData data{};
	const auto actions = battle()->getClientActionsForStack(actor, data);
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions.front(), PossiblePlayerBattleAction::MOVE_STACK);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy->getPosition(), actor->getPosition())));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(actor, BattleHex(80))));
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsOpportunistTest, OrdinaryLuckyShootRetainsTheSameActivation)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(80), 10);
	auto * enemy = target(); lucky(actor);
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, enemy)));
	expectLucky(actor);
	EXPECT_EQ(actor->pursuitMovementRemaining, 2);
	EXPECT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, enemy)));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
	EXPECT_FALSE(actor->defended());
}

TEST_F(NewHorizonsOpportunistTest, InnerDoubleAttackGrantsOneTailNotFourHexes)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(80), 10);
	auto * enemy = target(); lucky(actor);
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, enemy)));
	EXPECT_GE(std::ranges::count_if(server.attacks, [actor](const auto & hit)
		{ return hit.stackAttacking == actor->unitId() && hit.shot(); }), 2);
	EXPECT_EQ(actor->pursuitMovementRemaining, 2);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
}

TEST_F(NewHorizonsOpportunistTest, MasterGunnerHoldsLuckUntilAcceptedSecondRequest)
{
	prepare(true, true);
	// War machines do not count as surviving armies. Keep this battle live so
	// completing the follow-up exercises the real next-activation boundary.
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 3), 10);
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(80), 1);
	auto * first = target();
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(103), 1000);
	lucky(actor);
	beginCombat();
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	auto environment = std::make_shared<OpportunistEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto firstBranch = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache firstCache;
	BattleAttackInfo firstCandidate(firstBranch->getForUpdate(actor->unitId()).get(),
		firstBranch->getForUpdate(first->unitId()).get(), 0, true);
	EXPECT_EQ(firstBranch->captureFortuneStrikeOutcome(firstCandidate), ProjectedLuckOutcome::POSITIVE);
	const auto firstPrediction = AttackPossibility::evaluate(
		firstCandidate,
		actor->getPosition(), firstCache, firstBranch);
	EXPECT_TRUE(firstPrediction.attackerState->luckyOwnAttackSequence);
	EXPECT_EQ(firstPrediction.attackerState->pursuitMovementRemaining, 0);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, first)));
	expectLucky(actor);
	ASSERT_GT(actor->rangedFollowUpDamagePercent, 0);
	EXPECT_TRUE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback);
	branch->nextTurn(actor->unitId(), BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	EXPECT_TRUE(branch->getForUpdate(actor->unitId())->luckyOwnAttackSequence);
	DamageCache secondCache;
	const auto secondPrediction = AttackPossibility::evaluate(
		BattleAttackInfo(branch->getForUpdate(actor->unitId()).get(),
			branch->getForUpdate(second->unitId()).get(), 0, true),
		actor->getPosition(), secondCache, branch);
	EXPECT_FALSE(secondPrediction.attackerState->luckyOwnAttackSequence);
	EXPECT_EQ(secondPrediction.attackerState->pursuitMovementRemaining, 0);
	EXPECT_TRUE(branch->getForUpdate(actor->unitId())->luckyOwnAttackSequence);
	EXPECT_TRUE(actor->luckyOwnAttackSequence);
	for(const auto action : {BattleAction::makeMove(actor, BattleHex(81)), BattleAction::makeWait(actor), BattleAction::makeDefend(actor)})
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(actor->luckyOwnAttackSequence);
	const auto completedRound = battle()->getRound();
	const auto completedActivation = battle()->getActivationSerial();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, second)));
	EXPECT_EQ(actor->rangedFollowUpDamagePercent, 0);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->getMovementRange(0), 0u); // Innate SIEGE_WEAPON immobilization is not bypassed.
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	EXPECT_EQ(battle()->getRound(), completedRound);
	EXPECT_GT(battle()->getActivationSerial(), completedActivation);
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_NE(battle()->battleActiveUnit(), actor);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, second)));
}

TEST_F(NewHorizonsOpportunistTest, MasterGunnerDeclineFinalizesHeldSequenceWithoutAnotherAttack)
{
	prepare(true, true);
	// The Ballista alone would already lose, suppressing normal queue advancement.
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 3), 10);
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(80), 1);
	auto * enemy = target(); lucky(actor);
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, enemy)));
	ASSERT_TRUE(actor->luckyOwnAttackSequence);
	auto environment = std::make_shared<OpportunistEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto declined = std::make_shared<HypotheticBattle>(environment.get(), callback);
	ASSERT_TRUE(declined->getForUpdate(actor->unitId())->luckyOwnAttackSequence);
	const auto attacks = server.attacks.size();
	const auto completedRound = battle()->getRound();
	const auto completedActivation = battle()->getActivationSerial();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeNoAction(actor)));
	EXPECT_EQ(server.attacks.size(), attacks);
	// There is no detached NO_ACTION projector. Mirror the genuine activation
	// boundary after its accepted live decline, not a fabricated extra shot.
	declined->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(declined->getForUpdate(actor->unitId())->luckyOwnAttackSequence);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->rangedFollowUpDamagePercent, 0);
	EXPECT_EQ(actor->getMovementRange(0), 0u); // Innate SIEGE_WEAPON immobilization is not bypassed.
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	EXPECT_EQ(battle()->getRound(), completedRound);
	EXPECT_GT(battle()->getActivationSerial(), completedActivation);
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_NE(battle()->battleActiveUnit(), actor);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, enemy)));
}

TEST_F(NewHorizonsOpportunistTest, UnselectedPositiveLuckDoesNotGrantMovement)
{
	prepare(false);
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(80), 10);
	auto * enemy = target(); lucky(actor);
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, enemy)));
	expectLucky(actor);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsOpportunistTest, LuckyRetaliationCannotCreateAnOutOfTurnTail)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 100);
	lucky(actor);
	ASSERT_TRUE(act(enemy, BattleAction::makeMeleeAttack(enemy, actor->getPosition(), enemy->getPosition())));
	expectLucky(actor);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsOpportunistTest, ExistingPursuitWinsByMaximumRatherThanAddition)
{
	prepare();
	select(attackerSideHero, "new-horizons:offense", "new-horizons:offense.pursuit");
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 1);
	lucky(actor); blockRetaliation(actor); forceMaximumDamage(actor);
	ASSERT_TRUE(act(actor, BattleAction::makeMeleeAttack(actor, enemy->getPosition(), actor->getPosition())));
	EXPECT_EQ(actor->pursuitMovementRemaining, actor->getMovementRange(0));
}

TEST_F(NewHorizonsOpportunistTest, SharedAllowanceRespectsRemainingMovementAndIncapacitation)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	actor->luckyOwnAttackSequence = true;
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), actor, 1), 1);
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), actor, 0), 0);
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), actor, 9), 2);
	actor->armorerLastStandEndedActivation = true;
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), actor, 9), 0);
	actor->armorerLastStandEndedActivation = false;
	auto state = actor->acquireState();
	int64_t lethal = state->getAvailableHealth();
	state->damage(lethal);
	EXPECT_FALSE(state->luckyOwnAttackSequence);
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), state.get(), 9), 0);
}

TEST_F(NewHorizonsOpportunistTest, CopyJsonBoundariesAndMalformedLoadPreserveTypedReceipt)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(80), 1);
	actor->luckyOwnAttackSequence = true;
	auto copy = actor->acquireState();
	EXPECT_TRUE(copy->luckyOwnAttackSequence);
	const auto saved = copy->save();
	copy->luckyOwnAttackSequence = false;
	copy->load(saved);
	EXPECT_TRUE(copy->luckyOwnAttackSequence);
	auto bad = saved; bad["state"]["luckyOwnAttackSequence"].String() = "true";
	EXPECT_THROW(copy->load(bad), std::runtime_error);
	EXPECT_TRUE(copy->luckyOwnAttackSequence);
	auto legacy = saved; legacy["state"].Struct().erase("luckyOwnAttackSequence");
	copy->load(legacy);
	EXPECT_FALSE(copy->luckyOwnAttackSequence);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::PURSUIT_CONTINUATION);
	EXPECT_TRUE(actor->luckyOwnAttackSequence);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	actor->luckyOwnAttackSequence = true;
	actor->afterNewRound(false);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
}

TEST_F(NewHorizonsOpportunistTest, CurrentPacketRoundTripAndOldCompositePrefixRejection)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(80), 1);
	actor->luckyOwnAttackSequence = true;
	UnitChanges update(actor->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = actor->save();
	CMemorySerializer current;
	current.oser & update;
	UnitChanges read;
	current.iser & read;
	EXPECT_TRUE(read.data["state"]["luckyOwnAttackSequence"].Bool());
	BattleUnitsChanged units;
	units.battleID = BattleID(0); units.changedStacks.push_back(update);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES;
	EXPECT_THROW(old.oser & units, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	BattleAttack attack; attack.battleID = BattleID(0); attack.attackerChanges = units;
	CMemorySerializer oldAttack; oldAttack.oser.version = old.oser.version;
	EXPECT_THROW(oldAttack.oser & attack, std::runtime_error);
	EXPECT_TRUE(oldAttack.extractBuffer().empty());
	UnitChanges ordinary;
	ordinary.id = actor->unitId();
	ordinary.data["state"]["luckyOwnAttackSequence"].Bool() = false;
	CMemorySerializer oldPlain; oldPlain.oser.version = old.oser.version; oldPlain.iser.version = old.oser.version;
	ASSERT_NO_THROW(oldPlain.oser & ordinary);
	read.data["state"]["luckyOwnAttackSequence"].Bool() = true;
	ASSERT_NO_THROW(oldPlain.iser & read);
	EXPECT_FALSE(read.data["state"]["luckyOwnAttackSequence"].Bool());
}

TEST_F(NewHorizonsOpportunistTest, DetachedCertainLuckPredictionArmsOnlyItsOwnBranch)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 1000);
	lucky(actor); blockRetaliation(actor);
	beginCombat();
	auto environment = std::make_shared<OpportunistEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto source = branch->getForUpdate(actor->unitId());
	auto target = branch->getForUpdate(enemy->unitId());
	BattleAttackInfo candidate(source.get(), target.get(), 0, false);
	EXPECT_EQ(branch->captureFortuneStrikeOutcome(candidate), ProjectedLuckOutcome::POSITIVE);
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(candidate, actor->getPosition(), cache, branch);
	EXPECT_EQ(prediction.attackerState->pursuitMovementRemaining, 2);
	auto selected = std::make_shared<HypotheticBattle>(environment.get(), callback);
	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, selected, cache);
	EXPECT_EQ(selected->getForUpdate(actor->unitId())->pursuitMovementRemaining, 2);
	EXPECT_EQ(branch->getForUpdate(actor->unitId())->pursuitMovementRemaining, 0);
	EXPECT_FALSE(prediction.attackerState->luckyOwnAttackSequence);
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
}

TEST_F(NewHorizonsOpportunistTest, NoLuckPhysicalAttackCannotEarnSequence)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(80), 10);
	auto * enemy = target(); lucky(actor);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(act(actor, BattleAction::makeShotAttack(actor, enemy)));
	EXPECT_FALSE(std::ranges::any_of(server.attacks, [actor](const auto & hit)
		{ return hit.stackAttacking == actor->unitId() && hit.lucky(); }));
	EXPECT_FALSE(actor->luckyOwnAttackSequence);
	EXPECT_EQ(actor->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsOpportunistTest, SharedTailCannotEscapeFrozenOrTimeStop)
{
	prepare();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	actor->luckyOwnAttackSequence = true;
	actor->addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::makeFrozenMarker(BonusSourceID(), 1)));
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), actor, 2), 0);
	auto * stopped = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(90), 1);
	stopped->luckyOwnAttackSequence = true;
	stopped->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::SPELL_EFFECT, 1, BonusSourceID()));
	EXPECT_EQ(newHorizonsOpportunist::movementAllowance(*battle(), stopped, 2), 0);
}

TEST_F(NewHorizonsOpportunistTest, BinaryDescriptorRebindPreservesReusedTailAndOldPrefixesReject)
{
	prepare();
	CStackBasicDescriptor descriptor(creatureByName("core:pikeman"), 3);
	CStack source(&descriptor, PlayerColor(0), 10000, BattleSide::ATTACKER, SlotID(0));
	source.initialPosition = BattleHex(80);
	source.pursuitMovementRemaining = 2;
	CMemorySerializer current;
	current.oser & source;
	CStack restored;
	current.iser.cb = gameState().get();
	current.iser & restored;
	EXPECT_EQ(restored.pursuitMovementRemaining, 2);
	restored.localInit(battle());
	EXPECT_EQ(restored.pursuitMovementRemaining, 2);
	EXPECT_FALSE(restored.luckyOwnAttackSequence);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	source.pursuitMovementRemaining = 0;
	source.luckyOwnAttackSequence = true;
	CMemorySerializer heldOld; heldOld.oser.version = old.oser.version;
	EXPECT_THROW(heldOld.oser & source, std::runtime_error);
	EXPECT_TRUE(heldOld.extractBuffer().empty());
}
