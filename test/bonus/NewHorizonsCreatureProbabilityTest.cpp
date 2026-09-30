/*
 * NewHorizonsCreatureProbabilityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
std::shared_ptr<Bonus> probabilityModifier(int multiplier)
{
	return std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS,
		BonusSource::OTHER, multiplier, BonusSourceID(), BonusSubtypeID(),
		BonusValueType::INDEPENDENT_MIN);
}
}

TEST(NewHorizonsCreatureProbabilityTest, UnmodifiedAndDeterministicAbilitiesRetainTheirChances)
{
	CBonusSystemNode unit(BonusNodeType::STACK_BATTLE);
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(30), 3000);
	unit.addNewBonus(probabilityModifier(2500));
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(-1), 0);
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(0), 0);
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(100), 10000);
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(150), 10000);
}

TEST(NewHorizonsCreatureProbabilityTest, IndependentMinimumPreservesFractionalChances)
{
	CBonusSystemNode unit(BonusNodeType::STACK_BATTLE);
	unit.addNewBonus(probabilityModifier(5000));
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(30), 1500);
	unit.addNewBonus(probabilityModifier(2500));
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(30), 750);
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(1), 25);
}

TEST(NewHorizonsCreatureProbabilityTest, ModifierBoundsCannotCreateInvalidProbabilities)
{
	CBonusSystemNode unit(BonusNodeType::STACK_BATTLE);
	unit.addNewBonus(probabilityModifier(20000));
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(30), 3000);
	unit.addNewBonus(probabilityModifier(-100));
	EXPECT_EQ(unit.favorableCreatureAbilityChanceBasisPoints(30), 0);
}

TEST(NewHorizonsCreatureProbabilityTest, TimedMarkersRoundTripAndCannotBeDownsaved)
{
	for(const auto type : {BonusType::MAXIMUM_LUCK,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS})
	{
		Bonus source(BonusDuration::N_TURNS, type, BonusSource::OTHER,
			type == BonusType::MAXIMUM_LUCK ? 0 : 4750, BonusSourceID());
		source.turnsRemain = 3;
		source.valType = BonusValueType::INDEPENDENT_MIN;
		CMemorySerializer wire;
		wire.oser & source;
		Bonus restored;
		wire.iser.version = ESerializationVersion::CURRENT;
		wire.iser & restored;
		EXPECT_EQ(restored.type, type);
		EXPECT_EQ(restored.val, source.val);
		EXPECT_EQ(restored.turnsRemain, 3);
		EXPECT_EQ(restored.valType, BonusValueType::INDEPENDENT_MIN);
		CMemorySerializer oldWire;
		oldWire.oser.version = ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL_LEVELS;
		EXPECT_THROW(oldWire.oser & source, std::runtime_error);
	}
}
