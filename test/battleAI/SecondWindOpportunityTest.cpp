/*
 * SecondWindOpportunityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/CanonicalOrderValue.h"
#include "../../AI/BattleAI/BattleDamageValue.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/SecondWindOpportunity.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <sstream>

namespace
{
class OpportunityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OpportunityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
}

class SecondWindOpportunityTest : public HeroCommandFixture
{
protected:
	CStack * recipient = nullptr;
	CStack * enemy = nullptr;
	std::unique_ptr<OpportunityEnvironment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<HypotheticBattle> parent;
	std::shared_ptr<HypotheticBattle> sibling;

	void prepare(BattleHex enemyHex = BattleHex(14, 5), bool immobile = true,
		BattleHex recipientHex = BattleHex(4, 5), const char * enemyType = "core:peasant")
	{
		startGame();
		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		auto * anchor = addStack(BattleSide::ATTACKER, creatureByName("core:stoneGolem"), BattleHex(3, 5), 1);
		recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), recipientHex, 50);
		enemy = addStack(BattleSide::DEFENDER, creatureByName(enemyType), enemyHex, 1);
		ASSERT_NE(anchor, nullptr);
		ASSERT_NE(recipient, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(immobile)
			recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_SPEED, BonusSource::OTHER, -static_cast<int32_t>(recipient->getMovementRange()), BonusSourceID()));
		beginCombat();
		recipient->movedThisRound = true;
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = anchor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_TRUE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
			HeroCommand::SECOND_WIND, {recipient->unitId()}));
		environment = std::make_unique<OpportunityEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}

	float score(const std::shared_ptr<CBattleInfoCallback> & source = nullptr)
	{
		const auto view = source ? source : live;
		const std::vector<uint32_t> targets{recipient->unitId()};
		heroOrderValue::CanonicalOrderValue estimate(*view, BattleSide::ATTACKER,
			HeroCommand::SECOND_WIND, targets);
		const auto value = estimate.directValue(std::nullopt, environment.get(), view);
		EXPECT_TRUE(value.direct);
		return value.direct.value_or(-1.0f);
	}

	std::shared_ptr<const CBattleInfoCallback> project(const std::shared_ptr<CBattleInfoCallback> & source = nullptr)
	{
		const auto view = source ? source : live;
		return heroOrderValue::projectSecondWindActivation(*view, environment.get(), view,
			BattleSide::ATTACKER, recipient->unitId());
	}

	void fork()
	{
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}

	std::string state(const CBattleInfoCallback & view)
	{
		std::ostringstream out;
		auto units = view.battleGetAllUnits(false);
		std::ranges::sort(units, {}, &battle::Unit::unitId);
		for(const auto * unit : units)
		{
			out << unit->unitId() << ':' << unit->acquireState()->save().toString();
			const auto bonuses = heroQueueTest::bonusSnapshots(*unit->getAllBonuses(
				CSelector([](const Bonus *) { return true; })));
			for(const auto & bytes : bonuses)
				out << bytes.size() << ':' << bytes;
		}
		out << view.getBattle()->getRound() << ':' << view.getBattle()->getActiveStackID();
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto ledger = serialized(view.getBattle()->getHeroActionAllowances(side));
			const auto orders = serialized(view.getBattle()->getHeroOrderStates(side));
			for(auto byte : ledger) out << static_cast<unsigned>(byte) << ',';
			for(auto byte : orders) out << static_cast<unsigned>(byte) << ',';
		}
		return out.str();
	}

	std::vector<std::string> outside()
	{
		std::vector<std::string> result{state(*live), state(*parent), state(*sibling)};
		const auto append = [&result](const auto & value)
		{
			const auto bytes = serialized(value);
			result.emplace_back(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		};
		append(std::vector<int64_t>{attackerSideHero->getManaAvailable(), attackerSideHero->getNormalSpellPoints(),
			attackerSideHero->getBufferSpellPoints(), defenderSideHero->getManaAvailable(),
			defenderSideHero->getNormalSpellPoints(), defenderSideHero->getBufferSpellPoints()});
		append(*gameHandler->randomizer);
		append(dynamic_cast<CRandomGenerator &>(*getRNG()));
		append(std::vector<size_t>{server.startedActions.size(), server.stackActivations.size(),
			server.orderStateUpdates.size(), server.attacks.size(), server.injuries.size(), server.battleLogLines.size(),
			server.casts.size(), server.battleAnimations.size(), server.unitAdditionRounds.size(), server.progressionPackets.size()});
		return result;
	}
};

TEST_F(SecondWindOpportunityTest, ImmobileDistantRecipientHasNoDirectDamageDespiteLegalGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(recipient->getMovementRange(0), 0u);
	ASSERT_FALSE(battle()->battleCanShootAction(recipient, enemy->getPosition()));
	ASSERT_FALSE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	const auto ledger = serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto orders = serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER));
	const auto rng = serialized(*gameHandler->randomizer);
	const auto traffic = server.startedActions.size();
	EXPECT_EQ(score(), 0.0f);
	EXPECT_EQ(serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), ledger);
	EXPECT_EQ(serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)), orders);
	EXPECT_EQ(serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(server.startedActions.size(), traffic);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, recipient->unitId())));
	EXPECT_EQ(battle()->getActiveStackID(), recipient->unitId());
	EXPECT_EQ(recipient->getMovementRange(0), 0u);
	EXPECT_FALSE(battle()->battleCanShootAction(recipient, enemy->getPosition()));
	EXPECT_FALSE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
}

TEST_F(SecondWindOpportunityTest, ImmobileAdjacentMeleeRecipientRetainsPositiveValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(5, 5)));
	ASSERT_TRUE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	const auto mean = battleDamageValue::averageOrderDamage(
		battle()->battleEstimateDamage(BattleAttackInfo(recipient, enemy, 0, false)));
	const auto expected = mean * static_cast<float>(heroCommands::secondWindPercent(*attackerSideHero)) / 100.0f;
	ASSERT_GT(expected, 0.0f);
	EXPECT_EQ(score(), expected) << "This reachability correction must not convert or retune the historical raw damage score";
}

TEST_F(SecondWindOpportunityTest, ReachableMobileMeleeRecipientRetainsPositiveValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(7, 5), false));
	ASSERT_TRUE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	EXPECT_GT(score(), 0.0f);
}

TEST_F(SecondWindOpportunityTest, ImmobileShooterRetainsItsLegalRangedOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto type : {BonusType::SHOOTER, BonusType::SHOTS})
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			type, BonusSource::OTHER, 10, BonusSourceID()));
	ASSERT_TRUE(battle()->battleCanShootAction(recipient, enemy->getPosition()));
	EXPECT_GT(score(), 0.0f);
}

TEST_F(SecondWindOpportunityTest, ImmobileLongReachRecipientRetainsItsLegalExtendedMelee)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(6, 5)));
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LONG_REACH, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	EXPECT_GT(score(), 0.0f);
}

TEST_F(SecondWindOpportunityTest, DoubleWideVictimsAreNotReducedToTheirHeadHex)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(7, 5), true, BattleHex(9, 5), "core:cavalier"));
	ASSERT_TRUE(enemy->doubleWide());
	ASSERT_FALSE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	ASSERT_TRUE(battle()->battleCanAttackHex(recipient, enemy->occupiedHex()));
	EXPECT_GT(score(), 0.0f);
}

TEST_F(SecondWindOpportunityTest, NoEscapeExpiresAtGenuineExtraActivationBeforeReachQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(8, 5), false));
	// Pin this geometric regression locally; the content's Pikeman Speed is not
	// the fixture contract. The battle-long adjustment must survive Second Wind.
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_SPEED, BonusSource::OTHER,
		4 - static_cast<int32_t>(recipient->getMovementRange(0)), BonusSourceID()));
	ASSERT_EQ(recipient->getMovementRange(0), 4u);
	recipient->addNewBonus(std::make_shared<Bonus>(newHorizonsShroud::noEscapeSpeedPenalty()));
	recipient->castSpellThisTurn = true;
	recipient->cleaveUsedThisActivation = true;
	ASSERT_EQ(recipient->getMovementRange(0), 2u);
	ASSERT_FALSE(live->battleCanAttackHex(recipient, enemy->getPosition()));
	fork();
	const auto before = outside();
	const auto authorityRng = serialized(*gameHandler->randomizer);
	const auto fixtureRng = serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto traffic = server.stackActivations.size();
	auto continuation = std::make_shared<HypotheticBattle>(environment.get(), parent);
	continuation->nextTurn(recipient->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(continuation->battleGetUnitByID(recipient->unitId())->getMovementRange(0), 2u);
	EXPECT_TRUE(continuation->battleGetUnitByID(recipient->unitId())->acquireState()->castSpellThisTurn);
	const auto forecast = project(parent);
	ASSERT_NE(forecast, nullptr);
	const auto * projected = forecast->battleGetUnitByID(recipient->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->getMovementRange(0), 4u);
	EXPECT_FALSE(projected->hasBonus(CSelector(newHorizonsShroud::isNoEscapeSpeedPenalty)));
	EXPECT_FALSE(projected->acquireState()->castSpellThisTurn);
	EXPECT_FALSE(projected->acquireState()->cleaveUsedThisActivation);
	EXPECT_TRUE(forecast->battleCanAttackHex(projected, enemy->getPosition()));
	EXPECT_TRUE(heroOrderValue::hasSecondWindAttackOpportunity(*forecast, projected->unitId(), enemy->unitId()));
	const auto rawMean = battleDamageValue::averageOrderDamage(
		battle()->battleEstimateDamage(BattleAttackInfo(recipient, enemy, 0, false)));
	const auto expected = rawMean * static_cast<float>(heroCommands::secondWindPercent(*attackerSideHero)) / 100.0f;
	ASSERT_GT(expected, 0.0f);
	EXPECT_EQ(score(parent), expected);
	EXPECT_EQ(outside(), before);
	EXPECT_EQ(serialized(*gameHandler->randomizer), authorityRng);
	EXPECT_EQ(serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	EXPECT_EQ(server.stackActivations.size(), traffic);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, recipient->unitId())));
	ASSERT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(battle()->getActiveStackID(), recipient->unitId());
	EXPECT_EQ(recipient->getMovementRange(0), 4u);
	EXPECT_TRUE(battle()->battleCanAttackHex(recipient, enemy->getPosition()));
	EXPECT_FALSE(recipient->castSpellThisTurn);
	EXPECT_FALSE(recipient->cleaveUsedThisActivation);
}

TEST_F(SecondWindOpportunityTest, ActivationExpiryRemovesInactivityBeforeCheckingActionPermission)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(5, 5)));
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		BonusType::NOT_ACTIVE, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_FALSE(recipient->canMove());
	const auto forecast = project();
	ASSERT_NE(forecast, nullptr);
	EXPECT_TRUE(forecast->battleGetUnitByID(recipient->unitId())->canMove());
	EXPECT_GT(score(), 0.0f);
	EXPECT_FALSE(recipient->canMove());
}

TEST_F(SecondWindOpportunityTest, LegacyGetsTurnInactivityDoesNotExpireDuringHeroCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(5, 5)));
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN,
		BonusType::NOT_ACTIVE, BonusSource::OTHER, 1, BonusSourceID()));
	const auto forecast = project();
	ASSERT_NE(forecast, nullptr);
	EXPECT_FALSE(forecast->battleGetUnitByID(recipient->unitId())->canMove());
	EXPECT_EQ(score(), 0.0f);
}

TEST_F(SecondWindOpportunityTest, FrozenExtraDoesNotThawOrExpireActivationBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(5, 5), false));
	recipient->addNewBonus(std::make_shared<Bonus>(newHorizonsShroud::noEscapeSpeedPenalty()));
	SetStackEffect frozen;
	frozen.battleID = BattleID(0);
	frozen.toAdd.emplace_back(recipient->unitId(), std::vector<Bonus>{newHorizonsFrozen::makeFrozenMarker(
		BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
	gameHandler->sendAndApply(frozen);
	fork();
	const auto before = outside();
	const auto forecast = project(parent);
	ASSERT_NE(forecast, nullptr);
	const auto * projected = forecast->battleGetUnitByID(recipient->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*projected));
	EXPECT_EQ(projected->acquireState()->frozenLastAppliedRound(), recipient->frozenLastAppliedRound());
	EXPECT_TRUE(projected->hasBonus(CSelector(newHorizonsShroud::isNoEscapeSpeedPenalty)));
	EXPECT_FALSE(heroOrderValue::hasSecondWindAttackOpportunity(*forecast, projected->unitId(), enemy->unitId()));
	EXPECT_EQ(score(parent), 0.0f);
	EXPECT_EQ(outside(), before);
}

TEST_F(SecondWindOpportunityTest, AbsentOrMismatchedContextDoesNotInventAZeroOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<uint32_t> ids{recipient->unitId()};
	heroOrderValue::CanonicalOrderValue prior(*live, BattleSide::ATTACKER, HeroCommand::SECOND_WIND, ids);
	EXPECT_GT(prior.directValue(std::nullopt).direct.value_or(-1.0f), 0.0f);
	EXPECT_EQ(heroOrderValue::projectSecondWindActivation(*live, nullptr, live,
		BattleSide::ATTACKER, recipient->unitId()), nullptr);
	fork();
	EXPECT_EQ(heroOrderValue::projectSecondWindActivation(*live, environment.get(), parent,
		BattleSide::ATTACKER, recipient->unitId()), nullptr);
}
