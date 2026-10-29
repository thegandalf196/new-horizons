/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsLivingInferno.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/battles/NewHorizonsLivingInferno.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
class NewHorizonsLivingInfernoBattleTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool fiur = true;
	CStack * source = nullptr;
	CStack * target = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(captured)
		{
			rules["livingInfernoSpecialty"]["version"].Integer() = 1;
			rules["livingInfernoSpecialty"]["damagePerCreature"].Integer() = 1;
			rules["livingInfernoSpecialty"]["durationRounds"].Integer() = 1;
		}
		else
			rules.Struct().erase("livingInfernoSpecialty");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(fiur ? "core:fiur" : "core:luna")));
	}
	void prepare(bool energy = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, creatureByName(energy ? "core:energyElemental" : "core:fireElemental"), BattleHex(5, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 8), 100);
		for(auto * unit : {source, target})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
		battle()->activeStack = source->unitId();
	}
	const SpellCreatedObstacle * onlyWall() const
	{
		const auto obstacles = battle()->battleGetAllObstacles();
		return obstacles.size() == 1 ? dynamic_cast<const SpellCreatedObstacle *>(obstacles.front().get()) : nullptr;
	}
};
TEST_F(NewHorizonsLivingInfernoBattleTest, ActualGroundMovementCreatesOneVacatedFootprintWall)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto start = source->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(source, BattleHex(8, 5))));
	const auto * wall = onlyWall();
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->ID, SpellID::FIRE_WALL);
	EXPECT_EQ(wall->minimalDamage, 10);
	EXPECT_TRUE(wall->damageSnapshot);
	EXPECT_TRUE(wall->renderAtEachHex);
	EXPECT_EQ(wall->turnsRemaining, 1);
	EXPECT_TRUE(wall->customSize.contains(start));
	for(const auto hex : source->getHexes())
		EXPECT_FALSE(wall->customSize.contains(hex));
	EXPECT_EQ(wall->getRenderTiles(), wall->customSize);
}
TEST_F(NewHorizonsLivingInfernoBattleTest, FlyingEnergyElementalBurnsIntermediateTrajectory)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(source->hasBonusOfType(BonusType::FLYING));
	const auto start = source->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(source, BattleHex(8, 7))));
	const auto * wall = onlyWall();
	ASSERT_NE(wall, nullptr);
	EXPECT_GT(wall->customSize.size(), 1);
	EXPECT_TRUE(wall->customSize.contains(start));
	for(const auto hex : source->getHexes())
		EXPECT_FALSE(wall->customSize.contains(hex));
}
TEST_F(NewHorizonsLivingInfernoBattleTest, HistoricalAbsentPolicyRetainsNoTrail)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(source, BattleHex(8, 5))));
	EXPECT_TRUE(battle()->battleGetAllObstacles().empty());
}
TEST_F(NewHorizonsLivingInfernoBattleTest, WrongHeroAndForcedDisplacementDoNotCreateTrail)
{
	fiur = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsLivingInferno::capture(*battle(), source, true).damage, 0);
	EXPECT_EQ(newHorizonsLivingInferno::capture(*battle(), source, false).damage, 0);
	battle()->moveUnit(source->unitId(), BattleHex(8, 5));
	EXPECT_TRUE(battle()->battleGetAllObstacles().empty());
}
TEST_F(NewHorizonsLivingInfernoBattleTest, CountSnapshotOverlapAndBothWideFinalHexesArePreserved)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto trail = newHorizonsLivingInferno::capture(*battle(), source, true);
	trail.doubleWide = true;
	trail.traverse(BattleHex(5, 5), BattleHexArray{BattleHex(6, 5), BattleHex(7, 5)}, false);
	auto first = newHorizonsLivingInferno::obstacle(*battle(), trail, BattleHex(7, 5));
	ASSERT_TRUE(first);
	EXPECT_EQ(first->minimalDamage, 10);
	for(const auto hex : battle::Unit::getHexes(BattleHex(7, 5), true, trail.side))
		EXPECT_FALSE(first->customSize.contains(hex));
	newHorizonsLivingInferno::publish(*gameHandler, *battle(), trail, BattleHex(7, 5));
	EXPECT_FALSE(newHorizonsLivingInferno::obstacle(*battle(), trail, BattleHex(7, 5)));
}
TEST_F(NewHorizonsLivingInfernoBattleTest, ExistingFireWallTriggerHandlesDamageDedupAndFireImmunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto trail = newHorizonsLivingInferno::capture(*battle(), source, true);
	trail.traverse(BattleHex(7, 5), BattleHexArray{BattleHex(8, 5)}, false);
	newHorizonsLivingInferno::publish(*gameHandler, *battle(), trail, BattleHex(8, 5));
	ASSERT_NE(onlyWall(), nullptr);
	battle()->moveUnit(target->unitId(), BattleHex(7, 5));
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *target, {}));
	EXPECT_EQ(before - target->getAvailableHealth(), 10);
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *target, {}));
	EXPECT_EQ(before - target->getAvailableHealth(), 10);
	battle()->moveUnit(target->unitId(), BattleHex(12, 8));
	battle()->moveUnit(source->unitId(), BattleHex(7, 5));
	const auto immuneBefore = source->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *source, {}));
	EXPECT_EQ(source->getAvailableHealth(), immuneBefore);
}

