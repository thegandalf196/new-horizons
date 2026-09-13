/*
 * SpellTargetsEvaluatorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_battle_Unit.h"
#include "../mock/mock_spells_Mechanics.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "lib/CStack.h"
#include "lib/battle/CBattleInfoCallback.h"
#include "lib/battle/CObstacleInstance.h"

namespace test
{
using namespace ::testing;
using namespace spells;
using namespace battle;
using PossiblePositions = std::vector<BattleHex>;

class CBattleInfoCallbackMock : public CBattleInfoCallback
{
public:
	MOCK_CONST_METHOD1(battleGetAllUnits, battle::Units(bool));
	MOCK_CONST_METHOD0(getBattle, IBattleInfo *());
	MOCK_CONST_METHOD0(getPlayerID, std::optional<PlayerColor>());
#if SCRIPTING_ENABLED
	MOCK_CONST_METHOD0(getContextPool, scripting::Pool *());
#endif
};

class CStackMock : public CStack
{
public:
	MOCK_CONST_METHOD0(unitSide, BattleSide());
	MOCK_CONST_METHOD0(isHypnotized, bool());
};

class SpellTargetEvaluatorTest : public ::testing::Test
{
public:
	MechanicsMock mechMock;
	CBattleInfoCallbackMock battleMock;
	NiceMock<BattleStateMock> battleState;
	battle::Units allUnits;
	TStacks allStacks;
	BattleSide casterSide = BattleSide::ATTACKER;
	BattleSide enemySide = BattleSide::DEFENDER;

	void SetUp() override
	{
		mechMock.casterSide = casterSide;
		ON_CALL(mechMock, battle()).WillByDefault(Return(&battleMock));
		ON_CALL(mechMock, getCasterColor()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(battleMock, getBattle()).WillByDefault(Return(&battleState));
		ON_CALL(battleState, getSidePlayer(BattleSide::ATTACKER)).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(battleState, getSidePlayer(BattleSide::DEFENDER)).WillByDefault(Return(PlayerColor(1)));
		ON_CALL(mechMock, canBeCastAt(_, _)).WillByDefault(Return(true));
	}

	void TearDown() override
	{
		for(const auto * unit : allUnits)
			delete unit;
		allUnits.clear();
		for(const auto * stack : allStacks)
			delete stack;
		allStacks.clear();
	}

	void spellTargetTypes(std::vector<AimType> targetTypes)
	{
		ON_CALL(mechMock, getTargetTypes()).WillByDefault(Return(std::move(targetTypes)));
	}

	CStackMock * addStack(BattleHex position, BattleSide battleSide, bool isSuspectible = true)
	{
		auto * stack = new CStackMock();
		ON_CALL(*stack, unitSide()).WillByDefault(Return(battleSide));
		ON_CALL(*stack, isHypnotized()).WillByDefault(Return(false));
		allStacks.push_back(stack);
		auto * unit = new UnitMock();
		ON_CALL(*unit, getPosition()).WillByDefault(Return(position));
		ON_CALL(*unit, unitId()).WillByDefault(Return(static_cast<uint32_t>(allUnits.size())));
		allUnits.push_back(unit);

		if(!isSuspectible)
			ON_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, position)), _)).WillByDefault(Return(false));
		return stack;
	}

	void setAffectedStacksForCast(BattleHex position, std::vector<const CStack *> stacks)
	{
		ON_CALL(mechMock, getAffectedStacks(Contains(Field(&Destination::hexValue, position)))).WillByDefault(Return(stacks));
	}

	void confirmResults(std::vector<PossiblePositions> allRequiredCasts)
	{
		std::vector<Target> result = SpellTargetEvaluator::getViableTargets(&mechMock);
		basicCheck(result);
		ASSERT_EQ(result.size(), allRequiredCasts.size());

		std::vector<BattleHex> targetedHexes;
		targetedHexes.reserve(result.size());
		for(Target target : result)
			targetedHexes.push_back(target.front().hexValue);

		for(const PossiblePositions & requiredCast : allRequiredCasts)
			EXPECT_TRUE(containCommonValue(requiredCast, targetedHexes));
	}

	void basicCheck(std::vector<Target> & result)
	{
		for(const Target & target : result)
			EXPECT_EQ(target.size(), 1); //multi-destination spells are not handled by targetEvaluator
	}

	template<typename T>
	bool containCommonValue(const std::vector<T> & v1, const std::vector<T> & v2)
	{
		for(T val1 : v1)
		{
			for(T val2 : v2)
			{
				if(val1 == val2)
					return true;
			}
		}
		return false;
	}
};

TEST_F(SpellTargetEvaluatorTest, ReturnsEmptyForUnsupportedMultiDestinationShape)
{
	spellTargetTypes({AimType::CREATURE, AimType::CREATURE, AimType::LOCATION});
	std::vector<Target> result = SpellTargetEvaluator::getViableTargets(&mechMock);
	EXPECT_TRUE(result.empty());
}

TEST_F(SpellTargetEvaluatorTest, SacrificePreservesCorpseIdentityAndValidatesEachOrderedVictimPair)
{
	spellTargetTypes({AimType::CREATURE, AimType::CREATURE});
	addStack(BattleHex(90), casterSide);
	addStack(BattleHex(90), casterSide); // Distinct corpse IDs on the same hex.
	addStack(BattleHex(71), casterSide);
	addStack(BattleHex(88), enemySide);
	addStack(BattleHex(72), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	int prefixChecks = 0;
	int pairChecks = 0;
	ON_CALL(mechMock, canBeCastAt(_, _)).WillByDefault(Invoke(
		[&](const Target & target, Problem &) -> bool
		{
			const auto * source = target.front().unitValue;
			if(target.size() == 1)
			{
				++prefixChecks;
				return source == allUnits[0] || source == allUnits[1];
			}
			++pairChecks;
			EXPECT_EQ(target.size(), 2u);
			return (source == allUnits[0] && target[1].unitValue == allUnits[2])
				|| (source == allUnits[1] && target[1].unitValue == allUnits[4]);
		}));
	const auto result = SpellTargetEvaluator::getViableTargets(&mechMock);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(prefixChecks, 5);
	EXPECT_EQ(pairChecks, 10);
	ASSERT_EQ(result[0].size(), 2u);
	ASSERT_EQ(result[1].size(), 2u);
	EXPECT_EQ(result[0][0].unitValue, allUnits[0]);
	EXPECT_EQ(result[0][0].unitValue->unitId(), 0u);
	EXPECT_EQ(result[0][1].unitValue, allUnits[2]);
	EXPECT_EQ(result[1][0].unitValue, allUnits[1]);
	EXPECT_EQ(result[1][0].unitValue->unitId(), 1u);
	EXPECT_EQ(result[1][1].unitValue, allUnits[4]);
	EXPECT_EQ(result[0][0].hexValue, result[1][0].hexValue);
}

TEST_F(SpellTargetEvaluatorTest, SacrificeDoesNotReturnAnEligiblePrefixWithoutACompleteLegalPair)
{
	spellTargetTypes({AimType::CREATURE, AimType::CREATURE});
	addStack(BattleHex(90), casterSide);
	addStack(BattleHex(71), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	ON_CALL(mechMock, canBeCastAt(_, _)).WillByDefault(Invoke(
		[](const Target & target, Problem &) -> bool { return target.size() == 1; }));
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(&mechMock).empty());
}

TEST_F(SpellTargetEvaluatorTest, TeleportEnumeratesOnlyValidatedLandingsForAnEligibleExactUnit)
{
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	addStack(BattleHex(90), casterSide);
	addStack(BattleHex(106), enemySide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	const auto * source = allUnits.front();
	int sourceChecks = 0;
	int landingChecks = 0;
	EXPECT_CALL(mechMock, canBeCastAt(_, _)).WillRepeatedly(Invoke(
		[&](const Target & target, Problem &) -> bool
		{
			if(target.size() == 1)
			{
				++sourceChecks;
				return target.front().unitValue == source;
			}
			++landingChecks;
			EXPECT_EQ(target.size(), 2u);
			EXPECT_EQ(target.front().unitValue, source);
			return target.back().hexValue == BattleHex(71) || target.back().hexValue == BattleHex(88);
		}));
	const auto result = SpellTargetEvaluator::getViableTargets(&mechMock);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(sourceChecks, 2);
	EXPECT_EQ(landingChecks, GameConstants::BFIELD_SIZE - 1);
	for(size_t index = 0; index < result.size(); ++index)
	{
		ASSERT_EQ(result[index].size(), 2u);
		EXPECT_EQ(result[index][0].unitValue, source);
		EXPECT_EQ(result[index][0].hexValue, BattleHex(90));
		EXPECT_EQ(result[index][1].unitValue, nullptr);
		EXPECT_EQ(result[index][1].hexValue, index == 0 ? BattleHex(71) : BattleHex(88));
	}
}

TEST_F(SpellTargetEvaluatorTest, TeleportValidatesEachSourceDestinationPairSeparately)
{
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	addStack(BattleHex(90), casterSide);
	addStack(BattleHex(106), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	EXPECT_CALL(mechMock, canBeCastAt(_, _)).WillRepeatedly(Invoke(
		[&](const Target & target, Problem &) -> bool
		{
			if(target.size() == 1)
				return true;
			return (target.front().unitValue == allUnits[0] && target.back().hexValue == BattleHex(71))
				|| (target.front().unitValue == allUnits[1] && target.back().hexValue == BattleHex(88));
		}));
	const auto result = SpellTargetEvaluator::getViableTargets(&mechMock);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0][0].unitValue, allUnits[0]);
	EXPECT_EQ(result[0][1].hexValue, BattleHex(71));
	EXPECT_EQ(result[1][0].unitValue, allUnits[1]);
	EXPECT_EQ(result[1][1].hexValue, BattleHex(88));
}

TEST_F(SpellTargetEvaluatorTest, TeleportDoesNotOfferPartialOrUnchangedDestinations)
{
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	addStack(BattleHex(90), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	EXPECT_CALL(mechMock, canBeCastAt(_, _)).WillRepeatedly(Invoke(
		[](const Target & target, Problem &) -> bool
		{
			return target.size() == 1 || target.back().hexValue == BattleHex(90);
		}));
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(&mechMock).empty());
}

TEST_F(SpellTargetEvaluatorTest, ReturnSingleEmptyDestinationIfTargetIsNone)
{
	spellTargetTypes({AimType::NOTHING});
	std::vector<Target> result = SpellTargetEvaluator::getViableTargets(&mechMock);
	EXPECT_EQ(result.size(), 1);
	EXPECT_TRUE(result.front().empty());
}

TEST_F(SpellTargetEvaluatorTest, ReturnsSuspectibleCreaturePositionsIfSpellTargetsCreatures)
{
	spellTargetTypes({AimType::CREATURE});

	addStack(BattleHex(1), casterSide);
	addStack(BattleHex(2), enemySide, false);
	addStack(BattleHex(3), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(Eq(false))).WillByDefault(Return(allUnits));

	confirmResults({{BattleHex(1)}, {BattleHex(3)}});
}

TEST_F(SpellTargetEvaluatorTest, ReturnsSuspectibleCreaturePositionsAndSingleRandomSurroundingHexForEachStackIfNeutralLocationSpell)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNeutralSpell()).WillByDefault(Return(true));

	addStack(BattleHex(72), casterSide);
	addStack(BattleHex(159), enemySide, false);
	addStack(BattleHex(23), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(Eq(false))).WillByDefault(Return(allUnits));

	confirmResults(
		{{BattleHex(72)},
		 BattleHex(72).getAllNeighbouringTiles().toVector(),
		 BattleHex(159).getAllNeighbouringTiles().toVector(),
		 {BattleHex(23)},
		 BattleHex(23).getAllNeighbouringTiles().toVector()}
	);
}

TEST_F(SpellTargetEvaluatorTest, NeutralLocationSpellsEnumerateVisibleObstacleFootprintsWithoutUnitsOrDuplicates)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNeutralSpell()).WillByDefault(Return(true));
	ON_CALL(battleMock, getPlayerID()).WillByDefault(Return(std::optional<PlayerColor>(PlayerColor(0))));
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(battle::Units{}));
	auto first = std::make_shared<SpellCreatedObstacle>();
	first->casterSide = casterSide;
	first->customSize.insert(BattleHex(70));
	first->customSize.insert(BattleHex(71));
	auto second = std::make_shared<SpellCreatedObstacle>();
	second->casterSide = casterSide;
	second->customSize.insert(BattleHex(71));
	second->customSize.insert(BattleHex(72));
	auto hidden = std::make_shared<SpellCreatedObstacle>();
	hidden->casterSide = enemySide;
	hidden->hidden = true;
	hidden->nativeVisible = false;
	hidden->customSize.insert(BattleHex(88));
	ON_CALL(battleState, getAllObstacles()).WillByDefault(Return(IBattleInfo::ObstacleCList{first, second, hidden}));
	EXPECT_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, BattleHex(71))), _))
		.Times(1).WillOnce(Return(true));
	EXPECT_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, BattleHex(70))), _))
		.Times(1).WillOnce(Return(true));
	EXPECT_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, BattleHex(72))), _))
		.Times(1).WillOnce(Return(false));
	EXPECT_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, BattleHex(88))), _)).Times(0);
	confirmResults({{BattleHex(70)}, {BattleHex(71)}});
}

TEST_F(SpellTargetEvaluatorTest, AreaSpellHarmFilterTracksControlRestorationAndCasterColor)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNegativeSpell()).WillByDefault(Return(true));
	auto * originalAlly = addStack(BattleHex(90), casterSide);
	auto * originalEnemy = addStack(BattleHex(106), enemySide);
	setAffectedStacksForCast(BattleHex(71), {originalAlly});
	setAffectedStacksForCast(BattleHex(88), {originalEnemy});

	ON_CALL(*originalAlly, isHypnotized()).WillByDefault(Return(true));
	ON_CALL(*originalEnemy, isHypnotized()).WillByDefault(Return(true));
	confirmResults({{BattleHex(71)}});

	ON_CALL(*originalAlly, isHypnotized()).WillByDefault(Return(false));
	ON_CALL(*originalEnemy, isHypnotized()).WillByDefault(Return(false));
	confirmResults({{BattleHex(88)}});

	mechMock.casterSide = BattleSide::DEFENDER;
	ON_CALL(mechMock, getCasterColor()).WillByDefault(Return(PlayerColor(1)));
	confirmResults({{BattleHex(71)}});
	ON_CALL(mechMock, isNegativeSpell()).WillByDefault(Return(false));
	ON_CALL(mechMock, isPositiveSpell()).WillByDefault(Return(true));
	confirmResults({{BattleHex(88)}});
}

TEST_F(SpellTargetEvaluatorTest, NegativeAreaSpellPrefersCurrentEnemiesWithoutCurrentAlliedCollateral)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNegativeSpell()).WillByDefault(Return(true));
	auto * enemy = addStack(BattleHex(90), enemySide);
	auto * controlledAlly = addStack(BattleHex(106), enemySide);
	auto * controlledEnemy = addStack(BattleHex(107), casterSide);
	ON_CALL(*controlledAlly, isHypnotized()).WillByDefault(Return(true));
	ON_CALL(*controlledEnemy, isHypnotized()).WillByDefault(Return(true));

	setAffectedStacksForCast(BattleHex(21), {enemy, controlledAlly});
	setAffectedStacksForCast(BattleHex(22), {enemy, controlledEnemy});
	setAffectedStacksForCast(BattleHex(23), {enemy, controlledEnemy, controlledAlly});
	//21 and22 affect equally many stacks, but22 strictly improves BOTH partitions.
	confirmResults({{BattleHex(22)}});
}

TEST_F(SpellTargetEvaluatorTest, PositiveAreaSpellPrefersCurrentAlliesWithoutHelpingCurrentEnemies)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isPositiveSpell()).WillByDefault(Return(true));
	auto * ally = addStack(BattleHex(90), casterSide);
	auto * controlledAlly = addStack(BattleHex(106), enemySide);
	auto * controlledEnemy = addStack(BattleHex(107), casterSide);
	ON_CALL(*controlledAlly, isHypnotized()).WillByDefault(Return(true));
	ON_CALL(*controlledEnemy, isHypnotized()).WillByDefault(Return(true));

	setAffectedStacksForCast(BattleHex(21), {ally, controlledEnemy});
	setAffectedStacksForCast(BattleHex(22), {ally, controlledAlly});
	setAffectedStacksForCast(BattleHex(23), {ally, controlledAlly, controlledEnemy});
	confirmResults({{BattleHex(22)}});
}

TEST_F(SpellTargetEvaluatorTest, ReturnsOneCaseOfEachOptimalCastIfNegativeLocationSpell)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNegativeSpell()).WillByDefault(Return(true));

	auto * enemyStack1 = addStack(BattleHex(90), enemySide);
	auto * enemyStack2 = addStack(BattleHex(106), enemySide);
	auto * enemyStack3 = addStack(BattleHex(1), enemySide);
	auto * enemyStack4 = addStack(BattleHex(37), enemySide);
	auto * enemyStack5 = addStack(BattleHex(41), enemySide);

	auto * alliedStack1 = addStack(BattleHex(107), casterSide);
	auto * alliedStack2 = addStack(BattleHex(19), casterSide);

	//optimal
	setAffectedStacksForCast(BattleHex(71), {enemyStack1, enemyStack2});
	setAffectedStacksForCast(BattleHex(88), {enemyStack1, enemyStack2});
	//suboptimal
	setAffectedStacksForCast(BattleHex(55), {enemyStack1});
	setAffectedStacksForCast(BattleHex(89), {enemyStack1, enemyStack2, alliedStack1});

	//optimal
	setAffectedStacksForCast(BattleHex(18), {enemyStack3, alliedStack2});
	setAffectedStacksForCast(BattleHex(2), {enemyStack3, alliedStack2});
	//suboptimal
	setAffectedStacksForCast(BattleHex(53), {alliedStack2});

	//optimal
	setAffectedStacksForCast(BattleHex(39), {enemyStack4, enemyStack5});
	//suboptimal
	setAffectedStacksForCast(BattleHex(21), {enemyStack4});
	setAffectedStacksForCast(BattleHex(25), {enemyStack5});

	confirmResults({
		{BattleHex(71), BattleHex(88)},
        {BattleHex(2), BattleHex(18)},
        {BattleHex(39)}
	});
}

}
