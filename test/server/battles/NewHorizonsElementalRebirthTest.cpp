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
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <limits>
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
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(int rank, int sourceCount = 7, const char * sourceCreature = "core:peasant",
		bool includeCloneStack = false, bool selectPrimalBurst = false,
		bool selectGreaterEssence = false, bool selectElementalWard = false)
	{
		startGame();
		defenderSideHero->setHeroType(heroType("core:brissa"));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();

		const auto skillNumber = SecondarySkill::decode(std::string(REBIRTH_SKILL));
		ASSERT_GE(skillNumber, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(skillNumber), rank, ChangeValueMode::ABSOLUTE);
		if(selectGreaterEssence || selectElementalWard)
			selectPrimalBurst = true;
		if(selectPrimalBurst)
			defenderSideHero->applyPerkSelection({std::string(REBIRTH_SKILL),
				"new-horizons:elementalRebirth.primalBurst"});
		if(selectGreaterEssence)
			defenderSideHero->applyPerkSelection({std::string(REBIRTH_SKILL),
				"new-horizons:elementalRebirth.greaterEssence"});
		if(selectElementalWard)
			defenderSideHero->applyPerkSelection({std::string(REBIRTH_SKILL),
				"new-horizons:elementalRebirth.elementalWard"});
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

	uint32_t addTemporaryStack(BattleSide side, const CreatureID & type,
		const BattleHex & position, int32_t count)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = count;
		info.type = type;
		info.side = side;
		info.position = position;
		info.summoned = true;
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(add.changedStacks.back().data);
		gameHandler->sendAndApply(add);
		return info.id;
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
		const auto initialHP = expectedHP(basisAtStart, expectedPercent(rank));
		EXPECT_EQ(reborn->getAvailableHealth(), initialHP);
		EXPECT_EQ(reborn->getRebirthOriginalAggregateHP(), initialHP)
			<< "The exact spawn target must be retained as immutable Rebirth output provenance";
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

TEST_F(NewHorizonsElementalRebirthTest, RebirthOutputOriginalHPRemainsFixedAfterFurtherDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 17));
	applyInjury(source, source->getAvailableHealth());

	const auto spawns = summonedUnits();
	ASSERT_EQ(spawns.size(), 1u);
	auto * reborn = battle()->getStack(spawns.front()->unitId(), false);
	ASSERT_NE(reborn, nullptr);
	const auto originalHP = expectedHP(basisAtStart, 25);
	ASSERT_EQ(reborn->getRebirthOriginalAggregateHP(), originalHP);
	ASSERT_GT(reborn->getAvailableHealth(), 1);

	const auto liveHP = reborn->getAvailableHealth();
	applyInjury(reborn, 1, false, false);
	EXPECT_EQ(reborn->getAvailableHealth(), liveHP - 1);
	EXPECT_EQ(reborn->getRebirthOriginalAggregateHP(), originalHP)
		<< "Current wounds must not replace the immutable original Rebirth HP";
}

TEST_F(NewHorizonsElementalRebirthTest, AdvancedRankAloneDoesNotEnableUnselectedPerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED));
	const auto profile = newHorizonsElementalRebirth::activeProfile(defenderSideHero);
	ASSERT_TRUE(profile.has_value());
	EXPECT_FALSE(profile->greaterEssence);
	EXPECT_FALSE(profile->elementalWard);
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(
		{source->unitId(), source->unitSide(), source->getPosition(), basisAtStart, *profile}),
		expectedHP(basisAtStart, 40));
}

