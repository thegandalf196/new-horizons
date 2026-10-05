/*
 * NewHorizonsCreatureSiegeAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleUnitTurnReason.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

#include <memory>
#include <utility>
#include <vector>

namespace
{
class CreatureSiegeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CreatureSiegeEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingCreatureSiegeCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> unitActions;

	RecordingCreatureSiegeCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction &) override
	{
		// The fixture consumes the ordinary Hero Action before invoking the
		// creature AI, so a spell/order decision here would be a test setup bug.
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		unitActions.push_back(action);
	}

	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
};
}

class NewHorizonsCreatureSiegeAITest : public HeroCommandFixture
{
protected:
	CGTownInstance * fortifiedTown = nullptr;
	CStack * cyclops = nullptr;
	CStack * alliedGroundStack = nullptr;
	CStack * hostileStack = nullptr;
	CStack * visibleHostileStack = nullptr;
	std::shared_ptr<CreatureSiegeEnvironment> environment;
	std::shared_ptr<RecordingCreatureSiegeCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	BattleHex freeHex(bool insideWalls, const std::vector<BattleHex> & reserved,
		BattleHex near = BattleHex::INVALID, int minimumDistance = 0, int maximumDistance = GameConstants::BFIELD_SIZE) const
	{
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex candidate(index);
			if(!candidate.isAvailable()
				|| battle()->battleIsInsideWalls(candidate) != insideWalls
				|| battle()->battleHexToWallPart(candidate) != EWallPart::INVALID
				|| candidate.isTower()
				|| candidate == BattleHex(BattleHex::GATE_BRIDGE)
				|| candidate == BattleHex(BattleHex::GATE_OUTER)
				|| candidate == BattleHex(BattleHex::GATE_INNER)
				|| vstd::contains(reserved, candidate)
				|| battle()->battleGetStackByPos(candidate))
				continue;

			if(near.isValid())
			{
				const int distance = BattleHex::getDistance(near, candidate);
				if(distance < minimumDistance || distance > maximumDistance)
					continue;
			}
			return candidate;
		}
		return BattleHex::INVALID;
	}

	void removeInitialStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepareFortifiedSiege(const CreatureID & attackerCreature, bool needBreach)
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		fortifiedTown = towns.front();
		ASSERT_EQ(fortifiedTown->fortLevel(), CGTownInstance::FORT);

		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(fortifiedTown->id, BuildingID::CITADEL));

		startBattle(fortifiedTown);
		removeInitialStacks();
		std::vector<BattleHex> reserved;

		const auto cyclopsHex = freeHex(false, reserved);
		ASSERT_TRUE(cyclopsHex.isAvailable());
		reserved.push_back(cyclopsHex);
		cyclops = addStack(BattleSide::ATTACKER, attackerCreature, cyclopsHex, 10);
		ASSERT_NE(cyclops, nullptr);

		if(needBreach)
		{
			const auto allyHex = freeHex(false, reserved);
			ASSERT_TRUE(allyHex.isAvailable());
			reserved.push_back(allyHex);
			alliedGroundStack = addStack(BattleSide::ATTACKER,
				creatureByName("core:pikeman"), allyHex, 40);
			ASSERT_NE(alliedGroundStack, nullptr);

			const auto hostileHex = freeHex(true, reserved);
			ASSERT_TRUE(hostileHex.isAvailable());
			reserved.push_back(hostileHex);
			hostileStack = addStack(BattleSide::DEFENDER,
				creatureByName("core:pikeman"), hostileHex, 40);
			ASSERT_NE(hostileStack, nullptr);

			// Keep an immediately useful ordinary ranged action in the evaluator's
			// choice set.  The separate hostile unit inside the fort still creates
			// the breach need, so the AI should prefer the gate over this shot.
			const auto visibleTargetHex = freeHex(false, reserved, cyclopsHex, 5, 9);
			ASSERT_TRUE(visibleTargetHex.isAvailable());
			reserved.push_back(visibleTargetHex);
			visibleHostileStack = addStack(BattleSide::DEFENDER,
				creatureByName("core:pikeman"), visibleTargetHex, 40);
			ASSERT_NE(visibleHostileStack, nullptr);
		}
		else
		{
			// A controlled defender deliberately remains outside the walls.  With no
			// other ground force on the attacker side, the ordinary ranged attack
			// should be preserved instead of being replaced by a wall shot.
			const auto targetHex = freeHex(false, reserved, cyclopsHex, 5, 9);
			ASSERT_TRUE(targetHex.isAvailable());
			hostileStack = addStack(BattleSide::DEFENDER,
				creatureByName("core:pikeman"), targetHex, 40);
			ASSERT_NE(hostileStack, nullptr);
			visibleHostileStack = hostileStack;
		}

		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = cyclops->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), cyclops);
		ASSERT_EQ(battle()->battleGetOwner(cyclops), PlayerColor(0));
		ASSERT_EQ(battle()->battleGetActionController(cyclops), PlayerColor(0));

		// This is an ordinary accepted Order, not a test-only disable of Hero
		// Actions.  It leaves the current creature activation open for the AI.
		ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
		ASSERT_EQ(battle()->battleActiveUnit(), cyclops);

		callback = std::make_shared<RecordingCreatureSiegeCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CreatureSiegeEnvironment>(gameState());
	}

	void runActiveStackAI()
	{
		ASSERT_NE(visibleHostileStack, nullptr);
		ASSERT_TRUE(battle()->battleCanShoot(cyclops, visibleHostileStack->getPosition()))
			<< "The breach-priority fixture must offer a legal ordinary shot before AI selection";

		CBattleAI ai;
		ai.initBattleInterface(environment, callback);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), cyclops);
	}

	void assertAcceptedCreatureCatapultHitsGate()
	{
		ASSERT_EQ(callback->unitActions.size(), 1u);
		const auto & action = callback->unitActions.front();
		ASSERT_EQ(action.actionType, EActionType::CATAPULT);
		ASSERT_EQ(action.stackNumber, cyclops->unitId());
		ASSERT_EQ(action.target.size(), 1u);
		const auto gateHex = battle()->wallPartToBattleHex(EWallPart::GATE);
		ASSERT_TRUE(gateHex.isValid());
		EXPECT_EQ(action.target.front().hexValue, gateHex);

		EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER,
			HeroCommand::HOLD_THE_LINE).has_value());
		ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
		ASSERT_TRUE(battle()->isWallPartAttackable(EWallPart::GATE));
		const int32_t gateHPBefore = battle()->getWallStructuralHP(EWallPart::GATE);
		ASSERT_GT(gateHPBefore, 0);
		ASSERT_GT(battle()->battleGetCatapultStructuralDamage(cyclops, 1), 0);

		// Keep the chance-based original Cyclops ability reproducible; the AI must
		// not turn its authored hit chance into a guaranteed result.
		gameHandler->randomizer->setSeed(BattleTestFixture::seed);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_LT(battle()->getWallStructuralHP(EWallPart::GATE), gateHPBefore);
		EXPECT_NE(battle()->battleActiveUnit(), cyclops)
			<< "The accepted creature Catapult action should finish this activation";
	}
};

TEST_F(NewHorizonsCreatureSiegeAITest, CyclopsUsesRealCreatureCatapultToOpenAClosedGateForGroundForces)
{
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedSiege(creatureByName("core:cyclop"), true));
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	ASSERT_FALSE(battle()->battleIsInsideWalls(alliedGroundStack->getPosition()));
	ASSERT_TRUE(battle()->battleIsInsideWalls(hostileStack->getPosition()));
	ASSERT_NE(visibleHostileStack, nullptr);
	ASSERT_TRUE(cyclops->hasBonusOfType(BonusType::CATAPULT));
	EXPECT_FALSE(cyclops->isCatapult()) << "A Cyclops creature ability is not the Catapult war machine";

	const auto ordinaryWait = BattleAction::makeWait(cyclops);
	EXPECT_FALSE(CBattleAI::shouldUseCreatureCatapult(*battle(), cyclops, ordinaryWait, PlayerColor(0)))
		<< "The siege override must preserve WAIT";

	runActiveStackAI();
	assertAcceptedCreatureCatapultHitsGate();
}

TEST_F(NewHorizonsCreatureSiegeAITest, CyclopsKingUsesItsAuthoredExtraShotOnTheSameGateAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedSiege(creatureByName("core:cyclopKing"), true));
	const auto ability = cyclops->getFirstBonus(Selector::type()(BonusType::CATAPULT));
	ASSERT_NE(ability, nullptr);
	EXPECT_EQ(cyclops->valOfBonuses(Selector::typeSubtype(BonusType::CATAPULT_EXTRA_SHOTS,
		ability->subtype)), 1);
	runActiveStackAI();
	assertAcceptedCreatureCatapultHitsGate();
}

TEST_F(NewHorizonsCreatureSiegeAITest, VisibleOrdinaryShotIsNotReplacedWhenNoBreachGroundForceNeedsTheGate)
{
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedSiege(creatureByName("core:cyclop"), false));
	ASSERT_TRUE(battle()->battleCanShoot(cyclops, hostileStack->getPosition()));
	ASSERT_FALSE(battle()->battleIsInsideWalls(hostileStack->getPosition()));

	runActiveStackAI();
	ASSERT_EQ(callback->unitActions.size(), 1u);
	const auto & action = callback->unitActions.front();
	EXPECT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	EXPECT_EQ(action.target.front().unitValue, static_cast<int32_t>(hostileStack->unitId()));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
}

TEST_F(NewHorizonsCreatureSiegeAITest, FieldBattleDoesNotProjectCreatureCatapultWithoutDefendedWalls)
{
	startGame();
	startBattle();
	removeInitialStacks();
	const BattleHex actorHex(3, 5);
	cyclops = addStack(BattleSide::ATTACKER, creatureByName("core:cyclop"), actorHex, 10);
	ASSERT_NE(cyclops, nullptr);
	const auto defenderHex = freeHex(false, {actorHex}, BattleHex::INVALID);
	ASSERT_TRUE(defenderHex.isAvailable());
	hostileStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), defenderHex, 10);
	ASSERT_NE(hostileStack, nullptr);
	beginCombat();
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = cyclops->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_EQ(battle()->battleActiveUnit(), cyclops);
	EXPECT_EQ(battle()->battleGetFortifications().wallsHealth, 0);
	EXPECT_FALSE(CBattleAI::shouldUseCreatureCatapult(*battle(), cyclops,
		BattleAction::makeDefend(cyclops), PlayerColor(0)));
}
