/*
 * NewHorizonsHoldFastTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsDiscipline.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../include/vcmi/ServerCallback.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <string>

namespace
{
constexpr auto disciplineSkill = "new-horizons:discipline";
constexpr auto holdFastPerk = "new-horizons:discipline.holdFast";
constexpr auto basicDisciplinePerk = "new-horizons:discipline.inspirationalLeader";
constexpr auto commandSkill = "new-horizons:command";

bool setHoldFastActive(JsonNode & rules)
{
	auto & perks = rules["skills"][disciplineSkill]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == holdFastPerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

bool isHoldFastFloor(const Bonus * bonus)
{
	return newHorizonsDiscipline::isHoldFastMoraleFloorBonus(bonus);
}

template<typename Unit>
std::size_t holdFastFloorCount(const Unit * unit)
{
	return static_cast<std::size_t>(unit->getAllBonuses(CSelector(isHoldFastFloor))->size());
}

void addNegativeMorale(CStack * stack)
{
	stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, -20, BonusSourceID()));
}

class HoldFastTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HoldFastTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHoldFastTest : public HeroCommandFixture
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
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setHoldFastActive(perkRules))
			throw std::runtime_error("Missing Hold Fast from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		JsonNode badMoraleChances;
		for(int index = 0; index < 10; ++index)
			badMoraleChances.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, std::move(badMoraleChances));
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptDisciplinePerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == disciplineSkill
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<std::size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(disciplineSkill, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal Discipline perk offer";
	}

	void selectHoldFast(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(disciplineSkill);
		ASSERT_GE(decoded, 0);
		const SecondarySkill skill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptDisciplinePerk(hero, basicDisciplinePerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptDisciplinePerk(hero, holdFastPerk);
		ASSERT_TRUE(hero->hasActivePerk(disciplineSkill, holdFastPerk));
	}

	void selectBasicCommand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}

	bool issueOrder(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	void activate(const CStack * stack, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = reason;
		gameHandler->sendAndApply(activation);
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 24; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}
		FAIL() << "The expected stack did not receive an ordinary activation";
	}

	std::size_t badMoraleActions(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::count_if(server.startedActions.begin(), server.startedActions.end(),
			[stack](const StartAction & action)
			{
				return action.ba.stackNumber == stack->unitId()
					&& action.ba.actionType == EActionType::BAD_MORALE;
			}));
	}

	void addHoldFastFloor(CStack * stack)
	{
		battle()->addOrUpdateUnitBonus(stack, newHorizonsDiscipline::holdFastMoraleFloorBonus(), true);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
}

TEST_F(NewHorizonsHoldFastTest, AcceptedDefendProtectsTheNextRealBadMoraleGate)
{
	startGame();
	selectHoldFast(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);

	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	ASSERT_EQ(holdFastFloorCount(target), 1u);
	const auto floor = target->getAllBonuses(CSelector(isHoldFastFloor))->front();
	EXPECT_EQ(floor->duration, BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION);
	EXPECT_EQ(floor->type, BonusType::MINIMUM_MORALE);
	EXPECT_EQ(floor->val, 0);
	EXPECT_EQ(floor->valType, BonusValueType::INDEPENDENT_MAX);
	addNegativeMorale(target);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);

	const auto badMoraleBefore = badMoraleActions(target);
	advanceUntilActive(target);
	EXPECT_EQ(badMoraleActions(target), badMoraleBefore)
		<< "The live Morale gate must read Hold Fast before the next-activation expiry is applied";
	EXPECT_EQ(holdFastFloorCount(target), 0u);
	EXPECT_LT(battle()->battleGetMorale(target), 0)
		<< "The accepted activation expires Hold Fast after its Morale gate";
}

TEST_F(NewHorizonsHoldFastTest, HoldTheLineProtectsOnlyItsCapturedRecipientAtTheRealMoraleGate)
{
	startGame();
	selectHoldFast(attackerSideHero);
	selectBasicCommand(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);

	ASSERT_TRUE(issueOrder(HeroCommand::HOLD_THE_LINE));
	ASSERT_EQ(holdFastFloorCount(target), 1u);
	auto * lateArrival = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	ASSERT_NE(lateArrival, nullptr);
	EXPECT_EQ(holdFastFloorCount(lateArrival), 0u)
		<< "Hold Fast follows the accepted Order's captured anchors, not later arrivals";

	addNegativeMorale(target);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	BattleHex oneHexMove = BattleHex::INVALID;
	for(const auto destination : battle()->battleGetAvailableHexes(target, false))
	{
		if(destination != target->getPosition()
			&& BattleHex::getDistance(destination, target->getPosition()) == 1)
		{
			oneHexMove = destination;
			break;
		}
	}
	ASSERT_TRUE(oneHexMove.isAvailable());
	ASSERT_TRUE(act(target, BattleAction::makeMove(target, oneHexMove)));
	EXPECT_EQ(target->getPosition(), oneHexMove);
	const auto orderAfterMoving = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(orderAfterMoving);
	EXPECT_TRUE(orderAfterMoving->containsHoldBroken(target->unitId()));
	EXPECT_EQ(holdFastFloorCount(target), 1u)
		<< "Breaking the Hold the Line anchor does not shorten the granted activation lifetime";
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	const auto badMoraleBefore = badMoraleActions(target);
	advanceUntilActive(target);
	EXPECT_EQ(badMoraleActions(target), badMoraleBefore);
	EXPECT_EQ(holdFastFloorCount(target), 0u);
	EXPECT_LT(battle()->battleGetMorale(target), 0);
}

TEST_F(NewHorizonsHoldFastTest, HeroAndCreatureSpellContinuationsAndRoundBoundaryPreserveFloor)
{
	startGame();
	selectHoldFast(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	ASSERT_EQ(holdFastFloorCount(target), 1u);

	activate(target, BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(holdFastFloorCount(target), 1u);
	activate(target, BattleUnitTurnReason::UNIT_SPELLCAST);
	EXPECT_EQ(holdFastFloorCount(target), 1u);
	activate(target, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(holdFastFloorCount(target), 1u)
		<< "An ordinary Hero Command transition is still a continuation, not Second Wind";
	advanceRound();
	EXPECT_EQ(holdFastFloorCount(target), 1u)
		<< "The activation lifetime is not a round countdown";
}

TEST_F(NewHorizonsHoldFastTest, QueueAndMoraleStartsExpireFloorButTimeStopQueueDoesNot)
{
	startGame();
	startBattle();
	removeStartingUnits();
	auto * queued = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
	auto * morale = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * stopped = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 5), 10);
	ASSERT_NE(queued, nullptr);
	ASSERT_NE(morale, nullptr);
	ASSERT_NE(stopped, nullptr);
	beginCombat();

	addHoldFastFloor(queued);
	activate(queued, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(holdFastFloorCount(queued), 0u);

	addHoldFastFloor(morale);
	activate(morale, BattleUnitTurnReason::MORALE);
	EXPECT_EQ(holdFastFloorCount(morale), 0u);

	addHoldFastFloor(stopped);
	auto timeStop = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	battle()->addOrUpdateUnitBonus(stopped, *timeStop, true);
	ASSERT_TRUE(stopped->isTimeStopped());
	activate(stopped, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(holdFastFloorCount(stopped), 1u)
		<< "A synthetic queue slot while stopped is not a genuine Creature Activation";

	stopped->removeBonusesRecursive(Selector::type()(BonusType::TIME_STOP));
	activate(stopped, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(holdFastFloorCount(stopped), 0u);
}

TEST_F(NewHorizonsHoldFastTest, AcceptedSecondWindIsAGenuineActivationAndExpiresTheFloor)
{
	startGame();
	selectHoldFast(attackerSideHero);
	selectBasicCommand(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	auto * commandWindow = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(commandWindow, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	ASSERT_TRUE(target->defended());
	ASSERT_FALSE(target->moved())
		<< "Defend completes a normal activation without setting the legacy moved flag";
	ASSERT_EQ(holdFastFloorCount(target), 1u);
	addNegativeMorale(target);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);

	advanceUntilActive(commandWindow);
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
			target->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), target);
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(holdFastFloorCount(target), 0u)
		<< "The explicit Second Wind Hero Command begins a Creature Activation";
}

TEST_F(NewHorizonsHoldFastTest, HypnotizedDefenderUsesItsControllerForDefendAndSecondWind)
{
	startGame();
	selectHoldFast(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(12, 5), 10);
	auto * commandWindow = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(commandWindow, nullptr);
	ASSERT_NE(enemy, nullptr);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(target->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(target), PlayerColor(0));
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	ASSERT_EQ(holdFastFloorCount(target), 1u);
	addNegativeMorale(target);
	advanceUntilActive(commandWindow);

	auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::SECOND_WIND, {target->unitId()});
	ASSERT_TRUE(prepared);
	prepared->secondWindActive = true;
	HoldFastTestEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	branch->setHeroOrderState(BattleSide::ATTACKER, prepared);
	ASSERT_TRUE(branch->battleBeginsActivation(branch->battleGetUnitByID(target->unitId()),
		BattleUnitTurnReason::HERO_COMMAND));
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(holdFastFloorCount(branch->getForUpdate(target->unitId()).get()), 0u);
	EXPECT_EQ(holdFastFloorCount(parent->getForUpdate(target->unitId()).get()), 1u);
	EXPECT_EQ(holdFastFloorCount(target), 1u);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
			target->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), target);
	EXPECT_EQ(holdFastFloorCount(target), 0u);
	const auto activeOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(activeOrder);
	ASSERT_TRUE(activeOrder->secondWindActive);

	BattleHex destination = BattleHex::INVALID;
	for(const auto hex : battle()->battleGetAvailableHexes(target, false))
	{
		if(hex != target->getPosition() && BattleHex::getDistance(hex, target->getPosition()) == 1)
		{
			destination = hex;
			break;
		}
	}
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(act(target, BattleAction::makeMove(target, destination)));
	const auto finishedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	EXPECT_TRUE(!finishedOrder || !finishedOrder->secondWindActive);
}

TEST_F(NewHorizonsHoldFastTest, DurationRoundTripsAndHypotheticalExpiryIsBranchLocal)
{
	startGame();
	selectHoldFast(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	const auto bonuses = target->getAllBonuses(CSelector(isHoldFastFloor));
	ASSERT_EQ(bonuses->size(), 1u);
	Bonus source = *bonuses->front();

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & source;
	current.iser.cb = gameState().get();
	Bonus restored;
	current.iser & restored;
	EXPECT_TRUE(newHorizonsDiscipline::isHoldFastMoraleFloorBonus(&restored));

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_RESERVE;
	EXPECT_THROW(older.oser & source, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty())
		<< "A down-save cannot silently discard the new duration bit";

	HoldFastTestEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_EQ(holdFastFloorCount(parent->getForUpdate(target->unitId()).get()), 1u);
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(holdFastFloorCount(branch->getForUpdate(target->unitId()).get()), 0u);
	EXPECT_EQ(holdFastFloorCount(parent->getForUpdate(target->unitId()).get()), 1u);
	EXPECT_EQ(holdFastFloorCount(sibling->getForUpdate(target->unitId()).get()), 1u);
	EXPECT_EQ(holdFastFloorCount(target), 1u);
}