TEST_F(NewHorizonsElementalRebirthTest, SelectedBasicPrimalBurstSplitsOneBudgetAcrossUniqueHostiles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 1000, "core:peasant", false, true));
	ASSERT_TRUE(newHorizonsElementalRebirth::activeProfile(defenderSideHero)->primalBurst);

	std::vector<BattleHex> emptyAdjacent;
	for(const auto & hex : source->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, false))
			emptyAdjacent.push_back(hex);
	ASSERT_GE(emptyAdjacent.size(), 3u);

	const auto firstHostile = addTemporaryStack(BattleSide::ATTACKER, creature("core:pikeman"), emptyAdjacent[0], 20);
	const auto secondHostile = addTemporaryStack(BattleSide::ATTACKER, creature("core:pikeman"), emptyAdjacent[1], 20);
	const auto friendly = addTemporaryStack(BattleSide::DEFENDER, creature("core:pikeman"), emptyAdjacent[2], 20);
	Bonus reduction(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::OTHER, 2000, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	SetStackEffect protectFirst;
	protectFirst.battleID = BattleID(0);
	protectFirst.toAdd.emplace_back(firstHostile, std::vector<Bonus>{reduction});
	gameHandler->sendAndApply(protectFirst);
	const auto * firstBefore = battle()->battleGetUnitByID(firstHostile);
	const auto * secondBefore = battle()->battleGetUnitByID(secondHostile);
	const auto * friendlyBefore = battle()->battleGetUnitByID(friendly);
	ASSERT_NE(firstBefore, nullptr);
	ASSERT_NE(secondBefore, nullptr);
	ASSERT_NE(friendlyBefore, nullptr);
	const auto firstHealthBefore = firstBefore->getAvailableHealth();
	const auto secondHealthBefore = secondBefore->getAvailableHealth();
	const auto friendlyHealthBefore = friendlyBefore->getAvailableHealth();

	applyInjury(source, source->getAvailableHealth());

	const auto units = battle()->battleGetAllUnits(false);
	const auto rebornIt = std::ranges::find_if(units, [](const auto * unit)
	{
		if(!unit || !unit->isSummoned() || !unit->unitType())
			return false;
		const auto key = unit->unitType()->getJsonKey();
		return key == "core:airElemental" || key == "core:waterElemental"
			|| key == "core:fireElemental" || key == "core:earthElemental"
			|| key == "core:magicElemental";
	});
	ASSERT_NE(rebornIt, units.end());
	const auto * reborn = *rebornIt;
	const auto budget = newHorizonsElementalRebirth::primalBurstDamageBudget(reborn->getAvailableHealth());
	const auto targetCount = newHorizonsElementalRebirth::adjacentHostileUnitIds(*battle(), *reborn).size();
	EXPECT_EQ(targetCount, 2u) << "Each hostile unit ID receives only one equal share";
	const auto share = newHorizonsElementalRebirth::primalBurstShare(budget, targetCount);
	ASSERT_GT(share, 0);
	const auto * firstAfter = battle()->battleGetUnitByID(firstHostile);
	const auto * secondAfter = battle()->battleGetUnitByID(secondHostile);
	ASSERT_NE(firstAfter, nullptr);
	ASSERT_NE(secondAfter, nullptr);
	const auto expectedFirstDamage = newHorizonsMagicalAbilityDamage::adjustDamage(
		*battle(), *firstAfter, share);
	const auto expectedSecondDamage = newHorizonsMagicalAbilityDamage::adjustDamage(
		*battle(), *secondAfter, share);
	EXPECT_LT(expectedFirstDamage, share) << "Primal Burst uses the shared magical ability reduction seam";
	EXPECT_EQ(expectedSecondDamage, share) << "An unprotected target receives its full equal share";

	EXPECT_EQ(firstAfter->getAvailableHealth(), firstHealthBefore - expectedFirstDamage);
	EXPECT_EQ(secondAfter->getAvailableHealth(), secondHealthBefore - expectedSecondDamage);
	const auto * friendlyAfter = battle()->battleGetUnitByID(friendly);
	ASSERT_NE(friendlyAfter, nullptr);
	EXPECT_EQ(friendlyAfter->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Primal Burst deals") != std::string::npos;
	}));
	ASSERT_EQ(std::ranges::count_if(server.injuries, [](const StacksInjured & injury)
	{
		return injury.stacks.size() == 2;
	}), 1);
}

TEST_F(NewHorizonsElementalRebirthTest, BasicRankWithoutPrimalBurstSelectionDoesNotDamageAdjacentHostiles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 100));
	const auto profile = newHorizonsElementalRebirth::activeProfile(defenderSideHero);
	ASSERT_TRUE(profile.has_value());
	EXPECT_FALSE(profile->primalBurst);

	const auto emptyAdjacent = std::ranges::find_if(source->getSurroundingHexes(), [this](const BattleHex & hex)
	{
		return hex.isAvailable() && !battle()->battleGetUnitByPos(hex, false);
	});
	ASSERT_NE(emptyAdjacent, source->getSurroundingHexes().end());
	const auto hostile = addTemporaryStack(BattleSide::ATTACKER, creature("core:pikeman"), *emptyAdjacent, 20);
	const auto * before = battle()->battleGetUnitByID(hostile);
	ASSERT_NE(before, nullptr);
	const auto healthBefore = before->getAvailableHealth();

	applyInjury(source, source->getAvailableHealth());

	const auto * after = battle()->battleGetUnitByID(hostile);
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Primal Burst deals") != std::string::npos;
	}));
}

