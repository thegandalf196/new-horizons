/*
 * NewHorizonsBloodObeliskTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../battles/BattleTestFixture.h"
#include "../../mock/TinyH3MBuilder.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/CBonusSystemNode.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr int3 VISITOR_TOWN_POS{18, 18, 0};
constexpr int3 SECOND_VISITOR_TOWN_POS{27, 18, 0};
constexpr int3 SIEGE_TOWN_POS{18, 27, 0};
constexpr int3 VISITOR_HERO_POS{5, 5, 0};
constexpr int3 SECOND_VISITOR_HERO_POS{7, 7, 0};
constexpr int3 DEFENDER_HERO_POS{9, 9, 0};
constexpr int3 OUTSIDE_HERO_POS{11, 11, 0};

BonusSourceID obeliskSource(const CGTownInstance & town)
{
	const auto & building = town.getTown()->buildings.at(BuildingID::SPECIAL_2);
	return BonusSourceID(building->getUniqueTypeID());
}

std::vector<const Bonus *> bonusesFrom(const CBonusSystemNode & bearer, const BonusSourceID & source, BonusType type)
{
	std::vector<const Bonus *> result;
	const auto bonuses = bearer.getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::type()(type)));
	for(const auto & bonus : *bonuses)
	{
		if(bonus)
			result.push_back(bonus.get());
	}
	return result;
}

std::vector<const Bonus *> oneBattleDamageBonuses(const CGHeroInstance & hero, const BonusSourceID & source)
{
	auto result = bonusesFrom(hero, source, BonusType::PERCENTAGE_DAMAGE_BOOST);
	std::erase_if(result, [](const Bonus * bonus)
	{
		return !Bonus::OneBattle(bonus);
	});
	return result;
}

std::vector<const Bonus *> siegeAttackBonuses(const CGHeroInstance & hero, const BonusSourceID & source)
{
	auto result = bonusesFrom(hero, source, BonusType::PRIMARY_SKILL);
	std::erase_if(result, [](const Bonus * bonus)
	{
		return bonus->subtype != BonusSubtypeID(PrimarySkill::ATTACK)
			|| !bonus->stacking.starts_with("townDefendingHero:");
	});
	return result;
}

class NewHorizonsBloodObeliskTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	void startScenario()
	{
		const CreatureID archer(CreatureID::decode("core:archer"));
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		const CreatureID gnoll(CreatureID::decode("core:gnoll"));
		const auto fortress = FactionID(FactionID::decode("core:fortress"));
		ASSERT_GE(archer.getNum(), 0);
		ASSERT_GE(pikeman.getNum(), 0);
		ASSERT_GE(gnoll.getNum(), 0);
		ASSERT_TRUE(fortress.isValid());

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsBloodObelisk")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero(VISITOR_HERO_POS, HeroTypeID(0), PlayerColor(0)).heroGarrison({{archer, 10}})
			.hero(SECOND_VISITOR_HERO_POS, HeroTypeID(1), PlayerColor(0)).heroGarrison({{pikeman, 2}})
			.hero(DEFENDER_HERO_POS, HeroTypeID(2), PlayerColor(1)).heroGarrison({{pikeman, 10}})
			.hero(OUTSIDE_HERO_POS, HeroTypeID(3), PlayerColor(1)).heroGarrison({{gnoll, 2}})
			.town(VISITOR_TOWN_POS, fortress, PlayerColor(0)).townGarrison({})
			.town(SECOND_VISITOR_TOWN_POS, fortress, PlayerColor(0)).townGarrison({})
			.town(SIEGE_TOWN_POS, fortress, PlayerColor(1)).townGarrison({{gnoll, 10}});
		startWithMap(std::move(builder));

		attackerSideHero = findHeroAt(VISITOR_HERO_POS);
		defenderSideHero = findHeroAt(DEFENDER_HERO_POS);
		visitor2 = findHeroAt(SECOND_VISITOR_HERO_POS);
		outsideHero = findHeroAt(OUTSIDE_HERO_POS);
		visitorTown = expectAt<CGTownInstance>(VISITOR_TOWN_POS);
		secondVisitorTown = expectAt<CGTownInstance>(SECOND_VISITOR_TOWN_POS);
		siegeTown = expectAt<CGTownInstance>(SIEGE_TOWN_POS);
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(visitor2, nullptr);
		ASSERT_NE(outsideHero, nullptr);
		ASSERT_NE(visitorTown, nullptr);
		ASSERT_NE(secondVisitorTown, nullptr);
		ASSERT_NE(siegeTown, nullptr);

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void buildObelisks()
	{
		for(auto * town : {visitorTown, secondVisitorTown, siegeTown})
		{
			ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::SPECIAL_2, true));
			ASSERT_TRUE(town->hasBuilt(BuildingID::SPECIAL_2));
		}
		if(!siegeTown->hasBuilt(BuildingID::FORT))
		{
			ASSERT_TRUE(gameHandler->buildStructure(siegeTown->id, BuildingID::FORT, true));
		}
		ASSERT_TRUE(siegeTown->hasBuilt(BuildingID::FORT));
	}

	void visit(CGTownInstance * town, CGHeroInstance * hero)
	{
		town->setVisitingHero(hero);
		gameHandler->heroVisitCastle(town, hero);
		town->setVisitingHero(nullptr);
	}

	void startSiege(bool withDefenderHero)
	{
		BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero,
			withDefenderHero ? defenderSideHero : nullptr};
		BattleSideArray<const CArmedInstance *> armies{attackerSideHero,
			withDefenderHero ? static_cast<const CArmedInstance *>(defenderSideHero)
				: static_cast<const CArmedInstance *>(siegeTown)};
		const int3 tile(4, 4, 0);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		const std::string battlefieldName = "core:sand_shore";
		const auto battlefieldIdentifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName);
		ASSERT_TRUE(battlefieldIdentifier.has_value());
		const BattleField battlefield(*battlefieldIdentifier);
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), armies[BattleSide::ATTACKER],
			armies[BattleSide::DEFENDER]);

		BattleStart start;
		start.battleID = gameState()->nextBattleID;
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield,
			armies, heroes, layout, siegeTown);
		// Isolate the building from the independent native-terrain +1 Attack.
		start.info->removeBonuses(Selector::sourceType()(BonusSource::TERRAIN_NATIVE));
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->tacticDistance = 0;
		battle()->obstacles.clear();
	}

	CGHeroInstance * visitor2 = nullptr;
	CGHeroInstance * outsideHero = nullptr;
	CGTownInstance * visitorTown = nullptr;
	CGTownInstance * secondVisitorTown = nullptr;
	CGTownInstance * siegeTown = nullptr;
};
}

TEST_F(NewHorizonsBloodObeliskTest, AuthoritativeWeeklyVisitsArePerHeroAndPerPhysicalObeliskAndPersist)
{
	startScenario();
	buildObelisks();
	const auto source = obeliskSource(*visitorTown);
	ASSERT_EQ(source, obeliskSource(*secondVisitorTown));

	auto * rewardable = visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2).get();
	ASSERT_NE(rewardable, nullptr);
	ASSERT_EQ(rewardable->configuration.visitMode, Rewardable::VISIT_HERO);
	ASSERT_EQ(rewardable->configuration.resetParameters.weeks, 1);
	ASSERT_TRUE(rewardable->configuration.resetParameters.visitors);
	const auto firstVisit = std::find_if(rewardable->configuration.info.begin(), rewardable->configuration.info.end(),
		[](const Rewardable::VisitInfo & info)
		{
			return info.visitType == Rewardable::EEventType::EVENT_FIRST_VISIT && !info.reward.heroBonuses.empty();
		});
	ASSERT_NE(firstVisit, rewardable->configuration.info.end());
	ASSERT_EQ(firstVisit->reward.heroBonuses.size(), 2u);
	for(const auto & bonus : firstVisit->reward.heroBonuses)
	{
		ASSERT_NE(bonus, nullptr);
		EXPECT_EQ(bonus->type, BonusType::PERCENTAGE_DAMAGE_BOOST);
		EXPECT_EQ(bonus->val, 10);
		EXPECT_TRUE(Bonus::OneBattle(bonus.get()));
	}
	EXPECT_TRUE(std::ranges::any_of(firstVisit->reward.heroBonuses, [](const auto & bonus)
	{
		return bonus->subtype == BonusCustomSubtype::damageTypeMelee;
	}));
	EXPECT_TRUE(std::ranges::any_of(firstVisit->reward.heroBonuses, [](const auto & bonus)
	{
		return bonus->subtype == BonusCustomSubtype::damageTypeRanged;
	}));

	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	visit(visitorTown, attackerSideHero);
	ASSERT_EQ(oneBattleDamageBonuses(*attackerSideHero, source).size(), 2u);
	EXPECT_TRUE(visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(attackerSideHero));

	// One hero cannot collect twice from one physical Obelisk in the same week.
	visit(visitorTown, attackerSideHero);
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, source).size(), 2u);

	// A second physical Obelisk grants independently to the same hero.
	visit(secondVisitorTown, attackerSideHero);
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, source).size(), 4u);
	EXPECT_TRUE(secondVisitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(attackerSideHero));

	// The first physical Obelisk also tracks a different hero independently.
	visit(visitorTown, visitor2);
	EXPECT_EQ(oneBattleDamageBonuses(*visitor2, source).size(), 2u);
	EXPECT_TRUE(visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor2));
	EXPECT_TRUE(oneBattleDamageBonuses(*defenderSideHero, obeliskSource(*siegeTown)).empty());
	EXPECT_TRUE(oneBattleDamageBonuses(*outsideHero, source).empty());

	const auto visitorID = attackerSideHero->id;
	const auto secondVisitorID = visitor2->id;
	const auto visitorTownID = visitorTown->id;
	const auto secondTownID = secondVisitorTown->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredVisitor = restored.getHero(visitorID);
	const auto * restoredSecondVisitor = restored.getHero(secondVisitorID);
	auto * restoredVisitorTown = restored.getTown(visitorTownID);
	auto * restoredSecondTown = restored.getTown(secondTownID);
	ASSERT_NE(restoredVisitor, nullptr);
	ASSERT_NE(restoredSecondVisitor, nullptr);
	ASSERT_NE(restoredVisitorTown, nullptr);
	ASSERT_NE(restoredSecondTown, nullptr);
	EXPECT_EQ(oneBattleDamageBonuses(*restoredVisitor, source).size(), 4u);
	EXPECT_EQ(oneBattleDamageBonuses(*restoredSecondVisitor, source).size(), 2u);
	EXPECT_TRUE(restoredVisitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredVisitor));
	EXPECT_TRUE(restoredVisitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredSecondVisitor));
	EXPECT_TRUE(restoredSecondTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredVisitor));

	BattleResultAccepted acceptedResult;
	acceptedResult.battleID = BattleID(0);
	acceptedResult.heroResult[BattleSide::ATTACKER].heroID = visitorID;
	acceptedResult.heroResult[BattleSide::DEFENDER].heroID = defenderSideHero->id;
	acceptedResult.winnerSide = BattleSide::ATTACKER;
	gameHandler->sendAndApply(acceptedResult);
	EXPECT_TRUE(oneBattleDamageBonuses(*attackerSideHero, source).empty());
	EXPECT_EQ(oneBattleDamageBonuses(*visitor2, source).size(), 2u);
	visit(visitorTown, attackerSideHero);
	EXPECT_TRUE(oneBattleDamageBonuses(*attackerSideHero, source).empty())
		<< "Consuming the blessing does not reopen this week's entitlement";

	// Real day events advance the ordinary weekly visit ledger; the same hero can
	// collect again from the same physical Obelisk after its weekly reset.
	const auto calendar = gameState()->getCalendar();
	const auto resetDuration = visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)
		->configuration.getResetDuration(calendar);
	ASSERT_GT(resetDuration, 0);
	int nextResetDay = calendar.getCurrentDay() + 1;
	while(nextResetDay <= 1 || (nextResetDay - 1) % resetDuration != 0)
		++nextResetDay;
	for(int day = calendar.getCurrentDay(); day < nextResetDay; ++day)
		gameHandler->onNewTurn();
	EXPECT_FALSE(visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(attackerSideHero));
	EXPECT_FALSE(visitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor2));
	EXPECT_FALSE(secondVisitorTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(attackerSideHero));
	visit(visitorTown, attackerSideHero);
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, source).size(), 2u);
}

TEST_F(NewHorizonsBloodObeliskTest, SiegeAttackIsDefenderOnlyAndScopedCleanupPreservesVisitBlessing)
{
	startScenario();
	buildObelisks();
	const auto visitingSource = obeliskSource(*visitorTown);
	const auto defendingSource = obeliskSource(*siegeTown);
	visit(visitorTown, attackerSideHero);
	ASSERT_EQ(oneBattleDamageBonuses(*attackerSideHero, visitingSource).size(), 2u);
	const auto visitorAttack = attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const auto defenderAttack = defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const auto outsideAttack = outsideHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	JsonNode attackCoefficientFormula;
	attackCoefficientFormula["base"].Float() = 0;
	attackCoefficientFormula["attack"].Float() = 1;
	attackCoefficientFormula["defense"].Float() = 0;
	const auto defenderCommandCoefficient = heroCommands::coefficient(attackCoefficientFormula, *defenderSideHero);

	startSiege(true);
	ASSERT_NE(battle()->getSideHero(BattleSide::DEFENDER), nullptr);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), defenderAttack + 20);
	EXPECT_EQ(heroCommands::coefficient(attackCoefficientFormula, *defenderSideHero), defenderCommandCoefficient + 20);
	const auto defendingStacks = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER;
	});
	ASSERT_EQ(defendingStacks.size(), 1u);
	EXPECT_EQ(defendingStacks.front()->getAttack(false), defendingStacks.front()->unitType()->getBaseAttack())
		<< "The siege-only Hero Attack bonus must not be added to creature Attack";
	const auto attackBonuses = siegeAttackBonuses(*defenderSideHero, defendingSource);
	ASSERT_EQ(attackBonuses.size(), 1u);
	EXPECT_EQ(attackBonuses.front()->val, 20);
	EXPECT_TRUE(Bonus::OneBattle(attackBonuses.front()));
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), visitorAttack);
	EXPECT_EQ(outsideHero->getPrimSkillLevel(PrimarySkill::ATTACK), outsideAttack);
	EXPECT_TRUE(siegeAttackBonuses(*attackerSideHero, defendingSource).empty());
	EXPECT_TRUE(siegeAttackBonuses(*outsideHero, defendingSource).empty());
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, visitingSource).size(), 2u);

	BattleCancelled cancelled;
	cancelled.battleID = battle()->battleID;
	gameHandler->sendAndApply(cancelled);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), defenderAttack);
	EXPECT_EQ(heroCommands::coefficient(attackCoefficientFormula, *defenderSideHero), defenderCommandCoefficient);
	EXPECT_TRUE(siegeAttackBonuses(*defenderSideHero, defendingSource).empty());
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, visitingSource).size(), 2u)
		<< "Cancelling a siege must not consume the separate weekly visit blessing";

	// The result-cleanup visitor also strips only the scoped siege grant. A
	// successful result acceptance, which normally consumes ONE_BATTLE buffs,
	// is intentionally not modeled as restoration cleanup here.
	startSiege(true);
	ASSERT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), defenderAttack + 20);
	BattleResultsApplied applied;
	applied.battleID = battle()->battleID;
	gameHandler->sendAndApply(applied);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), defenderAttack);
	EXPECT_EQ(heroCommands::coefficient(attackCoefficientFormula, *defenderSideHero), defenderCommandCoefficient);
	EXPECT_TRUE(siegeAttackBonuses(*defenderSideHero, defendingSource).empty());
	EXPECT_EQ(oneBattleDamageBonuses(*attackerSideHero, visitingSource).size(), 2u);
	BattleEnded ended;
	ended.battleID = applied.battleID;
	gameHandler->sendAndApply(ended);

	// A fortified town fight without a defending hero must not leak the building
	// bonus onto the attacking hero or the town's creatures.
	startSiege(false);
	EXPECT_EQ(battle()->getSideHero(BattleSide::DEFENDER), nullptr);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), visitorAttack);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), defenderAttack);
	EXPECT_EQ(outsideHero->getPrimSkillLevel(PrimarySkill::ATTACK), outsideAttack);
	EXPECT_TRUE(siegeAttackBonuses(*attackerSideHero, defendingSource).empty());
	BattleCancelled noHeroCancelled;
	noHeroCancelled.battleID = battle()->battleID;
	gameHandler->sendAndApply(noHeroCancelled);
}
