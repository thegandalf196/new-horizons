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
#include "../../../lib/pathfinder/CPathfinder.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/battle/Destination.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/spells/ProxyCaster.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameConstants.h"
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
#include "../../../lib/modding/CModHandler.h"
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
		// Real turn processing needs a usable Calendar.  Tiny maps do not carry
		// the global calendar settings reliably, so pin the canonical Heroes III
		// week/month lengths in this server-side fixture before state init.
		JsonNode daysPerWeek;
		daysPerWeek.Integer() = 7;
		map->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, daysPerWeek);
		JsonNode weeksPerMonth;
		weeksPerMonth.Integer() = 4;
		map->overrideGameSetting(EGameSettings::GENERAL_WEEKS_PER_MONTH, weeksPerMonth);
		auto rules = growthEnabled ? testHeroRules() : JsonNode();
		if(growthEnabled)
			for(auto & extra : rules["extraGrowth"].Vector())
				for(int rank = 1; rank <= 3; ++rank)
					extra["chances"].Vector()[rank].Integer() = extraChance;
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
};

class LegacySpellCostTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
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

class NewHorizonsPerSlotLeadershipTest : public NewHorizonsCapabilityStateTest
{
protected:
	void mapLoaded(CMap * map) override
	{
		NewHorizonsCapabilityStateTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}
};

class NewHorizonsWarMachinesTest : public NewHorizonsPerSlotLeadershipTest
{
protected:
	void mapLoaded(CMap * map) override
	{
		NewHorizonsPerSlotLeadershipTest::mapLoaded(map);
		// Keep this fixture independent of the build tree's copied config files:
		// extend the known-good per-slot v2 fixture with the canonical v3 tables.
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		rules["rulesetVersion"].Integer() = 3;
		rules["siege"]["siegeRating"].Vector().clear();
		rules["siege"]["directControlChance"].Vector().clear();
		for(int value : {0, 20, 40, 60})
			rules["siege"]["siegeRating"].Vector().emplace_back(value);
		for(int value : {0, 100, 100, 100})
			rules["siege"]["directControlChance"].Vector().emplace_back(value);
		for(const auto & [output, base, coefficientHalf] : {
			std::tuple{"ballistaDamage", 50, 4}, std::tuple{"catapultStructuralDamage", 100, 6},
			std::tuple{"firstAidHealing", 75, 6}, std::tuple{"defensiveTowerDamage", 60, 3}})
		{
			rules["siege"]["outputs"][output]["base"].Integer() = base;
			rules["siege"]["outputs"][output]["siegeCoefficientHalf"].Integer() = coefficientHalf;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
	}

	void SetUp() override
	{
		NewHorizonsPerSlotLeadershipTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	SecondarySkill warMachines() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:warMachines");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}
};

TEST_F(NewHorizonsWarMachinesTest, CanonicalRanksProvideExactSiegeOutputAndControl)
{
	prepareCommands();
	ASSERT_EQ(attackerSideHero->getCapabilityRules()["rulesetVersion"].Integer(), 3);
	ASSERT_EQ(SecondarySkill::encode(warMachines().getNum()), "new-horizons:warMachines");
	auto * ours = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(70), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(77), 10000);
	const auto damage = [&]
	{
		return battle()->calculateDmgRange(BattleAttackInfo(ours, enemy, 0, true));
	};
	const auto * battleHero = battle()->battleGetOwnerHero(ours);
	ASSERT_EQ(battleHero, attackerSideHero);
	ASSERT_EQ(battleHero->getCapabilityRules()["rulesetVersion"].Integer(), 3);
	ASSERT_TRUE(ours->hasBonusOfType(BonusType::SIEGE_WEAPON));

	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
	for(const auto & [rank, siege, ballista, catapult, healing, tower, control] : {
		std::tuple{0, 0, 50, 100, 75, 60, 0}, std::tuple{1, 20, 90, 160, 135, 90, 100},
		std::tuple{2, 40, 130, 220, 195, 120, 100}, std::tuple{3, 60, 170, 280, 255, 150, 100}})
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(warMachines(), rank, ChangeValueMode::ABSOLUTE);
		const auto capabilities = attackerSideHero->getSiegeCapabilities();
		ASSERT_TRUE(capabilities);
		EXPECT_EQ(capabilities->warMachinesRank, rank);
		EXPECT_EQ(capabilities->siegeRating, siege);
		EXPECT_EQ(capabilities->ballistaDamage, ballista);
		EXPECT_EQ(capabilities->catapultStructuralDamage, catapult);
		EXPECT_EQ(capabilities->firstAidHealing, healing);
		EXPECT_EQ(capabilities->defensiveTowerDamage, tower);
		EXPECT_EQ(capabilities->ballistaControlChance, control);
		EXPECT_EQ(capabilities->catapultControlChance, control);
		EXPECT_EQ(capabilities->firstAidControlChance, control);
		const auto currentDamage = damage();
		// This fixture's Ballista has a fixed +25% ordinary Attack advantage;
		// the canonical formula supplies its exact base before that combat rule.
		const int expectedAfterAttack = ballista * 125 / 100;
		EXPECT_EQ(currentDamage.damageBeforeDefense.min, expectedAfterAttack);
		EXPECT_EQ(currentDamage.damageBeforeDefense.max, expectedAfterAttack);
	}
}

