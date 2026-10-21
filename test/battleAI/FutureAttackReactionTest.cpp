/*
 * FutureAttackReactionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/PreemptiveAttackRequest.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"

class FutureAttackReactionTest : public OriginalMeleeSequenceTest
{
protected:
	std::shared_ptr<HypotheticBattle> model()
	{
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto result = std::make_shared<HypotheticBattle>(environment.get(), player);
		result->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		return result;
	}
	std::vector<std::byte> state(const CBattleInfoCallback & source)
	{
		std::vector<std::byte> bytes;
		const auto append = [&](const auto & value)
		{
			const auto more = projectedHitTest::serialized(value);
			bytes.insert(bytes.end(), more.begin(), more.end());
		};
		for(const auto * unit : source.battleGetAllUnits(false))
			append(unit->acquireState()->save());
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			append(source.getBattle()->getHeroOrderStates(side));
			append(source.getBattle()->getHeroActionAllowances(side));
		}
		append(*gameHandler->randomizer);
		append(dynamic_cast<CRandomGenerator &>(*getRNG()));
		return bytes;
	}
	void prepareFuture(bool brace = true, int extra = 0)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(brace, false, false, false, extra));
		releaseKnown();
		ASSERT_TRUE(incoming->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
	}
	void selectDoubleCommand()
	{
		const std::string skill = "new-horizons:command";
		const auto id = SecondarySkill(SecondarySkill::decode(skill));
		for(const auto & [rank, perk] : {
			std::pair{MasteryLevel::BASIC, "new-horizons:command.aggressiveCommander"},
			std::pair{MasteryLevel::ADVANCED, "new-horizons:command.veteranCommander"},
			std::pair{MasteryLevel::EXPERT, "new-horizons:command.doubleCommand"}})
		{
			attackerSideHero->setSecSkillLevel(id, rank, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(skill, perk,
				attackerSideHero->getPerkSkillRank(skill)));
			attackerSideHero->applyPerkSelection({skill, perk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
		}
	}
	void expectShortcut(const std::shared_ptr<HypotheticBattle> & branch, bool shooting = false)
	{
		auto actor = branch->getForUpdate(incoming->unitId());
		auto target = branch->getForUpdate(braced->unitId());
		const auto actorBefore = actor->getAvailableHealth();
		const auto position = actor->getPosition();
		const auto targetBefore = target->getAvailableHealth();
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		BattleExchangeVariant exchange;
		exchange.trackAttack(actor, target, shooting, false, cache, branch);
		EXPECT_EQ(actor->getAvailableHealth(), actorBefore);
		EXPECT_EQ(actor->getPosition(), position);
		EXPECT_LT(target->getAvailableHealth(), targetBefore);
	}
	void compareApplied(const std::shared_ptr<HypotheticBattle> & branch,
		uint32_t requestedTarget, int distance)
	{
		auto selected = std::make_shared<HypotheticBattle>(environment.get(), branch);
		auto future = std::make_shared<HypotheticBattle>(environment.get(), branch);
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		const auto ap = AttackPossibility::evaluate(BattleAttackInfo(
			selected->battleGetUnitByID(incoming->unitId()), selected->battleGetUnitByID(requestedTarget),
			distance, false), BattleHex(92), cache, selected);
		ASSERT_EQ(std::ranges::count(ap.fortuneStrikes, true,
			&FortuneStrikeProjection::bracePreemptive), 1);
		BattleExchangeVariant replay, applied;
		replay.trackAttack(ap, selected, cache);
		applied.trackAttack(future->getForUpdate(incoming->unitId()), future->getForUpdate(requestedTarget),
			false, false, cache, future);
		for(const auto * original : branch->battleGetAllUnits(false))
			EXPECT_EQ(future->battleGetUnitByID(original->unitId())->getAvailableHealth(),
				selected->battleGetUnitByID(original->unitId())->getAvailableHealth());
		auto appliedScore = applied.getScore(), replayScore = replay.getScore();
		EXPECT_FLOAT_EQ(appliedScore.value(), -replayScore.value());
	}
	void compareExtraHitOwnership(bool brace)
	{
		ASSERT_NO_FATAL_FAILURE(prepareFuture(brace, 1));
		auto branch = model();
		ASSERT_EQ(AttackPossibility::getAttackCount(*incoming, false, *branch), 2);
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		BattleExchangeVariant applied;
		BattleExchangeAttackResult result;
		int calls = 0;
		for(int hit = 0; hit < 2; ++hit)
		{
			applied.trackAttack(branch->getForUpdate(incoming->unitId()), branch->getForUpdate(braced->unitId()),
				false, false, cache, branch, false, hit == 0, nullptr, nullptr, nullptr, &result);
			++calls;
			if(result.completedAction)
				break;
		}
		EXPECT_EQ(calls, brace ? 1 : 2);
		EXPECT_EQ(result.completedAction, brace);
		const auto actorHealth = branch->battleGetUnitByID(incoming->unitId())->getAvailableHealth();
		const auto targetHealth = branch->battleGetUnitByID(braced->unitId())->getAvailableHealth();
		branch.reset();
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
		ASSERT_EQ(server.attacks.size(), brace ? 3u : 2u);
		if(brace)
		{
			EXPECT_EQ(server.attacks.front().stackAttacking, braced->unitId());
			EXPECT_FALSE(server.attacks.front().counter());
		}
		for(size_t i = brace ? 1 : 0; i < server.attacks.size(); ++i)
		{
			EXPECT_EQ(server.attacks[i].stackAttacking, incoming->unitId());
			EXPECT_FALSE(server.attacks[i].counter());
			ASSERT_EQ(server.attacks[i].bsa.size(), 1u);
			EXPECT_EQ(server.attacks[i].bsa[0].stackAttacked, braced->unitId());
		}
		EXPECT_EQ(incoming->getAvailableHealth(), actorHealth);
		EXPECT_EQ(braced->getAvailableHealth(), targetHealth);
	}
	void checkRecipientSpecificBrace(const std::shared_ptr<HypotheticBattle> & parent, uint32_t protector)
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
		// Exercise the existing public saved carry-recipient contract on detached
		// states: Ward not ready, actual intercepted Protector ready, then reverse.
		for(auto order : branch->battleGetHeroOrderStates(BattleSide::ATTACKER))
		{
			order.divineDisciplineRecipientUnitIds = order.command == HeroCommand::BRACE
				? std::vector<uint32_t>{protector} : std::vector<uint32_t>{braced->unitId(), protector};
			std::ranges::sort(order.divineDisciplineRecipientUnitIds);
			branch->setHeroOrderState(BattleSide::ATTACKER, order);
		}
		branch->nextRound();
		branch->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		auto actor = branch->getForUpdate(incoming->unitId());
		auto ward = branch->getForUpdate(braced->unitId());
		auto shield = branch->getForUpdate(protector);
		ASSERT_EQ(branch->battleResolveHeroOrderTarget(actor.get(), ward.get(), false), shield.get());
		ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(actor.get(), ward.get(), 3, false, false));
		ASSERT_TRUE(branch->battleCanTriggerHeroOrderBrace(actor.get(), shield.get(), 3, false, false));
		ASSERT_NO_FATAL_FAILURE(compareApplied(branch, ward->unitId(), 3));
		auto order = branch->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::BRACE);
		ASSERT_TRUE(order);
		order->divineDisciplineRecipientUnitIds = {ward->unitId()};
		branch->setHeroOrderState(BattleSide::ATTACKER, order);
		ASSERT_TRUE(branch->battleCanTriggerHeroOrderBrace(actor.get(), ward.get(), 3, false, false));
		ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(actor.get(), shield.get(), 3, false, false));
		const auto before = actor->getAvailableHealth(), shieldBefore = shield->getAvailableHealth();
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		BattleExchangeVariant applied;
		BattleExchangeAttackResult result;
		applied.trackAttack(actor, ward, false, false, cache, branch,
			false, true, nullptr, nullptr, nullptr, &result);
		EXPECT_FALSE(result.completedAction);
		EXPECT_EQ(actor->getAvailableHealth(), before);
		EXPECT_LT(shield->getAvailableHealth(), shieldBefore);
	}
	void prepareTwoActorExchange()
	{
		ASSERT_NO_FATAL_FAILURE(prepareFuture(false));
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		braced = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(93), 100);
		incoming = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), 100);
		for(auto * unit : {braced, incoming})
		{
			forceMaximumDamage(unit);
			blockRetaliation(unit);
		}
		incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
		activate(braced->unitId());
		ASSERT_TRUE(issue(HeroCommand::BRACE));
	}
};

TEST_F(FutureAttackReactionTest, TargetProbesAreSiblingAndLiveStateIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	auto parent = model();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto before = state(*parent), liveBefore = state(*battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	BattleExchangeVariant probe;
	std::optional<bool> overwatch;
	const auto first = probe.trackAttack(parent->getForUpdate(incoming->unitId()),
		parent->getForUpdate(braced->unitId()), false, false, cache, parent, true, true, &overwatch);
	const auto second = probe.trackAttack(parent->getForUpdate(incoming->unitId()),
		parent->getForUpdate(braced->unitId()), false, false, cache, parent, true, true, &overwatch);
	EXPECT_FLOAT_EQ(first, second);
	auto score = probe.getScore();
	EXPECT_FLOAT_EQ(score.value(), 0);
	EXPECT_EQ(state(*parent), before);
	EXPECT_EQ(state(*sibling), before);
	EXPECT_EQ(state(*battle()), liveBefore);
	ASSERT_NO_FATAL_FAILURE(compareApplied(parent, braced->unitId(), 3));
	EXPECT_EQ(state(*parent), before);
	EXPECT_EQ(state(*sibling), before);
	EXPECT_EQ(state(*battle()), liveBefore);
}

TEST_F(FutureAttackReactionTest, TwoHexApproachDoesNotManufactureBrace)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	auto branch = model();
	branch->getForUpdate(incoming->unitId())->setPosition(BattleHex(90));
	const auto reachable = branch->getReachability(branch->battleGetUnitByID(incoming->unitId()));
	ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(reachable, BattleHex(92)), 2);
	ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(branch->battleGetUnitByID(incoming->unitId()),
		branch->battleGetUnitByID(braced->unitId()), 2, false, false));
	ASSERT_NO_FATAL_FAILURE(expectShortcut(branch));
}

TEST_F(FutureAttackReactionTest, ExpiredOrderRetainsOrdinaryShortcut)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	auto branch = model();
	branch->nextRound();
	branch->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(branch->battleGetUnitByID(incoming->unitId()),
		branch->battleGetUnitByID(braced->unitId()), 3, false, false));
	ASSERT_NO_FATAL_FAILURE(expectShortcut(branch));
}

TEST_F(FutureAttackReactionTest, NoBraceRetainsOrdinaryShortcut)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture(false));
	auto branch = model();
	ASSERT_FALSE(branch->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::BRACE));
	ASSERT_NO_FATAL_FAILURE(expectShortcut(branch));
}

TEST_F(FutureAttackReactionTest, BlockedMovementCannotInventThreeTileApproach)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	auto branch = model();
	branch->addUnitBonus(incoming->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::OTHER, 1, BonusSourceID())});
	const auto actor = branch->getForUpdate(incoming->unitId());
	const auto reachable = branch->getReachability(actor.get());
	const auto available = branch->battleGetAvailableHexes(reachable, actor.get(), true);
	ASSERT_FALSE(available.contains(BattleHex(92)));
	// The historical distant-damage abstraction remains; only false reaction
	// realization is excluded here. This is NOT proof an unreachable hit is legal.
	ASSERT_NO_FATAL_FAILURE(expectShortcut(branch));
}

TEST_F(FutureAttackReactionTest, ActualCounterContextCannotTriggerBrace)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	auto branch = model();
	auto actor = branch->getForUpdate(incoming->unitId());
	auto target = branch->getForUpdate(braced->unitId());
	BattleAttackInfo counter(actor.get(), target.get(), 3, false);
	counter.retaliation = true;
	ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(actor.get(), target.get(), 3, false, true));
	EXPECT_FALSE(attackPreview::preparePreemptiveStage(attackPreview::PreemptiveStage::BRACE,
		*branch, counter, true, *actor, *target, 0, branch->battleGetRound()));
}

TEST_F(FutureAttackReactionTest, LegalShotDoesNotRealizeMeleeBrace)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(incoming->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	incoming = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(89), 100);
	forceMaximumDamage(incoming);
	blockRetaliation(incoming);
	activate(incoming->unitId());
	auto branch = model();
	ASSERT_TRUE(branch->battleCanShootAction(branch->battleGetUnitByID(incoming->unitId()), braced->getPosition()));
	ASSERT_FALSE(branch->battleCanTriggerHeroOrderBrace(branch->battleGetUnitByID(incoming->unitId()),
		branch->battleGetUnitByID(braced->unitId()), 3, true, false));
	ASSERT_NO_FATAL_FAILURE(expectShortcut(branch, true));
}

TEST_F(FutureAttackReactionTest, ProtectUsesRequestedWardApproachAndReadyRemoteProtector)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture(false));
	auto protector = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 100);
	forceMaximumDamage(protector);
	blockRetaliation(protector);
	ASSERT_NO_FATAL_FAILURE(selectDoubleCommand());
	activate(braced->unitId());
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), braced->unitId())));
	activate(incoming->unitId());
	ASSERT_EQ(battle()->battleResolveHeroOrderTarget(incoming, braced, false), protector);
	ASSERT_TRUE(battle()->isMeleeAttackPossible(incoming, braced, BattleHex(92)));
	ASSERT_FALSE(battle()->isMeleeAttackPossible(incoming, protector, BattleHex(92)));
	std::map<uint32_t, int64_t> expected;
	{
		auto branch = model();
		ASSERT_TRUE(branch->battleCanTriggerHeroOrderBrace(branch->battleGetUnitByID(incoming->unitId()),
			branch->battleGetUnitByID(protector->unitId()), 3, false, false));
		ASSERT_NO_FATAL_FAILURE(checkRecipientSpecificBrace(branch, protector->unitId()));
		ASSERT_NO_FATAL_FAILURE(compareApplied(branch, braced->unitId(), 3));
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		BattleExchangeVariant applied;
		applied.trackAttack(branch->getForUpdate(incoming->unitId()), branch->getForUpdate(braced->unitId()),
			false, false, cache, branch);
		for(const auto * original : battle()->battleGetAllUnits(false))
			expected.emplace(original->unitId(), branch->battleGetUnitByID(original->unitId())->getAvailableHealth());
	}
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[0].stackAttacking, protector->unitId());
	EXPECT_FALSE(server.attacks[0].counter());
	ASSERT_EQ(server.attacks[0].bsa.size(), 1u);
	EXPECT_EQ(server.attacks[0].bsa[0].stackAttacked, incoming->unitId());
	EXPECT_EQ(server.attacks[1].stackAttacking, incoming->unitId());
	EXPECT_FALSE(server.attacks[1].counter());
	ASSERT_EQ(server.attacks[1].bsa.size(), 1u);
	EXPECT_EQ(server.attacks[1].bsa[0].stackAttacked, protector->unitId());
	for(const auto & [id, health] : expected)
		EXPECT_EQ(battle()->battleGetUnitByID(id)->getAvailableHealth(), health);
}

TEST_F(FutureAttackReactionTest, FullBraceActionOwnsBothHitsWithoutOuterLoopDuplication)
{
	ASSERT_NO_FATAL_FAILURE(compareExtraHitOwnership(true));
}

TEST_F(FutureAttackReactionTest, OrdinaryFirstAndExtraHitsRetainSingleHitOwnership)
{
	ASSERT_NO_FATAL_FAILURE(compareExtraHitOwnership(false));
}

TEST_F(FutureAttackReactionTest, FalseDirectCallRemainsSingleHitNotCounterOrMovedAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture(true, 1));
	auto branch = model();
	const auto actor = branch->getForUpdate(incoming->unitId());
	const auto target = branch->getForUpdate(braced->unitId());
	DamageCache cache;
	cache.buildDamageCache(branch, BattleSide::ATTACKER);
	BattleExchangeVariant applied;
	BattleExchangeAttackResult result{true};
	const auto health = actor->getAvailableHealth();
	applied.trackAttack(actor, target, false, false, cache, branch,
		false, false, nullptr, nullptr, nullptr, &result);
	EXPECT_FALSE(result.completedAction);
	EXPECT_EQ(actor->getPosition(), BattleHex(89));
	EXPECT_EQ(actor->getAvailableHealth(), health);
	EXPECT_LT(target->getAvailableHealth(), braced->getAvailableHealth());
}

TEST_F(FutureAttackReactionTest, RealExchangeFutureActorReplaysWholeBraceActionOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepareTwoActorExchange());
	const auto liveBefore = state(*battle());
	int64_t actorHealth = 0, targetHealth = 0;
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto env = std::make_shared<projectedHitTest::Environment>(gameState());
		auto parent = std::make_shared<HypotheticBattle>(env.get(), callback);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::ATTACKER);
		ASSERT_TRUE(parent->battleCanShootAction(parent->battleGetUnitByID(braced->unitId()), incoming->getPosition()));
		const auto opening = AttackPossibility::evaluate(BattleAttackInfo(
			parent->battleGetUnitByID(braced->unitId()), parent->battleGetUnitByID(incoming->unitId()), 0, true),
			braced->getPosition(), cache, parent);
		PotentialTargets targets(parent->battleGetUnitByID(braced->unitId()), cache, parent);
		BattleExchangeEvaluator evaluator(callback, env, 1.0f, 1);
		evaluator.updateReachabilityMap(parent);
		const auto units = evaluator.getExchangeUnits(opening, 0, targets, parent);
		ASSERT_EQ(units.units.size(), 2u);
		ASSERT_EQ(units.units.at(0).size(), 2u);
		ASSERT_EQ(units.units.at(0)[0]->unitId(), braced->unitId());
		ASSERT_EQ(units.units.at(0)[1]->unitId(), incoming->unitId());
		const auto actual = evaluator.evaluateExchange(opening, 0, targets, cache, parent);
		// Independent reference uses only selected AP replay, never the future
		// overload or a duplicate of its outer per-hit/completedAction loop.
		auto reference = std::make_shared<HypotheticBattle>(env.get(), parent);
		BattleExchangeVariant first, second;
		first.trackAttack(opening, reference, cache);
		reference->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		const auto * actor = reference->battleGetUnitByID(incoming->unitId());
		const auto * target = reference->battleGetUnitByID(braced->unitId());
		ASSERT_EQ(AttackPossibility::getAttackCount(*actor, false, *reference), 2);
		const auto reachable = reference->getReachability(actor);
		ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(reachable, BattleHex(92)), 3);
		ASSERT_TRUE(reference->battleCanTriggerHeroOrderBrace(actor, target, 3, false, false));
		const auto future = AttackPossibility::evaluate(BattleAttackInfo(actor, target, 3, false),
			BattleHex(92), cache, reference);
		ASSERT_EQ(std::ranges::count(future.fortuneStrikes, true,
			&FortuneStrikeProjection::bracePreemptive), 1);
		second.trackAttack(future, reference, cache);
		auto firstScore = first.getScore(), secondScore = second.getScore();
		// Same existing one-round/two-queue scaling as AttackResourceProjectionTest.
		EXPECT_FLOAT_EQ(actual, (firstScore.value() - secondScore.value()) / 2.0f);
		actorHealth = reference->battleGetUnitByID(incoming->unitId())->getAvailableHealth();
		targetHealth = reference->battleGetUnitByID(braced->unitId())->getAvailableHealth();
		ASSERT_GT(actorHealth, 0);
		ASSERT_GT(targetHealth, 0);
	}
	EXPECT_EQ(state(*battle()), liveBefore);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(braced, incoming)));
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), incoming->unitId());
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	ASSERT_EQ(server.attacks.size(), 3u);
	EXPECT_EQ(server.attacks[0].stackAttacking, braced->unitId());
	EXPECT_EQ(server.attacks[1].stackAttacking, incoming->unitId());
	EXPECT_EQ(server.attacks[2].stackAttacking, incoming->unitId());
	EXPECT_EQ(incoming->getAvailableHealth(), actorHealth);
	EXPECT_EQ(braced->getAvailableHealth(), targetHealth);
}