namespace
{
class FiurEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FiurEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}
TEST_F(NewHorizonsLivingInfernoBattleTest, HypotheticalEnergyTrailMatchesServerWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	FiurEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(&environment, callback);
	const auto start = source->getPosition();
	const BattleHex destination(8, 7);
	model.projectVoluntaryMovement(source->unitId(), destination);
	ASSERT_TRUE(battle()->battleGetAllObstacles().empty());
	EXPECT_EQ(source->getPosition(), start);
	const auto projected = model.battleGetAllObstacles();
	ASSERT_EQ(projected.size(), 1);
	const auto * expected = dynamic_cast<const SpellCreatedObstacle *>(projected.front().get());
	ASSERT_NE(expected, nullptr);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(source, destination)));
	const auto * actual = onlyWall();
	ASSERT_NE(actual, nullptr);
	EXPECT_EQ(actual->customSize, expected->customSize);
	EXPECT_EQ(actual->minimalDamage, expected->minimalDamage);
	EXPECT_EQ(actual->turnsRemaining, expected->turnsRemaining);
	EXPECT_EQ(actual->renderAtEachHex, expected->renderAtEachHex);
}
TEST_F(NewHorizonsLivingInfernoBattleTest, FireWallDamageReductionAppliesToCountSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto trail = newHorizonsLivingInferno::capture(*battle(), source, true);
	trail.traverse(BattleHex(7, 5), BattleHexArray{BattleHex(8, 5)}, false);
	newHorizonsLivingInferno::publish(*gameHandler, *battle(), trail, BattleHex(8, 5));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	battle()->moveUnit(target->unitId(), BattleHex(7, 5));
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *target, {}));
	EXPECT_EQ(before - target->getAvailableHealth(), 5);
}
TEST_F(NewHorizonsLivingInfernoBattleTest, FriendlyCreaturesUseSameFireWallTrigger)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto trail = newHorizonsLivingInferno::capture(*battle(), source, true);
	trail.traverse(BattleHex(7, 5), BattleHexArray{BattleHex(8, 5)}, false);
	newHorizonsLivingInferno::publish(*gameHandler, *battle(), trail, BattleHex(8, 5));
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 100);
	const auto before = ally->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *ally, {}));
	EXPECT_EQ(before - ally->getAvailableHealth(), 10);
}

TEST_F(NewHorizonsLivingInfernoBattleTest, AllegianceHeroAndPhysicalFootprintSideStayDistinct)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))));
	EXPECT_EQ(newHorizonsLivingInferno::capture(*battle(), source, true).damage, 0);
	auto * converted = addStack(BattleSide::DEFENDER, creatureByName("core:fireElemental"), BattleHex(10, 5), 10);
	converted->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))));
	const auto captured = newHorizonsLivingInferno::capture(*battle(), converted, true);
	EXPECT_EQ(captured.damage, 10);
	EXPECT_EQ(captured.side, BattleSide::DEFENDER);
	EXPECT_EQ(captured.casterSide, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsLivingInfernoBattleTest, LethalOverwatchKeepsOnlyCommittedTrailAndStartCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:battlecraft"));
	defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.overwatch"});
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), 10000);
	forceMaximumDamage(shooter);
	battle()->moveUnit(source->unitId(), BattleHex(2, 5));
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeWait(shooter)));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	battle()->activeStack = source->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(source, BattleHex(6, 5))));
	ASSERT_FALSE(source->alive());
	ASSERT_EQ(source->getPosition(), BattleHex(4, 5));
	const auto * wall = onlyWall();
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->minimalDamage, 10); // Count captured before the lethal reaction.
	EXPECT_TRUE(wall->customSize.contains(BattleHex(2, 5)));
	EXPECT_TRUE(wall->customSize.contains(BattleHex(3, 5)));
	EXPECT_FALSE(wall->customSize.contains(BattleHex(4, 5))); // Actual final corpse footprint.
	EXPECT_FALSE(wall->customSize.contains(BattleHex(5, 5))); // Never committed.
	EXPECT_FALSE(wall->customSize.contains(BattleHex(6, 5)));
}