TEST_F(NewHorizonsWarMachinesTest, CanonicalBallistaShotMatchesPreviewRange)
{
	startGame();
	attackerSideHero->setSecSkillLevel(warMachines(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));

	beginCombat();
	const auto firstRound = battle()->getRound();
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns && battle()->battleActiveUnit() != ballista; ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), ballista);

	const auto preview = battle()->calculateDmgRange(BattleAttackInfo(ballista, target, 0, true)).damage;
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, target)));
	const auto actual = healthBefore - target->getAvailableHealth();
	EXPECT_GE(actual, preview.min);
	EXPECT_LE(actual, preview.max);
}

TEST_F(NewHorizonsWarMachinesTest, SavedRankAndCapabilitySnapshotRoundTrip)
{
	startGame();
	ASSERT_EQ(attackerSideHero->getCapabilityRules()["rulesetVersion"].Integer(), 3);
	ASSERT_EQ(SecondarySkill::encode(warMachines().getNum()), "new-horizons:warMachines");
	attackerSideHero->setSecSkillLevel(warMachines(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	const auto capabilities = hero->getSiegeCapabilities();
	ASSERT_TRUE(capabilities);
	EXPECT_EQ(capabilities->warMachinesRank, MasteryLevel::ADVANCED);
	EXPECT_EQ(capabilities->siegeRating, 40);
	EXPECT_EQ(capabilities->ballistaDamage, 130);
	EXPECT_EQ(capabilities->catapultStructuralDamage, 220);
	EXPECT_EQ(capabilities->firstAidHealing, 195);
	EXPECT_EQ(capabilities->defensiveTowerDamage, 120);
	EXPECT_EQ(hero->getCapabilityRules(), attackerSideHero->getCapabilityRules());
}

TEST_F(NewHorizonsWarMachinesTest, FirstAidPreviewUsesExactSiegeFormula)
{
	prepareCommands();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(leftHex), 1);
	StacksInjured injury;
	injury.battleID = BattleID(0);
	auto & attacked = injury.stacks.emplace_back();
	attacked.stackAttacked = target->unitId();
	attacked.damageAmount = 500;
	target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
	ASSERT_LT(attacked.killedAmount, target->getCount());
	gameHandler->sendAndApply(injury);
	ASSERT_TRUE(target->canBeHealed());

	for(const auto & [rank, expected] : {std::pair{0, 75}, std::pair{1, 135}, std::pair{2, 195}, std::pair{3, 255}})
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(warMachines(), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(battle()->getFirstAidHealValue(attackerSideHero, target), expected);
	}
}

TEST_F(NewHorizonsWarMachinesTest, ExpertFirstAidActionHealsExactSiegeAmount)
{
	startGame();
	attackerSideHero->setSecSkillLevel(warMachines(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
	startBattle();
	const auto tents = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isFirstAidTent();
	});
	ASSERT_EQ(tents.size(), 1u);
	const auto * tent = tents.front();
	const auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(leftHex), 1);
	StacksInjured injury;
	injury.battleID = BattleID(0);
	auto & attacked = injury.stacks.emplace_back();
	attacked.stackAttacked = target->unitId();
	attacked.damageAmount = 500;
	target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
	gameHandler->sendAndApply(injury);
	ASSERT_TRUE(target->canBeHealed());
	const auto healthBefore = target->getAvailableHealth();

	beginCombat();
	const auto firstRound = battle()->getRound();
	for(size_t turn = 0; turn < battle()->stacks.size() * 2 && battle()->battleActiveUnit() != tent; ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), tent);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(tent->unitSide()), BattleAction::makeHeal(tent, target)));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, 255);
}

