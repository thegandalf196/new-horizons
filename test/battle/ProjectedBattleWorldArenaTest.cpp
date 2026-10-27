/*
 * ProjectedBattleWorldArenaTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <limits>
#include <type_traits>
#include <stdexcept>

namespace
{
using Arena = battle::ProjectedBattleWorldArena;
static_assert(!std::is_copy_constructible_v<Arena>);
static_assert(!std::is_move_constructible_v<Arena>);

class ProjectedBattleWorldArenaTest : public BattleTestFixture
{
protected:
	std::unique_ptr<Arena> arena;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}
	void TearDown() override
	{
		arena.reset(); // graph/finalizer retire BEFORE borrowed battle identities
		BattleTestFixture::TearDown();
	}
	JsonNode addition(CreatureID type = CreatureID(0), BattleHex position = BattleHex(leftHex))
	{
		battle::UnitInfo info;
		info.type = type;
		info.count = 4;
		info.side = BattleSide::ATTACKER;
		info.position = position;
		info.summoned = true;
		JsonNode data;
		info.save(data);
		return data;
	}
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	void expectScalarAddition(JsonNode data)
	{
		capture();
		const auto index = arena->size();
		const auto id = arena->nextUnitId();
		const auto version = PreparedBonusGraphStorage::currentGeneration();
		arena->addUnit(id, data);
		const auto expected = arena->snapshotAt(index);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), version);
		EXPECT_EQ(battle()->stacks.size(), index);
		arena.reset(); // no borrowed-source mutation while projection exists
		battle()->addUnit(id, data);
		ASSERT_EQ(battle()->stacks.size(), index + 1);
		EXPECT_EQ(battle()->stacks.back()->save(), expected);
	}
};

TEST_F(ProjectedBattleWorldArenaTest, CaptureRetainsEveryPhysicalOccurrenceAndExactValues)
{
	capture();
	ASSERT_EQ(arena->size(), battle()->stacks.size());
	for(size_t i = 0; i < arena->size(); ++i)
	{
		EXPECT_EQ(&arena->originalAt(i), battle()->stacks[i].get());
		EXPECT_EQ(arena->roster().orderedUnits()[i], &arena->unitAt(i));
		EXPECT_EQ(arena->snapshotAt(i), battle()->stacks[i]->save());
	}
}

TEST_F(ProjectedBattleWorldArenaTest, OrdinaryAdditionMatchesLiveInitializationWholeJson)
{
	expectScalarAddition(addition());
}

TEST_F(ProjectedBattleWorldArenaTest, NatureSummonedAdditionPreservesResetAndRestoreOrdering)
{
	auto data = addition();
	data["natureSummoned"].Bool() = true;
	expectScalarAddition(data);
}

TEST_F(ProjectedBattleWorldArenaTest, PhantomProfileMatchesLiveWholeJsonWithoutChangingPacket)
{
	auto data = addition();
	data["phantomIntegrity"].Integer() = 7;
	data["phantomDuration"].Integer() = newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS;
	const auto originalData = data;
	expectScalarAddition(data);
	EXPECT_EQ(data, originalData);
}

TEST_F(ProjectedBattleWorldArenaTest, CapturedActiveFormUsesExactProjectedProviderNotOrdinarySaveFallback)
{
	auto * original = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
	original->beginBattleForm(CreatureID(2), 2);
	const auto expected = original->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	capture();
	const auto index = battle()->stacks.size() - 1;
	ASSERT_TRUE(arena->unitAt(index).hasBattleForm());
	EXPECT_EQ(arena->snapshotAt(index), expected);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedBattleWorldArenaTest, RebirthAdditionMatchesLiveAndRetainsSeparateTemporaryDomain)
{
	auto data = addition();
	data["rebirthOriginalAggregateHP"].Integer() = 1;
	capture();
	const auto index = arena->size();
	const auto id = arena->nextUnitId();
	const auto & added = arena->addUnit(id, data);
	EXPECT_EQ(added.getRebirthOriginalAggregateHP(), 1);
	EXPECT_EQ(arena->retainedQueryCount(), 1);
	EXPECT_EQ(arena->size(), index + 1);
	EXPECT_EQ(arena->roster().orderedUnits().size(), index + 1);
	EXPECT_EQ(arena->findUnit(uint32_t(-1)), nullptr);
	const auto expected = arena->snapshotAt(index);
	arena.reset();
	battle()->addUnit(id, data);
	EXPECT_EQ(battle()->stacks.back()->save(), expected);
}

TEST_F(ProjectedBattleWorldArenaTest, RepeatedQueriesRetainDistinctIdentitiesOutsideRoster)
{
	auto data = addition();
	data["rebirthOriginalAggregateHP"].Integer() = 1;
	capture();
	const auto initial = arena->size();
	arena->addUnit(arena->nextUnitId(), data);
	arena->addUnit(arena->nextUnitId(), data);
	EXPECT_EQ(arena->retainedQueryCount(), 2);
	EXPECT_EQ(arena->size(), initial + 2);
	EXPECT_EQ(arena->ownedGraphNodes().size(), 4);
	EXPECT_EQ(arena->findUnit(uint32_t(-1)), nullptr);
	EXPECT_EQ(arena->roster().orderedUnits().size(), initial + 2);
}

TEST_F(ProjectedBattleWorldArenaTest, AppendingKeepsOriginalStateAndRosterAddressesStable)
{
	capture();
	const auto initial = arena->size();
	const auto * original = &arena->originalAt(0);
	const auto * state = &arena->unitAt(0);
	const auto * roster = &arena->roster();
	for(int i = 0; i < 24; ++i)
		arena->addUnit(arena->nextUnitId(), addition());
	EXPECT_EQ(&arena->originalAt(0), original);
	EXPECT_EQ(&arena->unitAt(0), state);
	EXPECT_EQ(&arena->roster(), roster);
	EXPECT_EQ(arena->size(), initial + 24);
	EXPECT_EQ(battle()->stacks.size(), initial);
}

TEST_F(ProjectedBattleWorldArenaTest, PlacementUsesLatestAddedAndRemovedOccurrences)
{
	capture();
	const auto position = BattleHex(leftHex);
	ASSERT_EQ(arena->accessibility()[position.toInt()], EAccessibility::ACCESSIBLE);
	const auto id = arena->nextUnitId();
	arena->addUnit(id, addition(CreatureID(0), position));
	EXPECT_EQ(arena->accessibility()[position.toInt()], EAccessibility::ALIVE_STACK);
	ASSERT_TRUE(arena->removeValuesAndEdges(id).completed);
	EXPECT_EQ(arena->accessibility()[position.toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_TRUE(arena->findUnit(id)->isGhost());
	EXPECT_EQ(arena->findUnit(id, true), nullptr);
}

TEST_F(ProjectedBattleWorldArenaTest, EnvironmentUsesLatestAmmoCartAndPreservesDeadFirstCartPolicy)
{
	const auto attackers = battle()->getUnitsIf([](const battle::Unit * unit)
	{
		return unit->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_FALSE(attackers.empty());
	capture();
	const auto * selected = arena->findUnit(attackers.front()->unitId());
	ASSERT_NE(selected, nullptr);
	const auto & subject = *selected;
	ASSERT_EQ(subject.unitSide(), BattleSide::ATTACKER);
	ASSERT_FALSE(arena->environment().unitHasAmmoCart(&subject));
	const auto first = arena->nextUnitId();
	arena->addUnit(first, addition(CreatureID::AMMO_CART));
	EXPECT_TRUE(arena->environment().unitHasAmmoCart(&subject));
	ASSERT_TRUE(arena->removeValuesAndEdges(first).completed);
	arena->addUnit(arena->nextUnitId(), addition(CreatureID::AMMO_CART));
	EXPECT_FALSE(arena->environment().unitHasAmmoCart(&subject));
}

TEST_F(ProjectedBattleWorldArenaTest, MissingRemoveIsEarlyReturnButMissingUpdateAbandonsOwner)
{
	capture();
	const auto before = arena->snapshotAt(0);
	const auto missing = arena->removeValuesAndEdges(0xfffffff0u);
	EXPECT_FALSE(missing.completed);
	EXPECT_EQ(arena->snapshotAt(0), before);
	EXPECT_THROW(arena->classifyUpdate(0xfffffff0u, 0), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
}

TEST_F(ProjectedBattleWorldArenaTest, TimeStopSkipsHealthAndZeroDeltaStateBeforeAnyPayloadAdmission)
{
	auto * stopped = battle()->stacks.front().get();
	stopped->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	capture();
	EXPECT_EQ(arena->classifyUpdate(stopped->unitId(), -1), Arena::UpdateDecision::IGNORE_TIME_STOP);
	EXPECT_EQ(arena->classifyUpdate(stopped->unitId(), 0), Arena::UpdateDecision::IGNORE_TIME_STOP);
	// Deliberately no JSON argument: ignored payloads never reach the ADD decoder
	// or invalid-creature guard; this is the shared PREPARSE decision only.
	EXPECT_EQ(arena->size(), battle()->stacks.size());
}

TEST_F(ProjectedBattleWorldArenaTest, OccupiedCorpseIgnoreUsesSequentialPlacementBeforePayloadParse)
{
	auto * corpse = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
	int64_t damage = corpse->getAvailableHealth();
	corpse->damage(damage);
	ASSERT_FALSE(corpse->alive());
	capture();
	const auto blocker = arena->nextUnitId();
	arena->addUnit(blocker, addition());
	EXPECT_EQ(arena->classifyUpdate(corpse->unitId(), 1), Arena::UpdateDecision::IGNORE_OCCUPIED_RESURRECTION);
	ASSERT_TRUE(arena->removeValuesAndEdges(blocker).completed);
	EXPECT_EQ(arena->classifyUpdate(corpse->unitId(), 1), Arena::UpdateDecision::APPLY);
}

TEST_F(ProjectedBattleWorldArenaTest, AbsentCreatureRejectsBeforeColdConstructionAndAbandonsEntirePrefix)
{
	capture();
	const auto originalSize = battle()->stacks.size();
	arena->addUnit(arena->nextUnitId(), addition());
	auto invalid = addition();
	invalid.Struct().erase("type");
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(arena->addUnit(arena->nextUnitId(), invalid), std::runtime_error);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(battle()->stacks.size(), originalSize);
	EXPECT_THROW(arena->snapshotAt(0), std::logic_error);
}

TEST_F(ProjectedBattleWorldArenaTest, CapacityFailureRetiresWholeOwnerWithoutBattlefieldPrefix)
{
	auto invalid = addition();
	invalid["rebirthOriginalAggregateHP"].Integer() = std::numeric_limits<int64_t>::max();
	capture();
	const auto originalSize = battle()->stacks.size();
	EXPECT_THROW(arena->addUnit(arena->nextUnitId(), invalid), std::runtime_error);
	EXPECT_EQ(battle()->stacks.size(), originalSize);
	EXPECT_THROW(arena->retainedQueryCount(), std::logic_error);
}

class RejectAddedIdentityUpdater final : public IUpdater
{
	uint32_t target;
public:
	explicit RejectAddedIdentityUpdater(uint32_t target) : target(target) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(const auto * unit = context.battleUnit(); unit && unit->unitId() == target)
			throw std::runtime_error("synthetic added identity health query rejection");
		return bonus;
	}
};

TEST_F(ProjectedBattleWorldArenaTest, InitializationFailureAfterPrivateAttachAbandonsBeforeOwnedKeysRetire)
{
	const auto id = battle()->nextUnitId();
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 1, BonusSourceID());
	bonus->updater = std::make_shared<RejectAddedIdentityUpdater>(id);
	attackerSideHero->addNewBonus(bonus);
	capture();
	const auto originalSize = battle()->stacks.size();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(arena->addUnit(id, addition()), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
	EXPECT_EQ(battle()->stacks.size(), originalSize);
	arena.reset();
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedBattleWorldArenaTest, IncompleteMutableAuthorityCannotFreezeOrPublishGraphStorage)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	EXPECT_THROW(arena->prepareGraphStorage({}), std::invalid_argument);
	EXPECT_FALSE(arena->graphStorageReady());
	EXPECT_THROW(arena->size(), std::logic_error);
}

TEST_F(ProjectedBattleWorldArenaTest, OwnedIdentityRetirementDoesNotEmitNativeGraphEvents)
{
	auto data = addition();
	data["rebirthOriginalAggregateHP"].Integer() = 1;
	capture();
	arena->addUnit(arena->nextUnitId(), data);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	arena.reset();
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedBattleWorldArenaTest, UnregisteredCreatureCannotConstructOrExposeAnAddedIdentity)
{
	capture();
	auto invalid = addition();
	invalid["type"].String() = "vcmi-test:unregisteredArenaCreature";
	const auto size = battle()->stacks.size();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(arena->addUnit(arena->nextUnitId(), invalid), std::exception);
	EXPECT_EQ(battle()->stacks.size(), size);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(arena->size(), std::logic_error);
}

class LegacyProjectedBattleWorldArenaTest : public ProjectedBattleWorldArenaTest
{
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode legacyRules;
		legacyRules.Struct();
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, legacyRules);
		// Same paired legacy-primary fixture boundary as HeroCommandFixture.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		// Weighted New Horizons perks require hero rules; this fixture is fully legacy.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
	}
};

TEST_F(LegacyProjectedBattleWorldArenaTest, HistoricalDuplicateIdsRetainPhysicalOrderAndFirstMatchLookup)
{
	ASSERT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
	capture();
	const auto * first = &arena->unitAt(0);
	const auto id = first->unitId();
	const auto index = arena->size();
	const auto & second = arena->addUnit(id, addition());
	EXPECT_NE(&second, first);
	EXPECT_NE(&arena->originalAt(0), &arena->originalAt(index));
	EXPECT_EQ(arena->findUnit(id), first);
	EXPECT_EQ(arena->roster().firstById(id), first);
	ASSERT_TRUE(arena->removeValuesAndEdges(id).completed);
	EXPECT_EQ(arena->findUnit(id), first); // ghost is still first for unfiltered lookup
	EXPECT_TRUE(first->isGhost());
	EXPECT_EQ(arena->findUnit(id, true), &second); // filter BEFORE first-match
	EXPECT_TRUE(second.alive());
	EXPECT_EQ(arena->nextUnitId(), index + 1);
}
}

TEST_F(ProjectedBattleWorldArenaTest, TimedSummonSpawnMatchesAuthoritativeWholeState)
{
	auto data = addition();
	data["summonRoundsRemaining"].Integer() = 3;
	expectScalarAddition(data);
}
