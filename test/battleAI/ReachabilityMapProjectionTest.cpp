/*
 * ReachabilityMapProjectionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/CRandomGenerator.h"

namespace
{
using TurnOrder = std::vector<battle::Units>;

// Independent pre-hoist path: a new projection for EACH order entry, including
// empty entries. Deliberately do not share the production projection or helper.
battle::Units perTurnProjectionReference(const std::shared_ptr<HypotheticBattle> & source,
	const std::shared_ptr<Environment> & environment, const TurnOrder & order,
	uint8_t turn, BattleHex hex)
{
	std::map<uint32_t, ReachabilityInfo> reachability;
	for(const auto & queue : order)
		for(const auto * unit : queue)
			if(!reachability.contains(unit->unitId()))
				reachability.emplace(unit->unitId(), source->getReachability(unit));
	battle::Units result;
	for(const auto & queue : order)
	{
		HypotheticBattle projection(environment.get(), source);
		for(const auto * unit : queue)
		{
			if(unit->isTurret())
				continue;
			if(projection.battleCanShoot(unit))
			{
				result.push_back(unit);
				continue;
			}
			const auto radius = unit->getMovementRange(turn) * (turn + 1);
			const auto & distances = reachability.at(unit->unitId());
			bool reachable = distances.isReachable(hex) && distances.distances.at(hex.toInt()) <= radius;
			if(!reachable && distances.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
			{
				const auto * occupant = projection.battleGetUnitByPos(hex);
				if(occupant)
				{
					const auto owner = projection.battleGetOwner(unit);
					const auto otherOwner = projection.battleGetOwner(occupant);
					if(owner != PlayerColor::CANNOT_DETERMINE && otherOwner != PlayerColor::CANNOT_DETERMINE
						&& owner != otherOwner)
					{
						for(const auto & neighbour : hex.getNeighbouringTiles())
						{
							reachable = distances.isReachable(neighbour)
								&& distances.distances.at(neighbour.toInt()) <= radius;
							if(reachable)
								break;
						}
					}
				}
			}
			if(reachable)
				result.push_back(unit);
		}
		++turn; // Preserve the old uint8_t wrap and queue/duplicate ordering.
	}
	return result;
}

class ReachabilityMapProjectionTest : public ProjectedTargetHitFixture
{
protected:
	std::shared_ptr<Environment> queryEnvironment;
	void prepareQueries()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 3), 50);
		addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 7), 50);
		resetChild();
		ASSERT_TRUE(child->battleCanShoot(shooter));
		ASSERT_FALSE(child->battleCanShoot(child->battleGetUnitByID(incoming->unitId())));
		queryEnvironment = std::make_shared<projectedHitTest::Environment>(gameState());
	}
	TurnOrder order(const std::shared_ptr<HypotheticBattle> & source)
	{
		TurnOrder result;
		source->battleGetTurnOrder(result, std::numeric_limits<int>::max(), 2);
		return result;
	}
	std::map<uint32_t, JsonNode> snapshot(const std::shared_ptr<HypotheticBattle> & source)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : source->battleGetAllUnits(false))
			result.emplace(unit->unitId(), source->getForUpdate(unit->unitId())->save());
		return result;
	}
	void expectParity(const std::shared_ptr<HypotheticBattle> & source, const TurnOrder & queues,
		const std::vector<BattleHex> & hexes, const std::vector<uint8_t> & turns)
	{
		const auto before = snapshot(source);
		std::map<uint32_t, JsonNode> liveBefore;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			liveBefore.emplace(unit->unitId(), unit->acquireState()->save());
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		ReachabilityMapCache cache;
		cache.update(queues, source);
		for(const auto turn : turns)
			for(const auto hex : hexes)
			{
				SCOPED_TRACE(::testing::Message() << "turn=" << int(turn) << " hex=" << hex.toInt());
				const auto expected = perTurnProjectionReference(source, queryEnvironment, queues, turn, hex);
				const auto & actual = cache.getOneTurnReachableUnits(source, queryEnvironment, queues, turn, hex);
				EXPECT_EQ(actual, expected); // Exact pointers, multiplicity and iteration order.
				EXPECT_EQ(cache.getOneTurnReachableUnits(source, queryEnvironment, queues, turn, hex), expected);
			}
		EXPECT_EQ(snapshot(source), before);
		for(const auto & [id, saved] : liveBefore)
			EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), saved);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	}
};

TEST_F(ReachabilityMapProjectionTest, AllHexesTwoTurnMixedRosterMatchIndependentPerTurnProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareQueries());
	const auto queues = order(child);
	ASSERT_EQ(queues.size(), 2u);
	ASSERT_FALSE(queues[0].empty());
	ASSERT_FALSE(queues[1].empty());
	std::vector<BattleHex> hexes;
	for(BattleHex hex = BattleHex::TOP_LEFT; hex.isValid(); ++hex)
		hexes.push_back(hex);
	ASSERT_NO_FATAL_FAILURE(expectParity(child, queues, hexes, {0, 1}));
}

TEST_F(ReachabilityMapProjectionTest, SeparateInvocationsKeepChangedActorPositionControllerAndTurnParameters)
{
	ASSERT_NO_FATAL_FAILURE(prepareQueries());
	const auto parentBefore = snapshot(child);
	auto left = std::make_shared<HypotheticBattle>(queryEnvironment.get(), child);
	auto right = std::make_shared<HypotheticBattle>(queryEnvironment.get(), child);
	const auto leftBefore = snapshot(left);
	const std::vector<BattleHex> hexes{BattleHex(89), BattleHex(93), BattleHex(92), BattleHex(3, 3), BattleHex(12, 7)};
	ASSERT_NO_FATAL_FAILURE(expectParity(right, order(right), hexes, {0, 1, 255}));
	auto actor = right->getForUpdate(incoming->unitId());
	actor->setPosition(BattleHex(11, 5));
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID())});
	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		3, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 1;
	actor->addUnitBonus({haste});
	ASSERT_NE(actor->getPosition(), left->battleGetUnitByID(incoming->unitId())->getPosition());
	ASSERT_NE(right->battleGetOwner(actor.get()), left->battleGetOwner(left->battleGetUnitByID(actor->unitId())));
	auto queues = order(right);
	queues.insert(queues.begin(), battle::Units{}); // Empty entry still increments the turn parameter.
	std::reverse(queues.begin(), queues.end());
	ASSERT_NO_FATAL_FAILURE(expectParity(right, queues, hexes, {0, 1, 255}));
	ASSERT_NO_FATAL_FAILURE(expectParity(left, order(left), hexes, {0, 1}));
	EXPECT_EQ(snapshot(left), leftBefore);
	EXPECT_EQ(snapshot(child), parentBefore);
}

TEST_F(ReachabilityMapProjectionTest, EmptyOrderDoesNotConstructOrDereferenceAProjection)
{
	ReachabilityMapCache cache;
	// Missing providers are valid only for this old early-return admission.
	EXPECT_TRUE(cache.getOneTurnReachableUnits({}, {}, {}, 0, BattleHex(93)).empty());
}

TEST_F(ReachabilityMapProjectionTest, OwnedExportForkReadCachesMatchFreshPerTurnProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareQueries());
	// Explicit authorized all-knowing fixture source, not a player-view importer.
	std::shared_ptr<CBattleInfoCallback> fullInformation(gameState(), battle());
	auto owned = InitialExportBattleSubject::capture(fullInformation, *battle());
	auto branch = std::make_shared<HypotheticBattle>(queryEnvironment.get(), owned);
	ASSERT_NE(branch->initialExportSubject(), nullptr);
	ASSERT_NO_FATAL_FAILURE(expectParity(branch, order(branch),
		{BattleHex(89), BattleHex(93), BattleHex(92), BattleHex(3, 3)}, {0, 1, 255}));
}
}