TEST_F(NewHorizonsWarMachinesTest, DefensiveTowerUsesDefendingHeroSiegeFormula)
{
	prepareCommands();
	auto * tower = addStack(BattleSide::DEFENDER, CreatureID::ARROW_TOWERS, BattleHex(rightHex), 1);
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	ASSERT_TRUE(tower->isTurret());
	ASSERT_EQ(battle()->battleGetOwnerHero(tower), defenderSideHero);
	for(const auto & [rank, expected] : {std::pair{0, 60}, std::pair{1, 90}, std::pair{2, 120}, std::pair{3, 150}})
	{
		SCOPED_TRACE(rank);
		defenderSideHero->setSecSkillLevel(warMachines(), rank, ChangeValueMode::ABSOLUTE);
		const auto result = battle()->calculateDmgRange(BattleAttackInfo(tower, target, 0, true));
		EXPECT_EQ(result.damageBeforeDefense.min, expected);
		EXPECT_EQ(result.damageBeforeDefense.max, expected);
	}
}

TEST_F(NewHorizonsWarMachinesTest, CanonicalCatapultPacketConsumesStructuralHP)
{
	startGame(true);
	const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
	ASSERT_EQ(towns.size(), 1u);
	ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
	giveArtifact(attackerSideHero, ArtifactID::CATAPULT, ArtifactPosition::MACH4);
	startBattle(towns.front());

	ASSERT_TRUE(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL) == 300);
	ASSERT_TRUE(battle()->getWallStructuralHP(EWallPart::GATE) == 450);
	EXPECT_EQ(SiegeInfo::maximumStructuralHP(EWallPart::BOTTOM_TOWER), 350);
	if(battle()->getWallState(EWallPart::BOTTOM_TOWER) != EWallState::NONE)
		EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 350);
	EXPECT_EQ(battle()->getWallState(EWallPart::BOTTOM_WALL), EWallState::INTACT);

	CatapultAttack hit;
	hit.battleID = BattleID(0);
	hit.attackedPart = EWallPart::BOTTOM_WALL;
	hit.damageDealt = 1;
	hit.structuralDamage = 160; // Basic canonical output: 100 + 3 * 20.
	gameHandler->sendAndApply(hit);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 140);
	EXPECT_EQ(battle()->getWallState(EWallPart::BOTTOM_WALL), EWallState::DAMAGED);

	hit.structuralDamage = 160;
	gameHandler->sendAndApply(hit);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 0);
	EXPECT_EQ(battle()->getWallState(EWallPart::BOTTOM_WALL), EWallState::DESTROYED);
}

TEST_F(NewHorizonsWarMachinesTest, BasicRankRoutesBallistaTurnToThePlayer)
{
	startGame();
	// The canonical skill is authoritative even if stale legacy Artillery data is present.
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(warMachines(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();

	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));

	const auto ballistaActivated = [&]
	{
		return std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & activation)
		{
			return activation.battleID == BattleID(0) && activation.stack == ballista->unitId();
		});
	};

	beginCombat();
	const auto firstRound = battle()->getRound();
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns && !ballistaActivated(); ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_TRUE(ballistaActivated());

	const auto activation = std::find_if(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & candidate)
	{
		return candidate.battleID == BattleID(0) && candidate.stack == ballista->unitId();
	});
	ASSERT_NE(activation, server.stackActivations.end());
	EXPECT_EQ(activation->reason, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), ballista->unitId());
	EXPECT_TRUE(std::none_of(server.startedActions.begin(), server.startedActions.end(), [&](const auto & action)
	{
		return action.battleID == BattleID(0) && action.ba.isUnitAction() && action.ba.stackNumber == ballista->unitId();
	}));
}

TEST_F(NewHorizonsWarMachinesTest, RankZeroSuppressesStaleArtilleryManualControl)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(warMachines(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();

	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);

	const auto ballistaActivated = [&]
	{
		return std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & activation)
		{
			return activation.battleID == BattleID(0) && activation.stack == ballista->unitId();
		});
	};
	beginCombat();
	const auto firstRound = battle()->getRound();
	for(size_t turn = 0; turn < battle()->stacks.size() * 2 && !ballistaActivated(); ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_TRUE(ballistaActivated());
	const auto activation = std::find_if(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & candidate)
	{
		return candidate.battleID == BattleID(0) && candidate.stack == ballista->unitId();
	});
	ASSERT_NE(activation, server.stackActivations.end());
	EXPECT_EQ(activation->reason, BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(std::any_of(server.startedActions.begin(), server.startedActions.end(), [&](const auto & action)
	{
		return action.battleID == BattleID(0) && action.ba.actionType == EActionType::SHOOT
			&& action.ba.stackNumber == ballista->unitId();
	}));
}

