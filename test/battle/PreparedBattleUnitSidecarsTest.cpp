/*
 * PreparedBattleUnitSidecarsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/NewHorizonsRebirthChainFixture.h"
#include "../../lib/battle/PreparedBattleUnitSidecars.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <type_traits>

namespace
{
using Plan = battle::PreparedBattleUnitSidecars;
using Arena = battle::ProjectedBattleWorldArena;
constexpr auto ATT = BattleSide::ATTACKER;
constexpr auto DEF = BattleSide::DEFENDER;
static_assert(noexcept(std::declval<Plan &>().commit()));
static_assert(noexcept(std::declval<const Plan &>().readyToCommit()));
static_assert(!std::is_copy_constructible_v<Plan> && !std::is_move_constructible_v<Plan>);

class PreparedBattleUnitSidecarsTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<Arena> arena;
	CStack * first = nullptr;
	CStack * second = nullptr;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		ASSERT_TRUE(newHorizonsCrisisCommand::activeProfile(perks));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(CGHeroInstance * hero)
	{
		const SecondarySkill command(SecondarySkill::decode(newHorizonsCrisisCommand::SKILL));
		hero->setSecSkillLevel(command, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, "new-horizons:command.aggressiveCommander"});
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK});
		ASSERT_TRUE(newHorizonsCrisisCommand::eligible(hero));
		const SecondarySkill rage(SecondarySkill::decode("new-horizons:bloodrage"));
		hero->setSecSkillLevel(rage, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:bloodrage", "new-horizons:bloodrage.firstBlood"});
		ASSERT_TRUE(newHorizonsBloodrage::hasFirstBlood(hero));
	}
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		ASSERT_NO_FATAL_FAILURE(select(attackerSideHero));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero));
		startBattle();
		first = addStack(ATT, CreatureID(0), BattleHex(leftHex), 4);
		second = addStack(DEF, CreatureID(0), BattleHex(rightHex), 4);
	}
	void TearDown() override { arena.reset(); HeroCommandFixture::TearDown(); }
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	std::vector<std::byte> sidecarBytes()
	{
		CMemorySerializer writer;
		for(const auto side : {ATT, DEF})
		{
			auto & value = battle()->getSide(side);
			writer.oser & value.orderStates & value.reducedExtraActivation;
			writer.oser & value.rebirthChainUsed & value.phoenixSparkUsed & value.bloodrageDamagePercent;
			writer.oser & value.activeOrder & value.heroActionAllowances;
		}
		auto crisis = battle()->getCrisisCommandState();
		auto seize = battle()->getSeizeInitiativeState();
		writer.oser & crisis & seize;
		writer.oser & battle()->bloodrageDestroyedUnits & battle()->bloodrageFirstBloodUsed;
		return writer.extractBuffer();
	}
	void remove(CStack & unit)
	{
		arena->applyUnitChanges({UnitChanges(unit.unitId(), BattleChanges::EOperation::REMOVE)});
	}
	UnitChanges death(CStack & unit)
	{
		const auto original = unit.save();
		auto damage = unit.getAvailableHealth();
		const auto healthDelta = -damage;
		unit.damage(damage);
		UnitChanges change(unit.unitId(), BattleChanges::EOperation::UPDATE);
		change.healthDelta = healthDelta;
		change.data = unit.save();
		unit.load(original);
		EXPECT_EQ(unit.save(), original);
		return change;
	}
};

TEST_F(PreparedBattleUnitSidecarsTest, SuccessfulDeathTransfersBothSidesAndPreservesOriginalUnits)
{
	const auto firstJson = first->save(), secondJson = second->save();
	const auto firstDeath = death(*first), secondDeath = death(*second);
	capture();
	arena->applyUnitChanges({secondDeath, firstDeath, UnitChanges(second->unitId(), BattleChanges::EOperation::REMOVE),
		UnitChanges(first->unitId(), BattleChanges::EOperation::REMOVE)});
	ASSERT_EQ(arena->crisisState().pending.size(), 2);
	ASSERT_TRUE(arena->firstBloodUsed());
	ASSERT_GT(arena->bloodrageDamage(ATT), battle()->getBloodrageDamagePercent(ATT));
	ASSERT_GT(arena->bloodrageDamage(DEF), battle()->getBloodrageDamagePercent(DEF));
	Plan plan(*battle(), *arena);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	ASSERT_TRUE(plan.readyToCommit());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->bloodrageDestroyedUnits, arena->bloodrageDeaths());
	EXPECT_EQ(battle()->bloodrageFirstBloodUsed, arena->firstBloodUsed());
	EXPECT_EQ(battle()->getCrisisCommandState().pending, arena->crisisState().pending);
	EXPECT_EQ(battle()->getCrisisCommandState().used, arena->crisisState().used);
	for(const auto side : {ATT, DEF})
	{
		EXPECT_EQ(battle()->getBloodrageDamagePercent(side), arena->bloodrageDamage(side));
		EXPECT_EQ(battle()->getRebirthChainUsed(side), arena->rebirthChainUsed(side));
		EXPECT_EQ(battle()->getPhoenixSparkUsed(side), arena->phoenixSparkUsed(side));
	}
	EXPECT_EQ(first->save(), firstJson);
	EXPECT_EQ(second->save(), secondJson);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitSidecarsTest, BindingOriginCannotBeRecapturedAtPlanConstruction)
{
	capture();
	battle()->getSide(ATT).bloodrageDamagePercent += 1;
	const auto bytes = sidecarBytes();
	EXPECT_THROW(Plan(*battle(), *arena), std::logic_error);
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, AsymmetricSideValuesAndSingleCrisisReceiptKeepExactSideIdentity)
{
	battle()->getSide(ATT).bloodrageDamagePercent = 1;
	battle()->getSide(DEF).bloodrageDamagePercent = 7;
	const auto killed = death(*first);
	capture();
	arena->applyUnitChanges({killed, UnitChanges(first->unitId(), BattleChanges::EOperation::REMOVE)});
	ASSERT_NE(arena->bloodrageDamage(ATT), arena->bloodrageDamage(DEF));
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->getBloodrageDamagePercent(ATT), arena->bloodrageDamage(ATT));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(DEF), arena->bloodrageDamage(DEF));
	EXPECT_TRUE(battle()->getCrisisCommandState().used.at(static_cast<size_t>(ATT)));
	EXPECT_FALSE(battle()->getCrisisCommandState().used.at(static_cast<size_t>(DEF)));
}

TEST_F(PreparedBattleUnitSidecarsTest, OriginalChangedAfterPreparationRefusesWithoutPrefix)
{
	capture();
	remove(*first);
	Plan plan(*battle(), *arena);
	battle()->getSide(DEF).rebirthChainUsed = true;
	const auto bytes = sidecarBytes();
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, DifferentPhysicalBattleCannotReceiveThisArena)
{
	capture();
	BattleInfo other(nullptr);
	const auto bytes = sidecarBytes();
	EXPECT_THROW(Plan(other, *arena), std::invalid_argument);
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, ProjectedChangedAfterPreparationRefusesWithoutPrefix)
{
	capture();
	remove(*first);
	Plan plan(*battle(), *arena);
	remove(*second);
	const auto bytes = sidecarBytes();
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, FailedArenaRefusesWithoutPublishingEarlierSidecars)
{
	capture();
	remove(*first);
	Plan plan(*battle(), *arena);
	const auto bytes = sidecarBytes();
	EXPECT_THROW(arena->applyUnitChanges({UnitChanges(999999, BattleChanges::EOperation::UPDATE)}), std::runtime_error);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, UnchangedAllowanceInputIsStillAFreshnessWitness)
{
	capture();
	Plan plan(*battle(), *arena);
	battle()->getSide(ATT).heroActionAllowances.nextGrantId += 1;
	const auto bytes = sidecarBytes();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, UnchangedBloodrageRankAndCapAreBothFreshnessWitnesses)
{
	capture();
	Plan plan(*battle(), *arena);
	auto & side = battle()->getSide(DEF);
	const auto rank = side.bloodrageRank, cap = side.bloodrageCapPercent;
	++side.bloodrageRank;
	EXPECT_FALSE(plan.commit());
	side.bloodrageRank = rank;
	++side.bloodrageCapPercent;
	EXPECT_FALSE(plan.commit());
	side.bloodrageCapPercent = cap;
	EXPECT_TRUE(plan.readyToCommit());
}

TEST_F(PreparedBattleUnitSidecarsTest, LegacyMarkerIsGuardedButNeverReconstructedFromEmptyOrders)
{
	battle()->getSide(ATT).activeOrder = HeroCommand::FOCUS_FIRE;
	ASSERT_TRUE(battle()->getHeroOrderStates(ATT).empty());
	capture();
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->getSide(ATT).activeOrder, HeroCommand::FOCUS_FIRE);
	EXPECT_TRUE(battle()->getHeroOrderStates(ATT).empty());
}

TEST_F(PreparedBattleUnitSidecarsTest, ChangedMarkerRefusesEvenWithEmptyCanonicalOrders)
{
	capture();
	Plan plan(*battle(), *arena);
	battle()->getSide(ATT).activeOrder = HeroCommand::FOCUS_FIRE;
	const auto bytes = sidecarBytes();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
}

TEST_F(PreparedBattleUnitSidecarsTest, UnchangedContainerAllocationsAreNotExchanged)
{
	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 1;
	battle()->setHeroOrderStates(ATT, {order});
	const auto * orderAddress = battle()->getSide(ATT).orderStates.data();
	capture();
	remove(*second);
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->getSide(ATT).orderStates.data(), orderAddress);
}

TEST_F(PreparedBattleUnitSidecarsTest, ChangedProtectStorageIsRetainedUntilControlledPlanRetirement)
{
	auto * partner = addStack(ATT, CreatureID(0), BattleHex(leftHex + 1), 4);
	HeroOrderState protect;
	protect.command = HeroCommand::PROTECT;
	protect.issuedRound = 1;
	protect.primaryTargetUnitId = first->unitId();
	protect.secondaryTargetUnitId = partner->unitId();
	battle()->setHeroOrderStates(ATT, {protect});
	const auto * oldStorage = battle()->getSide(ATT).orderStates.data();
	capture();
	auto moved = first->save();
	moved["state"]["position"].Integer() = leftHex - 4;
	arena->updateUnit(first->unitId(), moved, 0);
	ASSERT_TRUE(arena->orderStates(ATT).front().protectBroken);
	Plan plan(*battle(), *arena);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	ASSERT_TRUE(plan.commit());
	EXPECT_TRUE(battle()->getHeroOrderStates(ATT).front().protectBroken);
	EXPECT_FALSE(oldStorage->protectBroken); // old allocation remains owned by plan
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitSidecarsTest, ReducedActivationClearsWithoutResettingUsedAllowance)
{
	ReducedExtraActivationState reduced;
	reduced.enabled = true;
	reduced.used = true;
	reduced.activeUnitId = first->unitId();
	reduced.outputPercent = 50;
	battle()->setReducedExtraActivationState(ATT, reduced);
	capture();
	remove(*first);
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->getReducedExtraActivationState(ATT), arena->reducedActivation(ATT));
	EXPECT_TRUE(battle()->getReducedExtraActivationState(ATT).used);
	EXPECT_FALSE(battle()->getReducedExtraActivationState(ATT).hasActiveUnit());
}

TEST_F(PreparedBattleUnitSidecarsTest, CommitIsSingleUseAndRetirementDoesNotChangeGraphGeneration)
{
	capture();
	remove(*first);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		Plan plan(*battle(), *arena);
		ASSERT_TRUE(plan.commit());
		const auto bytes = sidecarBytes();
		EXPECT_FALSE(plan.readyToCommit());
		EXPECT_FALSE(plan.commit());
		EXPECT_EQ(sidecarBytes(), bytes);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitSidecarsTest, DerivedTerrainInputChangeRefusesInsteadOfQueryingFallback)
{
	capture();
	Plan plan(*battle(), *arena);
	const auto original = battle()->terrainType;
	battle()->terrainType = TerrainId::NONE;
	const auto bytes = sidecarBytes();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(sidecarBytes(), bytes);
	battle()->terrainType = original;
}

TEST(BattleUnitSidecarValueEqualityTest, FullFrameComparisonDoesNotReplaceContinuationSemantics)
{
	newHorizonsCrisisCommand::ReturnFrame first;
	first.action = BattleAction::makeHeroCommand(ATT, HeroCommand::CHARGE);
	auto second = first;
	ASSERT_EQ(first, second);
	second.phase = newHorizonsCrisisCommand::Phase::GRANTED_EXTRA;
	EXPECT_TRUE(first.sameContinuation(second));
	EXPECT_NE(first, second);
	second = first;
	second.action.spellCurePhysicalAffliction = "frozen";
	EXPECT_FALSE(first.sameContinuation(second));
	EXPECT_NE(first, second);
}

class PreparedBattleElementalSidecarsTest : public NewHorizonsRebirthChainFixture
{
protected:
	std::unique_ptr<Arena> arena;
	void TearDown() override { arena.reset(); NewHorizonsRebirthChainFixture::TearDown(); }
	void prepareSpark()
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill skill(SecondarySkill::decode(SKILL));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, "new-horizons:elementalRebirth.primalBurst"});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, CHAIN});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, "new-horizons:elementalRebirth.phoenixSpark"});
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:angel"), 3));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == DEF && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
		ASSERT_NE(source, nullptr);
		auto lethal = source->getAvailableHealth();
		source->damage(lethal);
		ASSERT_FALSE(source->alive());
	}
};

TEST_F(PreparedBattleElementalSidecarsTest, ChainConsumptionTransfersFlagButNotPrivateAddedRoot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * reborn = firstReborn();
	ASSERT_NE(reborn, nullptr);
	auto lethal = reborn->getAvailableHealth();
	reborn->damage(lethal);
	const auto packet = candidate(reborn);
	const auto originalRoots = battle()->stacks.size();
	arena = std::make_unique<Arena>(*battle());
	arena->applyPacket(packet);
	ASSERT_TRUE(arena->rebirthChainUsed(DEF));
	ASSERT_FALSE(battle()->getRebirthChainUsed(DEF));
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_TRUE(battle()->getRebirthChainUsed(DEF));
	EXPECT_EQ(battle()->stacks.size(), originalRoots);
}

TEST_F(PreparedBattleElementalSidecarsTest, PhoenixConsumptionTransfersFlagWithoutResurrectingLiveStack)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{DEF, source->unitId()};
	arena = std::make_unique<Arena>(*battle());
	arena->applyPacket(packet);
	ASSERT_TRUE(arena->phoenixSparkUsed(DEF));
	ASSERT_FALSE(battle()->getPhoenixSparkUsed(DEF));
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_TRUE(battle()->getPhoenixSparkUsed(DEF));
	EXPECT_FALSE(source->alive());
}
}
