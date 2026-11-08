/*
 * NewHorizonsBattlePlanTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "BattleStartSnapshotFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <string_view>

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto battlePlanPerk = "new-horizons:command.battlePlan";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";

class NewHorizonsBattlePlanTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	bool offerContains(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		const auto weightContext = hero->getPerkOfferWeightContext();
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, weightContext);
			for(const auto & offer : offers)
				if(offer.selection.skillId == commandSkill && offer.selection.perkId == perkId)
					return true;
		}
		return false;
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		const auto weightContext = hero->getPerkOfferWeightContext();
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, weightContext);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(commandSkill, std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectBattlePlan(CGHeroInstance * hero, bool alsoDoubleCommand = false)
	{
		hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		EXPECT_FALSE(offerContains(hero, battlePlanPerk))
			<< "Battle Plan requires Basic Command";
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, battlePlanPerk));
		acceptPerk(hero, battlePlanPerk);
		ASSERT_TRUE(hero->hasActivePerk(commandSkill, battlePlanPerk));

		if(alsoDoubleCommand)
		{
			hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			acceptPerk(hero, advancedCommandPerk);
			hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			acceptPerk(hero, doubleCommandPerk);
			ASSERT_TRUE(hero->hasActivePerk(commandSkill, doubleCommandPerk));
		}
	}

	bool issueOrder(BattleSide side, HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeHeroCommand(side, command));
	}

	bool issueTargetedOrder(BattleSide side, HeroCommand command, uint32_t targetId)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeTargetedHeroCommand(side, command, targetId));
	}

	static size_t ordinaryActivationCount(const RecordingGameServer & server)
	{
		return static_cast<size_t>(std::ranges::count_if(server.stackActivations,
			[](const BattleSetActiveStack & activation)
			{
				return activation.reason == BattleUnitTurnReason::TURN_QUEUE
					|| activation.reason == BattleUnitTurnReason::AUTOMATIC_ACTION;
			}));
	}
};
}

TEST_F(NewHorizonsBattlePlanTest, BasicPerkOfferAndDescriptorPreserveCommandWithoutOpeningGrant)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	startBattle();
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	ASSERT_TRUE(resources.initialized);
	EXPECT_EQ(resources.remaining, resources.maximum);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase, PreCombatOrderState::Phase::NOT_GRANTED);
	EXPECT_EQ(battle()->getActivationSerial(), 0);
	const auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(descriptor, nullptr);
	EXPECT_EQ(descriptor->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(descriptor->getPreCombatOrderState(BattleSide::ATTACKER).phase, PreCombatOrderState::Phase::NOT_GRANTED);
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
}

TEST_F(NewHorizonsBattlePlanTest, BothBattlePlansBeginOrdinaryCreatureCombatAndPaidOrderConsumesNormalAction)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	selectBattlePlan(defenderSideHero);
	startBattle();
	const auto before = ordinaryActivationCount(server);
	beginCombat();
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_GT(ordinaryActivationCount(server), before);
	EXPECT_GT(battle()->getActivationSerial(), 0);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(side));
		EXPECT_EQ(battle()->getHeroActionAllowances(side).countBattlePlanOrderGrants(1), 0u);
		EXPECT_EQ(battle()->getCommandResources(side).remaining, battle()->getCommandResources(side).maximum);
	}
	const auto side = battle()->playerToSide(battle()->battleGetOwner(battle()->battleActiveUnit()));
	const auto resources = battle()->getCommandResources(side);
	ASSERT_TRUE(issueOrder(side, HeroCommand::BRACE));
	EXPECT_EQ(battle()->getCommandResources(side).remaining, resources.remaining - 1);
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(1).heroActions, 0u);
	EXPECT_FALSE(issueOrder(side, HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getCommandResources(side).remaining, resources.remaining - 1);
}

TEST_F(NewHorizonsBattlePlanTest, NoControlledOpeningAnchorDoesNotCreateAChoiceOrStallBattle)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	startBattle();
	for(const auto * unit : battle()->battleGetAllStacks(false))
		if(unit->unitSide() == BattleSide::ATTACKER)
			battle()->getStack(unit->unitId(), false)->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_GT(battle()->getActivationSerial(), 0);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase, PreCombatOrderState::Phase::NOT_GRANTED);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).countBattlePlanOrderGrants(1), 0u);
}

TEST_F(NewHorizonsBattlePlanTest, RoundsDoNotReopenAnOrderWindowOrRefillSpentCommand)
{
	startGame();
	selectBattlePlan(attackerSideHero, true); // Stable Double Command ID now owns Master Commander.
	selectBattlePlan(defenderSideHero, true);
	startBattle();
	beginCombat();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto side = battle()->playerToSide(battle()->battleGetOwner(active));
	const auto resources = battle()->getCommandResources(side);
	ASSERT_TRUE(issueOrder(side, HeroCommand::BRACE));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(side));
	EXPECT_FALSE(battle()->getDoubleCommandState(side).used);
	endRound();
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_EQ(battle()->getCommandResources(side).remaining, resources.remaining - 1);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(side));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(side));
	EXPECT_EQ(battle()->getHeroActionAllowances(side).countBattlePlanOrderGrants(2), 0u);
}