TEST_F(NewHorizonsPerSlotLeadershipTest, AuthoritativeStackMutationsRejectOversizedResultsAtomically)
{
	startGame();
	attackerSideHero->clearSlots();
	const CreatureID pikeman = creatureByName("core:pikeman");
	const CreatureID halberdier = creatureByName("core:halberdier");
	const auto capacity = attackerSideHero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->maximum, 17);

	EXPECT_FALSE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(0)), pikeman.toCreature(), 18));
	EXPECT_FALSE(attackerSideHero->hasStackAtSlot(SlotID(0)));
	ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(0)), pikeman.toCreature(), 17));
	EXPECT_FALSE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id, SlotID(0)),
		1, ChangeValueMode::RELATIVE));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 17);

	// The upgraded requirement is 60 * 120% = 72, rounded to 70; 1025 / 70 = 14.
	EXPECT_FALSE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), halberdier.toCreature()));
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0)), pikeman.toCreature());
}

TEST_F(NewHorizonsPerSlotLeadershipTest, WholeArmyPreflightCannotLoopOrPartiallyTransfer)
{
	startGame();
	const CreatureID pikeman = creatureByName("core:pikeman");
	const CreatureID archer = creatureByName("core:archer");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 18));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), archer, 1));

	EXPECT_FALSE(gameHandler->moveArmy(defenderSideHero, attackerSideHero, true));
	EXPECT_FALSE(attackerSideHero->hasStackAtSlot(SlotID(0)));
	EXPECT_EQ(defenderSideHero->getStackCount(SlotID(0)), 18);
	EXPECT_EQ(defenderSideHero->getStackCount(SlotID(1)), 1);
}

TEST_F(NewHorizonsPerSlotLeadershipTest, SeparateSlotsNeverShareOneLeadershipBudget)
{
	startGame();
	const CreatureID pikeman = creatureByName("core:pikeman");
	const CreatureID archer = creatureByName("core:archer");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 8));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), pikeman, 8));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 2));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), archer, 1));

	EXPECT_TRUE(gameHandler->moveStack(StackLocation(defenderSideHero->id, SlotID(0)),
		StackLocation(attackerSideHero->id, SlotID(0))));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 8);
}

TEST_F(NewHorizonsPerSlotLeadershipTest, DuplicateCreatureRewardIsRejectedBeforeAnyPartialGrant)
{
	startGame();
	const CreatureID pikeman = creatureByName("core:pikeman");
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 2));
	CCreatureSet reward;
	ASSERT_TRUE(reward.setCreature(SlotID(0), pikeman, 8));
	ASSERT_TRUE(reward.setCreature(SlotID(1), pikeman, 8));

	gameHandler->giveCreatures(attackerSideHero, reward);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 2);
	EXPECT_FALSE(attackerSideHero->hasStackAtSlot(SlotID(1)));
}

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

TEST_F(NewHorizonsCapabilityStateTest, ArmyUsageDoesNotScaleSharedMovementOrDeleteTroops)
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
		EXPECT_EQ(overloaded->getMovePointsLimitLand(), normal->getMovePointsLimitLand());
		EXPECT_EQ(overloaded->getMovePointsLimitWater(), normal->getMovePointsLimitWater());
		EXPECT_EQ(overloaded->getMovePointsLimitAir(), normal->getMovePointsLimitAir());
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

TEST_F(NewHorizonsCapabilityStateTest, AdventureMovementIgnoresCreatureInitiative)
{
	startGame();
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
	const auto slowStack = attackerSideHero->getTurnInfo(0);
	const int slowInitiative = attackerSideHero->getLowestCreatureSpeed();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:angel"), 1));
	const auto fastStack = attackerSideHero->getTurnInfo(0);
	const int fastInitiative = attackerSideHero->getLowestCreatureSpeed();

	EXPECT_NE(slowInitiative, fastInitiative);
	EXPECT_TRUE(slowStack->usesNewHorizonsMovement());
	EXPECT_EQ(slowStack->getMovePointsLimitLand(), 200);
	EXPECT_EQ(fastStack->getMovePointsLimitLand(), 200);
	EXPECT_EQ(slowStack->getMovePointsLimitWater(), fastStack->getMovePointsLimitWater());
}

