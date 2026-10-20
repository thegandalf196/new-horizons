/*
 * SummonedCreatureHealthTerrainTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../mock/mock_Creature.h"
#include "../../mock/mock_spells_Mechanics.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/battle/UnitQueryTerrain.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"

#include <stdexcept>

namespace
{
std::shared_ptr<Bonus> terrainHealth(int amount, TerrainId terrain)
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, amount, BonusSourceID());
	bonus->limiter = std::make_shared<TerrainLimiter>(terrain);
	return bonus;
}

class SummonIdentityUpdater final : public IUpdater
{
	TerrainId terrain;
	BattleSide side;
	PlayerColor owner;
public:
	SummonIdentityUpdater(TerrainId terrain, BattleSide side, PlayerColor owner)
		: terrain(terrain), side(side), owner(owner)
	{
	}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(context.getNodeType() != BonusNodeType::STACK_BATTLE)
			return bonus;
		const auto * stack = dynamic_cast<const CStack *>(&context.originalNode());
		const bool valid = stack && stack->isHypothetic() && !stack->getBattle()
			&& stack->unitSide() == side && stack->unitOwner() == owner && stack->isSummoned()
			&& stack->getQueryTerrain() == terrain && !context.battleContext()
			&& !context.battleTerrain() && context.queryTerrain() == terrain;
		auto result = std::make_shared<Bonus>(*bonus);
		result->val = valid ? (stack->natureSummoned ? 13 : 7) : -1000;
		result->updater = nullptr;
		return result;
	}
};

class AttackerMechanicsMock : public spells::MechanicsMock
{
public:
	BattleSide getCasterSide() const override { return BattleSide::ATTACKER; }
};

class SummonedCreatureHealthTerrainTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	CGHeroInstance * hero(BattleSide side) const
	{
		return side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
	}

	int32_t query(CreatureID creature, BattleSide side, spells::Mode mode, bool nature = true) const
	{
		spells::BattleCast cast(battle(), hero(side), mode, SpellID(SpellID::SUMMON_EARTH_ELEMENTAL).toSpell());
		const auto mechanics = cast.getSpell()->battleMechanics(&cast);
		return mechanics->getSummonedCreatureMaxHealth(creature.toCreature(), nature);
	}

	CStack * addSummon(CreatureID creature, BattleSide side, bool nature, BattleHex position)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = 1;
		info.type = creature;
		info.side = side;
		info.position = position;
		info.summoned = true;
		info.natureSummoned = nature;
		BattleUnitsChanged pack;
		pack.battleID = BattleID(0);
		pack.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(pack.changedStacks.back().data);
		gameHandler->sendAndApply(pack);
		return battle()->getStack(info.id);
	}
};

TEST_F(SummonedCreatureHealthTerrainTest, OrdinaryAndReflectedQueriesUseBattleTerrainAndOriginalSideBonuses)
{
	const auto attackerAdventure = attackerSideHero->getCurrentTerrain();
	const auto defenderAdventure = defenderSideHero->getCurrentTerrain();
	for(const auto terrain : {TerrainId(ETerrainId::GRASS), TerrainId(ETerrainId::SAND), TerrainId(ETerrainId::SNOW)})
		if(terrain != attackerAdventure && terrain != defenderAdventure)
			battle()->terrainType = terrain;
	const auto terrain = battle()->getTerrainType();
	ASSERT_NE(terrain, attackerAdventure);
	ASSERT_NE(terrain, defenderAdventure);
	int position = 5;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		ASSERT_EQ(hero(side)->battle, battle());
		const auto creature = CreatureID(CreatureID::decode("core:pikeman"));
		const auto baseline = query(creature, side, spells::Mode::HERO);
		const int bonus = side == BattleSide::ATTACKER ? 37 : 53;
		hero(side)->addNewBonus(terrainHealth(bonus, terrain));
		hero(side)->addNewBonus(terrainHealth(101, hero(side)->getCurrentTerrain()));
		for(const bool nature : {false, true})
		{
			const auto * actual = addSummon(creature, side, nature, BattleHex(position++, 5));
			ASSERT_NE(actual, nullptr);
			ASSERT_TRUE(actual->isSummoned());
			ASSERT_EQ(actual->natureSummoned, nature);
			EXPECT_EQ(actual->getMaxHealth(), baseline + bonus);
			const auto roster = battle()->getUnitsIf([](const battle::Unit *) { return true; });
			const auto additions = server.unitAdditionRounds;
			for(const auto mode : {spells::Mode::HERO, spells::Mode::MAGIC_MIRROR})
				EXPECT_EQ(query(creature, side, mode, nature), actual->getMaxHealth());
			EXPECT_EQ(battle()->getUnitsIf([](const battle::Unit *) { return true; }), roster);
			EXPECT_EQ(server.unitAdditionRounds, additions);
		}
	}
}

TEST_F(SummonedCreatureHealthTerrainTest, QueryTerrainDoesNotInventBattleMembershipOrChangeSummonProvenance)
{
	const auto terrain = battle()->getTerrainType();
	const auto creature = CreatureID(CreatureID::decode("core:pikeman"));
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto baseline = query(creature, side, spells::Mode::HERO);
		auto bonus = terrainHealth(1, terrain);
		bonus->updater = std::make_shared<SummonIdentityUpdater>(terrain, side, hero(side)->tempOwner);
		hero(side)->addNewBonus(bonus);
		for(const auto mode : {spells::Mode::HERO, spells::Mode::MAGIC_MIRROR})
		{
			EXPECT_EQ(query(creature, side, mode, false), baseline + 7);
			EXPECT_EQ(query(creature, side, mode, true), baseline + 13);
		}
	}
}

TEST_F(SummonedCreatureHealthTerrainTest, MissingBoundTerrainRetainsStrictLimiterFailure)
{
	attackerSideHero->addNewBonus(terrainHealth(5, battle()->getTerrainType()));
	battle()->terrainType = TerrainId::NONE;
	for(const auto mode : {spells::Mode::HERO, spells::Mode::MAGIC_MIRROR})
		EXPECT_THROW(query(CreatureID(CreatureID::decode("core:pikeman")), BattleSide::ATTACKER, mode), std::logic_error);
}

TEST_F(SummonedCreatureHealthTerrainTest, HeroFallbackWithoutBattleCallbackUsesOnlyItsActuallyBoundTerrain)
{
	ASSERT_EQ(attackerSideHero->battle, battle());
	testing::NiceMock<AttackerMechanicsMock> mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(testing::Return(nullptr));
	ON_CALL(mechanics, getHeroCaster()).WillByDefault(testing::Return(attackerSideHero));
	ON_CALL(mechanics, getCasterColor()).WillByDefault(testing::Return(PlayerColor(0)));
	const auto creature = CreatureID(CreatureID::decode("core:pikeman"));
	attackerSideHero->addNewBonus(terrainHealth(19, battle()->getTerrainType()));
	EXPECT_EQ(mechanics.getSummonedCreatureMaxHealth(creature.toCreature(), true),
		query(creature, BattleSide::ATTACKER, spells::Mode::HERO));
}

TEST_F(SummonedCreatureHealthTerrainTest, NullAndNonEngineCreaturesRetainTheirEarlyFallbacks)
{
	testing::StrictMock<spells::MechanicsMock> mechanics;
	EXPECT_EQ(mechanics.getSummonedCreatureMaxHealth(nullptr, true), 1);
	testing::StrictMock<CreatureMock> creature;
	EXPECT_CALL(creature, getMaxHealth()).WillOnce(testing::Return(0));
	EXPECT_EQ(mechanics.getSummonedCreatureMaxHealth(&creature, false), 1);
	EXPECT_CALL(creature, getMaxHealth()).WillOnce(testing::Return(73));
	EXPECT_EQ(mechanics.getSummonedCreatureMaxHealth(&creature, true), 73);
}

TEST_F(SummonedCreatureHealthTerrainTest, NullArmyAndNullHeroRetainCreatureOnlyQuery)
{
	testing::NiceMock<AttackerMechanicsMock> mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(testing::Return(nullptr));
	ON_CALL(mechanics, getHeroCaster()).WillByDefault(testing::Return(nullptr));
	ON_CALL(mechanics, getCasterColor()).WillByDefault(testing::Return(PlayerColor(0)));
	const auto * creature = CreatureID(CreatureID::decode("core:pikeman")).toCreature();
	EXPECT_EQ(mechanics.getSummonedCreatureMaxHealth(creature, false), creature->getMaxHealth());
}
}
