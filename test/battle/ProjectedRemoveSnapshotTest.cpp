/*
 * ProjectedRemoveSnapshotTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <stdexcept>

namespace
{
using Arena = battle::ProjectedBattleWorldArena;
using Operation = BattleChanges::EOperation;
struct Observation
{
	const battle::Unit * provider;
	size_t rosterSize;
};
struct Probe
{
	Arena * arena = nullptr;
	bool armed = false;
	bool fail = false;
	std::vector<Observation> observations;
};

// Test-only query observation; the bonus value and production source stay unchanged.
class SnapshotQueryProbe final : public IUpdater
{
	std::shared_ptr<Probe> probe;
public:
	explicit SnapshotQueryProbe(std::shared_ptr<Probe> value) : probe(std::move(value)) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(probe->armed && context.getNodeType() == BonusNodeType::STACK_BATTLE)
		{
			probe->observations.push_back({context.battleUnit(), probe->arena->size()});
			if(probe->fail)
				throw std::runtime_error("Snapshot health query probe");
		}
		return bonus;
	}
};

class ProjectedRemoveSnapshotTest : public BattleTestFixture
{
protected:
	std::shared_ptr<Probe> probe = std::make_shared<Probe>();
	std::unique_ptr<Arena> arena;
	CStack * first = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
		first = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(5, 5), 4);
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
			BonusSource::OTHER, 0, BonusSourceID());
		bonus->updater = std::make_shared<SnapshotQueryProbe>(probe);
		first->addNewBonus(bonus);
	}
	void TearDown() override
	{
		probe->armed = false;
		arena.reset();
		BattleTestFixture::TearDown();
	}
	void capture()
	{
		arena = std::make_unique<Arena>(*battle());
		probe->arena = arena.get();
	}
	const battle::CUnitStateDetached & subject()
	{
		const auto * result = arena->findUnit(first->unitId());
		if(!result)
			throw std::logic_error("Missing captured test subject");
		return *result;
	}
	JsonNode snapshotSubject()
	{
		const auto * selected = &subject();
		for(size_t index = 0; index < arena->size(); ++index)
		{
			if(&arena->unitAt(index) == selected)
				return arena->snapshotAt(index);
		}
		throw std::logic_error("Missing physical test occurrence");
	}
	void expectPreentryFailure(std::vector<UnitChanges> changes)
	{
		capture();
		const auto original = first->save();
		const auto initialSize = arena->size();
		const auto * provider = &subject();
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		probe->armed = probe->fail = true;
		EXPECT_THROW(arena->applyUnitChanges(changes), std::runtime_error);
		probe->armed = false;
		ASSERT_FALSE(probe->observations.empty());
		EXPECT_EQ(probe->observations.front().provider, provider);
		EXPECT_EQ(probe->observations.front().rosterSize, initialSize);
		EXPECT_EQ(first->save(), original);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		EXPECT_THROW(arena->size(), std::logic_error); // whole owner abandoned
	}
};

TEST_F(ProjectedRemoveSnapshotTest, SnapshotUsesRealDetachedOwnershipAndExpiresWithoutRosterInsertion)
{
	capture();
	const auto & source = subject();
	const auto count = arena->size();
	probe->armed = true;
	auto state = source.acquireState();
	probe->armed = false;
	const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(state.get());
	ASSERT_NE(detached, nullptr);
	EXPECT_NE(state.get(), &source);
	EXPECT_TRUE(detached->usesUnitInfo(source));
	EXPECT_TRUE(detached->usesBonusProvider(source));
	EXPECT_EQ(state->save(), snapshotSubject());
	EXPECT_EQ(state->summoned, source.summoned);
	ASSERT_FALSE(probe->observations.empty());
	for(const auto & observation : probe->observations)
		EXPECT_EQ(observation.provider, &source);
	std::weak_ptr<battle::CUnitState> lifetime = state;
	state.reset();
	EXPECT_TRUE(lifetime.expired());
	EXPECT_EQ(arena->size(), count);
}

TEST_F(ProjectedRemoveSnapshotTest, HealthQueryFailurePrecedesEveryEntryAndAbandonsWholeProgram)
{
	battle::UnitInfo info;
	info.type = CreatureID(0);
	info.count = 1;
	info.side = BattleSide::ATTACKER;
	info.position = BattleHex(3, 3);
	UnitChanges add(10000, Operation::ADD);
	info.save(add.data);
	expectPreentryFailure({add, UnitChanges(first->unitId(), Operation::REMOVE)});
}

TEST_F(ProjectedRemoveSnapshotTest, SummonedExclusionStillFollowsActualSnapshotQuery)
{
	first->summoned = true;
	expectPreentryFailure({UnitChanges(first->unitId(), Operation::REMOVE)});
}

TEST_F(ProjectedRemoveSnapshotTest, CloneExclusionStillFollowsActualSnapshotQuery)
{
	first->cloned = true;
	expectPreentryFailure({UnitChanges(first->unitId(), Operation::REMOVE)});
}

TEST_F(ProjectedRemoveSnapshotTest, MissingRemovalShortCircuitsBeforeAcquisition)
{
	capture();
	probe->armed = probe->fail = true;
	EXPECT_NO_THROW(arena->applyUnitChanges({UnitChanges(10000, Operation::REMOVE)}));
	probe->armed = false;
	EXPECT_TRUE(probe->observations.empty());
}

TEST_F(ProjectedRemoveSnapshotTest, DeadRemovalShortCircuitsBeforeAcquisition)
{
	int64_t damage = first->getAvailableHealth();
	first->damage(damage);
	ASSERT_FALSE(first->alive());
	capture();
	probe->armed = probe->fail = true;
	EXPECT_NO_THROW(arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE)}));
	probe->armed = false;
	EXPECT_TRUE(probe->observations.empty());
}

TEST_F(ProjectedRemoveSnapshotTest, ActiveFormSnapshotUsesCurrentPrivateProviderAndMeasuredNativeGenerationDelta)
{
	const auto original = first->save();
	first->beginBattleForm(CreatureID(2), 3);
	const auto requested = first->save();
	first->load(original);
	ASSERT_EQ(first->save(), original);
	capture();
	EXPECT_EQ(arena->updateUnit(first->unitId(), requested, 0), Arena::UpdateDecision::APPLY);
	const auto & source = subject();
	ASSERT_EQ(source.battleFormCreature(), CreatureID(2));
	ASSERT_EQ(first->unitType()->getId(), CreatureID(0)); // live original deliberately differs
	const auto before = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	const auto size = arena->size();
	JsonNode copied;
	probe->armed = true;
	{
		const auto state = source.acquireState();
		EXPECT_EQ(state->battleFormCreature(), source.battleFormCreature());
		EXPECT_EQ(state->getMaxHealth(), source.getMaxHealth());
		copied = state->save();
		EXPECT_EQ(copied, snapshotSubject());
	}
	probe->armed = false;
	const auto projectedDelta = uint32_t(PreparedBonusGraphStorage::currentGeneration()) - before;
	EXPECT_EQ(arena->size(), size);
	EXPECT_EQ(first->save(), original);
	ASSERT_FALSE(probe->observations.empty());
	for(const auto & observation : probe->observations)
		EXPECT_EQ(observation.provider, &source);
	arena.reset(); // native oracle changes only AFTER every private borrowed view retires
	first->load(requested);
	const auto beforeNative = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	{ const auto state = first->acquireState(); EXPECT_EQ(state->save(), copied); }
	const auto nativeDelta = uint32_t(PreparedBonusGraphStorage::currentGeneration()) - beforeNative;
	EXPECT_EQ(projectedDelta, nativeDelta); // measured native event delta, not an assumed zero
}

TEST_F(ProjectedRemoveSnapshotTest, DuplicateRemovePrepassDoesNotDeduplicateSnapshotAcquisitions)
{
	capture();
	probe->armed = true;
	{ const auto state = subject().acquireState(); EXPECT_FALSE(state->summoned); }
	const auto single = probe->observations.size();
	ASSERT_GT(single, 0);
	probe->observations.clear();
	const auto id = first->unitId();
	EXPECT_NO_THROW(arena->applyUnitChanges({UnitChanges(id, Operation::REMOVE), UnitChanges(id, Operation::REMOVE)}));
	probe->armed = false;
	EXPECT_GE(probe->observations.size(), 2 * single);
}

TEST_F(ProjectedRemoveSnapshotTest, WireCaptureAcquiresBeforeInvalidConsumptionShape)
{
	capture();
	BattleUnitsChanged packet;
	packet.battleID = battle()->getBattleID();
	packet.changedStacks.emplace_back(first->unitId(), Operation::REMOVE);
	packet.phoenixSparkConsumption.emplace(); // deliberately invalid receipt/entry combination
	const auto * provider = &subject();
	probe->armed = probe->fail = true;
	try
	{
		arena->applyPacket(packet);
		FAIL() << "Expected the REMOVE snapshot health query failure";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Snapshot health query probe");
	}
	probe->armed = false;
	ASSERT_FALSE(probe->observations.empty());
	EXPECT_EQ(probe->observations.front().provider, provider);
	EXPECT_THROW(arena->size(), std::logic_error);
}
}