TEST_F(NewHorizonsCapabilityStateTest, LegacyMovementRemainsSpeedDrivenWithoutCapabilityRules)
{
	capabilitiesEnabled = false;
	startGame();
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
	const auto slowStack = attackerSideHero->getTurnInfo(0);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:angel"), 1));
	const auto fastStack = attackerSideHero->getTurnInfo(0);

	EXPECT_FALSE(slowStack->usesNewHorizonsMovement());
	EXPECT_NE(slowStack->getMovePointsLimitLand(), fastStack->getMovePointsLimitLand());
}

TEST_F(NewHorizonsCapabilityStateTest, LogisticsModifiesHeroMovementPercentNotArmySpeed)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LOGISTICS, 1, ChangeValueMode::ABSOLUTE);
	const auto basic = attackerSideHero->getTurnInfo(0);
	attackerSideHero->setSecSkillLevel(SecondarySkill::LOGISTICS, 3, ChangeValueMode::ABSOLUTE);
	const auto expert = attackerSideHero->getTurnInfo(0);

	EXPECT_EQ(basic->getMovePointsLimitLand(), 220);
	EXPECT_EQ(expert->getMovePointsLimitLand(), 260);
	EXPECT_EQ(basic->getMovePointsLimitWater(), 220);
	EXPECT_EQ(expert->getMovePointsLimitWater(), 260);
}

TEST_F(NewHorizonsCapabilityStateTest, MovementModifiersKeepBonusListSourceBoundsAndDaySemantics)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LOGISTICS, 0, ChangeValueMode::ABSOLUTE);
	const auto landSubtype = BonusSubtypeID(BonusCustomSubtype::heroMovementLand);

	// The source and target-type modifiers are part of BonusList's ordinary
	// value pipeline: 10% * (1 + 50% + 10%) = 16%, then apply base/additive
	// values before the final percentage-to-all stage.
	auto sourcePercent = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 10, BonusSourceID(), landSubtype, BonusValueType::PERCENT_TO_BASE);
	attackerSideHero->addNewBonus(sourcePercent);
	auto percentToSource = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 50, BonusSourceID(), landSubtype, BonusValueType::PERCENT_TO_SOURCE);
	attackerSideHero->addNewBonus(percentToSource);
	auto percentToTarget = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_COMMAND, 10, BonusSourceID(), landSubtype, BonusValueType::PERCENT_TO_TARGET_TYPE);
	percentToTarget->targetSourceType = BonusSource::OTHER;
	attackerSideHero->addNewBonus(percentToTarget);
	auto flat = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 3, BonusSourceID(), landSubtype, BonusValueType::ADDITIVE_VALUE);
	attackerSideHero->addNewBonus(flat);
	auto base = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 5, BonusSourceID(), landSubtype, BonusValueType::BASE_NUMBER);
	attackerSideHero->addNewBonus(base);
	auto percentToAll = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 10, BonusSourceID(), landSubtype, BonusValueType::PERCENT_TO_ALL);
	attackerSideHero->addNewBonus(percentToAll);

	// Independent bounds surround the additive percentage+flat result.  The
	// values use a source not affected by the modifiers above, so the expected
	// lower/upper bounds remain explicit and easy to audit.
	auto minimum = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 250, BonusSourceID(), landSubtype, BonusValueType::INDEPENDENT_MAX);
	attackerSideHero->addNewBonus(minimum);
	auto maximum = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 270, BonusSourceID(), landSubtype, BonusValueType::INDEPENDENT_MIN);
	attackerSideHero->addNewBonus(maximum);

	EXPECT_EQ(attackerSideHero->getTurnInfo(0)->getMovePointsLimitLand(), 264);

	// ONE_DAY is active for today's refresh but intentionally absent from a
	// projected next-day TurnInfo selected with Selector::days(1).
	auto todayOnly = std::make_shared<Bonus>(BonusDuration::ONE_DAY, BonusType::MOVEMENT,
		BonusSource::HERO_BASE_SKILL, 30, BonusSourceID(), landSubtype, BonusValueType::PERCENT_TO_BASE);
	attackerSideHero->addNewBonus(todayOnly);
	EXPECT_EQ(attackerSideHero->getTurnInfo(0)->getMovePointsLimitLand(), 270);
	EXPECT_EQ(attackerSideHero->getTurnInfo(1)->getMovePointsLimitLand(), 264);

	// Remove the independent bounds to expose the timed bonus directly:
	// today's pool is 332, while the next day retains the permanent 264 result.
	attackerSideHero->removeBonus(minimum);
	attackerSideHero->removeBonus(maximum);
	EXPECT_EQ(attackerSideHero->getTurnInfo(0)->getMovePointsLimitLand(), 332);
	EXPECT_EQ(attackerSideHero->getTurnInfo(1)->getMovePointsLimitLand(), 264);
}