TEST_F(NewHorizonsElementalRebirthTest, SelectedAdvancedPerkRequiresAndPreservesBasicSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED, 100, "core:peasant", false, false, true));
	const auto profile = newHorizonsElementalRebirth::activeProfile(defenderSideHero);
	ASSERT_TRUE(profile.has_value());
	EXPECT_TRUE(defenderSideHero->hasActivePerk(std::string(REBIRTH_SKILL),
		"new-horizons:elementalRebirth.primalBurst"));
	EXPECT_TRUE(profile->primalBurst);
	EXPECT_TRUE(profile->greaterEssence);
	EXPECT_FALSE(profile->elementalWard);

	applyInjury(source, source->getAvailableHealth());
	const auto spawns = summonedUnits();
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	EXPECT_EQ(reborn->getAvailableHealth(), expectedHP(basisAtStart, 55));
	EXPECT_LT(reborn->getAvailableHealth(), static_cast<int64_t>(reborn->getCount()) * reborn->getMaxHealth());
}

TEST_F(NewHorizonsElementalRebirthTest, SelectedAdvancedWardIsAppliedToTheActualRebornStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED, 100, "core:peasant", false, false, false, true));
	const auto profile = newHorizonsElementalRebirth::activeProfile(defenderSideHero);
	ASSERT_TRUE(profile.has_value());
	EXPECT_TRUE(profile->primalBurst);
	EXPECT_FALSE(profile->greaterEssence);
	EXPECT_TRUE(profile->elementalWard);

	applyInjury(source, source->getAvailableHealth());
	const auto spawns = summonedUnits();
	ASSERT_EQ(spawns.size(), 1u);
	const auto wardReduction = spawns.front()->valOfBonuses(
		BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID(SpellSchool::ANY));
	EXPECT_GE(wardReduction, 2000);
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

	CMemorySerializer previousWriter;
	previousWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE;
	previousWriter.oser & *source;
	CMemorySerializer previousReader(previousWriter.extractBuffer());
	previousReader.iser.version = ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE;
	previousReader.iser.cb = gameState().get();
	CStack restoredLegacy;
	previousReader.iser & restoredLegacy;
	EXPECT_EQ(restoredLegacy.getRebirthOriginalAggregateHP(), 0)
		<< "Legacy stacks without the output metadata must load with the zero default";
}

TEST_F(NewHorizonsElementalRebirthTest, RebirthOutputOriginalHPRoundTripsAndRejectsOlderSave)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	applyInjury(source, source->getAvailableHealth());
	const auto spawns = summonedUnits();
	ASSERT_EQ(spawns.size(), 1u);
	auto * reborn = battle()->getStack(spawns.front()->unitId(), false);
	ASSERT_NE(reborn, nullptr);
	const auto originalHP = expectedHP(basisAtStart, 25);
	ASSERT_EQ(reborn->getRebirthOriginalAggregateHP(), originalHP);
	EXPECT_EQ(reborn->getBattleStartMaximumAggregateHP(), 0)
		<< "The spawned output HP is separate from the original stack's battle-start basis";

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE;
	EXPECT_THROW(oldWriter.oser & *reborn, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	reborn->detachFromAll();
	CMemorySerializer currentWriter;
	currentWriter.oser.version = ESerializationVersion::CURRENT;
	currentWriter.oser & *reborn;
	CMemorySerializer currentReader(currentWriter.extractBuffer());
	currentReader.iser.version = ESerializationVersion::CURRENT;
	currentReader.iser.cb = gameState().get();
	CStack restored;
	currentReader.iser & restored;
	EXPECT_EQ(restored.getRebirthOriginalAggregateHP(), originalHP);
	EXPECT_EQ(restored.getBattleStartMaximumAggregateHP(), 0);
}

