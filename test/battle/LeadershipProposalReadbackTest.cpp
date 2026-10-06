/*
 * LeadershipProposalReadbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/widgets/NewHorizonsLeadershipReadback.h"

#include <limits>

namespace
{
using namespace newHorizonsLeadershipReadback;
}

TEST(LeadershipProposalReadbackTest, ComputesZeroAndPositiveAddedDemandPerSlot)
{
	const newHorizonsHeroes::LeadershipSlotCapacity capacity{30, 5, 6};

	const auto unchanged = evaluate(capacity, 2, 0);
	EXPECT_EQ(unchanged.maximumCount, 6);
	EXPECT_EQ(unchanged.existingCount, 2);
	EXPECT_EQ(unchanged.incomingCount, 0);
	EXPECT_EQ(unchanged.legalIncomingCount, 0);
	EXPECT_EQ(unchanged.resultingCount, 2);
	EXPECT_EQ(unchanged.addedLeadership, 0);
	EXPECT_EQ(unchanged.resultingDemand, 10);
	EXPECT_EQ(unchanged.excess, 0);

	const auto proposal = evaluate(capacity, 2, 4);
	EXPECT_EQ(proposal.existingCount, 2);
	EXPECT_EQ(proposal.incomingCount, 4);
	EXPECT_EQ(proposal.legalIncomingCount, 4);
	EXPECT_EQ(proposal.resultingCount, 6);
	EXPECT_EQ(proposal.addedLeadership, 20);
	EXPECT_EQ(proposal.resultingDemand, 30);
	EXPECT_EQ(proposal.excess, 0);
}

TEST(LeadershipProposalReadbackTest, PreservesOverMaximumIncomingProposalAndReportsItsDeficit)
{
	const newHorizonsHeroes::LeadershipSlotCapacity capacity{20, 4, 5};
	const auto proposal = evaluate(capacity, 3, 4);
	EXPECT_EQ(proposal.maximumCount, 5);
	EXPECT_EQ(proposal.legalIncomingCount, 2)
		<< "Legal transfer capacity is separate from the un-clamped requested proposal";
	EXPECT_EQ(proposal.resultingCount, 7)
		<< "The readback reports the actual proposed count rather than clamping it to slot capacity";
	EXPECT_EQ(proposal.addedLeadership, 16);
	EXPECT_EQ(proposal.resultingDemand, 28);
	EXPECT_EQ(proposal.excess, 8);

	const auto changedProposal = evaluate(capacity, 3, 3);
	EXPECT_NE(proposal, changedProposal);
}

TEST(LeadershipProposalReadbackTest, FractionalCapacityReportsShortfallBeforeTheCountLimit)
{
	const newHorizonsHeroes::LeadershipSlotCapacity capacity{875, 50, 17};
	const auto proposal = evaluate(capacity, 17, 1);

	EXPECT_EQ(proposal.legalIncomingCount, 0);
	EXPECT_EQ(proposal.incomingCount, 1);
	EXPECT_EQ(proposal.addedLeadership, 50);
	EXPECT_EQ(proposal.resultingCount, 18);
	EXPECT_EQ(proposal.resultingDemand, 900);
	EXPECT_EQ(proposal.excess, 25);
}

TEST(LeadershipProposalReadbackTest, SaturatesCountAndLeadershipArithmeticAtInt64Maximum)
{
	const newHorizonsHeroes::LeadershipSlotCapacity capacity{
		std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
	const auto proposal = evaluate(capacity, 1, std::numeric_limits<int64_t>::max());

	EXPECT_EQ(proposal.existingCount, 1);
	EXPECT_EQ(proposal.incomingCount, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(proposal.legalIncomingCount, static_cast<int64_t>(std::numeric_limits<int>::max()) - 1);
	EXPECT_EQ(proposal.resultingCount, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(proposal.addedLeadership, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(proposal.resultingDemand, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(proposal.excess, std::numeric_limits<int64_t>::max() - proposal.leadership);
}

TEST(LeadershipProposalReadbackTest, InvalidNegativeCapacityAndCountsNormalizeToZero)
{
	const newHorizonsHeroes::LeadershipSlotCapacity invalidCapacity{-1, -2, -3};
	const auto proposal = evaluate(invalidCapacity, -4, -5);

	EXPECT_EQ(proposal.leadership, 0);
	EXPECT_EQ(proposal.requirement, 0);
	EXPECT_EQ(proposal.maximumCount, 0);
	EXPECT_EQ(proposal.existingCount, 0);
	EXPECT_EQ(proposal.incomingCount, 0);
	EXPECT_EQ(proposal.legalIncomingCount, 0);
	EXPECT_EQ(proposal.resultingCount, 0);
	EXPECT_EQ(proposal.addedLeadership, 0);
	EXPECT_EQ(proposal.resultingDemand, 0);
	EXPECT_EQ(proposal.excess, 0);
}