TEST_F(NewHorizonsCapabilityStateTest, IndependentOnlyMovementBoundsUseBonusListFallback)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::LOGISTICS, 0, ChangeValueMode::ABSOLUTE);
	const auto landSubtype = BonusSubtypeID(BonusCustomSubtype::heroMovementLand);

	auto upperOnly = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 300, BonusSourceID(), landSubtype, BonusValueType::INDEPENDENT_MIN);
	attackerSideHero->addNewBonus(upperOnly);
	EXPECT_EQ(attackerSideHero->getTurnInfo(0)->getMovePointsLimitLand(), 300);
	attackerSideHero->removeBonus(upperOnly);

	auto lowerOnly = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 100, BonusSourceID(), landSubtype, BonusValueType::INDEPENDENT_MAX);
	attackerSideHero->addNewBonus(lowerOnly);
	EXPECT_EQ(attackerSideHero->getTurnInfo(0)->getMovePointsLimitLand(), 100);
}

TEST_F(NewHorizonsCapabilityStateTest, PathfinderUsesNativeMixedDesertRoadAndDiagonalCosts)
{
	startGame();
	attackerSideHero->clearSlots();

	const auto source = attackerSideHero->visitablePos();
	const auto orthogonal = source + int3(1, 0, 0);
	const auto diagonal = source + int3(1, 1, 0);
	const auto movementCost = [&](TerrainId sourceTerrain, TerrainId destinationTerrain,
		bool road, bool diagonalStep)
	{
		TerrainTile sourceTile;
		TerrainTile destinationTile;
		sourceTile.terrainType = sourceTerrain;
		destinationTile.terrainType = destinationTerrain;
		if(road)
		{
			sourceTile.roadType = RoadId::DIRT_ROAD;
			destinationTile.roadType = RoadId::DIRT_ROAD;
		}
		CPathfinderHelper helper(*gameState(), attackerSideHero, PathfinderOptions(*gameState()));
		return helper.getMovementCost(source, diagonalStep ? diagonal : orthogonal,
			sourceTerrain == ETerrainId::WATER ? EPathfindingLayer::SAIL : EPathfindingLayer::LAND,
			1000, false, &sourceTile, &destinationTile);
	};

	// Castle is native to grass through the hero faction.
	EXPECT_EQ(movementCost(ETerrainId::GRASS, ETerrainId::GRASS, false, false), 10);
	EXPECT_EQ(movementCost(ETerrainId::GRASS, ETerrainId::GRASS, true, false), 7);
	EXPECT_EQ(movementCost(ETerrainId::SAND, ETerrainId::SAND, false, false), 18);
	EXPECT_EQ(movementCost(ETerrainId::SAND, ETerrainId::SAND, true, false), 13);
	EXPECT_EQ(movementCost(ETerrainId::SAND, ETerrainId::SAND, true, true), 17);
	EXPECT_EQ(movementCost(ETerrainId::WATER, ETerrainId::WATER, false, false), 10);

	// CPathfinderHelper and the legacy executor convention use the source tile
	// for terrain/road cost.  Keep that convention explicit when a route
	// crosses a terrain boundary rather than silently switching to destination.
	EXPECT_EQ(movementCost(ETerrainId::SAND, ETerrainId::GRASS, false, false), 18);
	EXPECT_EQ(movementCost(ETerrainId::GRASS, ETerrainId::SAND, false, false), 10);

	// Fortress gnolls make the whole army native to swamp; adding a Castle
	// pikeman makes it mixed and restores the non-native multiplier.
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:gnoll"), 1));
	EXPECT_EQ(movementCost(ETerrainId::SWAMP, ETerrainId::SWAMP, false, false), 10);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creatureByName("core:pikeman"), 1));
	EXPECT_EQ(movementCost(ETerrainId::SWAMP, ETerrainId::SWAMP, false, false), 14);
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
	EXPECT_EQ(attackerSideHero->movementPointsLimit(), 200);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 200);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 4000);
	attackerSideHero->setSecSkillLevel(SecondarySkill::LEADERSHIP, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->capacity, 2600);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 200);
	gameHandler->onNewTurn();
	EXPECT_EQ(gameState()->day, day + 2);
	EXPECT_EQ(attackerSideHero->movementPointsLimit(), 200);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 200);
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
	EXPECT_EQ(projection->getMovePointsLimitLand(), actual->getMovePointsLimitLand());
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity(*defenderSideHero)->used, 4000);
	EXPECT_FALSE(actual->hasNoTerrainPenalty(ETerrainId::SWAMP));
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:gnoll"), 1));
	const auto projectedNative = attackerSideHero->getTurnInfo(0, defenderSideHero);
	EXPECT_TRUE(projectedNative->hasNoTerrainPenalty(ETerrainId::SWAMP));
	EXPECT_EQ(projectedNative->getMovePointsLimitLand(), actual->getMovePointsLimitLand());
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:pikeman"), 1));
	const auto projectedMixed = attackerSideHero->getTurnInfo(0, defenderSideHero);
	EXPECT_FALSE(projectedMixed->hasNoTerrainPenalty(ETerrainId::SWAMP));
	EXPECT_EQ(projectedMixed->getMovePointsLimitLand(), actual->getMovePointsLimitLand());
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 4000));
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

