/*
 * NewHorizonsMoraleRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
class MoraleRulesEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MoraleRulesEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMoraleRulesTest : public BattleTestFixture
{
protected:
	bool useSavedMoraleRules = true;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate native New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			useSavedMoraleRules ? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : JsonNode());
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!changes.changedStacks.empty())
			gameHandler->sendAndApply(changes);
	}

	static std::shared_ptr<Bonus> addBonus(CStack * stack, BonusType type, int value = 1)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::OTHER, value, BonusSourceID());
		stack->addNewBonus(result);
		return result;
	}

	bool referenceRoll(int seed, int chance, int diceSize) const
	{
		GameRandomizer source(*gameState());
		source.setSeed(seed);
		RandomGeneratorWithBias expected(source.getDefault().nextInt());
		return expected.roll(chance, diceSize,
			gameState()->getSettings().getInteger(EGameSettings::COMBAT_MORALE_BIAS));
	}
};
}

TEST_F(NewHorizonsMoraleRulesTest, SavedCurveHasTenPointBoundsAndAuthoredSignedChance)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	const auto limits = newHorizonsMagic::moraleLimits(rules);
	ASSERT_TRUE(limits);
	EXPECT_EQ(limits->first, -10);
	EXPECT_EQ(limits->second, 10);
	EXPECT_EQ(newHorizonsMagic::moraleChance(rules, 10), 30);
	EXPECT_EQ(newHorizonsMagic::moraleChance(rules, -10), 30);
	EXPECT_EQ(newHorizonsMagic::moraleChance(rules, 1), 3);
	EXPECT_EQ(newHorizonsMagic::moraleChance(rules, -1), 3);
	EXPECT_EQ(newHorizonsMagic::moraleChance(rules, 0), 0);
	EXPECT_EQ(newHorizonsMagic::moraleDiceSize(rules), 100);

	JsonNode absentRules;
	EXPECT_FALSE(newHorizonsMagic::moraleLimits(absentRules));
	EXPECT_FALSE(newHorizonsMagic::moraleChance(absentRules, 10));
	EXPECT_FALSE(newHorizonsMagic::moraleDiceSize(absentRules));

	JsonNode malformed = rules;
	malformed["morale"]["maximum"] = JsonNode(9);
	EXPECT_FALSE(newHorizonsMagic::moraleLimits(malformed));
	EXPECT_FALSE(newHorizonsMagic::moraleChance(malformed, 10));
}

TEST_F(NewHorizonsMoraleRulesTest, SavedRulesBoundHeroArmyBattleAndDetachedMorale)
{
	startGame();
	ASSERT_EQ(attackerSideHero->getMagicRules()["morale"]["minimum"].Integer(), -10);
	ASSERT_EQ(attackerSideHero->getMagicRules()["morale"]["maximum"].Integer(), 10);
	EXPECT_EQ(attackerSideHero->moraleValWithBonus(25), 10);
	EXPECT_EQ(attackerSideHero->moraleValWithBonus(-25), -10);

	auto * armyStack = attackerSideHero->getStackPtr(SlotID(0));
	ASSERT_NE(armyStack, nullptr);
	EXPECT_EQ(armyStack->moraleValWithBonus(25), 10);
	EXPECT_EQ(armyStack->moraleValWithBonus(-25), -10);

	startBattle();
	const auto battleLimits = newHorizonsMagic::moraleLimits(battle()->getMagicRules());
	ASSERT_TRUE(battleLimits);
	EXPECT_EQ(battleLimits->first, -10);
	EXPECT_EQ(battleLimits->second, 10);
	clearStartingUnits();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	ASSERT_NE(target, nullptr);

	auto positive = addBonus(target, BonusType::MORALE, 25);
	EXPECT_EQ(target->moraleVal(), 10);
	target->removeBonus(positive);
	auto negative = addBonus(target, BonusType::MORALE, -25);
	EXPECT_EQ(target->moraleVal(), -10);
	auto floor = addBonus(target, BonusType::MINIMUM_MORALE, -4);
	EXPECT_EQ(target->moraleVal(), -4);
	target->removeBonus(floor);
	auto maximum = addBonus(target, BonusType::MAX_MORALE);
	EXPECT_EQ(target->moraleVal(), 10);
	target->removeBonus(maximum);
	auto immunity = addBonus(target, BonusType::NO_MORALE);
	EXPECT_EQ(target->moraleVal(), 0);
	target->removeBonus(immunity);
	target->removeBonus(negative);

	MoraleRulesEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto detachedLimits = newHorizonsMagic::moraleLimits(branch->getMagicRules());
	ASSERT_TRUE(detachedLimits);
	EXPECT_EQ(detachedLimits->first, -10);
	EXPECT_EQ(detachedLimits->second, 10);
	const int originalMorale = battle()->battleGetMorale(target);
	ASSERT_EQ(originalMorale, 1) << "The same-faction army supplies the baseline Morale bonus";
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(target->unitId()).get()), originalMorale);
	auto projected = branch->getForUpdate(target->unitId());
	auto branchPenalty = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::OTHER, -25, BonusSourceID());
	branch->addUnitBonus(target->unitId(), {*branchPenalty});
	EXPECT_EQ(branch->battleGetMorale(projected.get()), -10);
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(target->unitId()).get()), originalMorale);
	EXPECT_EQ(sibling->battleGetMorale(sibling->getForUpdate(target->unitId()).get()), originalMorale);
	EXPECT_EQ(battle()->battleGetMorale(target), originalMorale);
}

TEST_F(NewHorizonsMoraleRulesTest, SavedRollUsesItsSignedCurveAndHundredSidedDice)
{
	startGame();
	const auto rules = gameState()->getMagicRules();
	ASSERT_EQ(newHorizonsMagic::moraleChance(rules, 10), 30);
	ASSERT_EQ(newHorizonsMagic::moraleDiceSize(rules), 100);

	const auto & settings = gameState()->getSettings();
	const auto legacyGood = settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE);
	const auto legacyBad = settings.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	ASSERT_FALSE(legacyGood.empty());
	ASSERT_FALSE(legacyBad.empty());
	const int legacyDice = settings.getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	const int savedGood = *newHorizonsMagic::moraleChance(rules, 1);
	const int savedBad = *newHorizonsMagic::moraleChance(rules, -1);

	const auto runDistinctSignedRoll = [this, legacyDice](bool good, int savedChance, int legacyChance, ObjectInstanceID actor)
	{
		int seed = -1;
		bool expectedSaved = false;
		bool expectedLegacy = false;
		for(int candidate = 0; candidate < 4096; ++candidate)
		{
			const auto saved = referenceRoll(candidate, savedChance, 100);
			const auto legacy = referenceRoll(candidate, legacyChance, legacyDice);
			if(saved != legacy)
			{
				seed = candidate;
				expectedSaved = saved;
				expectedLegacy = legacy;
				break;
			}
		}
		ASSERT_GE(seed, 0) << "Test needs a deterministic seed distinguishing saved and original curves";
		ASSERT_NE(expectedSaved, expectedLegacy);

		GameRandomizer randomizer(*gameState());
		randomizer.setSeed(seed);
		const bool actual = good ? randomizer.rollGoodMorale(actor, 1) : randomizer.rollBadMorale(actor, 1);
		EXPECT_EQ(actual, expectedSaved);
		EXPECT_NE(actual, expectedLegacy);
		EXPECT_TRUE(randomizer.isBadMoraleRollStochastic(10));
	};

	runDistinctSignedRoll(true, savedGood, legacyGood.front(), ObjectInstanceID(777));
	runDistinctSignedRoll(false, savedBad, legacyBad.front(), ObjectInstanceID(778));
}

TEST_F(NewHorizonsMoraleRulesTest, AbsentSavedContextRetainsOriginalBoundsAndMoraleSettings)
{
	useSavedMoraleRules = false;
	startGame();
	const auto & rules = gameState()->getMagicRules();
	EXPECT_FALSE(newHorizonsMagic::moraleLimits(rules));
	EXPECT_FALSE(newHorizonsMagic::moraleChance(rules, 10));
	EXPECT_FALSE(newHorizonsMagic::moraleDiceSize(rules));

	const int maximumMorale = static_cast<int>(LIBRARY->engineSettings()->getVector(
		EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size());
	const int minimumMorale = -static_cast<int>(LIBRARY->engineSettings()->getVector(
		EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
	EXPECT_EQ(attackerSideHero->moraleValWithBonus(100), maximumMorale);
	EXPECT_EQ(attackerSideHero->moraleValWithBonus(-100), minimumMorale);

	GameRandomizer randomizer(*gameState());
	const auto & badChance = gameState()->getSettings().getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const int diceSize = gameState()->getSettings().getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	ASSERT_FALSE(badChance.empty());
	EXPECT_EQ(randomizer.isBadMoraleRollStochastic(10), badChance.back() > 0 && badChance.back() < diceSize);

	startBattle();
	EXPECT_FALSE(newHorizonsMagic::moraleLimits(battle()->getMagicRules()));
	const auto units = battle()->battleGetAllUnits(false);
	ASSERT_FALSE(units.empty());
	EXPECT_EQ(units.front()->moraleValWithBonus(100), maximumMorale);
	EXPECT_EQ(units.front()->moraleValWithBonus(-100), minimumMorale);
}
