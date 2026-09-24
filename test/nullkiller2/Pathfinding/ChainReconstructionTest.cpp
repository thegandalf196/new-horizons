/*
 * ChainReconstructionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{

using NK2AI::AIPath;
using NK2AI::AIPathNode;
using NK2AI::AINodeStorage;
using NK2AI::ChainActor;
using NK2AI::SpecialAction;

const PlayerColor PLAYER(0);
const int3 CARRIER_START(2, 2, 0);
const int3 DONOR_B_START(8, 2, 0);
const int3 DONOR_C_START(14, 2, 0);

TinyH3M::TinyH3MBuilder makeChainReconstructionMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("NK2ChainReconstruction")
		.playerActive(PLAYER)
		.hero(CARRIER_START, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
		.hero(DONOR_B_START, HeroTypeID(HeroTypeID::decode("core:valeska")), PLAYER)
		.hero(DONOR_C_START, HeroTypeID(HeroTypeID::decode("core:sorsha")), PLAYER);

	return builder;
}

class ReconstructionActor final : public ChainActor
{
public:
	ReconstructionActor(const CGHeroInstance * hero, uint64_t chainMask)
	{
		this->hero = hero;
		this->chainMask = chainMask;
	}
};

class NamedAction final : public SpecialAction
{
public:
	explicit NamedAction(std::string name)
		: name(std::move(name))
	{
	}

	std::string toString() const override
	{
		return name;
	}

private:
	std::string name;
};

void setNode(
	AIPathNode & node,
	const ChainActor & actor,
	int3 coord,
	const AIPathNode * predecessor = nullptr,
	const AIPathNode * other = nullptr,
	std::shared_ptr<const SpecialAction> action = {})
{
	node.actor = &actor;
	node.coord = coord;
	node.layer = EPathfindingLayer::LAND;
	node.action = EPathNodeAction::NORMAL;
	node.setCost(1.f);
	node.theNodeBefore = const_cast<AIPathNode *>(predecessor);
	node.chainOther = other;
	node.specialAction = std::move(action);
}

class Nullkiller2_Pathfinding_ChainReconstruction : public NullkillerTest
{
protected:
	std::unique_ptr<NK2AI::AIGateway> gateway;
	std::unique_ptr<AINodeStorage> storage;

	void start()
	{
		startWithMap(makeChainReconstructionMap());
		gateway = makeGateway(PLAYER);
		storage = std::make_unique<AINodeStorage>(gateway->nullkiller.get(), int3(36, 36, 1));
	}

	CGHeroInstance * carrierHero() const
	{
		return findHeroAt(CARRIER_START);
	}

	CGHeroInstance * donorBHero() const
	{
		return findHeroAt(DONOR_B_START);
	}

	CGHeroInstance * donorCHero() const
	{
		return findHeroAt(DONOR_C_START);
	}
};

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, continuesMovementAfterTownPurchase)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor carrierBefore(carrierHero(), 0b001);
	ReconstructionActor purchasedArmy(carrierHero(), 0b011);
	ReconstructionActor townArmy(donorBHero(), 0b010);

	AIPathNode carrierAtTown;
	AIPathNode townDonor;
	AIPathNode purchase;
	AIPathNode arrival;
	setNode(carrierAtTown, carrierBefore, int3(20, 20, 0));
	setNode(townDonor, townArmy, int3(20, 20, 0));
	setNode(purchase, purchasedArmy, int3(20, 20, 0), &carrierAtTown, &townDonor,
		std::make_shared<NamedAction>("town purchase"));
	setNode(arrival, purchasedArmy, int3(21, 20, 0), &purchase);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&arrival, path, parentIndex, commitments));
	ASSERT_EQ(path.nodes.size(), 4);
	EXPECT_EQ(parentIndex, 3);
	EXPECT_EQ(path.nodes[0].targetHero, carrierHero());
	EXPECT_EQ(path.nodes[0].chainMask, 0b011);
	EXPECT_EQ(path.nodes[1].targetHero, donorBHero());
	EXPECT_EQ(path.nodes[1].chainMask, 0b010);
	EXPECT_EQ(path.nodes[2].specialAction->toString(), "town purchase");
	EXPECT_EQ(path.nodes[2].parentIndex, 1);
	EXPECT_EQ(path.nodes[3].chainMask, 0b001);
	EXPECT_EQ(path.nodes[3].parentIndex, 2);
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, acceptsSequentialExchangesAndPreservesBranchOrdering)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);
	ASSERT_NE(donorCHero(), nullptr);

	ReconstructionActor carrierBase(carrierHero(), 0b001);
	ReconstructionActor afterFirstExchange(carrierHero(), 0b011);
	ReconstructionActor afterSecondExchange(carrierHero(), 0b111);
	ReconstructionActor donorB(donorBHero(), 0b010);
	ReconstructionActor donorC(donorCHero(), 0b100);

	AIPathNode carrierStart;
	AIPathNode carrierAtFirstExchange;
	AIPathNode donorBStart;
	AIPathNode donorBMove;
	AIPathNode firstExchange;
	AIPathNode carrierBetweenExchanges;
	AIPathNode donorCStart;
	AIPathNode donorCMove;
	AIPathNode secondExchange;
	AIPathNode arrival;

	setNode(carrierStart, carrierBase, carrierHero()->visitablePos());
	setNode(carrierAtFirstExchange, carrierBase, int3(20, 20, 0), &carrierStart);
	setNode(donorBStart, donorB, donorBHero()->visitablePos());
	setNode(donorBMove, donorB, int3(18, 18, 0), &donorBStart);
	setNode(firstExchange, afterFirstExchange, int3(20, 20, 0), &carrierAtFirstExchange, &donorBMove,
		std::make_shared<NamedAction>("first exchange"));
	setNode(carrierBetweenExchanges, afterFirstExchange, int3(25, 20, 0), &firstExchange);
	setNode(donorCStart, donorC, donorCHero()->visitablePos());
	setNode(donorCMove, donorC, int3(18, 16, 0), &donorCStart);
	setNode(secondExchange, afterSecondExchange, int3(25, 20, 0), &carrierBetweenExchanges, &donorCMove,
		std::make_shared<NamedAction>("second exchange"));
	setNode(arrival, afterSecondExchange, int3(26, 20, 0), &secondExchange);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&arrival, path, parentIndex, commitments));
	ASSERT_EQ(path.nodes.size(), 10);
	EXPECT_EQ(parentIndex, 9);

	const std::array<uint64_t, 10> expectedMasks{0b111, 0b100, 0b100, 0b111, 0b011, 0b010, 0b010, 0b011, 0b001, 0b001};
	for(size_t index = 0; index < expectedMasks.size(); ++index)
	{
		EXPECT_EQ(path.nodes[index].chainMask, expectedMasks[index]) << "node " << index;
		EXPECT_EQ(path.nodes[index].parentIndex, index == 0 ? -1 : static_cast<int>(index - 1)) << "node " << index;
	}
	EXPECT_EQ(path.nodes[1].targetHero, donorCHero());
	EXPECT_EQ(path.nodes[3].specialAction->toString(), "second exchange");
	EXPECT_EQ(path.nodes[5].targetHero, donorBHero());
	EXPECT_EQ(path.nodes[7].specialAction->toString(), "first exchange");
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, acceptsExchangeWithCompressedCarrierPredecessor)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor carrierBase(carrierHero(), 0b001);
	ReconstructionActor exchangedCarrier(carrierHero(), 0b011);
	ReconstructionActor donor(donorBHero(), 0b010);

	AIPathNode olderCarrierMovement;
	AIPathNode donorMovement;
	AIPathNode exchange;
	AIPathNode arrival;
	setNode(olderCarrierMovement, carrierBase, int3(19, 20, 0));
	setNode(donorMovement, donor, int3(18, 18, 0));
	setNode(exchange, exchangedCarrier, int3(22, 20, 0), &olderCarrierMovement, &donorMovement);
	setNode(arrival, exchangedCarrier, int3(23, 20, 0), &exchange);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&arrival, path, parentIndex, commitments));
	ASSERT_EQ(path.nodes.size(), 4);
	EXPECT_EQ(path.nodes[0].chainMask, 0b011);
	EXPECT_EQ(path.nodes[2].chainMask, 0b011);
	EXPECT_EQ(path.nodes[3].chainMask, 0b001);
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsIndependentBranchWithNestedMaskForSameHero)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);

	ReconstructionActor resultActor(carrierHero(), 0b011);
	ReconstructionActor carrierActor(carrierHero(), 0b010);
	ReconstructionActor nestedDonorActor(carrierHero(), 0b001);
	AIPathNode carrierPredecessor;
	AIPathNode nestedDonor;
	AIPathNode exchange;
	setNode(carrierPredecessor, carrierActor, int3(20, 20, 0));
	setNode(nestedDonor, nestedDonorActor, int3(19, 20, 0));
	setNode(exchange, resultActor, int3(20, 20, 0), &carrierPredecessor, &nestedDonor);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&exchange, path, parentIndex, commitments));
	EXPECT_TRUE(path.nodes.empty()); // A rejected branch does not partially append its candidate path.
	EXPECT_TRUE(commitments.empty());
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsSiblingBranchMatchingCurrentMaskWhenOlderMaskWasCommitted)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor exchangedCarrier(carrierHero(), 0b011);
	ReconstructionActor currentCarrier(carrierHero(), 0b001);
	ReconstructionActor donor(donorBHero(), 0b010);
	ReconstructionActor siblingCarrier(carrierHero(), 0b001);
	AIPathNode donorMovement;
	AIPathNode exchange;
	AIPathNode oldestMovement;
	AIPathNode siblingBranch;
	AIPathNode olderMovement;
	AIPathNode currentAfterExchange;
	AIPathNode arrival;
	setNode(donorMovement, donor, int3(18, 18, 0));
	setNode(oldestMovement, currentCarrier, int3(15, 20, 0));
	setNode(siblingBranch, siblingCarrier, int3(16, 20, 0));
	setNode(olderMovement, currentCarrier, int3(17, 20, 0), &oldestMovement, &siblingBranch);
	setNode(currentAfterExchange, currentCarrier, int3(18, 20, 0), &olderMovement);
	setNode(exchange, exchangedCarrier, int3(20, 20, 0), &currentAfterExchange, &donorMovement);
	setNode(arrival, exchangedCarrier, int3(21, 20, 0), &exchange);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&arrival, path, parentIndex, commitments));
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsMaskChangeAcrossPlainPredecessor)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);

	ReconstructionActor newMaskActor(carrierHero(), 0b011);
	ReconstructionActor oldMaskActor(carrierHero(), 0b001);
	AIPathNode oldMovement;
	AIPathNode newMovement;
	setNode(oldMovement, oldMaskActor, int3(19, 20, 0));
	setNode(newMovement, newMaskActor, int3(20, 20, 0), &oldMovement);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&newMovement, path, parentIndex, commitments));
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsOverlappingExchangeMasks)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor carrierBefore(carrierHero(), 0b001);
	ReconstructionActor resultActor(carrierHero(), 0b011);
	ReconstructionActor overlappingDonor(donorBHero(), 0b011);
	AIPathNode carrierPredecessor;
	AIPathNode donor;
	AIPathNode exchange;
	setNode(carrierPredecessor, carrierBefore, int3(20, 20, 0));
	setNode(donor, overlappingDonor, int3(19, 20, 0));
	setNode(exchange, resultActor, int3(20, 20, 0), &carrierPredecessor, &donor);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&exchange, path, parentIndex, commitments));
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsExchangeWithInexactMaskUnion)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor carrierBefore(carrierHero(), 0b001);
	ReconstructionActor inexactResult(carrierHero(), 0b111);
	ReconstructionActor donor(donorBHero(), 0b010);
	AIPathNode carrierPredecessor;
	AIPathNode donorNode;
	AIPathNode exchange;
	setNode(carrierPredecessor, carrierBefore, int3(20, 20, 0));
	setNode(donorNode, donor, int3(19, 20, 0));
	setNode(exchange, inexactResult, int3(20, 20, 0), &carrierPredecessor, &donorNode);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&exchange, path, parentIndex, commitments));
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, preservesStartingPositionMovementException)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);

	ReconstructionActor newMaskActor(carrierHero(), 0b011);
	ReconstructionActor oldMaskActor(carrierHero(), 0b001);
	AIPathNode atStartingPosition;
	AIPathNode movement;
	setNode(atStartingPosition, oldMaskActor, carrierHero()->visitablePos());
	setNode(movement, newMaskActor, int3(20, 20, 0), &atStartingPosition);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&movement, path, parentIndex, commitments));
	EXPECT_EQ(path.nodes.size(), 2);
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, verifiedExchangeAtStartingPositionUpdatesCarrierMask)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);
	ASSERT_NE(donorBHero(), nullptr);

	ReconstructionActor carrierBefore(carrierHero(), 0b001);
	ReconstructionActor exchangedCarrier(carrierHero(), 0b011);
	ReconstructionActor donor(donorBHero(), 0b010);
	AIPathNode earlierMovement;
	AIPathNode predecessorAtStart;
	AIPathNode donorMovement;
	AIPathNode exchangeAtStart;
	AIPathNode laterMovement;
	setNode(earlierMovement, carrierBefore, int3(19, 20, 0));
	setNode(predecessorAtStart, carrierBefore, carrierHero()->visitablePos(), &earlierMovement);
	setNode(donorMovement, donor, int3(18, 18, 0));
	setNode(exchangeAtStart, exchangedCarrier, carrierHero()->visitablePos(), &predecessorAtStart, &donorMovement);
	setNode(laterMovement, exchangedCarrier, int3(20, 20, 0), &exchangeAtStart);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&laterMovement, path, parentIndex, commitments));
	ASSERT_EQ(path.nodes.size(), 5);
	EXPECT_EQ(path.nodes[0].chainMask, 0b011);
	EXPECT_EQ(path.nodes[2].coord, carrierHero()->visitablePos());
	EXPECT_EQ(path.nodes[3].coord, carrierHero()->visitablePos());
	EXPECT_EQ(path.nodes[4].chainMask, 0b001);
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, rejectsPlainMaskChangeThroughStartingPosition)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);

	ReconstructionActor newMaskActor(carrierHero(), 0b011);
	ReconstructionActor oldMaskActor(carrierHero(), 0b001);
	AIPathNode earlierMovement;
	AIPathNode atStartingPosition;
	AIPathNode laterMovement;
	setNode(earlierMovement, oldMaskActor, int3(19, 20, 0));
	setNode(atStartingPosition, oldMaskActor, carrierHero()->visitablePos(), &earlierMovement);
	setNode(laterMovement, newMaskActor, int3(20, 20, 0), &atStartingPosition);

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	EXPECT_FALSE(storage->tryReconstructChainInfo(&laterMovement, path, parentIndex, commitments));
}

TEST_F(Nullkiller2_Pathfinding_ChainReconstruction, deduplicatesCommitmentsAcrossLongOrdinarySpine)
{
	start();
	ASSERT_NE(carrierHero(), nullptr);

	constexpr size_t SPINE_LENGTH = 48;
	ReconstructionActor carrier(carrierHero(), 0b001);
	std::vector<AIPathNode> nodes(SPINE_LENGTH);
	for(size_t index = 0; index < nodes.size(); ++index)
	{
		const AIPathNode * predecessor = index == 0 ? nullptr : &nodes[index - 1];
		setNode(nodes[index], carrier, int3(5 + static_cast<int>(index % 24), 5 + static_cast<int>(index / 24), 0), predecessor);
	}

	AIPath path;
	AINodeStorage::RealMoveMasksByHero commitments;
	int parentIndex = -1;
	ASSERT_TRUE(storage->tryReconstructChainInfo(&nodes.back(), path, parentIndex, commitments));
	ASSERT_EQ(path.nodes.size(), SPINE_LENGTH);
	const auto heroCommitments = commitments.find(carrierHero());
	ASSERT_NE(heroCommitments, commitments.end());
	EXPECT_EQ(heroCommitments->second.size(), 1);
	EXPECT_EQ(heroCommitments->second.front().mask, 0b001);
}

}