TEST_F(NewHorizonsHeroGrowthTest, SkillRanksDoNotAlterDeterministicClassGrowth)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 3, ChangeValueMode::ABSOLUTE);
	const auto view = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_TRUE(view->extraGrowth.empty());
	EXPECT_EQ(gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero), (std::array<int, 4>{4, 4, 1, 1}));
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero), (std::array<int, 4>{4, 4, 1, 1}));
}

TEST_F(NewHorizonsHeroGrowthTest, SerializedHeroRandomizerKeepsDeterministicClassVector)
{
	startGame();
	attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER, 3, ChangeValueMode::ABSOLUTE);
	gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero);
	CMemorySerializer memory;
	memory.oser & *gameHandler->randomizer;
	GameRandomizer restored(*gameState());
	memory.iser & restored;
	for(int i = 0; i < 32; ++i)
	{
		const auto expected = gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero);
		EXPECT_EQ(restored.rollPrimarySkillsForLevelup(attackerSideHero), expected);
		EXPECT_EQ(expected, (std::array<int, 4>{4, 4, 1, 1}));
	}
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
	action.aimToHex(BattleHex::INVALID);
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

TEST_F(NewHorizonsHeroGrowthTest, EstatesRanksGenerateCanonicalDailyGold)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	startGame();
	const int decoded = SecondarySkill::decode("new-horizons:estates");
	ASSERT_GE(decoded, 0);
	const SecondarySkill estates(decoded);
	attackerSideHero->setSecSkillLevel(estates, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const int baseline = attackerSideHero->dailyIncome()[EGameResID::GOLD];
	const std::array expected = {0, 125, 250, 500};
	for(int rank = MasteryLevel::BASIC; rank <= MasteryLevel::EXPERT; ++rank)
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(estates, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(attackerSideHero->dailyIncome()[EGameResID::GOLD] - baseline, expected[rank]);
	}
}

TEST_F(NewHorizonsHeroGrowthTest, LearningRanksApplyCanonicalExperienceGain)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	startGame();
	const int decoded = SecondarySkill::decode("new-horizons:learning");
	ASSERT_GE(decoded, 0);
	const SecondarySkill learning(decoded);
	attackerSideHero->setSecSkillLevel(learning, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto baseline = attackerSideHero->calculateXp(1000);
	const std::array expected = {0, 100, 200, 300};
	for(int rank = MasteryLevel::BASIC; rank <= MasteryLevel::EXPERT; ++rank)
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(learning, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(attackerSideHero->calculateXp(1000) - baseline, expected[rank]);
	}
}

TEST_F(NewHorizonsHeroGrowthTest, LuckRanksProvideOrdinaryLuckWithoutSylvanStrikeDamage)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	startGame();
	const int decoded = SecondarySkill::decode("new-horizons:luck");
	ASSERT_GE(decoded, 0);
	const SecondarySkill luck(decoded);
	attackerSideHero->setSecSkillLevel(luck, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const int baselineLuck = attackerSideHero->valOfBonuses(BonusType::LUCK);
	const int baselineStrikeDamage = attackerSideHero->valOfBonuses(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE);
	const std::array expected = {0, 1, 2, 3};
	for(int rank = MasteryLevel::BASIC; rank <= MasteryLevel::EXPERT; ++rank)
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(luck, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LUCK) - baselineLuck, expected[rank]);
		EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE), baselineStrikeDamage);
	}
}

