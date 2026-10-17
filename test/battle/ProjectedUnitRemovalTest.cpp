/*
 * ProjectedUnitRemovalTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../bonus/ProjectedUnitEnvironmentFixture.h"
#include "../../lib/battle/ProjectedUnitRemoval.h"
#include <stdexcept>

namespace test
{
using namespace ::testing;
class ProjectedUnitRemovalTest : public Test, protected environmentFixture::Projection
{
protected:
	std::vector<battle::ProjectedUnitRemoval::Binding> bindings()
	{
		std::vector<battle::ProjectedUnitRemoval::Binding> result;
		for(size_t index = 0; index < units.size(); ++index)
			result.push_back({originals[index], units[index].get()});
		return result;
	}
};

TEST_F(ProjectedUnitRemovalTest, ConcreteCloneRemovalChangesOwnedValuesAndEdgesWithoutChangingOriginals)
{
	subject.attachTo(base);
	firstCart.attachTo(base);
	initialize();
	units[0]->cloneID = 18;
	units[1]->cloned = true;
	units[2]->cloneID = 17;
	units[0]->guardianSpiritHitPoints = 4;
	units[0]->guardianSpiritRoundsRemaining = 1;
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto result = removal.removeValuesAndEdges(17);
	ASSERT_TRUE(result.completed);
	EXPECT_TRUE(units[0]->ghost);
	EXPECT_TRUE(units[1]->ghost);
	EXPECT_FALSE(units[2]->ghost);
	EXPECT_EQ(units[0]->guardianSpiritHitPoints, 0);
	EXPECT_EQ(units[0]->cloneID, -1);
	EXPECT_EQ(units[2]->cloneID, -1);
	EXPECT_TRUE(graph.parents(subject).empty());
	EXPECT_TRUE(graph.parents(firstCart).empty());
	EXPECT_TRUE(subject.alive());
	EXPECT_FALSE(subject.ghost);
	EXPECT_TRUE(firstCart.alive());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitRemovalTest, AlreadyGhostSkipsItsLinksButStillCompletesAsOriginalKernel)
{
	initialize();
	units[0]->ghost = true;
	units[0]->cloneID = 18;
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	EXPECT_TRUE(removal.removeValuesAndEdges(17).completed);
	EXPECT_EQ(units[0]->cloneID, 18);
	EXPECT_FALSE(units[1]->ghost);
}

TEST_F(ProjectedUnitRemovalTest, MissingCloneReturnsAfterPrivatePrefixAndBeforeWorldCompletionStage)
{
	initialize();
	units[0]->cloneID = 999;
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	const auto result = removal.removeValuesAndEdges(17);
	EXPECT_FALSE(result.completed);
	EXPECT_EQ(result.missingId, 999u);
	EXPECT_TRUE(units[0]->ghost);
	EXPECT_TRUE(subject.alive());
	// Legacy early return is not a malformed-state rejection; later operations
	// may still use this private owner, while skipping completion sidecars.
	EXPECT_TRUE(removal.removeValuesAndEdges(18).completed);
}

TEST_F(ProjectedUnitRemovalTest, MissingRootLeavesEveryProjectedValueUntouched)
{
	initialize();
	const auto before = units[0]->save();
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	const auto result = removal.removeValuesAndEdges(999);
	EXPECT_FALSE(result.completed);
	EXPECT_EQ(result.missingId, 999u);
	EXPECT_EQ(units[0]->save(), before);
}

TEST_F(ProjectedUnitRemovalTest, ActiveFormRemovalRunsSynchronousEndBeforeGeneralSourceDetach)
{
	initialize();
	units[0]->beginBattleForm(CreatureID(2), 2);
	ASSERT_TRUE(units[0]->hasBattleForm());
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	ASSERT_TRUE(removal.removeValuesAndEdges(17).completed);
	EXPECT_FALSE(units[0]->hasBattleForm());
	EXPECT_TRUE(graph.parents(subject).empty());
	EXPECT_TRUE(units[0]->ghost);
	EXPECT_FALSE(subject.hasBattleForm());
}

TEST_F(ProjectedUnitRemovalTest, ExactOriginalProviderAndOrderedRosterAreRequired)
{
	initialize();
	auto wrong = bindings();
	std::swap(wrong[0], wrong[1]);
	EXPECT_THROW((battle::ProjectedUnitRemoval(graph, *roster, wrong)), std::invalid_argument);
	wrong = bindings();
	wrong[0].original = &firstCart;
	EXPECT_THROW((battle::ProjectedUnitRemoval(graph, *roster, wrong)), std::invalid_argument);
}

TEST_F(ProjectedUnitRemovalTest, StaleGraphFailureAfterPrivateValuePrefixAbandonsEveryFurtherGraphQuery)
{
	initialize();
	battle::ProjectedUnitRemoval removal(graph, *roster, bindings());
	subject.nodeHasChanged();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(removal.removeValuesAndEdges(17), std::logic_error);
	EXPECT_TRUE(units[0]->ghost); // failure may leave private values partially staged
	EXPECT_FALSE(subject.ghost);
	EXPECT_TRUE(subject.alive());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.query(subject, Selector::all, false), std::logic_error);
	EXPECT_THROW(removal.removeValuesAndEdges(18), std::logic_error);
}
}
