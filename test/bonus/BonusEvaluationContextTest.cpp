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

TEST_F(BonusEvaluationContextTest, NativeCountOnlyContextPreservesIdentityAndRejectsNegativeCounts)
{
	const BonusEvaluationContext native(original), counted(original, 3);
	EXPECT_EQ(counted.stackCount(), 3);
	EXPECT_EQ(native.stackCount(), 8);
	EXPECT_EQ(counted.getOwner(), native.getOwner());
	EXPECT_EQ(counted.battleUnit(), native.battleUnit());
	EXPECT_EQ(counted.stackInstance(), native.stackInstance());
	EXPECT_EQ(counted.creature(), native.creature());
	EXPECT_EQ(original.getCount(), 8);
	EXPECT_THROW((BonusEvaluationContext(original, -1)), std::invalid_argument);
}

namespace
{
class CountQueryProbe final : public IUpdater
{
	std::shared_ptr<int> calls;
public:
	explicit CountQueryProbe(std::shared_ptr<int> calls) : calls(std::move(calls)) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		++*calls;
		return TimesStackSizeUpdater().createUpdatedBonus(bonus, context);
	}
};
std::shared_ptr<Bonus> countedDefense(const std::shared_ptr<int> & calls)
{
	auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PHYSICAL_DEFENSE,
		BonusSource::OTHER, 999, BonusSourceID());
	result->updater = std::make_shared<CountQueryProbe>(calls);
	return result;
}
void loseOne(battle::CUnitState & state)
{
	int64_t damage = state.getMaxHealth();
	state.damage(damage);
}
}

TEST_F(BonusEvaluationContextTest, DetachedCountQueryCachesOneCollectionPerKeyWithoutLiveWrites)
{
	const auto calls = std::make_shared<int>(0);
	original.addNewBonus(countedDefense(calls));
	ASSERT_EQ(original.valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8);
	const auto nativeState = original.save();
	const auto nativeVersion = original.getTreeVersion();
	auto parent = original.acquireState();
	const int nativeCalls = *calls;
	EXPECT_EQ(parent->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8);
	EXPECT_EQ(*calls, nativeCalls) << "Unchanged count uses the warmed native cache";
	loseOne(*parent);
	ASSERT_EQ(parent->getCount(), 7);
	EXPECT_EQ(parent->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	const int firstCollection = *calls;
	ASSERT_EQ(firstCollection, nativeCalls + 1) << "One changed-count key performs one canonical collection";
	EXPECT_EQ(parent->getBonusBearer()->getUnstackedBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE))->totalValue(), 7);
	EXPECT_EQ(parent->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_EQ(*calls, firstCollection) << "Selector/stacked variants reuse the same complete collection";
	auto child = parent->acquireState();
	loseOne(*child);
	ASSERT_EQ(child->getCount(), 6);
	EXPECT_EQ(child->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 6);
	EXPECT_EQ(parent->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_EQ(original.valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8);
	EXPECT_EQ(original.save(), nativeState);
	EXPECT_EQ(original.getTreeVersion(), nativeVersion);
	*child = static_cast<const battle::CUnitState &>(original);
	EXPECT_EQ(child->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8)
		<< "Restored child must bypass its still-injured parent's count";
	EXPECT_EQ(parent->getCount(), 7);
}

TEST_F(BonusEvaluationContextTest, DetachedAssignmentAndSourceVersionInvalidateOnlyOwnedDerivedLists)
{
	const auto calls = std::make_shared<int>(0);
	original.addNewBonus(countedDefense(calls));
	auto first = original.acquireState();
	loseOne(*first);
	ASSERT_EQ(first->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	const int firstCalls = *calls;
	auto second = original.acquireState();
	loseOne(*second); // Damage may itself query and warm this clone's new-count list.
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_GT(*calls, firstCalls) << "Fresh clones cannot share mutable derived caches";
	const int secondCalls = *calls;
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_EQ(*calls, secondCalls) << "Subsequent queries reuse the second clone's own list";
	auto & destination = dynamic_cast<battle::CUnitStateDetached &>(*second);
	destination = dynamic_cast<const battle::CUnitStateDetached &>(*first);
	const int beforeAssignmentQuery = *calls;
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_GT(*calls, beforeAssignmentQuery);
	const int beforeSourceChange = *calls;
	original.nodeHasChanged();
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_GT(*calls, beforeSourceChange);
	const int afterSourceChange = *calls;
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_EQ(*calls, afterSourceChange);
	original.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PHYSICAL_DEFENSE,
		BonusSource::OTHER, 2, BonusSourceID()));
	EXPECT_EQ(second->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 9);
	EXPECT_EQ(original.valOfBonuses(BonusType::PHYSICAL_DEFENSE), 10);
	EXPECT_EQ(original.getCount(), 8);
}

TEST_F(BonusEvaluationContextTest, CountAndSourceVersionCannotAliasScalarCacheEpochs)
{
	auto defense = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 999, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE));
	defense->updater = std::make_shared<TimesStackSizeUpdater>();
	original.addNewBonus(defense);
	auto state = original.acquireState();
	ASSERT_EQ(state->getDefense(false), 8);
	const int oldRevision = state->getTreeVersion();
	const int oldSourceVersion = original.getTreeVersion();
	original.nodeHasChanged();
	ASSERT_EQ(original.getTreeVersion(), oldSourceVersion + 1);
	loseOne(*state);
	ASSERT_EQ(state->getCount(), 7);
	EXPECT_NE(state->getTreeVersion(), oldRevision) << "count+sourceVersion sums would collide here";
	EXPECT_EQ(state->getDefense(false), 7);
	*state = static_cast<const battle::CUnitState &>(original);
	EXPECT_EQ(state->getDefense(false), 8);
	EXPECT_EQ(original.getDefense(false), 8);
}

TEST_F(BonusEvaluationContextTest, DetachedCountListsStackBeforeSelectionAndRetainUnstackedQuery)
{
	auto counted = countedDefense(std::make_shared<int>(0));
	counted->stacking = "test:count-context-order";
	original.addNewBonus(counted);
	auto constant = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PHYSICAL_DEFENSE,
		BonusSource::CREATURE_ABILITY, 7, BonusSourceID());
	constant->stacking = counted->stacking;
	original.addNewBonus(constant);
	ASSERT_EQ(original.valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8);
	auto state = original.acquireState();
	loseOne(*state);
	loseOne(*state);
	ASSERT_EQ(state->getCount(), 6);
	const auto selected = Selector::type()(BonusType::PHYSICAL_DEFENSE).And(Selector::sourceType()(BonusSource::OTHER));
	EXPECT_TRUE(state->getBonuses(selected)->empty());
	EXPECT_EQ(state->getUnstackedBonuses(selected)->totalValue(), 6);
	EXPECT_EQ(state->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 7);
	EXPECT_EQ(original.valOfBonuses(BonusType::PHYSICAL_DEFENSE), 8);
}

}
