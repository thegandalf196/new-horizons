/*
 * NewHorizonsFearlessTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto disciplineSkillId = "new-horizons:discipline";
constexpr auto fearlessPerkId = "new-horizons:discipline.fearless";
constexpr auto inspirationalLeaderPerkId = "new-horizons:discipline.inspirationalLeader";
constexpr auto rallyPerkId = "new-horizons:discipline.rally";
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorPerkId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortunePerkId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFatePerkId = "new-horizons:luck.twistOfFate";

bool setPerkActive(JsonNode & rules, std::string_view skillId, std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class FearlessTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit FearlessTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsFearlessTest : public BattleTestFixture
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
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, disciplineSkillId, fearlessPerkId)
			|| !setPerkActive(perkRules, disciplineSkillId, rallyPerkId)
			|| !setPerkActive(perkRules, disciplineSkillId, inspirationalLeaderPerkId)
			|| !setPerkActive(perkRules, luckSkillId, fortuneFavorPerkId)
			|| !setPerkActive(perkRules, luckSkillId, chainOfFortunePerkId)
			|| !setPerkActive(perkRules, luckSkillId, twistOfFatePerkId))
			throw std::runtime_error("Missing Fearless, Rally, or Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		JsonNode badMoraleChances;
		for(int index = 0; index < 10; ++index)
			badMoraleChances.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, std::move(badMoraleChances));
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view skillId, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & queriedSkillId)
		{
			return hero->getPerkSkillRank(queriedSkillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == skillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<std::size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(skillId), std::string(perkId)));
			return;
		}

		FAIL() << perkId << " never appeared in a legal perk offer";
	}

	void selectFearless(CGHeroInstance * hero, bool chooseRally = false)
	{
		const int decoded = SecondarySkill::decode(disciplineSkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill discipline(decoded);
		hero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId,
			chooseRally ? rallyPerkId : inspirationalLeaderPerkId);
		hero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId, fearlessPerkId);
		ASSERT_TRUE(hero->hasActivePerk(disciplineSkillId, fearlessPerkId));
	}

	void selectTwistOfFate(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill luck(decoded);

		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, fortuneFavorPerkId);
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, chainOfFortunePerkId);
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, twistOfFatePerkId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFatePerkId));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void addFearful(CStack * stack, BonusSource source, int chance, BonusSourceID sourceId,
		BonusValueType valueType = BonusValueType::ADDITIVE_VALUE)
	{
		auto fear = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FEARFUL, source, chance, sourceId);
		fear->valType = valueType;
		stack->addNewBonus(std::move(fear));
	}

	void setMorale(CStack * stack, int value)
	{
		const int current = stack->getBonusesOfType(BonusType::MORALE)->totalValue();
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::OTHER, value - current, BonusSourceID()));
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}

	void advanceUntilActivationRecorded(const CStack * expected)
	{
		for(int attempt = 0; attempt < 24; ++attempt)
		{
			if(activationCount(expected->unitId(), BattleUnitTurnReason::TURN_QUEUE) > 0
				|| activationCount(expected->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION) > 0)
				return;

			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}

		FAIL() << "The expected stack did not begin an activation";
	}

	std::size_t activationCount(uint32_t unitId, BattleUnitTurnReason reason) const
	{
		return static_cast<std::size_t>(std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
			[unitId, reason](const auto & activation)
			{
				return activation.stack == unitId && activation.reason == reason;
			}));
	}

	BonusSourceID creatureFearSource() const
	{
		return BonusSourceID(creatureByName("core:boneDragon"));
	}

	BonusSourceID spellFearSource() const
	{
		return BonusSourceID(SpellID(SpellID::CURSE));
	}

	std::optional<int> seedForFearfulRoll(ObjectInstanceID armyId, int chance, bool expectedResult) const
	{
		for(int seed = 1; seed < 100000; ++seed)
		{
			GameRandomizer expected(*gameState());
			expected.setSeed(seed);
			if(expected.rollCombatAbility(armyId, chance) == expectedResult)
				return seed;
		}
		return std::nullopt;
	}
};
}

TEST_F(NewHorizonsFearlessTest, LegalAdvancedFearlessSuppressesDeterministicFearAtTheRealTurnStart)
{
	startGame();
	selectFearless(attackerSideHero);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(disciplineSkillId, fearlessPerkId));
	startBattle();
	removeDeployedUnits();

	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	addFearful(target, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	setMorale(target, 0);
	ASSERT_EQ(target->valOfBonuses(BonusType::FEARFUL), 100);
	EXPECT_EQ(battle()->battleGetFearChance(target), 0);

	beginCombat();
	advanceUntilActivationRecorded(target);

	EXPECT_FALSE(target->fear);
	EXPECT_EQ(activationCount(target->unitId(), BattleUnitTurnReason::TURN_QUEUE), 1u)
		<< "The Fearless stack receives its ordinary real queue activation";
	EXPECT_EQ(activationCount(target->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 0u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Discipline: Fearless protects") != std::string::npos;
	})) << "The real turn-start suppression is visible in battle feedback";
}

TEST_F(NewHorizonsFearlessTest, WithoutFearlessTheSameDeterministicFearCausesTheRealAutomaticActivation)
{
	startGame();
	startBattle();
	removeDeployedUnits();

	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	addFearful(target, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	setMorale(target, 0);
	EXPECT_EQ(battle()->battleGetFearChance(target), 100);

	beginCombat();
	advanceUntilActivationRecorded(target);

	EXPECT_TRUE(target->fear);
	EXPECT_EQ(activationCount(target->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 1u);
	EXPECT_EQ(activationCount(target->unitId(), BattleUnitTurnReason::TURN_QUEUE), 0u)
		<< "A failed Fearless gate leaves the ordinary Fearful no-action activation intact";
}

TEST_F(NewHorizonsFearlessTest, MixedFearFiltersOnlyPositiveCreatureAbilityAndPreservesOtherStacking)
{
	startGame();
	selectFearless(attackerSideHero);
	startBattle();
	removeDeployedUnits();

	auto * mixed = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 10);
	auto * negativeCap = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	auto * zeroCap = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(10, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(mixed, nullptr);
	ASSERT_NE(negativeCap, nullptr);
	ASSERT_NE(zeroCap, nullptr);
	ASSERT_NE(enemy, nullptr);

	addFearful(mixed, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	addFearful(mixed, BonusSource::SPELL_EFFECT, 60, spellFearSource());
	addFearful(mixed, BonusSource::OTHER, 40, BonusSourceID());
	EXPECT_EQ(mixed->valOfBonuses(BonusType::FEARFUL), 200);
	EXPECT_EQ(battle()->battleGetFearChance(mixed), 100)
		<< "After filtering the native fear contribution, spell and unclassified values still stack";

	addFearful(negativeCap, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	addFearful(negativeCap, BonusSource::CREATURE_ABILITY, -5, creatureFearSource(), BonusValueType::INDEPENDENT_MIN);
	EXPECT_EQ(battle()->battleGetFearChance(negativeCap), -5)
		<< "Filtering positive creature fear leaves its negative independent cap in the bonus list";

	addFearful(zeroCap, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	addFearful(zeroCap, BonusSource::CREATURE_ABILITY, 0, creatureFearSource(), BonusValueType::INDEPENDENT_MIN);
	EXPECT_EQ(battle()->battleGetFearChance(zeroCap), 0)
		<< "A zero independent cap is retained while the positive creature contribution is filtered";

	setMorale(mixed, 0);
	beginCombat();
	advanceUntilActivationRecorded(mixed);
	EXPECT_TRUE(mixed->fear)
		<< "The spell/unclassified remainder still reaches the real turn-start Fearful roll";
	EXPECT_EQ(activationCount(mixed->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 1u);
}

TEST_F(NewHorizonsFearlessTest, SuppressedFearLeavesRallyAndTwistAvailableAndDoesNotEraseNegativeMorale)
{
	startGame();
	selectFearless(attackerSideHero, true);
	selectTwistOfFate(attackerSideHero);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(disciplineSkillId, rallyPerkId));
	startBattle();
	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	removeDeployedUnits();

	auto * fearful = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(fearful, nullptr);
	ASSERT_NE(enemy, nullptr);
	addFearful(fearful, BonusSource::CREATURE_ABILITY, 50, creatureFearSource());
	setMorale(fearful, 0);
	const auto fearSeed = seedForFearfulRoll(
		battle()->getSideArmy(BattleSide::DEFENDER)->id, 50, true);
	ASSERT_TRUE(fearSeed.has_value()) << "Could not find a deterministic adverse stochastic FEARFUL roll";
	gameHandler->randomizer->setSeed(*fearSeed);
	beginCombat();
	advanceUntilActivationRecorded(fearful);

	EXPECT_FALSE(fearful->fear);
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available())
		<< "Suppressing nonmagical fear does not spend Rally";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available())
		<< "Suppressing stochastic nonmagical fear does not spend Twist of Fate";
	ASSERT_TRUE(act(fearful, BattleAction::makeDefend(fearful)));

	auto * negativeMorale = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	ASSERT_NE(negativeMorale, nullptr);
	setMorale(negativeMorale, -10);
	ASSERT_LT(battle()->battleGetMorale(negativeMorale), 0);
	advanceUntilActivationRecorded(negativeMorale);

	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).used)
		<< "Fearless leaves an unrelated negative-Morale trigger for Rally to cancel";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available())
		<< "Rally cancels the realized negative Morale result before Twist can consume its allowance";
	EXPECT_FALSE(negativeMorale->fear);
	EXPECT_EQ(activationCount(negativeMorale->unitId(), BattleUnitTurnReason::TURN_QUEUE), 1u);
	EXPECT_EQ(activationCount(negativeMorale->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 0u);
}

TEST_F(NewHorizonsFearlessTest, CurrentControllerAndDetachedControlBranchDetermineFearImmunity)
{
	startGame();
	selectFearless(defenderSideHero);
	ASSERT_TRUE(defenderSideHero->hasActivePerk(disciplineSkillId, fearlessPerkId));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(disciplineSkillId, fearlessPerkId));
	startBattle();
	removeDeployedUnits();

	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	addFearful(target, BonusSource::CREATURE_ABILITY, 100, creatureFearSource());
	EXPECT_EQ(battle()->battleGetFearChance(target), 100)
		<< "The original controller has no Fearless perk";

	const Bonus hypnosis(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(std::make_shared<Bonus>(hypnosis));
	ASSERT_EQ(battle()->battleGetOwnerHero(target), defenderSideHero);
	EXPECT_EQ(battle()->battleGetFearChance(target), 0)
		<< "A hypnotized stack uses its current controller's active perk";

	FearlessTestEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	const auto parentTarget = parent->getForUpdate(target->unitId());
	ASSERT_EQ(parent->battleGetOwnerHero(parentTarget.get()), defenderSideHero)
		<< "The projection's player context can see the hypnotized stack's current controller";
	ASSERT_EQ(parent->battleGetFearChance(parentTarget.get()), 0);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto branchTarget = branch->getForUpdate(target->unitId());
	const auto siblingTarget = sibling->getForUpdate(target->unitId());
	EXPECT_EQ(branch->battleGetFearChance(branchTarget.get()), 0);
	EXPECT_EQ(sibling->battleGetFearChance(siblingTarget.get()), 0);

	branch->removeUnitBonus(target->unitId(), {hypnosis});
	const auto changedBranchTarget = branch->getForUpdate(target->unitId());
	EXPECT_EQ(branch->battleGetOwner(changedBranchTarget.get()), PlayerColor(0));
	EXPECT_EQ(branch->battleGetOwnerHero(changedBranchTarget.get()), nullptr)
		<< "The opposing hero remains hidden under the projection's player context";
	EXPECT_EQ(branch->battleGetFearChance(changedBranchTarget.get()), 100)
		<< "Changing control inside one detached candidate branch removes its Fearless protection";
	EXPECT_EQ(parent->battleGetFearChance(parentTarget.get()), 0);
	EXPECT_EQ(sibling->battleGetFearChance(siblingTarget.get()), 0);
	EXPECT_EQ(battle()->battleGetFearChance(target), 0)
		<< "The candidate control change does not mutate its sibling or the live battle";
}
