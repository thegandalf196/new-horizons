/*
 * NewHorizonsForcedMarchTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <memory>
#include <string>

namespace
{
constexpr auto logisticsSkillId = "new-horizons:logistics";
constexpr auto forcedMarchPerkId = "new-horizons:logistics.forcedMarch";
constexpr PlayerColor attackerPlayer(0);

bool activateForcedMarch(JsonNode & rules)
{
	for(auto & [skillId, skill] : rules["skills"].Struct())
		for(auto & perk : skill["perks"].Vector())
			if(perk["id"].String() == forcedMarchPerkId)
			{
				perk["effect"]["status"].String() = "active";
				return true;
			}
	return false;
}

class NewHorizonsForcedMarchTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activateForcedMarch(perkRules))
			throw std::runtime_error("Missing Forced March in the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void prepareGame()
	{
		startGame();
		ASSERT_TRUE(attackerSideHero->usesNewHorizonsMovement());
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
			{
				auto & tile = map()->getTile({x, y, 0});
				tile.terrainType = ETerrainId::GRASS;
				tile.roadType = RoadId::NO_ROAD;
			}
	}

	void chooseForcedMarch()
	{
		const auto decoded = SecondarySkill::decode(logisticsSkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill logistics(decoded);
		gameHandler->levelUpHero(attackerSideHero, logistics, false);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(logisticsSkillId), MasteryLevel::BASIC);

		const auto rankLookup = [this](const std::string & skillId)
		{
			return attackerSideHero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			const auto found = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == logisticsSkillId
					&& candidate.selection.perkId == forcedMarchPerkId;
			});
			if(found == offer.end())
				continue;

			ASSERT_EQ(found->requiredRank, MasteryLevel::BASIC);
			const auto choice = static_cast<size_t>(std::distance(offer.begin(), found));
			gameHandler->levelUpHero(attackerSideHero, offer, choice, seed, false);
			ASSERT_TRUE(attackerSideHero->hasActivePerk(logisticsSkillId, forcedMarchPerkId));
			return;
		}
		FAIL() << "Forced March did not appear in a legal Basic Logistics offer";
	}

	int orthogonalStepCost(const int3 & destination)
	{
		const int oldMovement = attackerSideHero->movementPointsRemaining();
		const int movementLimit = attackerSideHero->movementPointsLimit();
		gameHandler->setMovePoints(attackerSideHero->id, movementLimit);
		PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
		const auto info = paths.getPathsInfo(attackerSideHero);
		const auto * node = info->getNode(destination, EPathfindingLayer::LAND);
		EXPECT_NE(node, nullptr);
		if(!node)
		{
			gameHandler->setMovePoints(attackerSideHero->id, oldMovement);
			return 0;
		}
		EXPECT_EQ(node->turns, 0);
		const int cost = movementLimit - node->moveRemains;
		gameHandler->setMovePoints(attackerSideHero->id, oldMovement);
		return cost;
	}

	void exhaustMovementOnNextStandardStep(int expectedAward)
	{
		const auto source = attackerSideHero->visitablePos();
		const auto destination = source + int3(1, 0, 0);
		const int cost = orthogonalStepCost(destination);
		ASSERT_GT(cost, 0);
		gameHandler->setMovePoints(attackerSideHero->id, cost);

		PathfinderCache exactBudget(gameState().get(), PathfinderOptions(*gameState()));
		const auto exactPaths = exactBudget.getPathsInfo(attackerSideHero);
		const auto * exactNode = exactPaths->getNode(destination, EPathfindingLayer::LAND);
		ASSERT_NE(exactNode, nullptr);
		ASSERT_EQ(exactNode->turns, 0);
		ASSERT_EQ(exactNode->moveRemains, 0);
		ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id,
			attackerSideHero->convertFromVisitablePos(destination), EMovementMode::STANDARD,
			false, attackerPlayer, EPathfindingLayer::LAND));
		EXPECT_EQ(attackerSideHero->visitablePos(), destination);
		EXPECT_EQ(attackerSideHero->movementPointsRemaining(), expectedAward);
	}

	void resolveBattleDialogs()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			const auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
			}
		}
	}

	void advanceCurrentBattleRound()
	{
		BattleNextRound nextRound;
		nextRound.battleID = battle()->getBattleID();
		gameHandler->sendAndApply(nextRound);
	}
};
} // namespace

TEST_F(NewHorizonsForcedMarchTest, ExhaustionGrantsMovementAndSnapshotsOneFirstRoundPenalty)
{
	prepareGame();
	chooseForcedMarch();
	const int initialLimit = attackerSideHero->movementPointsLimit();
	const int awardedMovement = std::max(0, initialLimit) / 10;
	ASSERT_GT(awardedMovement, 0);

	exhaustMovementOnNextStandardStep(awardedMovement);
	const int useDay = gameState()->getCalendar().getCurrentDay();
	ASSERT_EQ(attackerSideHero->getNewHorizonsForcedMarchLastUseDay(), useDay);
	ASSERT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), useDay);
	ASSERT_EQ(attackerSideHero->movementPointsRemaining(), awardedMovement);

	const auto nextPosition = attackerSideHero->visitablePos() + int3(1, 0, 0);
	const int nextStepCost = orthogonalStepCost(nextPosition);
	ASSERT_GT(nextStepCost, 0);
	ASSERT_LE(nextStepCost, awardedMovement);
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id,
		attackerSideHero->convertFromVisitablePos(nextPosition), EMovementMode::STANDARD,
		false, attackerPlayer, EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->visitablePos(), nextPosition);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), awardedMovement - nextStepCost)
		<< "The extra Movement is spendable through an ordinary accepted step";

	// Test a later accepted exhaustion on the same day after the first allowance
	// has been spent; the exact-budget setup is only the second exhaustion boundary.
	const auto finalPosition = attackerSideHero->visitablePos() + int3(1, 0, 0);
	const int finalStepCost = orthogonalStepCost(finalPosition);
	ASSERT_GT(finalStepCost, 0);
	gameHandler->setMovePoints(attackerSideHero->id, finalStepCost);
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id,
		attackerSideHero->convertFromVisitablePos(finalPosition), EMovementMode::STANDARD,
		false, attackerPlayer, EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchLastUseDay(), useDay);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), useDay);

	const auto attackerHeroId = attackerSideHero->id;
	gameHandler->createWanderingMonster({20, 20, 0}, CreatureID(0), 1);
	auto * firstMonster = findFirst<CGCreature>();
	ASSERT_NE(firstMonster, nullptr);
	gameHandler->battles->startBattle(attackerSideHero, firstMonster);
	ASSERT_NE(battle(), nullptr);
	ASSERT_EQ(battle()->getFirstRoundMoraleModifier(BattleSide::ATTACKER), -1);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), -1)
		<< "The accepted battle setup consumes the pending marker, not the captured snapshot";

	// Retrying the same accepted battle must preserve its captured penalty even
	// though the hero's world marker is already consumed.
	const auto oldBattleID = battle()->getBattleID();
	const auto battleTile = battle()->getLocation();
	const auto layout = battle()->getLayout();
	gameHandler->battles->restartBattle(oldBattleID, attackerSideHero, firstMonster,
		battleTile, attackerSideHero, nullptr, layout, nullptr);
	ASSERT_NE(battle(), nullptr);
	EXPECT_EQ(battle()->getFirstRoundMoraleModifier(BattleSide::ATTACKER), -1);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), -1);

	ASSERT_EQ(battle()->getRound(), 1);
	const auto attackerStacks = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_FALSE(attackerStacks.empty());
	EXPECT_EQ(battle()->battleGetMorale(attackerStacks.front()),
		attackerStacks.front()->moraleValWithBonus(-1));
	advanceCurrentBattleRound();
	ASSERT_EQ(battle()->getRound(), 2);
	EXPECT_EQ(battle()->battleGetMorale(attackerStacks.front()), attackerStacks.front()->moraleVal());

	gameHandler->battles->cheatBattleVictory(attackerPlayer);
	resolveBattleDialogs();
	EXPECT_EQ(gameState()->getBattle(attackerPlayer), nullptr);

	// A second actual battle on that same calendar day sees no residual penalty.
	auto * survivingAttacker = gameState()->getHero(attackerHeroId);
	ASSERT_NE(survivingAttacker, nullptr);
	const auto previousMonsters = findAll<CGCreature>();
	gameHandler->createWanderingMonster({24, 24, 0}, CreatureID(0), 1);
	const auto monstersAfterSecondCreation = findAll<CGCreature>();
	ASSERT_GT(monstersAfterSecondCreation.size(), previousMonsters.size());
	auto * secondMonster = monstersAfterSecondCreation.back();
	ASSERT_NE(secondMonster, nullptr);
	gameHandler->battles->startBattle(survivingAttacker, secondMonster);
	ASSERT_NE(battle(), nullptr);
	EXPECT_EQ(battle()->getFirstRoundMoraleModifier(BattleSide::ATTACKER), 0);
	gameHandler->battles->cheatBattleVictory(attackerPlayer);
	resolveBattleDialogs();
}

TEST_F(NewHorizonsForcedMarchTest, RejectedAndNonstandardMovesDoNotGrantTheDailyAllowance)
{
	prepareGame();
	chooseForcedMarch();
	const auto source = attackerSideHero->visitablePos();
	const int cost = orthogonalStepCost(source + int3(1, 0, 0));
	ASSERT_GT(cost, 0);
	gameHandler->setMovePoints(attackerSideHero->id, cost);

	const auto blocked = source + int3(1, 0, 0);
	map()->getTile(blocked).terrainType = ETerrainId::ROCK;
	EXPECT_FALSE(gameHandler->moveHero(attackerSideHero->id,
		attackerSideHero->convertFromVisitablePos(blocked), EMovementMode::STANDARD,
		false, attackerPlayer, EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->visitablePos(), source);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), cost);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchLastUseDay(), -1);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), -1);

	// A valid nonstandard teleportation consumes Movement under NH rules but is
	// not an exhaustion-triggering Standard step.
	map()->getTile(blocked).terrainType = ETerrainId::GRASS;
	const auto remote = source + int3(5, 0, 0);
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id,
		attackerSideHero->convertFromVisitablePos(remote), EMovementMode::CASTLE_GATE,
		false, PlayerColor::NEUTRAL, EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->visitablePos(), remote);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchLastUseDay(), -1);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), -1);
}

TEST_F(NewHorizonsForcedMarchTest, PendingPenaltyExpiresOnTheFollowingCalendarDay)
{
	prepareGame();
	chooseForcedMarch();
	exhaustMovementOnNextStandardStep(std::max(0, attackerSideHero->movementPointsLimit()) / 10);
	const int useDay = gameState()->getCalendar().getCurrentDay();
	ASSERT_EQ(attackerSideHero->getNewHorizonsForcedMarchPenaltyDay(), useDay);

	NewTurn nextDay;
	nextDay.day = static_cast<ui32>(useDay + 1);
	gameHandler->sendAndApply(nextDay);
	ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), useDay + 1);

	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(battle(), nullptr);
	EXPECT_EQ(battle()->getFirstRoundMoraleModifier(BattleSide::ATTACKER), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsForcedMarchLastUseDay(), useDay);
}

TEST_F(NewHorizonsForcedMarchTest, HeroAndTypedStatePreserveCurrentDataAndGuardOlderFormats)
{
	prepareGame();
	attackerSideHero->setNewHorizonsForcedMarchState(17, 17);
	const auto currentCopy = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_NE(currentCopy, nullptr);
	EXPECT_EQ(currentCopy->getNewHorizonsForcedMarchLastUseDay(), 17);
	EXPECT_EQ(currentCopy->getNewHorizonsForcedMarchPenaltyDay(), 17);

	CMemorySerializer currentPacketWire;
	currentPacketWire.oser.version = ESerializationVersion::CURRENT;
	currentPacketWire.iser.version = ESerializationVersion::CURRENT;
	SetNewHorizonsForcedMarchState outgoing;
	outgoing.heroID = attackerSideHero->id;
	outgoing.lastUseDay = 19;
	outgoing.penaltyDay = 19;
	currentPacketWire.oser & outgoing;
	SetNewHorizonsForcedMarchState incoming;
	currentPacketWire.iser & incoming;
	EXPECT_EQ(incoming.heroID, outgoing.heroID);
	EXPECT_EQ(incoming.lastUseDay, 19);
	EXPECT_EQ(incoming.penaltyDay, 19);

	const auto previousVersion = ESerializationVersion::BONUS_STATUS_TAGS;
	attackerSideHero->setNewHorizonsForcedMarchState(-1, -1);
	CMemorySerializer oldHeroWire;
	oldHeroWire.oser.version = previousVersion;
	oldHeroWire.iser.version = previousVersion;
	oldHeroWire.iser.cb = gameState().get();
	oldHeroWire.oser & attackerSideHero;
	std::unique_ptr<CGHeroInstance> oldHeroCopy;
	oldHeroWire.iser & oldHeroCopy;
	ASSERT_NE(oldHeroCopy, nullptr);
	EXPECT_EQ(oldHeroCopy->getNewHorizonsForcedMarchLastUseDay(), -1);
	EXPECT_EQ(oldHeroCopy->getNewHorizonsForcedMarchPenaltyDay(), -1);

	attackerSideHero->setNewHorizonsForcedMarchState(17, 17);
	CMemorySerializer oldHeroWriter;
	oldHeroWriter.oser.version = previousVersion;
	EXPECT_THROW(oldHeroWriter.oser & *attackerSideHero, std::runtime_error);
	EXPECT_TRUE(oldHeroWriter.extractBuffer().empty())
		<< "A populated Forced March hero snapshot must be rejected before old-format bytes";

	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = previousVersion;
	EXPECT_THROW(oldPacketWriter.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty())
		<< "The typed packet must reject an older wire version before writing bytes";
}