TEST_F(NewHorizonsElementalRebirthTest, UnitInfoPreservesRebirthOriginalHPAsExactInt64)
{
	battle::UnitInfo info;
	info.id = 99;
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = 9007199254740993LL; // 2^53 + 1

	JsonNode data;
	info.save(data);
	EXPECT_EQ(data["rebirthOriginalAggregateHP"].Integer(), 9007199254740993LL);
	battle::UnitInfo restored;
	restored.load(info.id, data);
	EXPECT_EQ(restored.rebirthOriginalAggregateHP, info.rebirthOriginalAggregateHP);

	BattleUnitsChanged add;
	add.battleID = BattleID(0);
	add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	add.changedStacks.back().data = data;
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE;
	EXPECT_THROW(legacy.oser & add, std::runtime_error);
	EXPECT_TRUE(legacy.extractBuffer().empty())
		<< "The ADD pack must reject unsupported metadata before writing its header";

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & add;
	CMemorySerializer reader(current.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	BattleUnitsChanged received;
	reader.iser & received;
	ASSERT_EQ(received.changedStacks.size(), 1u);
	EXPECT_EQ(received.changedStacks.front().data["rebirthOriginalAggregateHP"].Integer(), 9007199254740993LL);

	info.rebirthOriginalAggregateHP = 0;
	JsonNode legacyData;
	info.save(legacyData);
	battle::UnitInfo restoredLegacy;
	restoredLegacy.load(info.id, legacyData);
	EXPECT_EQ(restoredLegacy.rebirthOriginalAggregateHP, 0)
		<< "Older ADD payloads that omit the key retain the zero default";
}

TEST_F(NewHorizonsElementalRebirthTest, UnitInfoRejectsMalformedRebirthOutputMetadata)
{
	battle::UnitInfo info;
	info.id = 99;
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = 100;
	JsonNode data;

	info.rebirthOriginalAggregateHP = -1;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.rebirthOriginalAggregateHP = 100;
	info.summoned = false;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.summoned = true;
	info.natureSummoned = true;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.natureSummoned = false;
	info.count = 0;
	EXPECT_THROW(info.save(data), std::runtime_error);
}

TEST_F(NewHorizonsElementalRebirthTest, InvalidRebirthOutputADDDoesNotMutateBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	const auto originalStackCount = battle()->battleGetAllUnits(false).size();

	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId();
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = std::numeric_limits<int64_t>::max();
	JsonNode data;
	info.save(data);

	EXPECT_THROW(battle()->addUnit(info.id, data), std::runtime_error);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalStackCount)
		<< "Invalid Rebirth output metadata must be rejected before publishing a stack";
}

TEST_F(NewHorizonsElementalRebirthTest, SharedWardBonusReducesMagicArrowDamageAndLeavesUnbonusedStackUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 7, "core:pikeman", true));
	auto * protectedUnit = stackAt(BattleSide::DEFENDER, SlotID(0));
	auto * control = stackAt(BattleSide::DEFENDER, SlotID(1));
	ASSERT_NE(protectedUnit, nullptr);
	ASSERT_NE(control, nullptr);

	const auto ward = newHorizonsElementalRebirth::elementalWardBonus(
		{MasteryLevel::ADVANCED, 40, false, false, true});
	ASSERT_TRUE(ward.has_value());
	SetStackEffect applyWard;
	applyWard.battleID = BattleID(0);
	applyWard.toAdd.emplace_back(protectedUnit->unitId(), std::vector<Bonus>{*ward});
	gameHandler->sendAndApply(applyWard);

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);

	EXPECT_EQ(mechanics->adjustEffectValue(control), rawDamage);
	EXPECT_EQ(mechanics->adjustEffectValue(protectedUnit), rawDamage * 80 / 100)
		<< "The existing spell receiver path must consume Ward as an independent 20% magical reduction";
}

TEST(NewHorizonsElementalRebirthPerkRulesTest, GreaterEssenceAddsFifteenPointsOnlyAtAdvancedAndExpert)
{
	newHorizonsElementalRebirth::DeathSnapshot snapshot;
	snapshot.battleStartMaximumAggregateHP = 1001;
	snapshot.profile = {MasteryLevel::ADVANCED, 40, false, false, false};
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(snapshot), 400);
	snapshot.profile.greaterEssence = true;
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(snapshot), 550);

	snapshot.profile = {MasteryLevel::EXPERT, 50, false, true, false};
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(snapshot), 650);

	snapshot.profile = {MasteryLevel::BASIC, 25, false, true, false};
	EXPECT_EQ(newHorizonsElementalRebirth::targetHP(snapshot), 0)
		<< "An advanced-only saved perk cannot be represented at Basic rank";
}

TEST(NewHorizonsElementalRebirthPerkRulesTest, ElementalWardBuildsOneBattleAnySchoolReduction)
{
	using namespace newHorizonsElementalRebirth;

	EXPECT_FALSE(elementalWardBonus({MasteryLevel::BASIC, 25, false, false, true}));
	const auto bonus = elementalWardBonus({MasteryLevel::ADVANCED, 40, false, false, true});
	ASSERT_TRUE(bonus.has_value());
	EXPECT_EQ(bonus->duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(bonus->type, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(bonus->source, BonusSource::SECONDARY_SKILL);
	EXPECT_EQ(bonus->sid, BonusSourceID(SecondarySkill(
		SecondarySkill::decode("new-horizons:elementalRebirth"))));
	EXPECT_EQ(bonus->val, 2000);
	EXPECT_EQ(bonus->subtype, BonusSubtypeID(SpellSchool::ANY));
	EXPECT_EQ(bonus->stacking, "new-horizons:elementalRebirth.elementalWard");
}
