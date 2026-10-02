/*
 * NewHorizonsUnstackedBonusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"

namespace
{
using namespace ::testing;

class AddValueUpdater final : public IUpdater
{
public:
	explicit AddValueUpdater(const int32_t amount)
		: amount(amount)
	{}

	std::shared_ptr<Bonus> createUpdatedBonus(
		const std::shared_ptr<Bonus> & bonus, const CBonusSystemNode &) const override
	{
		auto updated = std::make_shared<Bonus>(*bonus);
		updated->val += amount;
		return updated;
	}

private:
	int32_t amount;
};

std::shared_ptr<Bonus> makeBonus(const BonusType type, const int32_t value, std::string stacking = {})
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		type, BonusSource::OTHER, value, BonusSourceID());
	bonus->stacking = std::move(stacking);
	return bonus;
}

std::shared_ptr<BonusList> selectBonuses(const BonusList & bonuses, const CSelector & selector)
{
	auto selected = std::make_shared<BonusList>();
	bonuses.getBonuses(*selected, selector);
	return selected;
}
}

TEST(NewHorizonsUnstackedBonus, ReturnsLimitedUpdatedEffectsBeforeStacking)
{
	CBonusSystemNode bearer(BonusNodeType::STACK_INSTANCE);
	auto updatedMorale = makeBonus(BonusType::MORALE, 1, "unstacked-test");
	updatedMorale->updater = std::make_shared<AddValueUpdater>(5);
	auto secondMorale = makeBonus(BonusType::MORALE, 2, "unstacked-test");
	auto dependentLuck = makeBonus(BonusType::LUCK, 3);
	dependentLuck->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::MORALE);
	auto rejectedLuck = makeBonus(BonusType::LUCK, 7);
	rejectedLuck->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::STACKS_SPEED);

	bearer.addNewBonus(dependentLuck);
	bearer.addNewBonus(rejectedLuck);
	bearer.addNewBonus(updatedMorale);
	bearer.addNewBonus(secondMorale);

	const auto unstacked = bearer.getUnstackedBonuses(Selector::all);
	ASSERT_EQ(unstacked->size(), 3u);
	const auto rawMorale = selectBonuses(*unstacked, Selector::type()(BonusType::MORALE));
	const auto rawLuck = selectBonuses(*unstacked, Selector::type()(BonusType::LUCK));
	ASSERT_EQ(rawMorale->size(), 2u);
	ASSERT_EQ(rawLuck->size(), 1u);
	EXPECT_EQ(rawMorale->front()->val, 6);
	EXPECT_EQ(updatedMorale->val, 1) << "The updater must return a processed copy, not mutate source state";
	EXPECT_EQ(rawLuck->front()->val, 3);

	const auto stacked = bearer.getAllBonuses(Selector::all);
	ASSERT_EQ(stacked->size(), 2u);
	const auto stackedMorale = selectBonuses(*stacked, Selector::type()(BonusType::MORALE));
	const auto stackedLuck = selectBonuses(*stacked, Selector::type()(BonusType::LUCK));
	ASSERT_EQ(stackedMorale->size(), 1u);
	ASSERT_EQ(stackedLuck->size(), 1u);
	EXPECT_EQ(stackedMorale->front()->val, 6);
	EXPECT_EQ(stackedLuck->front()->val, 3);
}

TEST(NewHorizonsUnstackedBonus, CacheInvalidatesOnBonusAdditionAndRemoval)
{
	CBonusSystemNode bearer(BonusNodeType::STACK_INSTANCE);
	const auto selector = Selector::type()(BonusType::MORALE);
	auto weaker = makeBonus(BonusType::MORALE, 1, "unstacked-invalidation");
	auto stronger = makeBonus(BonusType::MORALE, 4, "unstacked-invalidation");

	bearer.addNewBonus(weaker);
	ASSERT_EQ(bearer.getUnstackedBonuses(selector)->size(), 1u);
	ASSERT_EQ(bearer.getAllBonuses(selector, "unstacked_invalidation")->front()->val, 1);

	bearer.addNewBonus(stronger);
	const auto afterAdd = bearer.getUnstackedBonuses(selector);
	ASSERT_EQ(afterAdd->size(), 2u);
	EXPECT_EQ(bearer.getAllBonuses(selector, "unstacked_invalidation")->front()->val, 4);

	bearer.removeBonus(stronger);
	const auto afterRemove = bearer.getUnstackedBonuses(selector);
	ASSERT_EQ(afterRemove->size(), 1u);
	EXPECT_EQ(afterRemove->front()->val, 1);
	EXPECT_EQ(bearer.getAllBonuses(selector, "unstacked_invalidation")->front()->val, 1);
}

TEST(NewHorizonsUnstackedBonus, MockPreservesRawInputsAndExistingStackedOutput)
{
	BonusBearerMock bearer;
	const auto selector = Selector::type()(BonusType::MORALE);
	auto weaker = makeBonus(BonusType::MORALE, 1, "mock-unstacked");
	auto stronger = makeBonus(BonusType::MORALE, 5, "mock-unstacked");
	bearer.addNewBonus(weaker);
	bearer.addNewBonus(stronger);

	const auto stacked = bearer.getAllBonuses(selector);
	ASSERT_EQ(stacked->size(), 1u);
	EXPECT_EQ(stacked->front()->val, 5);

	const auto unstacked = bearer.getUnstackedBonuses(selector);
	ASSERT_EQ(unstacked->size(), 2u);
	EXPECT_EQ(unstacked->front()->val, 1);
	EXPECT_EQ(unstacked->back()->val, 5);
	EXPECT_EQ(bearer.getAllBonuses(selector)->front()->val, 5)
		<< "Reading the stacked view must not destructively stack the mock's input list";
}
