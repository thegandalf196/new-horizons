/*
 * ProjectedAdditionTerrainTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/UnitQueryTerrain.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include <vcmi/Environment.h>
#include <stdexcept>

namespace
{
class AdditionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AdditionEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class QueryOnlyTerrainUpdater final : public IUpdater
{
	TerrainId terrain;
public:
	explicit QueryOnlyTerrainUpdater(TerrainId terrain) : terrain(terrain) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(context.getNodeType() != BonusNodeType::STACK_BATTLE)
			return bonus;
		auto result = std::make_shared<Bonus>(*bonus);
		result->val = !context.battleContext() && !context.battleTerrain()
			&& context.queryTerrain() == terrain && context.stackCount() == 3 ? 37 : 0;
		result->updater = nullptr;
		return result;
	}
};

// Fixture-only metadata suppression, restored even when a prospective query throws.
class ScopedUnboundArmy
{
	CArmedInstance * army;
	BattleInfo * previous;
public:
	explicit ScopedUnboundArmy(CArmedInstance * army) : army(army), previous(army->battle) { army->battle = nullptr; }
	~ScopedUnboundArmy() { army->battle = previous; }
};

class ProjectedAdditionTerrainTest : public BattleTestFixture
{
protected:
	std::shared_ptr<AdditionEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;
	CStack * live = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
		live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
		ASSERT_NE(live, nullptr);
		environment = std::make_shared<AdditionEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	std::shared_ptr<HypotheticBattle> model()
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback);
	}
	JsonNode addition(uint32_t id)
	{
		battle::UnitInfo info;
		info.id = id;
		info.type = creatureByName("core:angel");
		info.count = 3;
		info.side = BattleSide::ATTACKER;
		info.position = BattleHex(rightHex + 3);
		info.summoned = true;
		JsonNode result;
		info.save(result);
		return result;
	}
};

TEST_F(ProjectedAdditionTerrainTest, AddedNativeUnitHasDefinedTerrainWithoutInventedUpdaterBattleMembership)
{
	ASSERT_EQ(attackerSideHero->battle, battle());
	const auto terrain = battle()->getTerrainType();
	auto probe = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 0, BonusSourceID());
	probe->updater = std::make_shared<QueryOnlyTerrainUpdater>(terrain);
	attackerSideHero->addNewBonus(probe);
	const auto original = live->save();
	const auto maximum = live->getMaxHealth();
	const auto id = battle()->battleNextUnitId();
	auto projected = model();
	ASSERT_EQ(projected->getSideArmy(BattleSide::ATTACKER), attackerSideHero);
	ASSERT_NO_THROW(projected->addUnit(id, addition(id)));
	const auto * added = projected->battleGetUnitByID(id);
	ASSERT_NE(added, nullptr);
	EXPECT_EQ(added->getMaxHealth(), maximum + 37);
	EXPECT_EQ(added->getMaxHealth(), maximum + 37); // retained native bonuses remain queryable
	EXPECT_EQ(added->getAttack(false), live->getAttack(false));
	EXPECT_EQ(added->getDefense(false), live->getDefense(false));
	EXPECT_EQ(added->unitId(), id);
	EXPECT_EQ(added->unitOwner(), PlayerColor(0));
	EXPECT_EQ(added->unitSlot(), SlotID::SUMMONED_SLOT_PLACEHOLDER);
	EXPECT_EQ(battle()->getStack(id, false), nullptr);
	EXPECT_EQ(live->save(), original);
	EXPECT_EQ(attackerSideHero->battle, battle());
}

TEST_F(ProjectedAdditionTerrainTest, MissingActualSourceBindingCannotBorrowOwnerBattleOrAdventureTile)
{
	const auto id = battle()->battleNextUnitId();
	auto projected = model();
	ASSERT_TRUE(battle()->getTerrainType().hasValue());
	{
		ScopedUnboundArmy unbound(attackerSideHero);
		ASSERT_FALSE(captureUnitQueryTerrain(attackerSideHero).terrain);
		EXPECT_THROW(projected->addUnit(id, addition(id)), std::logic_error);
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(projected->battleGetUnitByID(id), nullptr);
	EXPECT_EQ(battle()->getStack(id, false), nullptr);
}

TEST_F(ProjectedAdditionTerrainTest, BoundSourceWithUnavailableTerrainDoesNotInventGrass)
{
	battle()->terrainType = TerrainId(TerrainId::NONE); // fixture metadata BEFORE private capture
	ASSERT_EQ(attackerSideHero->battle, battle());
	ASSERT_FALSE(captureUnitQueryTerrain(attackerSideHero).terrain);
	const auto id = battle()->battleNextUnitId();
	auto projected = model();
	EXPECT_THROW(projected->addUnit(id, addition(id)), std::logic_error);
	EXPECT_EQ(projected->battleGetUnitByID(id), nullptr);
	EXPECT_EQ(battle()->getStack(id, false), nullptr);
}

TEST_F(ProjectedAdditionTerrainTest, NestedCopiesRetainOwnedTerrainAndKeepLocalHealthBonusesPrivate)
{
	const auto original = live->save();
	const auto id = battle()->battleNextUnitId();
	auto parent = model();
	ASSERT_NO_THROW(parent->addUnit(id, addition(id)));
	const auto maximum = parent->battleGetUnitByID(id)->getMaxHealth();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_NE(child->getForUpdate(id), nullptr);
	ASSERT_NE(sibling->getForUpdate(id), nullptr);
	EXPECT_EQ(child->battleGetUnitByID(id)->getMaxHealth(), maximum);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->getMaxHealth(), maximum);
	const Bonus health(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 7, BonusSourceID());
	child->addUnitBonus(id, {health});
	EXPECT_EQ(child->battleGetUnitByID(id)->getMaxHealth(), maximum + 7);
	EXPECT_EQ(parent->battleGetUnitByID(id)->getMaxHealth(), maximum);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->getMaxHealth(), maximum);
	EXPECT_EQ(battle()->getStack(id, false), nullptr);
	EXPECT_EQ(live->save(), original);
}
}
