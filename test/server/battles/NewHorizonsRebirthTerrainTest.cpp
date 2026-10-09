/*
 * NewHorizonsRebirthTerrainTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/callback/CBattleCallback.h"

namespace
{
class TerrainEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TerrainEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class TerrainCallback final : public CBattleCallback
{
public:
	explicit TerrainCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
};

struct TerrainCase
{
	TerrainId terrain;
	const char * arena;
	const char * elemental;
};
}

class NewHorizonsRebirthTerrainTest : public NewHorizonsElementalTerrainFixture,
	public testing::WithParamInterface<TerrainCase>
{
protected:
	static constexpr auto skill = "new-horizons:elementalRebirth";
	static constexpr auto attunement = "new-horizons:elementalRebirth.elementalAttunement";
	static constexpr auto adaptive = "new-horizons:elementalRebirth.adaptiveElement";
	static constexpr auto perfect = "new-horizons:elementalRebirth.perfectConvergence";
	CStack * source = nullptr;
	CStack * enemy = nullptr;

	void prepare(int rank, bool selectTerrainPerks = true)
	{
		ASSERT_NO_FATAL_FAILURE(expectRegisteredPerk(attunement));
		ASSERT_NO_FATAL_FAILURE(expectRegisteredPerk(adaptive));
		ASSERT_NO_FATAL_FAILURE(expectRegisteredPerk(perfect));
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill id(SecondarySkill::decode(skill));
		ASSERT_GE(id.getNum(), 0);
		for(int current = MasteryLevel::BASIC; current <= rank; ++current)
		{
			defenderSideHero->setSecSkillLevel(id, current, ChangeValueMode::ABSOLUTE);
			if(selectTerrainPerks)
				defenderSideHero->applyPerkSelection({skill,
					current == MasteryLevel::BASIC ? attunement : current == MasteryLevel::ADVANCED ? adaptive : perfect});
		}
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 101));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), 10));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
		startTerrainBattle(GetParam().terrain, GetParam().arena);
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				enemy = battle()->getStack(unit->unitId(), false);
		}
		ASSERT_NE(source, nullptr);
		ASSERT_NE(enemy, nullptr);
		// A single-width source at the default defender edge leaves no room for
		// Water's two-hex footprint. Use a real central corpse, not relaxed legality.
		auto positioned = source->acquireState();
		positioned->setPosition(BattleHex(12, 5));
		BattleUnitsChanged move;
		move.battleID = BattleID(0);
		move.changedStacks.emplace_back(source->unitId(), UnitChanges::EOperation::UPDATE);
		move.changedStacks.back().data = positioned->save();
		gameHandler->sendAndApply(move);
		beginCombat();
		// Adventure terrain subsequently differs: the captured battle is authoritative.
		gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = TerrainId::LAVA;
	}

	void killSource()
	{
		BattleStackAttacked hit;
		hit.attackerID = enemy->unitId();
		hit.stackAttacked = source->unitId();
		hit.damageAmount = source->getAvailableHealth();
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), source->acquireState());
		ASSERT_TRUE(hit.killed());
		StacksInjured pack;
		pack.battleID = BattleID(0);
		pack.stacks.push_back(std::move(hit));
		gameHandler->sendAndApply(pack);
	}
};

TEST_P(NewHorizonsRebirthTerrainTest, AdaptiveAndAttunementMatchLiveAndDetachedExactHP)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED));
	const auto expectedType = creatureByName(GetParam().elemental);
	const auto snapshot = newHorizonsElementalRebirth::captureDeathSource(*source, defenderSideHero);
	ASSERT_TRUE(snapshot);
	const auto pool = newHorizonsElementalRebirth::legalCandidatePool(*battle(), battle()->getAccessibility(source), *snapshot);
	ASSERT_EQ(pool.size(), 1u);
	EXPECT_EQ(pool.front(), expectedType);
	const auto baseHP = source->getBattleStartMaximumAggregateHP() * 40 / 100;
	const auto expectedHP = baseHP * 120 / 100;
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(*snapshot, expectedType, *battle()), expectedHP);

	TerrainEnvironment environment(gameState());
	auto callback = std::make_shared<TerrainCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
	callback->onBattleStarted(battle());
	auto detached = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	const auto captured = detached->captureElementalRebirthSource(*detached->battleGetUnitByID(source->unitId()));
	ASSERT_TRUE(captured);
	auto doomed = detached->getForUpdate(source->unitId());
	int64_t damage = doomed->getAvailableHealth();
	doomed->damage(damage);
	const auto output = detached->projectElementalRebirth(detached->battleGetUnitByID(source->unitId()),
		*captured, true, false, false);
	ASSERT_TRUE(output);
	const auto * projected = detached->battleGetUnitByID(*output);
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->creatureId(), expectedType);
	EXPECT_EQ(projected->getAvailableHealth(), expectedHP);
	EXPECT_TRUE(source->alive());

	ASSERT_NO_FATAL_FAILURE(killSource());
	const battle::Unit * live = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && unit->isSummoned())
		{
			ASSERT_EQ(live, nullptr);
			live = unit;
		}
	ASSERT_NE(live, nullptr);
	EXPECT_EQ(live->creatureId(), expectedType);
	EXPECT_EQ(live->getAvailableHealth(), expectedHP);
	EXPECT_EQ(live->getRebirthOriginalAggregateHP(), expectedHP);
	EXPECT_EQ(live->getPosition(), source->getPosition());
	EXPECT_EQ(live->getAvailableHealth(), projected->getAvailableHealth());
}

TEST_P(NewHorizonsRebirthTerrainTest, PerfectForcesPrimaryWhileUnselectedPerksLeaveBaseRules)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	auto snapshot = newHorizonsElementalRebirth::captureDeathSource(*source, defenderSideHero);
	ASSERT_TRUE(snapshot);
	EXPECT_TRUE(snapshot->profile.perfectConvergence);
	const auto expectedType = creatureByName(GetParam().elemental);
	// Perfect must independently force the primary even without Adaptive.
	snapshot->profile.adaptiveElement = false;
	const auto forced = newHorizonsElementalRebirth::legalCandidatePool(*battle(), battle()->getAccessibility(source), *snapshot);
	ASSERT_EQ(forced.size(), 1u);
	EXPECT_EQ(forced.front(), expectedType);
	const auto rawHP = source->getBattleStartMaximumAggregateHP() * 50 / 100;
	snapshot->profile.elementalAttunement = false;
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(*snapshot, expectedType, *battle()), rawHP);
	snapshot->profile.perfectConvergence = false;
	const auto ordinary = newHorizonsElementalRebirth::legalCandidatePool(*battle(), battle()->getAccessibility(source), *snapshot);
	EXPECT_GT(ordinary.size(), 1u);
	snapshot->profile.elementalAttunement = true;
	const auto mismatch = expectedType == creatureByName("core:fireElemental")
		? creatureByName("core:waterElemental") : creatureByName("core:fireElemental");
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(*snapshot, mismatch, *battle()), rawHP);
	AccessibilityInfo blocked;
	blocked.fill(EAccessibility::UNAVAILABLE);
	snapshot->profile.perfectConvergence = true;
	EXPECT_TRUE(newHorizonsElementalRebirth::legalCandidatePool(*battle(), blocked, *snapshot).empty())
		<< "A forced primary may not fall back to a different element on an illegal corpse footprint";
	auto chain = *snapshot;
	chain.chain = true;
	chain.profile.rebirthChain = true;
	chain.rebirthOriginalAggregateHP = 77;
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(chain, expectedType, *battle()), 22)
		<< "Chain floors 25% of immutable first output, then applies matching Attunement once";
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(chain, mismatch, *battle()), 19);

	// The saved Expert selection drives a real authoritative destruction too.
	ASSERT_NO_FATAL_FAILURE(killSource());
	bool found = false;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && unit->isSummoned())
		{
			EXPECT_FALSE(found);
			found = true;
			EXPECT_EQ(unit->creatureId(), expectedType);
			EXPECT_EQ(unit->getAvailableHealth(), rawHP * 120 / 100);
		}
	EXPECT_TRUE(found);
}

INSTANTIATE_TEST_SUITE_P(CapturedTerrain, NewHorizonsRebirthTerrainTest, testing::Values(
	TerrainCase{TerrainId::DIRT, "core:dirt_hills", "core:earthElemental"},
	TerrainCase{TerrainId::SAND, "core:sand_mesas", "core:earthElemental"},
	TerrainCase{TerrainId::SWAMP, "core:swamp_trees", "core:waterElemental"},
	TerrainCase{TerrainId::SAND, "core:sand_shore", "core:waterElemental"},
	TerrainCase{TerrainId::LAVA, "core:lava", "core:fireElemental"},
	TerrainCase{TerrainId::GRASS, "core:grass_hills", "core:airElemental"},
	TerrainCase{TerrainId::SNOW, "core:snow_trees", "core:waterElemental"},
	TerrainCase{TerrainId::ROUGH, "core:subterranean", "core:earthElemental"},
	TerrainCase{TerrainId::GRASS, "core:magic_plains", "core:magicElemental"}));
