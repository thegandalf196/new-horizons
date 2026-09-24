/*
 * NewHorizonsFireWallTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"

namespace
{
class NewHorizonsFireWallRuntimeTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void prepareFireWall(int32_t spellPower = 43)
	{
		prepareCommands(true);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit());
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
	}

	BattleAction fireWallAction(const BattleHex & start, BattleHex::EDir direction) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::FIRE_WALL;
		action.aimToHex(start);
		action.spellFireWallDirection = direction;
		return action;
	}
};
}

TEST(NewHorizonsFireWallTest, DirectionActionAndActivationStateRequireCanonicalProtocol)
{
	BattleAction action;
	action.side = BattleSide::ATTACKER;
	action.actionType = EActionType::HERO_SPELL;
	action.spell = SpellID::FIRE_WALL;
	action.aimToHex(BattleHex(70));
	action.spellFireWallDirection = BattleHex::RIGHT;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_EQ(restored.target.size(), 1u);
	EXPECT_EQ(restored.target.front().hexValue, BattleHex(70));
	EXPECT_EQ(restored.spellFireWallDirection, BattleHex::RIGHT);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_LAND_MINE;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::FIRE_WALL;
	obstacle.damageSnapshot = true;
	obstacle.minimalDamage = 83;
	obstacle.turnsRemaining = 3;
	obstacle.lastTriggerUnit = 17;
	obstacle.lastTriggerActivation = 9;
	CMemorySerializer obstacleCurrent;
	obstacleCurrent.oser.version = ESerializationVersion::CURRENT;
	obstacleCurrent.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(obstacleCurrent.oser & obstacle);
	SpellCreatedObstacle restoredObstacle;
	ASSERT_NO_THROW(obstacleCurrent.iser & restoredObstacle);
	EXPECT_TRUE(restoredObstacle.damageSnapshot);
	EXPECT_EQ(restoredObstacle.minimalDamage, 83);
	EXPECT_EQ(restoredObstacle.turnsRemaining, 3);
	EXPECT_EQ(restoredObstacle.lastTriggerUnit, 17);
	EXPECT_EQ(restoredObstacle.lastTriggerActivation, 9);

	// Isolate the Fire Wall activation fields for the older-protocol rejection.
	obstacle.damageSnapshot = false;
	CMemorySerializer oldObstacle;
	oldObstacle.oser.version = ESerializationVersion::NEW_HORIZONS_LAND_MINE;
	EXPECT_THROW(oldObstacle.oser & obstacle, std::runtime_error);
	EXPECT_TRUE(oldObstacle.extractBuffer().empty());
}

TEST(NewHorizonsFireWallTest, TriggerProxyUsesTheCastTimeSnapshotExactly)
{
	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::FIRE_WALL;
	obstacle.minimalDamage = 83;
	obstacle.damageSnapshot = true;
	spells::ObstacleCasterProxy proxy(PlayerColor(0), nullptr, obstacle);

	EXPECT_EQ(proxy.getSpellBonus(nullptr, 85, nullptr), 85);
	EXPECT_EQ(proxy.getEffectValue(nullptr), 83);
}

TEST_F(NewHorizonsFireWallRuntimeTest, ServerBuildsThreeHexLineAndRejectsInvalidPlacements)
{
	prepareFireWall();
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get());
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->customSize.size(), 3u);
	EXPECT_TRUE(wall->customSize.contains(BattleHex(70)));
	EXPECT_TRUE(wall->customSize.contains(BattleHex(71)));
	EXPECT_TRUE(wall->customSize.contains(BattleHex(72)));
	EXPECT_EQ(wall->minimalDamage, 83); // 40 + 1.0 * Spell Power
	EXPECT_TRUE(wall->damageSnapshot);
	EXPECT_TRUE(wall->passable);
	EXPECT_EQ(wall->turnsRemaining, 3); // canonical Fire Wall lasts three full rounds

	// Canonical New Horizons Fire Wall is level 3 and costs 12 mana.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
}

TEST_F(NewHorizonsFireWallRuntimeTest, ServerRejectsMissingDirectionBoundaryAndOccupiedLineAtomically)
{
	prepareFireWall();
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::NONE)));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(15, 0), BattleHex::RIGHT)));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 10);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));

	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsFireWallRuntimeTest, GroundFriendAndFoeTriggerOncePerActivationAndFlyingUnitsDoNot)
{
	prepareFireWall();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));
	ASSERT_EQ(battle()->obstacles.size(), 1u);

	auto * foe = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 100);
	const auto foeBefore = foe->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(foeBefore - foe->getAvailableHealth(), 83);
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get());
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->lastTriggerUnit, static_cast<si32>(foe->unitId()));
	EXPECT_EQ(wall->lastTriggerActivation, battle()->getActivationSerial());
	const auto foeAfter = foe->getAvailableHealth();
	// Movement can call the same trigger helper more than once during one
	// activation; the snapshot state prevents duplicate damage.
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(foe->getAvailableHealth(), foeAfter);

	// A fresh activation of the same creature is allowed to trigger again.
	battle()->nextTurn(foe->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(foeAfter - foe->getAvailableHealth(), 83);

	auto * friendUnit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(71), 100);
	const auto friendBefore = friendUnit->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *friendUnit));
	EXPECT_EQ(friendBefore - friendUnit->getAvailableHealth(), 83);

	auto * flying = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(72), 100);
	flying->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FLYING,
		BonusSource::OTHER, 0, BonusSourceID()));
	const auto flyingBefore = flying->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *flying));
	EXPECT_EQ(flying->getAvailableHealth(), flyingBefore);
	EXPECT_EQ(battle()->obstacles.size(), 1u);
}

TEST_F(NewHorizonsFireWallRuntimeTest, SpellContinuationsKeepTheSameActivationToken)
{
	prepareFireWall();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));

	auto * foe = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 100);
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	const auto healthAfterFirstTrigger = foe->getAvailableHealth();
	const auto activation = battle()->getActivationSerial();

	// These transitions are continuations of the same creature activation. A
	// movement callback after either one must remain suppressed.
	battle()->nextTurn(foe->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(battle()->getActivationSerial(), activation);
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(foe->getAvailableHealth(), healthAfterFirstTrigger);

	battle()->nextTurn(foe->unitId(), BattleUnitTurnReason::UNIT_SPELLCAST);
	EXPECT_EQ(battle()->getActivationSerial(), activation);
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(foe->getAvailableHealth(), healthAfterFirstTrigger);
}

TEST_F(NewHorizonsFireWallRuntimeTest, FireWallExpiresAfterThreeRoundBoundaries)
{
	prepareFireWall();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));
	ASSERT_EQ(battle()->obstacles.size(), 1u);

	endRound();
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	EXPECT_EQ(dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get())->turnsRemaining, 2);
	endRound();
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	EXPECT_EQ(dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get())->turnsRemaining, 1);
	endRound();
	EXPECT_TRUE(battle()->obstacles.empty());
}

TEST_F(NewHorizonsFireWallRuntimeTest, SecondWindLethalWallTriggerAdvancesBattleFlow)
{
	prepareFireWall();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		fireWallAction(BattleHex(70), BattleHex::RIGHT)));
	// The hero spell consumes this round's shared hero exchange.  Begin the
	// next round before issuing Second Wind so the command is independently
	// legal while the three-round wall is still active.
	endRound();

	// One Pikeman cannot survive the cast-time Fire Wall snapshot. Marking it
	// moved makes it a legal Second Wind recipient without requiring a separate
	// player action that would move the active fixture stack.
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 1);
	target->movedThisRound = true;
	// endRound() legitimately leaves whichever side wins the next-round queue
	// active. Pin this command test to an attacker activation so player/side
	// validation is deterministic across creature initiative data sets.
	auto * commander = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 10);
	battle()->activeStack = commander->unitId();
	const auto activation = battle()->getActivationSerial();
	ASSERT_TRUE(target->moved());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, target->unitId())));
	EXPECT_FALSE(target->alive());
	EXPECT_GT(battle()->getActivationSerial(), activation);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_NE(battle()->battleActiveUnit()->unitId(), target->unitId());
}
