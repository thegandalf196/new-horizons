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
#include "../../lib/battle/NewHorizonsCommandResources.h"
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
		ASSERT_TRUE(newHorizonsCommand::usesPerkRules(perks));
		ASSERT_FALSE(newHorizonsCrisisCommand::activeProfile(perks));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(CGHeroInstance * hero)
	{
		const SecondarySkill rage(SecondarySkill::decode("new-horizons:bloodrage"));
		hero->setSecSkillLevel(rage, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:bloodrage", "new-horizons:bloodrage.firstBlood"});
		ASSERT_TRUE(newHorizonsBloodrage::hasFirstBlood(hero));
	}
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true; // Current finite Command rules, not the historical V3 snapshot.
		startGame();
		ASSERT_NO_FATAL_FAILURE(select(attackerSideHero));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero));
		startBattle();
		for(const auto side : {ATT, DEF})
		{
			const auto & resource = battle()->getCommandResources(side);
			ASSERT_TRUE(resource.initialized);
			ASSERT_GT(resource.remaining, 0);
			ASSERT_EQ(resource.remaining, resource.maximum);
		}
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
			writer.oser & value.activeOrder & value.heroActionAllowances & value.commandResources;
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
	const auto attackerCommand = battle()->getCommandResources(ATT);
	const auto defenderCommand = battle()->getCommandResources(DEF);
	capture();
	arena->applyUnitChanges({secondDeath, firstDeath, UnitChanges(second->unitId(), BattleChanges::EOperation::REMOVE),
		UnitChanges(first->unitId(), BattleChanges::EOperation::REMOVE)});
	ASSERT_TRUE(arena->crisisState().pending.empty());
	ASSERT_FALSE(arena->crisisState().used.at(static_cast<size_t>(ATT)));
	ASSERT_FALSE(arena->crisisState().used.at(static_cast<size_t>(DEF)));
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
	EXPECT_EQ(battle()->getCommandResources(ATT), attackerCommand);
	EXPECT_EQ(battle()->getCommandResources(DEF), defenderCommand);
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

TEST_F(PreparedBattleUnitSidecarsTest, AsymmetricBloodrageValuesKeepExactSideIdentityWithoutCrisisReceipt)
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
	EXPECT_TRUE(battle()->getCrisisCommandState().pending.empty());
	EXPECT_FALSE(battle()->getCrisisCommandState().used.at(static_cast<size_t>(ATT)));
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
	void prepareCorpse()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		ASSERT_FALSE(newHorizonsElementalRebirth::activeProfile(defenderSideHero));
		auto lethal = source->getAvailableHealth();
		source->damage(lethal);
		ASSERT_FALSE(source->alive());
	}
};

TEST_F(PreparedBattleElementalSidecarsTest, RetiredChainCannotPublishSidecarsOrPrivateAddedRoot)
{
	ASSERT_NO_FATAL_FAILURE(prepareCorpse());
	const auto packet = retiredCandidate();
	const auto roots = battle()->stacks.size();
	const auto state = source->save();
	arena = std::make_unique<Arena>(*battle());
	EXPECT_THROW(arena->applyPacket(packet), std::runtime_error);
	EXPECT_THROW((Plan(*battle(), *arena)), std::logic_error);
	EXPECT_FALSE(battle()->getRebirthChainUsed(DEF));
	EXPECT_EQ(battle()->stacks.size(), roots);
	EXPECT_EQ(source->save(), state);
}

TEST_F(PreparedBattleElementalSidecarsTest, RetiredPhoenixCannotPublishUseOrResurrectLiveStack)
{
	ASSERT_NO_FATAL_FAILURE(prepareCorpse());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{DEF, source->unitId()};
	const auto roots = battle()->stacks.size();
	const auto state = source->save();
	arena = std::make_unique<Arena>(*battle());
	EXPECT_THROW(arena->applyPacket(packet), std::runtime_error);
	EXPECT_THROW((Plan(*battle(), *arena)), std::logic_error);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(DEF));
	EXPECT_EQ(battle()->stacks.size(), roots);
	EXPECT_EQ(source->save(), state);
}

TEST_F(PreparedBattleElementalSidecarsTest, IndependentSummonDoesNotPublishPrivateRootOrRetiredTokens)
{
	ASSERT_NO_FATAL_FAILURE(prepareCorpse());
	auto packet = retiredCandidate();
	packet.rebirthChainConsumption.reset(); // Ordinary independent summon; no retired effect request.
	const auto roots = battle()->stacks.size();
	arena = std::make_unique<Arena>(*battle());
	ASSERT_NO_THROW(arena->applyPacket(packet));
	ASSERT_NE(arena->findUnit(packet.changedStacks.front().id), nullptr);
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(battle()->stacks.size(), roots); // Sidecar commit never adopts the private added root.
	EXPECT_EQ(battle()->battleGetUnitByID(packet.changedStacks.front().id), nullptr);
	EXPECT_FALSE(battle()->getRebirthChainUsed(DEF));
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(DEF));
}
}
