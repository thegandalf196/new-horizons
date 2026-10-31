/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/CUnitState.h"
#include "../mock/mock_UnitInfo.h"
#include "../mock/mock_battle_Unit.h"
#include <stdexcept>
namespace
{
class CopiedInfo final : public UnitInfoMock
{
public:
	std::optional<battle::UnitPreparationHealthBasis> facts;
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override { return facts; }
};
class AggregateUnit : public UnitMock
{
public:
	int64_t start = 900;
	int64_t rebirth = 800;
	mutable int reads = 0;
	int64_t getBattleStartMaximumAggregateHP() const override { ++reads; return start; }
	int64_t getRebirthOriginalAggregateHP() const override { ++reads; return rebirth; }
};
class CopiedAggregateUnit final : public AggregateUnit
{
public:
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override
	{
		return battle::UnitPreparationHealthBasis{0, 0};
	}
};
}
TEST(UnitPreparationHealthBasisTest, CapturedZeroOverridesPositiveBonusProvider)
{
	CopiedAggregateUnit info;
	AggregateUnit bonus;
	battle::CUnitStateDetached detached(&info, &bonus);
	EXPECT_EQ(detached.getBattleStartMaximumAggregateHP(), 0);
	EXPECT_EQ(detached.getRebirthOriginalAggregateHP(), 0);
	EXPECT_EQ(bonus.reads, 0);
	EXPECT_EQ(info.reads, 0);
}
TEST(UnitPreparationHealthBasisTest, NestedDetachedPreservesCopiedZeroAndPositiveValues)
{
	CopiedInfo info;
	info.facts = battle::UnitPreparationHealthBasis{0, 37};
	AggregateUnit bonus;
	battle::CUnitStateDetached parent(&info, &bonus);
	battle::CUnitStateDetached child(&parent, &bonus);
	ASSERT_TRUE(child.unitPreparationHealthBasis());
	EXPECT_EQ(child.getBattleStartMaximumAggregateHP(), 0);
	EXPECT_EQ(child.getRebirthOriginalAggregateHP(), 37);
	EXPECT_EQ(bonus.reads, 0);
}
TEST(UnitPreparationHealthBasisTest, LegacySourcePositivePrecedesBonusAndZeroFallsBack)
{
	AggregateUnit source, bonus;
	source.start = 19; source.rebirth = 23;
	battle::CUnitStateDetached detached(&source, &bonus);
	EXPECT_EQ(detached.getBattleStartMaximumAggregateHP(), 19);
	EXPECT_EQ(detached.getRebirthOriginalAggregateHP(), 23);
	EXPECT_EQ(bonus.reads, 0);
	source.start = source.rebirth = 0;
	EXPECT_EQ(detached.getBattleStartMaximumAggregateHP(), 900);
	EXPECT_EQ(detached.getRebirthOriginalAggregateHP(), 800);
	EXPECT_EQ(bonus.reads, 2);
}
TEST(UnitPreparationHealthBasisTest, AbsentLegacyInfoUsesBonusButMalformedCopiedGroupNeverFallsBack)
{
	CopiedInfo info;
	AggregateUnit bonus;
	battle::CUnitStateDetached detached(&info, &bonus);
	EXPECT_EQ(detached.getBattleStartMaximumAggregateHP(), 900);
	EXPECT_EQ(detached.getRebirthOriginalAggregateHP(), 800);
	bonus.reads = 0;
	info.facts = battle::UnitPreparationHealthBasis{-1, 3};
	EXPECT_THROW(detached.getBattleStartMaximumAggregateHP(), std::invalid_argument);
	EXPECT_THROW(detached.getRebirthOriginalAggregateHP(), std::invalid_argument);
	EXPECT_EQ(bonus.reads, 0);
	info.facts = battle::UnitPreparationHealthBasis{4, -1};
	EXPECT_THROW(detached.getBattleStartMaximumAggregateHP(), std::invalid_argument);
	EXPECT_THROW(detached.getRebirthOriginalAggregateHP(), std::invalid_argument);
	EXPECT_EQ(bonus.reads, 0);
}
