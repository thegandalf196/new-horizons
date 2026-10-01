/*
 * NewHorizonsStandardBearerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto discipline = "new-horizons:discipline";
constexpr auto standardBearer = "new-horizons:discipline.standardBearer";
constexpr auto inspirationalLeader = "new-horizons:discipline.inspirationalLeader";

class StandardBearerEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit StandardBearerEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsStandardBearerTest : public BattleTestFixture
{
protected:
	int goodChance = 0;
	int badChance = 0;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(auto & perk : rules["skills"][discipline]["perks"].Vector())
		{
			if(perk["id"].String() == standardBearer)
			{
				perk["effect"]["status"].String() = "active";
				found = true;
			}
		}
		if(!found)
			throw std::runtime_error("Missing Standard Bearer registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
		for(const auto setting : {EGameSettings::COMBAT_GOOD_MORALE_CHANCE, EGameSettings::COMBAT_BAD_MORALE_CHANCE})
		{
			JsonNode chances;
			for(int index = 0; index < 10; ++index)
				chances.Vector().emplace_back(setting == EGameSettings::COMBAT_GOOD_MORALE_CHANCE ? goodChance : badChance);
			loaded->overrideGameSetting(setting, std::move(chances));
		}
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptPerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == discipline && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(discipline, perkId));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectStandardBearer(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(discipline);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, inspirationalLeader);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, standardBearer);
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}

	std::shared_ptr<Bonus> bonus(CStack * stack, BonusType type, int value)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
		stack->addNewBonus(result);
		return result;
	}

	void rawMorale(CStack * stack, int value)
	{
		bonus(stack, BonusType::MORALE, value - stack->getBonusesOfType(BonusType::MORALE)->totalValue());
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(stack), action);
	}
};
}

TEST_F(NewHorizonsStandardBearerTest, AcceptedSupporterMovementPreventsBadMoraleOnlyWhileAdjacent)
{
	badChance = 100;
	startGame();
	selectStandardBearer(attackerSideHero);
	startBattle();
	clearStartingUnits();
	auto * supporter = addStack(BattleSide::ATTACKER, creatureByName("core:sprite"), BattleHex(3, 5), 10);
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	bonus(supporter, BonusType::NO_MORALE, 1);
	rawMorale(recipient, -1);
	ASSERT_EQ(battle()->battleGetMorale(recipient), -1);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), supporter);
	ASSERT_TRUE(act(supporter, BattleAction::makeMove(supporter, BattleHex(6, 5))));
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	ASSERT_EQ(battle()->battleActiveUnit(), recipient)
		<< "Adjacent +1 prevents the otherwise certain negative Morale trigger";
	endRound();
	ASSERT_EQ(battle()->battleActiveUnit(), supporter);
	ASSERT_TRUE(act(supporter, BattleAction::makeMove(supporter, BattleHex(3, 5))));
	EXPECT_EQ(battle()->battleGetMorale(recipient), -1);
	EXPECT_NE(battle()->battleActiveUnit(), recipient);
	EXPECT_TRUE(std::ranges::any_of(server.startedActions, [recipient](const StartAction & action)
	{
		return action.ba.stackNumber == recipient->unitId() && action.ba.actionType == EActionType::BAD_MORALE;
	}));
}

TEST_F(NewHorizonsStandardBearerTest, AdjacentMoraleIsUsedByTheRealPositiveMoraleGate)
{
	goodChance = 100;
	startGame();
	selectStandardBearer(attackerSideHero);
	startBattle();
	clearStartingUnits();
	auto * supporter = addStack(BattleSide::ATTACKER, creatureByName("core:sprite"), BattleHex(6, 5), 10);
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	bonus(supporter, BonusType::NO_MORALE, 1);
	rawMorale(recipient, 0);
	beginCombat();
	ASSERT_TRUE(act(supporter, BattleAction::makeDefend(supporter)));
	ASSERT_EQ(battle()->battleActiveUnit(), recipient);
	BattleHex destination = BattleHex::INVALID;
	for(const auto hex : battle()->battleGetAvailableHexes(recipient, false))
	{
		if(hex != recipient->getPosition() && BattleHex::getDistance(hex, supporter->getPosition()) == 1)
		{
			destination = hex;
			break;
		}
	}
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(act(recipient, BattleAction::makeMove(recipient, destination)));
	EXPECT_EQ(recipient->moraleVal(), 0);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 1);
	EXPECT_TRUE(recipient->hadMorale);
	EXPECT_TRUE(std::ranges::any_of(server.stackActivations, [recipient](const BattleSetActiveStack & activation)
	{
		return activation.stack == recipient->unitId() && activation.reason == BattleUnitTurnReason::MORALE;
	}));
}

TEST_F(NewHorizonsStandardBearerTest, NonstackingCurrentControlAndRawBeforeCapPreserveMoraleRules)
{
	startGame();
	selectStandardBearer(defenderSideHero);
	startBattle();
	clearStartingUnits();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	auto * supporter = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(6, 5), 10);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	rawMorale(recipient, 0);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	auto hypnotized = bonus(recipient, BonusType::HYPNOTIZED, 1);
	ASSERT_EQ(battle()->battleGetOwnerHero(recipient), defenderSideHero);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 1) << "Two neighbors grant only +1; an undead supporter qualifies";
	auto noMorale = bonus(recipient, BonusType::NO_MORALE, 1);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	recipient->removeBonus(noMorale);
	rawMorale(recipient, -20);
	const int minimumMorale = -static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
	const int maximumMorale = static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size());
	EXPECT_EQ(battle()->battleGetMorale(recipient), std::clamp(-19, minimumMorale, maximumMorale))
		<< "Add to raw -20 before clamping, not clamped minimum +1";
	auto minimum = bonus(recipient, BonusType::MINIMUM_MORALE, 0);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	recipient->removeBonus(minimum);
	auto maximum = bonus(recipient, BonusType::MAX_MORALE, 1);
	EXPECT_EQ(battle()->battleGetMorale(recipient), maximumMorale);
	recipient->removeBonus(maximum);
	rawMorale(recipient, 0);
	recipient->removeBonus(hypnotized);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	// The supporter's Morale immunity does not disqualify it; a removed stack does.
	BattleUnitsChanged changes;
	changes.battleID = BattleID(0);
	changes.changedStacks.emplace_back(second->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(changes);
	bonus(recipient, BonusType::HYPNOTIZED, 1);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 1);
	changes.changedStacks.clear();
	changes.changedStacks.emplace_back(supporter->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(changes);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
}

TEST_F(NewHorizonsStandardBearerTest, DetachedNeighborMovementRemovalAndControlAreBranchLocal)
{
	startGame();
	selectStandardBearer(attackerSideHero);
	startBattle();
	clearStartingUnits();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	auto * supporter = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(6, 5), 10);
	rawMorale(recipient, 0);
	StandardBearerEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = branch->getForUpdate(recipient->unitId());
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 1);
	branch->moveUnit(supporter->unitId(), BattleHex(3, 5));
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 0);
	branch->moveUnit(supporter->unitId(), BattleHex(6, 5));
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 1);
	Bonus hypnosis(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	branch->addUnitBonus(supporter->unitId(), {hypnosis});
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 0);
	branch->removeUnitBonus(supporter->unitId(), {hypnosis});
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 1);
	branch->removeUnit(supporter->unitId());
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 0);
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(recipient->unitId()).get()), 1);
	EXPECT_EQ(sibling->battleGetMorale(sibling->getForUpdate(recipient->unitId()).get()), 1);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 1);
}