TEST_F(NewHorizonsHeroGrowthTest, WisdomDiscountsOrdinaryListedCostAfterMassMultiplierOnly)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	prepareCommands();
	const int decoded = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(decoded, 0);
	const SecondarySkill wisdom(decoded);
	const auto * ordinary = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto * adventure = SpellID(SpellID::DIMENSION_DOOR).toSpell();
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(adventure, nullptr);
	ASSERT_FALSE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), ordinary->getId()));
	ASSERT_TRUE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), adventure->getId()));
	const int listed = attackerSideHero->getListedSpellCost(ordinary);
	const int adventureListed = attackerSideHero->getListedSpellCost(adventure);
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(wisdom, rank, ChangeValueMode::ABSOLUTE);
		const int expected = rank == MasteryLevel::NONE ? listed
			: newHorizonsMagic::wisdomAdjustedCost(listed, 1, rank);
		EXPECT_EQ(attackerSideHero->getSpellCost(ordinary), expected);
		EXPECT_EQ(attackerSideHero->getSpellCost(adventure), adventureListed);
		const int expectedMass = rank == MasteryLevel::NONE ? listed * 3
			: newHorizonsMagic::wisdomAdjustedCost(listed, 3, rank);
		EXPECT_EQ(battle()->battleGetSpellCost(ordinary, attackerSideHero, 3), expectedMass);
	}
}

TEST_F(NewHorizonsHeroGrowthTest, WisdomAndAlliedMageReductionApplyInBattleOrder)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	prepareCommands();
	const int wisdomID = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomID, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(wisdomID), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto * ordinary = SpellID(SpellID::MAGIC_ARROW).toSpell();
	auto * mage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:mage")), BattleHex(70), 1);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(mage, nullptr);
	const int listed = attackerSideHero->getListedSpellCost(ordinary);
	const int reduction = mage->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ALLY);
	ASSERT_GT(reduction, 0);
	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(listed, 3, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(ordinary, attackerSideHero, 3), wisdomCost - reduction);
}

TEST_F(NewHorizonsHeroGrowthTest, AlliedMageReductionsUseTheStrongestUnitOnly)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	prepareCommands();
	const auto * ordinary = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	auto * mage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:mage")), BattleHex(70), 1);
	auto * archMage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:archMage")), BattleHex(80), 1);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(mage, nullptr);
	ASSERT_NE(archMage, nullptr);
	archMage->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 3, BonusSourceID()));
	const int mageReduction = mage->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ALLY);
	const int archMageReduction = archMage->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ALLY);
	ASSERT_GT(mageReduction, 0);
	ASSERT_GT(archMageReduction, mageReduction);
	const int listed = attackerSideHero->getListedSpellCost(ordinary);
	EXPECT_EQ(battle()->battleGetSpellCost(ordinary, attackerSideHero), listed - archMageReduction);
}

TEST_F(NewHorizonsHeroGrowthTest, OrdinarySpellCostNeverFallsBelowOneMana)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	prepareCommands();
	const auto * ordinary = SpellID(SpellID::MAGIC_ARROW).toSpell();
	auto * mage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:mage")), BattleHex(70), 1);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(mage, nullptr);
	mage->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetSpellCost(ordinary, attackerSideHero), 1);
}

TEST_F(NewHorizonsHeroGrowthTest, FreeCreatureAbilityRetainsZeroManaCost)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	prepareCommands();
	const auto * ability = SpellID(SpellID::STONE_GAZE).toSpell();
	auto * mage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:mage")), BattleHex(70), 1);
	ASSERT_NE(ability, nullptr);
	ASSERT_NE(mage, nullptr);
	ASSERT_FALSE(ability->isCommonHeroSpell());
	mage->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetSpellCost(ability, attackerSideHero), 0);
}

TEST_F(LegacySpellCostTest, OrdinarySpellCostCanStillReachZero)
{
	prepareCommands();
	const auto * ordinary = SpellID(SpellID::MAGIC_ARROW).toSpell();
	auto * mage = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:mage")), BattleHex(70), 1);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(mage, nullptr);
	mage->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_FALSE(newHorizonsMagic::rulesActive(attackerSideHero->getMagicRules()));
	EXPECT_EQ(battle()->battleGetSpellCost(ordinary, attackerSideHero), 0);
}
