/*
 * NewHorizonsBerserkAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <cstddef>

namespace
{
class BerserkEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BerserkEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BerserkCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	explicit BerserkCallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

void addBerserkBonus(CStack * unit)
{
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 1,
		BonusSourceID(SpellID(SpellID::BERSERK))));
}

template<typename T>
std::vector<std::byte> serializedState(T & object)
{
	CMemorySerializer serializer;
	serializer.oser & object;
	return serializer.extractBuffer();
}
}

class NewHorizonsBerserkAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		// Keep this fixture focused on the spell's projected activation value.
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		for(auto & commandEntry : commandRules["commands"].Struct())
			for(auto & effectEntry : commandEntry.second["effects"].Struct())
			{
				auto & formula = effectEntry.second;
				formula["base"].Float() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void removeInitialStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void startEmptyBattle()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		removeInitialStacks();
	}

	void prepareBerserkCaster()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(SpellID::BERSERK);
		const auto chaosMagic = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		removeInitialStacks();
	}
};

TEST_F(NewHorizonsBerserkAITest, V3ShooterForecastUsesForcedMeleeAttack)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 5);
	auto * target = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(9, 5), 5);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(target, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_FALSE(forced.empty());
	ASSERT_EQ(forced.front().type, EActionType::WALK_AND_ATTACK);
	ASSERT_NE(forced.front().target, nullptr);
	EXPECT_EQ(forced.front().target->unitId(), target->unitId());

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_FALSE(candidates.possibleAttacks.empty());
	for(const auto & attack : candidates.possibleAttacks)
	{
		EXPECT_FALSE(attack.attack.shooting);
		EXPECT_EQ(attack.attack.defender->unitId(), target->unitId());
		EXPECT_EQ(attack.from, forced.front().position);
	}
}

TEST_F(NewHorizonsBerserkAITest, ForcedWalkKeepsItsDestinationAndNoActionDoesNotReposition)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(1, 5), 5);
	auto * distantTarget = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(16, 5), 5);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(distantTarget, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_EQ(forced.size(), 1u);
	ASSERT_EQ(forced.front().type, EActionType::WALK);

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	BattleExchangeEvaluator evaluator(forecast, environment, 1.0f, 1);
	const auto movement = evaluator.findMoveTowardsUnreachable(
		projectedBerserker.get(), candidates, damage, forecast);
	ASSERT_EQ(movement.positions.size(), 1u);
	EXPECT_TRUE(movement.positions.contains(forced.front().position));
	EXPECT_EQ(candidates.unreachableEnemies.size(), 1u);
	EXPECT_EQ(candidates.unreachableEnemies.front()->unitId(), distantTarget->unitId());

	auto stationaryForecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -20, BonusSourceID());
	stationaryForecast->addUnitBonus(berserker->unitId(), {immobilized});
	const auto stationary = stationaryForecast->getForUpdate(berserker->unitId());
	const auto stationaryForced = stationaryForecast->getBerserkForcedActions(stationary.get());
	EXPECT_TRUE(stationaryForced.empty());
	DamageCache stationaryDamage;
	PotentialTargets stationaryCandidates(stationary.get(), stationaryDamage, stationaryForecast);
	ASSERT_EQ(stationaryCandidates.forcedBerserkActions.size(), 1u);
	EXPECT_EQ(stationaryCandidates.forcedBerserkActions.front().type, EActionType::NO_ACTION);
	BattleExchangeEvaluator stationaryEvaluator(stationaryForecast, environment, 1.0f, 1);
	const auto stationaryMove = stationaryEvaluator.findMoveTowardsUnreachable(
		stationary.get(), stationaryCandidates, stationaryDamage, stationaryForecast);
	EXPECT_TRUE(stationaryMove.positions.empty());
	EXPECT_TRUE(stationaryCandidates.unreachableEnemies.empty());
}

TEST_F(NewHorizonsBerserkAITest, ExpectedBerserkValueAveragesEveryTiedAttackCandidate)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 20);
	auto * hostileVictim = addStack(BattleSide::ATTACKER,
		creatureByName("core:archangel"), BattleHex(7, 5), 1);
	auto * friendlyVictim = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(9, 5), 30);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(hostileVictim, nullptr);
	ASSERT_NE(friendlyVictim, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_EQ(forced.size(), 2u);
	for(const auto & action : forced)
		EXPECT_EQ(action.type, EActionType::WALK_AND_ATTACK);

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_EQ(candidates.possibleAttacks.size(), forced.size());
	float summedActionValue = 0.0f;
	for(const auto & action : forced)
	{
		const auto attack = std::ranges::find_if(candidates.possibleAttacks, [&](const AttackPossibility & candidate)
		{
			return candidate.attack.defender->unitId() == action.target->unitId()
				&& candidate.from == action.position;
		});
		ASSERT_NE(attack, candidates.possibleAttacks.end());
		summedActionValue += attack->attackValue();
	}
	EXPECT_NEAR(candidates.expectedBerserkActionValue(),
		summedActionValue / static_cast<float>(forced.size()), 0.001f);
	EXPECT_EQ(candidates.bestAction().attack.defender->unitId(), forced.front().target->unitId());
}

TEST_F(NewHorizonsBerserkAITest, FullEvaluatorChoosesBerserkWhenItPreventsAShotAndKeepsScoringReadOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepareBerserkCaster());
	auto * active = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(1, 5), 1);
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 20);
	auto * enemyAlly = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(9, 5), 30);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(enemyAlly, nullptr);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto berserkerHealthBefore = berserker->getAvailableHealth();
	const auto allyHealthBefore = enemyAlly->getAvailableHealth();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());
	const auto * berserkSpell = SpellID(SpellID::BERSERK).toSpell();
	ASSERT_TRUE(berserkSpell->canBeCast(
		battle(), spells::Mode::HERO, attackerSideHero, false));

	auto callback = std::make_shared<BerserkCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	const bool choseAction = evaluator.attemptCastingSpell(active);
	ASSERT_TRUE(choseAction);
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID::BERSERK);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, berserker->unitId());

	// Candidate scoring reads only detached battle state and deterministic
	// expected tied outcomes. It does not commit the spell or consume either RNG.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(berserker->getAvailableHealth(), berserkerHealthBefore);
	EXPECT_EQ(enemyAlly->getAvailableHealth(), allyHealthBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	EXPECT_FALSE(berserker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));

	const auto spellCost = battle()->battleGetSpellCost(berserkSpell, attackerSideHero);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - spellCost);
	EXPECT_TRUE(berserker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_EQ(enemyAlly->getAvailableHealth(), allyHealthBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
}
