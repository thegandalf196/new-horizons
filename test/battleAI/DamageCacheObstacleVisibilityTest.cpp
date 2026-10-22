/*
 * DamageCacheObstacleVisibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/spells/ISpellMechanics.h"

namespace
{
class AuthorityObstacleCallback final : public CPlayerBattleCallback
{
public:
	explicit AuthorityObstacleCallback(const IBattleInfo * battle)
		: CPlayerBattleCallback(battle, PlayerColor(0)) {}
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};
}

class DamageCacheObstacleVisibilityTest : public HeroCommandFixture
{
protected:
	using DamageTable = std::map<int, std::map<uint32_t, int64_t>>;
	using UnitSnapshot = std::map<uint32_t, std::pair<JsonNode, std::vector<JsonNode>>>;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::vector<SpellCreatedObstacle> descriptors;
	std::vector<uint32_t> victimIds;
	uint32_t immuneId = 0;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void addMine(int id, BattleSide casterSide, bool hidden, BattleHex position)
	{
		SpellCreatedObstacle mine;
		mine.uniqueID = id;
		mine.ID = SpellID::LAND_MINE;
		mine.trigger = SpellID(SpellID::decode("core:landMineTrigger"));
		ASSERT_TRUE(mine.trigger.hasValue());
		mine.casterSide = casterSide;
		mine.pos = position;
		mine.customSize.insert(position);
		mine.hidden = hidden;
		mine.nativeVisible = false;
		mine.passable = true;
		mine.trap = false;
		mine.removeOnTrigger = true;
		mine.damageSnapshot = true;
		mine.minimalDamage = 100;
		mine.casterSpellPower = 10;
		mine.spellLevel = 3;
		ObstacleChanges change;
		mine.toInfo(change);
		BattleObstaclesChanged pack;
		pack.battleID = BattleID(0);
		pack.change = change;
		gameHandler->sendAndApply(pack);
		descriptors.push_back(mine);
	}

	void prepare(BattleSide ownSide)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto * victim = addStack(side, creatureByName("core:ogre"),
				side == BattleSide::ATTACKER ? BattleHex(3, 5) : BattleHex(12, 5), 100);
			victimIds.push_back(victim->unitId());
		}
		auto * immune = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 7), 100);
		immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::decode("core:landMineTrigger")))));
		immuneId = immune->unitId();
		victimIds.push_back(immuneId);
		const auto opposite = CBattleInfoEssentials::otherSide(ownSide);
		ASSERT_NO_FATAL_FAILURE(addMine(701, ownSide, true, BattleHex(7, 3)));
		ASSERT_NO_FATAL_FAILURE(addMine(702, opposite, true, BattleHex(8, 3)));
		ASSERT_NO_FATAL_FAILURE(addMine(703, opposite, false, BattleHex(9, 3)));
		ASSERT_EQ(battle()->getAllObstacles().size(), 3u);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
	}

	UnitSnapshot snapshotUnits(const std::shared_ptr<HypotheticBattle> & model)
	{
		UnitSnapshot snapshot;
		for(const auto * unit : model->battleGetAllUnits(false))
		{
			const auto state = model->getForUpdate(unit->unitId());
			std::vector<JsonNode> bonuses;
			const auto records = state->getUnstackedBonuses(Selector::all);
			for(const auto & bonus : *records)
				bonuses.push_back(bonus->toJsonNode());
			snapshot.emplace(unit->unitId(), std::pair{state->save(), std::move(bonuses)});
		}
		return snapshot;
	}

	DamageTable oracle(const std::shared_ptr<HypotheticBattle> & model,
		BattleSide knowledgeSide)
	{
		DamageTable table;
		for(const auto & mine : descriptors)
		{
			// Deliberately independent of the production obstacle selection query.
			if(mine.hidden && mine.casterSide != knowledgeSide)
				continue;
			auto inner = std::make_shared<HypotheticBattle>(environment.get(), model);
			const auto observer = model->battleGetMySide();
			const bool casterKnown = observer == BattleSide::ALL_KNOWING || observer == mine.casterSide;
			const auto * hero = casterKnown ? model->battleGetFightingHero(mine.casterSide) : nullptr;
			spells::ObstacleCasterProxy caster(model->getSidePlayer(mine.casterSide), hero, mine);
			spells::BattleCast cast(model.get(), &caster, spells::Mode::PASSIVE, mine.trigger.toSpell());
			for(const auto * victim : model->battleGetUnitsIf([](const battle::Unit * unit)
			{
				return unit->alive() && !unit->isTurret() && unit->getPosition().isValid();
			}))
			{
				const auto updated = inner->getForUpdate(victim->unitId());
				cast.castEval(inner->getServerCallback(), spells::Target{spells::Destination(updated.get())});
				table[mine.pos.toInt()][victim->unitId()] = victim->getAvailableHealth() - updated->getAvailableHealth();
			}
		}
		return table;
	}

	void check(BattleSide ownSide, bool authority)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(ownSide));
		std::shared_ptr<CPlayerBattleCallback> source;
		if(authority)
			source = std::make_shared<AuthorityObstacleCallback>(battle());
		else
			source = std::make_shared<CPlayerBattleCallback>(battle(), battle()->getSidePlayer(ownSide));
		ASSERT_EQ(source->battleGetMySide(), authority ? BattleSide::ALL_KNOWING : ownSide);
		auto model = std::make_shared<HypotheticBattle>(environment.get(), source);
		ASSERT_EQ(model->getAllObstacles().size(), authority ? 3u : 2u);
		// Warm only before baselines; snapshots must not repair later mutations.
		for(const auto & stack : battle()->stacks)
			(void)stack->counterAttacks.available();
		for(const auto * unit : model->battleGetAllUnits(false))
			(void)model->getForUpdate(unit->unitId())->counterAttacks.available();
		const auto unitsBefore = snapshotUnits(model);
		const auto liveBefore = gameState()->saveToMemory();
		const auto battleBefore = projectedHitTest::serialized(*battle());
		const auto serverRngBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRngBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultAiRngBefore = projectedHitTest::serialized(CRandomGenerator::getDefault());
		std::vector<JsonNode> obstaclesBefore;
		for(const auto & obstacle : model->getAllObstacles())
		{
			ObstacleChanges change;
			const auto spellObstacle = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
			ASSERT_TRUE(spellObstacle);
			auto copy = *spellObstacle;
			copy.toInfo(change);
			obstaclesBefore.push_back(change.data);
		}
		for(const auto valuedSide : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			SCOPED_TRACE(static_cast<int>(valuedSide));
			const auto expected = oracle(model, authority ? valuedSide : ownSide);
			ASSERT_FALSE(expected.empty());
			DamageCache cache;
			cache.buildDamageCache(model, valuedSide);
			DamageCache terminal;
			terminal.buildCreaturePressureCache(model, valuedSide);
			bool positiveKnownMine = false;
			for(const auto & mine : descriptors)
			{
				for(const auto id : victimIds)
				{
					const auto hex = expected.find(mine.pos.toInt());
					const int64_t damage = hex == expected.end() ? 0 : hex->second.at(id);
					const auto actual = cache.getObstacleDamage(mine.pos, model->battleGetUnitByID(id));
					EXPECT_EQ(actual, damage) << "obstacle=" << mine.uniqueID << " victim=" << id;
					EXPECT_EQ(terminal.getObstacleDamage(mine.pos, model->battleGetUnitByID(id)), damage);
					if(mine.uniqueID == 701 && damage > 0)
						positiveKnownMine = true;
					if(id == immuneId)
						EXPECT_EQ(actual, 0);
				}
			}
			if(!authority || valuedSide == ownSide)
				EXPECT_TRUE(positiveKnownMine);
			EXPECT_EQ(cache.getObstacleDamage(BattleHex(10, 3), model->battleGetUnitByID(victimIds.front())), 0);
			EXPECT_EQ(terminal.getObstacleDamage(BattleHex(10, 3), model->battleGetUnitByID(victimIds.front())), 0);
			DamageCache childCache(&cache);
			childCache.buildDamageCache(model, CBattleInfoEssentials::otherSide(valuedSide));
			for(const auto & mine : descriptors)
				for(const auto id : victimIds)
					EXPECT_EQ(childCache.getObstacleDamage(mine.pos, model->battleGetUnitByID(id)),
						cache.getObstacleDamage(mine.pos, model->battleGetUnitByID(id)));
		}
		EXPECT_EQ(snapshotUnits(model), unitsBefore);
		std::vector<JsonNode> obstaclesAfter;
		for(const auto & obstacle : model->getAllObstacles())
		{
			ObstacleChanges change;
			auto copy = *std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
			copy.toInfo(change);
			obstaclesAfter.push_back(change.data);
		}
		EXPECT_EQ(obstaclesAfter, obstaclesBefore);
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(projectedHitTest::serialized(*battle()), battleBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRngBefore);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRngBefore);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultAiRngBefore);
	}
};

TEST_F(DamageCacheObstacleVisibilityTest, AttackerObserverRetainsKnownMineWhenValuingEnemy)
{
	ASSERT_NO_FATAL_FAILURE(check(BattleSide::ATTACKER, false));
}

TEST_F(DamageCacheObstacleVisibilityTest, DefenderObserverRetainsKnownMineWhenValuingEnemy)
{
	ASSERT_NO_FATAL_FAILURE(check(BattleSide::DEFENDER, false));
}

TEST_F(DamageCacheObstacleVisibilityTest, AuthorityAttackerKeepsRequestedKnowledge)
{
	ASSERT_NO_FATAL_FAILURE(check(BattleSide::ATTACKER, true));
}

TEST_F(DamageCacheObstacleVisibilityTest, AuthorityDefenderKeepsRequestedKnowledge)
{
	ASSERT_NO_FATAL_FAILURE(check(BattleSide::DEFENDER, true));
}
