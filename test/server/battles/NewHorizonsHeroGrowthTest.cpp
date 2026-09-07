/*
 * NewHorizonsHeroGrowthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/BattleEvaluator.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/battle/Destination.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/spells/ProxyCaster.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/Quest.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class NewHorizonsHeroGrowthTest : public HeroCommandFixture
{
protected:
	void prepareScaledExpert(SpellID spell)
	{
		prepareCommands(true);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->mana = attackerSideHero->manaLimit();
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_SCHOOL_SKILL,
			BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		attackerSideHero->addSpellToSpellbook(spell);
	}

	bool growthEnabled = true;
	int extraChance = 100;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto rules = growthEnabled ? testHeroRules() : JsonNode();
		if(growthEnabled)
			for(auto & extra : rules["extraGrowth"].Vector())
				for(int rank = 1; rank <= 3; ++rank)
					extra["chances"].Vector()[rank].Integer() = extraChance;
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
};

class NewHorizonsCapabilityStateTest : public NewHorizonsHeroGrowthTest
{
protected:
	bool capabilitiesEnabled = true;
	JsonNode capabilityWorldRules() const
	{
		JsonNode result;
		result["schemaVersion"].Integer() = 1;
		result["rulesetVersion"].Integer() = 1;
		const auto primary = testHeroRules();
		for(const auto & [key, ignored] : primary["classProfiles"].Struct())
		{
			result["classProfiles"][key]["base"].Integer() = 2000;
			result["classProfiles"][key]["perLevel"].Integer() = 200;
		}
		for(int value : {0, 10, 20, 30})
			result["leadership"]["skillBonusPercent"].Vector().emplace_back(value);
		result["leadership"]["minimumMovementPercent"].Integer() = 50;
		for(int value : {1, 2, 3, 4})
			result["siege"]["ballistaDamageMultiplier"].Vector().emplace_back(value);
		return result;
	}
	void mapLoaded(CMap * map) override
	{
		NewHorizonsHeroGrowthTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			capabilitiesEnabled ? capabilityWorldRules() : JsonNode());
	}
};

TEST_F(NewHorizonsCapabilityStateTest, InitializationCapturesIndependentWorldAndResolvedClass)
{
	startGame();
	EXPECT_EQ(gameState()->getHeroCapabilityRules(), capabilityWorldRules());
	const auto & resolved = attackerSideHero->getCapabilityRules();
	EXPECT_EQ(resolved["profile"]["base"].Integer(), 2000);
	EXPECT_EQ(resolved["profile"]["perLevel"].Integer(), 200);
	EXPECT_TRUE(resolved["classProfiles"].isNull());
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadership(resolved, 4, 3, 0).capacity, 3380);
}

TEST_F(NewHorizonsCapabilityStateTest, CapabilityOnlyHeroDoesNotFabricatePrimaryGrowth)
{
	growthEnabled = false;
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthView());
	ASSERT_TRUE(attackerSideHero->getLeadershipCapacity());
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->capacity, 2000);
	EXPECT_TRUE(attackerSideHero->getPrimaryGrowthRules().isNull());
	EXPECT_FALSE(attackerSideHero->getCapabilityRules().isNull());
}

TEST_F(NewHorizonsCapabilityStateTest, ArmyUsageScalesAllMovementLayersWithoutDeletingOrAwardingMovement)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 2000));
	const auto normal = attackerSideHero->getTurnInfo(0);
	const int remaining = attackerSideHero->movementPointsRemaining();
	attackerSideHero->setStackCount(SlotID(0), 2500);
	const auto leadership = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(leadership);
	EXPECT_EQ(leadership->capacity, 2000);
	EXPECT_EQ(leadership->used, 2500);
	EXPECT_EQ(leadership->movementPercent, 80);
	for(int day : {0, 1})
	{
		const auto overloaded = attackerSideHero->getTurnInfo(day);
		EXPECT_EQ(overloaded->getMovePointsLimitLand(), normal->getMovePointsLimitLand() * 80 / 100);
		EXPECT_EQ(overloaded->getMovePointsLimitWater(), normal->getMovePointsLimitWater() * 80 / 100);
		EXPECT_EQ(overloaded->getMovePointsLimitAir(), normal->getMovePointsLimitAir() * 80 / 100);
	}
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 2500);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), remaining);
	attackerSideHero->setStackCount(SlotID(0), 2000);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creatureByName("core:pikeman"), 500));
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->used, 2500);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->movementPercent, 80);
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->capacity, 2600);
	EXPECT_EQ(attackerSideHero->movementPointsLimit(), normal->getMovePointsLimitLand());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), remaining);
}

TEST_F(NewHorizonsCapabilityStateTest, AuthoritativeNewDayRefreshUsesCapacityAndSkillWithoutLosingTroops)
{
	// Independent post-freeze native control, not a GUI journey or candidate change.
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setStackCount(SlotID(0), 4000);
	attackerSideHero->setMovementPoints(123);
	const auto day = gameState()->day;
	gameHandler->onNewTurn();
	EXPECT_EQ(gameState()->day, day + 1);
	EXPECT_EQ(attackerSideHero->movementPointsLimit(), 780);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 780);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 4000);
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->capacity, 2600);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 780);
	gameHandler->onNewTurn();
	EXPECT_EQ(gameState()->day, day + 2);
	EXPECT_EQ(attackerSideHero->movementPointsLimit(), 1014);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 1014);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 4000);
}

TEST_F(NewHorizonsCapabilityStateTest, HypotheticalExchangeBudgetUsesProjectedArmyWithoutMutatingCarrier)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 4000));
	const int remaining = attackerSideHero->movementPointsRemaining();
	const auto actual = attackerSideHero->getTurnInfo(0);
	const auto projection = attackerSideHero->getTurnInfo(0, defenderSideHero);
	EXPECT_EQ(projection->getMovePointsLimitLand(), actual->getMovePointsLimitLand() / 2);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity(*defenderSideHero)->used, 4000);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->used, 100);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 100);
	EXPECT_EQ(defenderSideHero->getStackCount(SlotID(0)), 4000);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), remaining);
}

TEST_F(NewHorizonsCapabilityStateTest, RealSiegeDamageUsesArtilleryNotExpandedAttackOrHeroLevel)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(70), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(77), 1);
	const auto damage = [&] { return battle()->calculateDmgRange(BattleAttackInfo(ours, enemy, 0, true)).damage; };
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, 0, ChangeValueMode::ABSOLUTE);
	const auto untrained = damage();
	ASSERT_GT(untrained.min, 0);
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 500, ChangeValueMode::ABSOLUTE);
	attackerSideHero->level = 20;
	EXPECT_EQ(damage().min, untrained.min);
	EXPECT_EQ(damage().max, untrained.max);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(damage().min, untrained.min * 4);
	EXPECT_EQ(damage().max, untrained.max * 4);
}

TEST_F(NewHorizonsCapabilityStateTest, BattleAIBallistaExecutesValidatedShotWithScaledRange)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, 3, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	defenderSideHero->setStackCount(SlotID(0), 1000);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();
	const auto targets = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER;
	});
	ASSERT_EQ(targets.size(), 1u);
	const auto * target = targets.front();
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));
	beginCombat();
	const auto firstRound = battle()->getRound();
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns; ++turn)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		if(active->unitId() == ballista->unitId())
			break;
		ASSERT_EQ(battle()->getRound(), firstRound);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), ballista->unitId());
	BattleAttackInfo estimate(ballista, target, 0, true);
	estimate.doubleDamage = true; // Expert Artillery's existing 100% doubled-shot chance.
	const auto range = battle()->calculateDmgRange(estimate).damage;
	ASSERT_GT(range.min, 0);
	ASSERT_EQ(ballista->getTotalAttacks(true), 1);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(CreatureID(CreatureID::BALLISTA))), 1);
	class CapabilityEnvironment final : public Environment
	{
		std::shared_ptr<CGameState> state;
	public:
		explicit CapabilityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
		const Services * services() const override { return LIBRARY; }
		const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
		const GameCb * game() const override { return state.get(); }
	};
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<CapabilityEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, ballista, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto firstChoice = evaluator.selectStackAction(ballista);
	// The real evaluator chose WAIT in both recorded RED contexts. Exercise that
	// legal decision rather than forcing an immediate shot or changing AI policy.
	ASSERT_EQ(firstChoice.actionType, EActionType::WAIT);
	ASSERT_FALSE(ballista->waited());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), firstChoice));
	for(size_t turn = 0; turn < maximumTurns; ++turn)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(battle()->getRound(), firstRound);
		if(active->unitId() == ballista->unitId())
			break;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), ballista->unitId());
	ASSERT_TRUE(ballista->waited());
	BattleEvaluator waitedEvaluator(environment, callback, ballista, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto chosen = waitedEvaluator.selectStackAction(ballista);
	ASSERT_EQ(chosen.actionType, EActionType::SHOOT);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chosen));
	const auto lost = before - target->getAvailableHealth();
	EXPECT_GE(lost, range.min * 2);
	EXPECT_LE(lost, range.max * 2);
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->used, 1); // Artifact machine is not a roster creature.
}

TEST_F(NewHorizonsCapabilityStateTest, SiegeViewReportsActualSkillRanksAndManualControl)
{
	prepareCommands();
	const auto untrained = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(untrained);
	EXPECT_EQ(untrained->artilleryRank, 0);
	EXPECT_EQ(untrained->ballistaDamageMultiplier, 1);
	EXPECT_EQ(untrained->ballistaControlChance, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, 3, ChangeValueMode::ABSOLUTE);
	const auto trained = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(trained);
	EXPECT_EQ(trained->artilleryRank, 3);
	EXPECT_EQ(trained->ballistaDamageMultiplier, 4);
	EXPECT_EQ(trained->ballistaControlChance, 100);
	EXPECT_EQ(trained->ballisticsRank, 0);
	EXPECT_EQ(trained->firstAidRank, 0);
	EXPECT_EQ(trained->catapultControlChance, 0);
	EXPECT_EQ(trained->firstAidControlChance, 0);
}

TEST_F(NewHorizonsCapabilityStateTest, LegacySiegeRetainsAttackBasedDamage)
{
	capabilitiesEnabled = false;
	growthEnabled = false;
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(70), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(77), 1);
	const auto damage = [&] { return battle()->calculateDmgRange(BattleAttackInfo(ours, enemy, 0, true)).damage.min; };
	const auto before = damage();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 9, ChangeValueMode::ABSOLUTE);
	EXPECT_GT(damage(), before);
	EXPECT_FALSE(attackerSideHero->getLeadershipCapacity());
}

TEST_F(NewHorizonsCapabilityStateTest, FullWorldRoundtripKeepsBothIndependentIdentities)
{
	startGame();
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getCapabilityRules(), attackerSideHero->getCapabilityRules());
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(restored.getHeroCapabilityRules(), gameState()->getHeroCapabilityRules());
}

TEST_F(NewHorizonsCapabilityStateTest, GrowthCheckpointBinaryDoesNotAcquireCapabilities)
{
	capabilitiesEnabled = false;
	startGame();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_GROWTH;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_GROWTH;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->getPrimaryGrowthView());
	EXPECT_TRUE(hero->getCapabilityRules().isNull());
	EXPECT_TRUE(restored.getHeroCapabilityRules().isNull());
	EXPECT_FALSE(hero->getLeadershipCapacity());
	EXPECT_FALSE(hero->getSiegeCapabilities());
}

TEST_F(NewHorizonsCapabilityStateTest, OldCrossoverRemainsWithoutCapabilitiesEvenWhenReinitialized)
{
	startGame();
	CampaignState campaign;
	auto node = campaign.crossoverSerialize(attackerSideHero);
	const auto current = campaign.crossoverDeserialize(node, map());
	EXPECT_EQ(current->getCapabilityRules(), attackerSideHero->getCapabilityRules());
	node.Struct().erase("capabilityRules");
	const auto legacy = campaign.crossoverDeserialize(node, map());
	legacy->initHero(*gameHandler->randomizer);
	EXPECT_TRUE(legacy->getPrimaryGrowthView());
	EXPECT_TRUE(legacy->getCapabilityRules().isNull());
}

TEST_F(NewHorizonsHeroGrowthTest, RealInitializationCapturesProfileAndKnowledgeMana)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0)
		.heroGarrison({{CreatureID(0), 10}});
	startWithMap(std::move(builder));
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto view = hero->getPrimaryGrowthView();
	ASSERT_TRUE(view.has_value());
	EXPECT_EQ(view->base, (std::array<int, 4>{15, 20, 5, 10}));
	EXPECT_EQ(view->modified, view->base);
	EXPECT_EQ(view->profile.growth, (std::array<int, 4>{4, 4, 1, 1}));
	EXPECT_EQ(hero->manaLimit(), 10);
	EXPECT_EQ(hero->mana, 10);
	EXPECT_EQ(gameState()->getHeroDevelopmentRules(), testHeroRules());
}

TEST_F(NewHorizonsHeroGrowthTest, MapExperienceUsesTheSameFourAttributeGrowthPath)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(LIBRARY->heroh->reqExp(3))
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}, {SecondarySkill::ARCHERY, 1},
			{SecondarySkill::LOGISTICS, 1}, {SecondarySkill::SCOUTING, 1},
			{SecondarySkill::DIPLOMACY, 1}, {SecondarySkill::NAVIGATION, 1},
			{SecondarySkill::LEADERSHIP, 1}, {SecondarySkill::WISDOM, 1}})
		.heroGarrison({{CreatureID(0), 10}});
	startWithMap(std::move(builder));
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->level, 3);
	ASSERT_TRUE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getPrimaryGrowthView()->base, (std::array<int, 4>{23, 28, 7, 12}));
}

TEST_F(NewHorizonsHeroGrowthTest, LegacyInstanceHasNoGrowthViewAndOriginalScale)
{
	growthEnabled = false;
	startGame();
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthView());
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 20, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->manaLimit(), 200);
	EXPECT_EQ(attackerSideHero->getEffectPowerDivisor(nullptr), 1);
}

TEST_F(NewHorizonsHeroGrowthTest, RealSeededGrowthAddsIndependentExtrasToAllFourBaseGains)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 3, ChangeValueMode::ABSOLUTE);
	const auto view = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	ASSERT_EQ(view->extraGrowth.size(), 2);
	EXPECT_EQ(gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero), (std::array<int, 4>{5, 5, 1, 1}));
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero), (std::array<int, 4>{4, 4, 1, 1}));
}

TEST_F(NewHorizonsHeroGrowthTest, SerializedHeroRandomizerContinuesIndependentPercentileSequence)
{
	extraChance = 50;
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 3, ChangeValueMode::ABSOLUTE);
	gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero);
	CMemorySerializer memory;
	memory.oser & *gameHandler->randomizer;
	GameRandomizer restored(*gameState());
	memory.iser & restored;
	std::set<std::array<int, 4>> outcomes;
	for(int i = 0; i < 32; ++i)
	{
		const auto expected = gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero);
		EXPECT_EQ(restored.rollPrimarySkillsForLevelup(attackerSideHero), expected);
		outcomes.insert(expected);
	}
	EXPECT_GT(outcomes.size(), 1); // Not a vacuous 0%/100% continuation check.
}

TEST_F(NewHorizonsHeroGrowthTest, AuthorityAppliesAndReportsAllFourActualLevelGains)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 3, ChangeValueMode::ABSOLUTE);
	const auto before = attackerSideHero->getPrimaryGrowthView()->base;
	const auto level = attackerSideHero->level;
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(level + 1), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	// Human level-up queries defer their notification until the ordinary
	// adventure-interface readiness event. Primary packets already applied.
	EXPECT_EQ(attackerSideHero->level, level);
	EXPECT_EQ(attackerSideHero->getPrimaryGrowthView()->lastGains, (std::array<int, 4>{0, 0, 0, 0}));
	gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	EXPECT_EQ(attackerSideHero->level, level + 1);
	const auto view = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_EQ(view->lastGains, (std::array<int, 4>{5, 5, 1, 1}));
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(view->base[i] - before[i], view->lastGains[i]);
}

TEST_F(NewHorizonsHeroGrowthTest, ChainedLevelQueriesReportActualCapClippedGainsIncludingAllZeros)
{
	// Deliberate native cap diagnostic, not the ordinary canonical GUI map.
	startGame();
	for(SecondarySkill skill : {SecondarySkill::PATHFINDING, SecondarySkill::ARCHERY,
		SecondarySkill::LOGISTICS, SecondarySkill::SCOUTING, SecondarySkill::DIPLOMACY,
		SecondarySkill::NAVIGATION, SecondarySkill::LEADERSHIP, SecondarySkill::WISDOM})
		attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	const auto maximum = attackerSideHero->getPrimaryGrowthView()->maximumPrimary;
	for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
		attackerSideHero->setPrimarySkill(PrimarySkill(i), maximum - (i == 0 ? 5 : 0), ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->getPrimaryGrowthView()->extraGrowth.empty());
	const auto owner = attackerSideHero->getOwner();
	gameHandler->onAdvInterfaceReady(owner);
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(4), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	const auto firstSnapshot = *attackerSideHero->getPrimaryGrowthView();
	const std::array<std::array<int, 4>, 3> expected = {{{4, 0, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}}};
	for(int step = 0; step < 3; ++step)
	{
		SCOPED_TRACE(step);
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(owner));
		ASSERT_NE(query, nullptr);
		ASSERT_TRUE(query->prompted);
		ASSERT_TRUE(query->hlu.skills.empty());
		EXPECT_EQ(attackerSideHero->level, step + 2);
		EXPECT_EQ(query->hlu.primaryGains, expected[step]);
		EXPECT_EQ(attackerSideHero->getPrimaryGrowthView()->lastGains, expected[step]);
		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, owner));
	}
	EXPECT_EQ(gameHandler->queries->topQuery(owner), nullptr);
	EXPECT_EQ(firstSnapshot.lastGains, expected[0]);
	EXPECT_EQ(firstSnapshot.base[0], maximum - 1);
	EXPECT_EQ(attackerSideHero->getPrimaryGrowthView()->base, (std::array<int, 4>{maximum, maximum, maximum, maximum}));
}

TEST_F(NewHorizonsHeroGrowthTest, FullGameRoundtripPreservesResolvedHeroAndWorldRules)
{
	startGame();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 150, ChangeValueMode::ABSOLUTE);
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getPrimaryGrowthView()->base, attackerSideHero->getPrimaryGrowthView()->base);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(restored.getHeroDevelopmentRules(), gameState()->getHeroDevelopmentRules());
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 150);
}

TEST_F(NewHorizonsHeroGrowthTest, Actual034VersionAndOldCrossoverRemainLegacy)
{
	growthEnabled = false;
	startGame();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_MAGIC;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_MAGIC;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	EXPECT_FALSE(restored.getHero(attackerSideHero->id)->getPrimaryGrowthView());
	EXPECT_FALSE(newHorizonsHeroes::usesRules(restored.getHeroDevelopmentRules()));
	CampaignState campaign;
	auto node = campaign.crossoverSerialize(attackerSideHero);
	node.Struct().erase("primaryGrowthRules");
	const auto copy = campaign.crossoverDeserialize(node, map());
	EXPECT_FALSE(copy->getPrimaryGrowthView());
}

TEST_F(NewHorizonsHeroGrowthTest, CurrentCrossoverPreservesProfileRatherThanRebuildingFromClass)
{
	startGame();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 150, ChangeValueMode::ABSOLUTE);
	CampaignState campaign;
	const auto node = campaign.crossoverSerialize(attackerSideHero);
	const auto copy = campaign.crossoverDeserialize(node, map());
	EXPECT_EQ(copy->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	ASSERT_TRUE(copy->getPrimaryGrowthView());
	EXPECT_EQ(copy->getBasePrimarySkillValue(PrimarySkill::ATTACK), 150);
}

TEST_F(NewHorizonsHeroGrowthTest, DetachedCampaignPreviewKeepsRatingsWithoutAssumingWorldCommands)
{
	startGame();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 150, ChangeValueMode::ABSOLUTE);
	EXPECT_GT(attackerSideHero->getFightingStrength(), 1.0);
	CampaignState campaign;
	const auto node = campaign.crossoverSerialize(attackerSideHero);
	const auto detached = campaign.crossoverDeserialize(node, nullptr);
	ASSERT_NE(detached, nullptr);
	ASSERT_TRUE(detached->getPrimaryGrowthView());
	EXPECT_EQ(detached->getBasePrimarySkillValue(PrimarySkill::ATTACK), 150);
	EXPECT_EQ(detached->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_DOUBLE_EQ(detached->getFightingStrength(), 1.0);
}

TEST_F(NewHorizonsHeroGrowthTest, ExpandedRatingsDoNotBecomeCreatureDamageButDoStrengthenOrders)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto damage = [&] { return battle()->calculateDmgRange(BattleAttackInfo(ours, enemy, 0, false)).damage.min; };
	const auto before = damage();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 150, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(damage(), before); // Separate controls: equal A/D must not hide leakage.
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 150, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 150);
	EXPECT_EQ(damage(), before);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(damage(), before);
	advanceRound();
	EXPECT_EQ(damage(), before);
	// Creature-specific hero specialties are not hero ratings and must survive.
	auto specialty = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::HERO_SPECIAL, 5, BonusSourceID(attackerSideHero->getHeroTypeID()), BonusSubtypeID(PrimarySkill::ATTACK));
	auto limiter = std::make_shared<CCreatureTypeLimiter>();
	limiter->setCreature(creatureByName("angel"));
	specialty->limiter = limiter;
	attackerSideHero->addNewBonus(specialty);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 150);
	EXPECT_GT(damage(), before);
}

TEST_F(NewHorizonsHeroGrowthTest, RealCastScalesPowerTermAndPreservesFixedExpertTermAndBudget)
{
	prepareCommands(true);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 20, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::INTELLIGENCE, 2, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->manaLimit(), 30);
	attackerSideHero->mana = attackerSideHero->manaLimit();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_SCHOOL_SKILL,
		BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	attackerSideHero->addSpellToSpellbook(spell->getId());
	ASSERT_EQ(attackerSideHero->getEffectLevel(spell), 3);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), 622);
	spells::ProxyCaster proxy(attackerSideHero);
	EXPECT_EQ(proxy.getEffectPowerDivisor(spell), 10);
	EXPECT_EQ(spell->calculateDamage(&proxy), 622);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto health = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	const auto cost = attackerSideHero->getSpellCost(spell);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(enemy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(health - enemy->getAvailableHealth(), 622);
	EXPECT_EQ(attackerSideHero->mana, mana - cost);
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
}

TEST_F(NewHorizonsHeroGrowthTest, RealSummonRoundsAfterScaledPowerProduct)
{
	const SpellID spellID(SpellID::SUMMON_AIR_ELEMENTAL);
	prepareScaledExpert(spellID);
	const auto * spell = spellID.toSpell();
	ASSERT_EQ(spell->getLevelPower(3), 4);
	const auto mana = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spellID;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	int count = 0;
	for(const auto * unit : battle()->battleGetAllStacks())
		if(unit->isSummoned() && unit->unitSide() == BattleSide::ATTACKER)
			count += unit->getCount();
	EXPECT_EQ(count, 17); // floor(43 * 4 / 10), not floor(43 / 10) * 4.
	EXPECT_EQ(attackerSideHero->mana, mana - attackerSideHero->getSpellCost(spell));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
}

TEST_F(NewHorizonsHeroGrowthTest, RealSacrificeKeepsVictimHealthAndMasteryTermsUnscaled)
{
	const SpellID spellID(SpellID::SACRIFICE);
	prepareScaledExpert(spellID);
	auto * dead = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("pikeman"), BattleHex(72), 100);
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage); // Fixture casualty, before the authoritative cast.
	ASSERT_FALSE(dead->alive());
	const auto * spell = spellID.toSpell();
	const int64_t expected = (43 + 10 * victim->getMaxHealth() + 10 * spell->getLevelPower(3)) * victim->getCount() / 10;
	const auto mana = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spellID;
	action.setTarget({battle::Destination(dead), battle::Destination(victim)});
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(dead->alive());
	EXPECT_EQ(dead->getAvailableHealth(), expected);
	EXPECT_FALSE(victim->alive());
	EXPECT_EQ(attackerSideHero->mana, mana - attackerSideHero->getSpellCost(spell));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
}

TEST_F(NewHorizonsHeroGrowthTest, RealFireWallCreationTriggerAndBattlePacketKeepLatchedScale)
{
	prepareScaledExpert(SpellID::FIRE_WALL);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::FIRE_WALL;
	action.aimToHex(BattleHex(90));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const SpellCreatedObstacle * created = nullptr;
	for(const auto & obstacle : battle()->obstacles)
		if(const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get()))
			if(spellObstacle->getAffectedTiles().contains(BattleHex(90)))
				created = spellObstacle;
	ASSERT_NE(created, nullptr);
	ASSERT_EQ(created->casterSpellPower, 43);
	ASSERT_EQ(created->casterPowerDivisor, 10);
	const auto * trigger = created->getTrigger().toSpell();
	ASSERT_GT(trigger->getBasePower(), 0);
	const int64_t expected = 43LL * trigger->getBasePower() / 10 + trigger->getLevelPower(3);
	// Place a fixture recipient on the created wall, then use the same trigger
	// handler as movement. This proves the effect, not a GUI movement journey.
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("pikeman"), BattleHex(90), 100);
	const auto health = recipient->getAvailableHealth();
	ASSERT_GT(expected, 0);
	ASSERT_LT(expected, health);
	const auto mana = attackerSideHero->mana;
	battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *recipient, BattleHexArray());
	EXPECT_EQ(health - recipient->getAvailableHealth(), expected);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	// Ordinary game snapshots deliberately exclude active battles. Restore the
	// battle through its real packet path, against this independent army graph.
	ASSERT_TRUE(restored.currentBattles.empty());
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), &restored);
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = &restored;
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	restored.apply(incoming);
	ASSERT_EQ(restored.currentBattles.size(), 1u);
	EXPECT_EQ(restored.currentBattles.front()->obstacles.size(), battle()->obstacles.size());
	EXPECT_FALSE(restored.currentBattles.front()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	int savedWalls = 0;
	for(const auto & obstacle : restored.currentBattles.front()->obstacles)
		if(const auto * saved = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get()))
		{
			++savedWalls;
			EXPECT_EQ(saved->casterSpellPower, 43);
			EXPECT_EQ(saved->casterPowerDivisor, 10);
		}
	EXPECT_GT(savedWalls, 0);
}

TEST_F(NewHorizonsHeroGrowthTest, ObstacleKeepsCreationScaleNotCurrentSideHeroScale)
{
	startGame();
	SpellCreatedObstacle obstacle;
	obstacle.casterSpellPower = 43;
	obstacle.casterPowerDivisor = 10;
	spells::ObstacleCasterProxy stored(PlayerColor(0), nullptr, obstacle);
	EXPECT_EQ(stored.getEffectPowerDivisor(nullptr), 10);
	EXPECT_EQ(stored.getEffectPower(nullptr), 43);
	SpellCreatedObstacle creatureObstacle;
	spells::ObstacleCasterProxy creature(PlayerColor(0), attackerSideHero, creatureObstacle);
	EXPECT_EQ(creature.getEffectPowerDivisor(nullptr), 1);
}
