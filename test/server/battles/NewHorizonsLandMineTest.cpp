/*
 * NewHorizonsLandMineTest.cpp, part of VCMI engine
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
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"

namespace
{
class NewHorizonsLandMineRuntimeTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void prepareLandMine(int32_t spellPower = 43)
	{
		prepareCommands(true);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->mana = attackerSideHero->manaLimit();
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	}

	BattleAction landMineAction(std::initializer_list<int> hexes) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::LAND_MINE;
		for(const auto hex : hexes)
			action.aimToHex(BattleHex(hex));
		return action;
	}
};
}

TEST(NewHorizonsLandMineTest, SpellPowerThresholdsSelectCanonicalMineCount)
{
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(0), 2);
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(99), 2);
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(100), 3);
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(199), 3);
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(200), 4);
}

TEST(NewHorizonsLandMineTest, MultiHexActionRoundTripsOnlyOnCanonicalProtocol)
{
	BattleAction action;
	action.side = BattleSide::ATTACKER;
	action.actionType = EActionType::HERO_SPELL;
	action.spell = SpellID(SpellID::LAND_MINE);
	for(const auto hex : {BattleHex(40), BattleHex(57), BattleHex(74)})
		action.aimToHex(hex);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_EQ(restored.target.size(), 3u);
	EXPECT_EQ(restored.target[0].hexValue, BattleHex(40));
	EXPECT_EQ(restored.target[1].hexValue, BattleHex(57));
	EXPECT_EQ(restored.target[2].hexValue, BattleHex(74));

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NewHorizonsLandMineTest, SnapshotAndConsumptionStateRoundTripsAndFailsClosed)
{
	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::LAND_MINE;
	obstacle.casterSpellPower = 100;
	obstacle.casterPowerDivisor = 10;
	obstacle.minimalDamage = 170;
	obstacle.damageSnapshot = true;
	obstacle.removeOnTrigger = true;
	obstacle.revealed = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & obstacle);
	SpellCreatedObstacle restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.minimalDamage, 170);
	EXPECT_TRUE(restored.removeOnTrigger);
	EXPECT_TRUE(restored.revealed);
	EXPECT_TRUE(restored.damageSnapshot);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY;
	EXPECT_THROW(old.oser & obstacle, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NewHorizonsLandMineTest, TriggerProxyUsesTheCastTimeSnapshotExactly)
{
	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::LAND_MINE;
	obstacle.minimalDamage = 170;
	obstacle.damageSnapshot = true;
	spells::ObstacleCasterProxy proxy(PlayerColor(0), nullptr, obstacle);

	EXPECT_EQ(proxy.getSpellBonus(nullptr, 85, nullptr), 85);
	EXPECT_EQ(proxy.getEffectValue(nullptr), 170);
}

TEST_F(NewHorizonsLandMineRuntimeTest, ServerRejectsWrongCountDuplicateAndOccupiedHexes)
{
	prepareLandMine();
	const auto mana = attackerSideHero->mana;

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({70})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({70, 70})));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 10);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({70, 71})));

	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->mana, mana);
}

TEST_F(NewHorizonsLandMineRuntimeTest, ServerRejectsBattlefieldSpecificImpassableHexesAtomically)
{
	prepareLandMine();
	battle()->battlefieldType = BattleField(BattleField::decode("core:ship_to_ship"));
	const auto mana = attackerSideHero->mana;

	ASSERT_EQ(battle()->getAccessibility()[BattleHex(6).toInt()], EAccessibility::UNAVAILABLE);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({6, 70})));
	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->mana, mana);
}

TEST_F(NewHorizonsLandMineRuntimeTest, SelectedMinesSnapshotDamageAndConsumeOnHostileGroundTrigger)
{
	prepareLandMine();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({70, 71})));
	ASSERT_EQ(battle()->obstacles.size(), 2u);
	for(const auto & obstacle : battle()->obstacles)
	{
		const auto * mine = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		ASSERT_NE(mine, nullptr);
		EXPECT_EQ(mine->minimalDamage, 107); // 60 + floor(1.1 * 43)
		EXPECT_TRUE(mine->damageSnapshot);
		EXPECT_TRUE(mine->hidden);
		EXPECT_FALSE(mine->nativeVisible);
		EXPECT_FALSE(mine->visibleForSide(BattleSide::DEFENDER, true));
		EXPECT_TRUE(mine->visibleForSide(BattleSide::ATTACKER, true));
	}

	// The creating hero's spell power may change after placement; the mine
	// retains the direct-damage value captured by the cast.
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 100);
	const auto health = victim->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *victim));
	EXPECT_EQ(health - victim->getAvailableHealth(), 107);
	EXPECT_EQ(battle()->obstacles.size(), 1u);

	auto * protectedVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	protectedVictim->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	const auto protectedHealth = protectedVictim->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *protectedVictim));
	EXPECT_EQ(protectedHealth - protectedVictim->getAvailableHealth(), 53);
	EXPECT_TRUE(battle()->obstacles.empty());
}

TEST_F(NewHorizonsLandMineRuntimeTest, FriendlyAndFlyingUnitsDoNotRevealOrConsumeMine)
{
	prepareLandMine();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), landMineAction({70, 71})));
	ASSERT_EQ(battle()->obstacles.size(), 2u);

	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	const auto friendlyHealth = friendly->getAvailableHealth();
	EXPECT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *friendly));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyHealth);
	EXPECT_EQ(battle()->obstacles.size(), 2u);

	int64_t friendlyDamage = friendly->getAvailableHealth();
	friendly->damage(friendlyDamage);
	auto * flying = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 100);
	flying->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FLYING,
		BonusSource::OTHER, 0, BonusSourceID()));
	const auto flyingHealth = flying->getAvailableHealth();
	EXPECT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *flying));
	EXPECT_EQ(flying->getAvailableHealth(), flyingHealth);
	EXPECT_EQ(battle()->obstacles.size(), 2u);
}
