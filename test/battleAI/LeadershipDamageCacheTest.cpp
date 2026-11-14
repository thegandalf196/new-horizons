/* LeadershipDamageCacheTest.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../server/CGameHandler.h"
#include <limits>

namespace
{
class CacheEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CacheEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class LeadershipDamageCacheTest : public BattleTestFixture
{
protected:
	bool leadershipEnabled = true;
	std::unique_ptr<CacheEnvironment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<HypotheticBattle> parent;
	uint32_t actorId = 0, targetId = 0;

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		// A valid commands-off profile must exercise actual cache hits rather
		// than accidentally relying on the broad Focus Fire dynamic bypass.
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		// Weighted perks require their companion hero-development capture. This
		// count/capacity fixture disables both; perk valuation has separate tests.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
		JsonNode rules;
		if(leadershipEnabled)
		{
			rules = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
			rules["classProfiles"]["core:knight"]["base"].Integer() = 6000; // Pikeman Requirement60, capacity100.
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
	}

	void startHeroLessBattle()
	{
		// Initialize the side without a fighting hero. Its original-army snapshot
		// must also reflect that absence before the immutable AI export is made.
		const int3 tile(4, 4, 0);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		const std::string battlefieldName = "core:sand_shore";
		const BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName));
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		BattleStart start;
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield,
			{attackerSideHero, defenderSideHero}, {nullptr, defenderSideHero}, layout, nullptr);
		start.battleID = BattleID(0);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->obstacles.clear();
	}

	void prepare(int count = 1000, bool machine = false, bool heroLess = false)
	{
		startGame();
		ASSERT_TRUE(attackerSideHero->getPrimaryGrowthRules().isNull());
		ASSERT_TRUE(attackerSideHero->getPerkState().rules.isNull());
		if(leadershipEnabled)
			ASSERT_EQ(attackerSideHero->getCapabilityRules()["rulesetVersion"].Integer(), 7);
		const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, count));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 10000));
		if(heroLess)
			ASSERT_NO_FATAL_FAILURE(startHeroLessBattle());
		else
			startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actorId : targetId) = unit->unitId();
		if(machine)
			actorId = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:ballista")),
				BattleHex(70), 1)->unitId();
		environment = std::make_unique<CacheEnvironment>(gameState());
		live = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		parent = std::make_shared<HypotheticBattle>(environment.get(),
			InitialExportBattleSubject::capture(live, *battle()));
		ASSERT_TRUE(parent->getBattle()->getHeroCommandRules().isNull());
		if(leadershipEnabled && !machine && !heroLess)
			ASSERT_EQ(parent->battleLeadershipReadout(parent->battleGetUnitByID(actorId)).capacity, 100);
	}

	std::shared_ptr<HypotheticBattle> child() const
	{
		return std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	int64_t damage(const std::shared_ptr<HypotheticBattle> & model) const
	{
		return model->battleExpectedLuckDamage(BattleAttackInfo(model->battleGetUnitByID(actorId),
			model->battleGetUnitByID(targetId), 0, false));
	}
	void addLeadership(const std::shared_ptr<HypotheticBattle> & model)
	{
		Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::LEADERSHIP, BonusSource::OTHER, 6000, BonusSourceID());
		bonus.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		model->addUnitBonus(actorId, {bonus});
	}
	int64_t cached(DamageCache & cache, const std::shared_ptr<HypotheticBattle> & model,
		DamageCacheProbeTiming * timing = nullptr)
	{
		return cache.getDamage(model->battleGetUnitByID(actorId), model->battleGetUnitByID(targetId), model, timing);
	}
};

TEST_F(LeadershipDamageCacheTest, CachedCurrentDamageFollowsCasualtiesResurrectionAndReinforcement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(parent->battleLeadershipReadout(parent->battleGetUnitByID(actorId)).capacity, 100);
	DamageCache cache;
	cache.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto original = cache.captureSnapshot();
	const auto baseline = damage(parent);
	const auto branch = child();
	const auto actor = branch->getForUpdate(actorId);
	for(const int count : {500, 100})
	{
		int64_t lost = static_cast<int64_t>(actor->getCount() - count) * actor->getMaxHealth();
		actor->damage(lost);
		ASSERT_EQ(actor->getCount(), count);
		DamageCacheProbeTiming timing;
		const auto value = cached(cache, branch, &timing);
		EXPECT_NEAR(value, damage(branch), 2);
		EXPECT_GT(value, baseline * (static_cast<double>(count) / 1000));
		EXPECT_EQ(timing.dynamicBypasses, 0u);
		EXPECT_EQ(timing.rootCacheHits, 1u);
		EXPECT_EQ(timing.cacheFillCalls, 0u);
	}
	int64_t healing = static_cast<int64_t>(900) * actor->getMaxHealth();
	actor->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_EQ(actor->getCount(), 1000);
	EXPECT_EQ(cached(cache, branch), baseline);
	ASSERT_TRUE(actor->health.appendTemporaryCreatures(500));
	ASSERT_EQ(actor->getCount(), 1500);
	EXPECT_NEAR(cached(cache, branch), damage(branch), 2);
	EXPECT_TRUE(cache.matchesSnapshot(original));
	EXPECT_EQ(parent->battleGetUnitByID(actorId)->getCount(), 1000);
	EXPECT_EQ(live->battleGetUnitByID(actorId)->getCount(), 1000);
}

TEST_F(LeadershipDamageCacheTest, CurrentCapacityRefillsButOriginalComparisonKeepsItsCapturedCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	DamageCache original;
	original.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto snapshot = original.captureSnapshot();
	const auto branch = child(), reference = child();
	DamageCache current(&original);
	current.cacheDamage(branch->battleGetUnitByID(actorId), branch->battleGetUnitByID(targetId), branch);
	ASSERT_NO_FATAL_FAILURE(addLeadership(branch));
	ASSERT_EQ(branch->battleLeadershipReadout(branch->battleGetUnitByID(actorId)).capacity, 200);
	DamageCacheProbeTiming timing;
	EXPECT_NEAR(cached(current, branch, &timing), damage(branch), 1);
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.cacheFillCalls, 1u);
	EXPECT_EQ(current.getOriginalDamage(branch->battleGetUnitByID(actorId),
		branch->battleGetUnitByID(targetId), branch), damage(parent));
	for(const auto & model : {branch, reference})
	{
		int64_t lost = static_cast<int64_t>(500) * model->battleGetUnitByID(actorId)->getMaxHealth();
		model->getForUpdate(actorId)->damage(lost);
	}
	const auto baseline = current.getOriginalDamage(branch->battleGetUnitByID(actorId),
		branch->battleGetUnitByID(targetId), branch);
	EXPECT_NEAR(baseline, damage(reference), 2);
	EXPECT_NEAR(cached(current, branch), damage(branch), 2);
	EXPECT_GT(cached(current, branch), baseline);
	EXPECT_TRUE(original.matchesSnapshot(snapshot));
	EXPECT_EQ(parent->battleGetUnitByID(actorId)->getCount(), 1000);
	EXPECT_EQ(live->battleLeadershipReadout(live->battleGetUnitByID(actorId)).capacity, 100);
}

TEST_F(LeadershipDamageCacheTest, CacheFilledAtCapacityStillAppliesEfficiencyAfterReinforcementCrossesIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	DamageCache cache;
	cache.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto before = damage(parent);
	const auto branch = child();
	ASSERT_TRUE(branch->getForUpdate(actorId)->health.appendTemporaryCreatures(100));
	ASSERT_EQ(branch->battleGetUnitByID(actorId)->getCount(), 200);
	DamageCacheProbeTiming timing;
	const auto value = cached(cache, branch, &timing);
	EXPECT_NEAR(value, damage(branch), 2);
	EXPECT_GT(value, before);
	EXPECT_LT(value, 2 * before);
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
	EXPECT_EQ(timing.cacheFillCalls, 0u);
}

TEST_F(LeadershipDamageCacheTest, SnapshotDetectsCapacityChangeEvenWhenDamageIsUnchangedBelowCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10));
	const auto branch = child();
	DamageCache cache;
	cache.cacheDamage(branch->battleGetUnitByID(actorId), branch->battleGetUnitByID(targetId), branch);
	const auto snapshot = cache.captureSnapshot();
	const auto before = damage(branch);
	ASSERT_NO_FATAL_FAILURE(addLeadership(branch));
	ASSERT_EQ(damage(branch), before);
	cache.cacheDamage(branch->battleGetUnitByID(actorId), branch->battleGetUnitByID(targetId), branch);
	EXPECT_FALSE(cache.matchesSnapshot(snapshot));
	EXPECT_TRUE(cache.matchesSnapshot(cache.captureSnapshot()));
}

TEST_F(LeadershipDamageCacheTest, AbsentLeadershipProfileRetainsLinearCacheHits)
{
	leadershipEnabled = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(parent->battleLeadershipReadout(parent->battleGetUnitByID(actorId)).active);
	DamageCache cache;
	cache.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto branch = child();
	int64_t lost = static_cast<int64_t>(500) * branch->battleGetUnitByID(actorId)->getMaxHealth();
	branch->getForUpdate(actorId)->damage(lost);
	DamageCacheProbeTiming timing;
	EXPECT_NEAR(cached(cache, branch, &timing), damage(branch), 1);
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
}

TEST_F(LeadershipDamageCacheTest, HeroLessSideRetainsLinearCacheHits)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, false, true));
	ASSERT_EQ(parent->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	ASSERT_FALSE(parent->getLeadershipComposition(BattleSide::ATTACKER).captured);
	ASSERT_FALSE(parent->battleLeadershipReadout(parent->battleGetUnitByID(actorId)).active);
	DamageCache cache;
	cache.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto branch = child();
	int64_t lost = static_cast<int64_t>(500) * branch->battleGetUnitByID(actorId)->getMaxHealth();
	branch->getForUpdate(actorId)->damage(lost);
	DamageCacheProbeTiming timing;
	EXPECT_NEAR(cached(cache, branch, &timing), damage(branch), 1);
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
}

TEST_F(LeadershipDamageCacheTest, LinearCacheSaturatesWhenReinforcementExceedsInt64Damage)
{
	leadershipEnabled = false;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	parent->addUnitBonus(actorId, {
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::OTHER,
			1000000000, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_PREMY, BonusSource::OTHER, 1000, BonusSourceID())});
	ASSERT_GT(damage(parent), 10000000000LL); // The initial single-creature result is representable.
	DamageCache cache;
	cache.cacheDamage(parent->battleGetUnitByID(actorId), parent->battleGetUnitByID(targetId), parent);
	const auto branch = child();
	ASSERT_TRUE(branch->getForUpdate(actorId)->health.appendTemporaryCreatures(std::numeric_limits<int32_t>::max() - 1));
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cached(cache, branch, &timing), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
	EXPECT_EQ(timing.cacheFillCalls, 0u);
	EXPECT_EQ(parent->battleGetUnitByID(actorId)->getCount(), 1);
	EXPECT_EQ(live->battleGetUnitByID(actorId)->getCount(), 1);
}

TEST_F(LeadershipDamageCacheTest, MachinesKeepTheirCacheHitWhenHeroLeadershipChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, true));
	const auto branch = child();
	ASSERT_FALSE(branch->battleLeadershipReadout(branch->battleGetUnitByID(actorId)).active);
	DamageCache cache;
	cache.cacheDamage(branch->battleGetUnitByID(actorId), branch->battleGetUnitByID(targetId), branch);
	const auto before = damage(branch);
	ASSERT_NO_FATAL_FAILURE(addLeadership(branch));
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cached(cache, branch, &timing), before);
	EXPECT_EQ(damage(branch), before);
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
	EXPECT_EQ(timing.cacheFillCalls, 0u);
}
