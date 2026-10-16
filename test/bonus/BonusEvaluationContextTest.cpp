/*
 * BonusEvaluationContextTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
#include <limits>
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> templateBonus(int value = 17)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
}

std::vector<std::byte> bonusBytes(const Bonus & bonus)
{
	CMemorySerializer memory;
	memory.oser & bonus;
	return memory.extractBuffer();
}

std::array<std::shared_ptr<IUpdater>, 10> allUpdaters()
{
	return {std::make_shared<IUpdater>(), std::make_shared<GrowsWithLevelUpdater>(13, 2),
		std::make_shared<TimesHeroLevelUpdater>(2), std::make_shared<TimesHeroLevelDivideStackLevelUpdater>(),
		std::make_shared<TimesStackSizeUpdater>(), std::make_shared<TimesArmySizeUpdater>(),
		std::make_shared<TimesStackLevelUpdater>(), std::make_shared<DivideStackLevelUpdater>(),
		std::make_shared<OwnerUpdater>(), std::make_shared<CompositeUpdater>()};
}

class ZeroLevelStack final : public CStackInstance
{
public:
	ZeroLevelStack() : CStackInstance(nullptr, CreatureID(0), 8, true) {}
	int getLevel() const override { return 0; }
};
}

class BonusEvaluationContextTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStackInstance stableBase{nullptr, CreatureID(0), 8, true};
	CStack original{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	UnitInfoMock info;
	BonusBearerMock bonuses;
	UnitEnvironmentMock environment;
	battle::CUnitStateDetached projected{&info, &bonuses};

	void SetUp() override
	{
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(8));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(2)));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 20, BonusSourceID(CreatureID(0))));
		projected.localInit(&environment);
	}
};

TEST_F(BonusEvaluationContextTest, EveryUpdaterLiveAdapterMatchesExplicitLiveContext)
{
	const auto input = templateBonus();
	const auto before = bonusBytes(*input);
	const BonusEvaluationContext live(original);
	for(const auto & updater : allUpdaters())
	{
		const auto implicit = updater->createUpdatedBonus(input, original);
		const auto explicitLive = updater->createUpdatedBonus(input, live);
		EXPECT_EQ(bonusBytes(*implicit), bonusBytes(*explicitLive));
		EXPECT_EQ(implicit->bonusOwner, explicitLive->bonusOwner);
	}
	EXPECT_EQ(bonusBytes(*input), before);
	EXPECT_EQ(&live.originalNode(), &original);
	IUpdater noOp;
	CompositeUpdater emptyComposite;
	TimesHeroLevelUpdater nonHero;
	EXPECT_EQ(noOp.createUpdatedBonus(input, live), input);
	EXPECT_EQ(emptyComposite.createUpdatedBonus(input, live), input);
	EXPECT_EQ(nonHero.createUpdatedBonus(input, live), input);
}

TEST_F(BonusEvaluationContextTest, HeroAndArmyFormulasKeepOriginalTypedIdentity)
{
	CGHeroInstance hero(nullptr);
	hero.level = 7;
	hero.tempOwner = PlayerColor(4);
	const BonusEvaluationContext live(hero);
	const auto input = templateBonus(10);
	TimesHeroLevelUpdater timesHero(2);
	EXPECT_EQ(timesHero.createUpdatedBonus(input, live)->val, 30);
	GrowsWithLevelUpdater grows(13, 2);
	EXPECT_EQ(grows.createUpdatedBonus(input, live)->val, 2);
	TimesArmySizeUpdater army;
	EXPECT_EQ(army.createUpdatedBonus(input, live)->val, 0);
	for(const auto & updater : allUpdaters())
		EXPECT_EQ(bonusBytes(*updater->createUpdatedBonus(input, hero)),
			bonusBytes(*updater->createUpdatedBonus(input, live)));
	EXPECT_EQ(hero.level, 7u);
	EXPECT_EQ(&live.originalNode(), &hero);
}

TEST_F(BonusEvaluationContextTest, ChangedProjectedCountFeedsTheSameClampFormulaWithoutLiveWrites)
{
	const auto input = templateBonus(999);
	const auto originalCount = original.getCount();
	const auto originalVersion = original.getTreeVersion();
	const BonusEvaluationContext context(original, projected);
	TimesStackSizeUpdater updater(1, 5, 2, 3);
	EXPECT_EQ(updater.createUpdatedBonus(input, original)->val, 12);
	int64_t damage = 100;
	projected.damage(damage);
	ASSERT_EQ(projected.getCount(), 3);
	EXPECT_EQ(updater.createUpdatedBonus(input, context)->val, 3);
	EXPECT_EQ(original.getCount(), originalCount);
	EXPECT_EQ(original.getTreeVersion(), originalVersion);
	EXPECT_EQ(input->val, 999);
}

TEST_F(BonusEvaluationContextTest, BaselessStackLevelUsesProjectedEffectiveForm)
{
	const auto input = templateBonus(20);
	input->updater = std::make_shared<TimesStackLevelUpdater>();
	const auto retainedUpdater = input->updater;
	projected.beginBattleForm(CreatureID(2), 3);
	ASSERT_NE(projected.unitType()->getLevel(), original.unitType()->getLevel());
	const BonusEvaluationContext context(original, projected);
	TimesStackLevelUpdater multiply;
	DivideStackLevelUpdater divide;
	const int level = projected.unitType()->getLevel();
	const auto multiplied = multiply.createUpdatedBonus(input, context);
	EXPECT_EQ(multiplied->val, 20 * level);
	EXPECT_EQ(multiplied->updater, nullptr);
	EXPECT_EQ(divide.createUpdatedBonus(input, context)->val, 20 / std::max(1, level));
	EXPECT_EQ(input->updater, retainedUpdater);
	EXPECT_EQ(original.unitType()->getId(), CreatureID(0));
}

TEST_F(BonusEvaluationContextTest, BasedStackKeepsLegacyAdventureLevelDuringProjectedForm)
{
	original.base = &stableBase;
	projected.beginBattleForm(CreatureID(2), 3);
	ASSERT_NE(projected.unitType()->getLevel(), stableBase.getLevel());
	const BonusEvaluationContext context(original, projected);
	const auto input = templateBonus(20);
	TimesStackLevelUpdater multiply;
	DivideStackLevelUpdater divide;
	EXPECT_EQ(multiply.createUpdatedBonus(input, context)->val, 20 * stableBase.getLevel());
	EXPECT_EQ(divide.createUpdatedBonus(input, context)->val, 20 / std::max(1, stableBase.getLevel()));
	EXPECT_EQ(original.base, &stableBase);
}

TEST_F(BonusEvaluationContextTest, ProjectedOwnerUsesExistingUnflaggableToNeutralMapping)
{
	const auto input = templateBonus();
	const auto retainedOwner = input->bonusOwner;
	const BonusEvaluationContext context(original, projected);
	OwnerUpdater owner;
	EXPECT_EQ(owner.createUpdatedBonus(input, original)->bonusOwner, PlayerColor(0));
	EXPECT_EQ(owner.createUpdatedBonus(input, context)->bonusOwner, PlayerColor(2));
	EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor::UNFLAGGABLE));
	EXPECT_EQ(owner.createUpdatedBonus(input, context)->bonusOwner, PlayerColor::NEUTRAL);
	EXPECT_EQ(original.unitOwner(), PlayerColor(0));
	EXPECT_EQ(input->bonusOwner, retainedOwner);
}

TEST_F(BonusEvaluationContextTest, CompositeForwardsTheSameProjectedContextToEveryStage)
{
	int64_t damage = 100;
	projected.damage(damage);
	ASSERT_EQ(projected.getCount(), 3);
	const BonusEvaluationContext context(original, projected);
	CompositeUpdater composite;
	composite.updaters = {std::make_shared<TimesStackSizeUpdater>(), std::make_shared<OwnerUpdater>()};
	const auto result = composite.createUpdatedBonus(templateBonus(999), context);
	EXPECT_EQ(result->val, 3);
	EXPECT_EQ(result->bonusOwner, PlayerColor(2));
	EXPECT_EQ(original.getCount(), 8);
}

TEST_F(BonusEvaluationContextTest, HeroDeferredDividerUsesProjectedBaselessLevelAndExistingRounding)
{
	CGHeroInstance hero(nullptr);
	hero.level = 7;
	TimesHeroLevelDivideStackLevelUpdater specialty;
	const auto deferred = specialty.createUpdatedBonus(templateBonus(3), hero);
	ASSERT_NE(deferred->updater, nullptr);
	projected.beginBattleForm(CreatureID(2), 3);
	const BonusEvaluationContext context(original, projected);
	const int level = projected.unitType()->getLevel();
	const auto result = deferred->updater->createUpdatedBonus(deferred, context);
	EXPECT_EQ(result->val, 3 * (7 / std::max(1, level)));
	EXPECT_EQ(result->updater, nullptr);
	EXPECT_EQ(deferred->val, 3);
}

TEST_F(BonusEvaluationContextTest, MissingStackContextAndZeroLevelRetainLegacyFallbacks)
{
	CBonusSystemNode creature(BonusNodeType::CREATURE);
	const auto input = templateBonus(99);
	TimesStackSizeUpdater size(2, 5, 3, 7);
	EXPECT_EQ(size.createUpdatedBonus(input, creature)->val, 14);
	TimesStackLevelUpdater level;
	EXPECT_EQ(level.createUpdatedBonus(input, creature), input);
	ZeroLevelStack zero;
	DivideStackLevelUpdater divide;
	input->updater = std::make_shared<DivideStackLevelUpdater>();
	EXPECT_EQ(divide.createUpdatedBonus(input, zero), input);
	divide.heroLevel = 7;
	const auto result = divide.createUpdatedBonus(input, zero);
	EXPECT_EQ(result->val, 99 * 7);
	EXPECT_EQ(result->updater, nullptr);
}

TEST_F(BonusEvaluationContextTest, MismatchedProjectionIdentityRefusesBeforeEvaluation)
{
	EXPECT_CALL(info, unitId()).WillRepeatedly(Return(18));
	EXPECT_THROW((BonusEvaluationContext(original, projected)), std::invalid_argument);
	EXPECT_EQ(original.unitId(), 17u);
	EXPECT_EQ(original.getCount(), 8);
}
}
