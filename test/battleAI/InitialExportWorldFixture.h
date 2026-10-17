/*
 * InitialExportWorldFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/InitialExportWorld.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"

class InitialExportWorldTest : public ProjectedTargetHitFixture
{
protected:
	struct Fact
	{
		uint32_t id;
		int attack;
		int range;
		int count;
		int counters;
		int64_t health;
		BattleHex position;
		JsonNode saved;
		std::vector<std::vector<std::byte>> exports;
	};
	void prepareWorld()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		child.reset();
		live.reset();
		environment.reset(); // No unused legacy provider can outlive its source lease.
	}
	void preparePositiveDamageProfile()
	{
		startGame();
		const SecondarySkill discipline(SecondarySkill::decode(newHorizonsDiscipline::SKILL));
		const SecondarySkill bloodrage(SecondarySkill::decode("new-horizons:bloodrage"));
		ASSERT_GE(discipline.getNum(), 0);
		ASSERT_GE(bloodrage.getNum(), 0);
		attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsDiscipline::SKILL, newHorizonsDiscipline::STEADFAST});
		attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsDiscipline::SKILL, newHorizonsDiscipline::VETERAN_COHESION});
		attackerSideHero->setSecSkillLevel(bloodrage, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:bloodrage", "new-horizons:bloodrage.rageThroughPain"});
		ASSERT_TRUE(newHorizonsDiscipline::hasVeteranCohesion(attackerSideHero));
		ASSERT_TRUE(newHorizonsBloodrage::hasRageThroughPain(attackerSideHero));
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:angel"), 10));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSlot() == SlotID(0))
			{
				if(unit->unitSide() == BattleSide::ATTACKER)
					incoming = battle()->getStack(unit->unitId(), false);
				else
					braced = battle()->getStack(unit->unitId(), false);
			}
		ASSERT_NE(incoming, nullptr);
		ASSERT_NE(braced, nullptr);
		beginCombat();
		ASSERT_EQ(incoming->getBattleStartMaximumAggregateHP(), incoming->getAvailableHealth());
		ASSERT_EQ(battle()->getBloodrageRank(BattleSide::ATTACKER), MasteryLevel::BASIC);
		ASSERT_EQ(battle()->getBloodragePainIncrement(BattleSide::ATTACKER), newHorizonsBloodrage::BASIC_INCREMENT);
	}
	Bonus record(BonusDuration::Type duration = BonusDuration::ONE_BATTLE,
		BonusNodeType attachment = BonusNodeType::BATTLE_WIDE)
	{
		Bonus value(duration, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 4,
			BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
		value.stacking = "initial-export-world.test";
		value.propagator = std::make_shared<CPropagatorNodeType>(attachment);
		// This fixture witnesses raw CREATURE Attack, not a hero-rating change.
		// Keep A/D applicable to creatures across the inherited HERO boundary.
		value.limiter = std::make_shared<CreatureLevelLimiter>();
		value.statusTags = {BonusStatusTag::DEBUFF};
		value.statusIdentity = "initial-export-world.origin";
		value.appliedByEnemy = true;
		value.spellCasterOwner = PlayerColor(1);
		value.parameters = std::make_shared<BonusParameters>(JsonNode(17));
		value.description.appendRawString("Initial export metadata witness");
		return value;
	}
	InitialExportWorld::Source source(InitialExportWorld & world, const CStack & native)
	{
		for(size_t index = 0; index < world.size(); ++index)
			if(world.sourceAt(index).original == &native)
				return world.sourceAt(index);
		throw std::logic_error("Fixture original occurrence is missing");
	}
	Fact fact(const battle::Unit & unit, JsonNode saved, int counters)
	{
		std::vector<std::vector<std::byte>> records;
		const auto bonuses = unit.getUnstackedBonuses(Selector::all);
		for(const auto & bonus : *bonuses)
			if(bonus->stacking == "initial-export-world.test")
				records.push_back(projectedHitTest::serialized(*bonus));
		return {unit.unitId(), unit.getAttack(false), unit.getMovementRange(0), unit.getCount(), counters,
			unit.getAvailableHealth(), unit.getPosition(), std::move(saved), std::move(records)};
	}
	std::vector<Fact> freeze(InitialExportWorld & world)
	{
		std::vector<Fact> result;
		for(size_t index = 0; index < world.size(); ++index)
		{
			const int counters = world.unitAt(index).counterAttacks.available();
			result.push_back(fact(world.unitAt(index), world.snapshotAt(index), counters));
		}
		return result; // No provider/graph/unit pointers survive in this oracle.
	}
	void compareNative(const std::vector<Fact> & frozen)
	{
		for(const auto & expected : frozen)
		{
			const auto * actual = battle()->battleGetStackByID(expected.id, false);
			ASSERT_NE(actual, nullptr);
			const int counters = actual->counterAttacks.available();
			const auto native = fact(*actual, actual->acquireState()->save(), counters);
			EXPECT_EQ(native.attack, expected.attack);
			EXPECT_EQ(native.range, expected.range);
			EXPECT_EQ(native.count, expected.count);
			EXPECT_EQ(native.counters, expected.counters);
			EXPECT_EQ(native.health, expected.health);
			EXPECT_EQ(native.position, expected.position);
			EXPECT_EQ(native.saved, expected.saved);
			EXPECT_EQ(native.exports, expected.exports);
		}
	}
	void nativeUpdate(uint32_t id, const JsonNode & state, int64_t delta)
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		UnitChanges change(id, UnitChanges::EOperation::UPDATE);
		change.data = state;
		change.healthDelta = delta;
		packet.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(packet);
	}
	JsonNode injured(InitialExportWorld & world, size_t occurrence, int64_t damage)
	{
		return world.snapshotAfterPhysicalDamage(world.sourceAt(occurrence), damage);
	}
};
