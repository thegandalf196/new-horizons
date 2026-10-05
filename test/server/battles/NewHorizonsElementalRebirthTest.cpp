/*
 * NewHorizonsElementalRebirthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view REBIRTH_SKILL = "new-horizons:elementalRebirth";

HeroTypeID heroType(const char * identifier)
{
	const int decoded = HeroTypeID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing hero in Elemental Rebirth fixture: ") + identifier);
	return HeroTypeID(decoded);
}

CreatureID creature(const char * identifier)
{
	const int decoded = CreatureID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing creature in Elemental Rebirth fixture: ") + identifier);
	return CreatureID(decoded);
}
}

class NewHorizonsElementalRebirthTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int rank, int sourceCount = 7, const char * sourceCreature = "core:peasant",
		bool includeCloneStack = false)
	{
		startGame();
		defenderSideHero->setHeroType(heroType("core:brissa"));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();

		const auto skillNumber = SecondarySkill::decode(std::string(REBIRTH_SKILL));
		ASSERT_GE(skillNumber, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(skillNumber), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature(sourceCreature), sourceCount));
		if(includeCloneStack)
			ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature(sourceCreature), sourceCount));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 1));
		ASSERT_TRUE(newHorizonsElementalRebirth::activeProfile(defenderSideHero).has_value());

		startBattle();
		source = stackAt(BattleSide::DEFENDER, SlotID(0));
		attacker = stackAt(BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(source, nullptr);
		ASSERT_NE(attacker, nullptr);
		basisAtStart = source->getBattleStartMaximumAggregateHP();
		ASSERT_GT(basisAtStart, 0);
		ASSERT_EQ(basisAtStart, static_cast<int64_t>(sourceCount) * source->getMaxHealth());
		beginCombat();
	}

	CStack * stackAt(BattleSide side, SlotID slot) const
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit && unit->unitSide() == side && unit->unitSlot() == slot)
				return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}

	void applyInjury(CStack * target, int64_t damage, bool asBattleAttack = false,
		std::optional<bool> expectDeath = true, std::optional<bool> expectNativeRebirth = std::nullopt)
	{
		ASSERT_NE(target, nullptr);
		ASSERT_NE(attacker, nullptr);
		BattleStackAttacked hit;
		hit.attackerID = attacker->unitId();
		hit.stackAttacked = target->unitId();
		hit.damageAmount = damage;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), target->acquireState());
		if(expectDeath)
		{
			EXPECT_EQ(hit.killed(), *expectDeath);
		}
		if(expectNativeRebirth)
		{
			EXPECT_EQ(hit.willRebirth(), *expectNativeRebirth);
		}

		if(asBattleAttack)
		{
			BattleAttack pack;
			pack.battleID = BattleID(0);
			pack.stackAttacking = attacker->unitId();
			pack.attackerChanges.battleID = BattleID(0);
			UnitChanges attackerUpdate(attacker->unitId(), UnitChanges::EOperation::UPDATE);
			attackerUpdate.data = attacker->acquireState()->save();
			pack.attackerChanges.changedStacks.push_back(std::move(attackerUpdate));
			pack.bsa.push_back(std::move(hit));
			gameHandler->sendAndApply(pack);
		}
		else
		{
			StacksInjured pack;
			pack.battleID = BattleID(0);
			pack.stacks.push_back(std::move(hit));
			gameHandler->sendAndApply(pack);
		}
	}

	std::vector<const battle::Unit *> summonedUnits() const
	{
		std::vector<const battle::Unit *> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit && unit->alive() && unit->isSummoned())
				result.push_back(unit);
		return result;
	}

	static int expectedPercent(int rank)
	{
		return rank == MasteryLevel::BASIC ? 25
			: rank == MasteryLevel::ADVANCED ? 40 : 50;
	}

	static int64_t expectedHP(int64_t basis, int percent)
	{
		return std::max<int64_t>(1, basis / 100 * percent + basis % 100 * percent / 100);
	}

	void expectOneExactWoundedRebirth(int rank)
	{
		const auto spawns = summonedUnits();
		ASSERT_EQ(spawns.size(), 1u);
		const auto * reborn = spawns.front();
		EXPECT_TRUE(reborn->isSummoned());
		EXPECT_EQ(reborn->unitSide(), BattleSide::DEFENDER);
		EXPECT_EQ(reborn->getPosition(), source->getPosition());
		EXPECT_EQ(reborn->getAvailableHealth(), expectedHP(basisAtStart, expectedPercent(rank)));
		EXPECT_EQ(reborn->getCount(), 1);
		EXPECT_LT(reborn->getAvailableHealth(), reborn->getMaxHealth())
			<< "The exact aggregate target should be represented as a wounded final Elemental";
	}

	CStack * source = nullptr;
	CStack * attacker = nullptr;
	int64_t basisAtStart = 0;
};

TEST_F(NewHorizonsElementalRebirthTest, StackInjuryUsesBasicBattleStartFractionAndWoundedFinalElemental)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	ASSERT_EQ(newHorizonsElementalRebirth::activeProfile(defenderSideHero)->healthPercent, 25);

	applyInjury(source, 1, false, false);
	EXPECT_EQ(source->getAvailableHealth(), basisAtStart - 1);
	EXPECT_EQ(source->getBattleStartMaximumAggregateHP(), basisAtStart)
		<< "Current casualties must not shrink the frozen battle-start HP basis";
	applyInjury(source, source->getAvailableHealth());

	expectOneExactWoundedRebirth(MasteryLevel::BASIC);
}

TEST_F(NewHorizonsElementalRebirthTest, StackInjuryUsesAdvancedFortyPercentFraction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED));
	ASSERT_EQ(newHorizonsElementalRebirth::activeProfile(defenderSideHero)->healthPercent, 40);
	applyInjury(source, source->getAvailableHealth());
	expectOneExactWoundedRebirth(MasteryLevel::ADVANCED);
}

TEST_F(NewHorizonsElementalRebirthTest, BattleAttackUsesExpertFiftyPercentAfterPriorCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	ASSERT_EQ(newHorizonsElementalRebirth::activeProfile(defenderSideHero)->healthPercent, 50);
	applyInjury(source, 1, false, false);
	const auto remaining = source->getAvailableHealth();
	applyInjury(source, remaining, true);
	expectOneExactWoundedRebirth(MasteryLevel::EXPERT);
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_TRUE(server.attacks.front().bsa.front().killed());
}

TEST_F(NewHorizonsElementalRebirthTest, SummonedAndCloneDeathsDoNotRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 7, "core:peasant", true));
	CStack * clone = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit && unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
			clone = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(clone, nullptr);

	BattleHex summonPosition = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && !battle()->battleGetUnitByPos(candidate, false))
		{
			summonPosition = candidate;
			break;
		}
	}
	ASSERT_TRUE(summonPosition.isValid());
	// Battle-added units receive the summoned-slot identity that the runtime
	// `isSummoned()` predicate actually checks.
	battle::UnitInfo summonedInfo;
	summonedInfo.id = battle()->battleNextUnitId();
	summonedInfo.count = 1;
	summonedInfo.type = creature("core:peasant");
	summonedInfo.side = BattleSide::DEFENDER;
	summonedInfo.position = summonPosition;
	summonedInfo.summoned = true;
	BattleUnitsChanged addSummoned;
	addSummoned.battleID = BattleID(0);
	addSummoned.changedStacks.emplace_back(summonedInfo.id, UnitChanges::EOperation::ADD);
	summonedInfo.save(addSummoned.changedStacks.back().data);
	gameHandler->sendAndApply(addSummoned);
	auto * summoned = battle()->getStack(summonedInfo.id, false);
	ASSERT_NE(summoned, nullptr);
	ASSERT_TRUE(summoned->isSummoned());
	applyInjury(summoned, summoned->getAvailableHealth());
	EXPECT_TRUE(summonedUnits().empty());

	// Clone state is carried by an accepted unit-state update, as in other
	// native battle tests; direct mutation of the live CStack is not a packet.
	auto cloneState = clone->acquireState();
	cloneState->cloned = true;
	UnitChanges markClone(clone->unitId(), UnitChanges::EOperation::UPDATE);
	markClone.data = cloneState->save();
	BattleUnitsChanged updateClone;
	updateClone.battleID = BattleID(0);
	updateClone.changedStacks.push_back(std::move(markClone));
	gameHandler->sendAndApply(updateClone);
	ASSERT_TRUE(clone->isClone());
	applyInjury(clone, clone->getAvailableHealth());
	EXPECT_TRUE(clone->isClone());
	ASSERT_EQ(server.injuries.back().stacks.size(), 1u);
	EXPECT_TRUE(server.injuries.back().stacks.front().cloneKilled());
	EXPECT_EQ(server.injuries.back().stacks.front().flags & BattleStackAttacked::KILLED, 0u);
	EXPECT_TRUE(summonedUnits().empty());
}

TEST_F(NewHorizonsElementalRebirthTest, NativePhoenixRebirthSurvivorDoesNotCreateElemental)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 5, "core:phoenix"));
	const auto originalCount = battle()->battleGetAllUnits(false).size();
	applyInjury(source, source->getAvailableHealth(), false, std::nullopt, true);

	ASSERT_TRUE(source->alive()) << "The Phoenix's own Rebirth should leave its stack alive";
	EXPECT_TRUE(summonedUnits().empty());
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalCount);
	EXPECT_TRUE(std::ranges::none_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Elemental Rebirth summons") != std::string::npos;
	}));
}

TEST_F(NewHorizonsElementalRebirthTest, BasisRoundTripsAndRejectsOlderSave)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	const auto basis = source->getBattleStartMaximumAggregateHP();
	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_DIVINE_MANDATE;
	EXPECT_THROW(oldWriter.oser & *source, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	source->detachFromAll();
	CMemorySerializer currentWriter;
	currentWriter.oser.version = ESerializationVersion::CURRENT;
	currentWriter.oser & *source;
	CMemorySerializer currentReader(currentWriter.extractBuffer());
	currentReader.iser.version = ESerializationVersion::CURRENT;
	currentReader.iser.cb = gameState().get();
	CStack restored;
	currentReader.iser & restored;
	EXPECT_EQ(restored.getBattleStartMaximumAggregateHP(), basis);
}
