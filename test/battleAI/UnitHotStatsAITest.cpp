/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusList.h"
#include <thread>

namespace
{
class UnitHotStatsAITest : public ProjectedTargetHitFixture {};

using newHorizonsCreatureAbilitySuppression::suppressionLevel;

Bonus hotStatsSuppressionMarker(int level = 1)
{
	return Bonus(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, level, BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
}

Bonus hotStatsHypnotizedMarker()
{
	return Bonus(BonusDuration::N_TURNS, BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 1,
		BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
}

Bonus hotStatsHealthBonus()
{
	return Bonus(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 17, BonusSourceID());
}
}

TEST_F(UnitHotStatsAITest, NativeStackAnswersFollowEveryBonusTreeChange)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto health = braced->getMaxHealth();
	for(int i = 0; i < 3; ++i)
	{
		EXPECT_EQ(suppressionLevel(*braced), 0);
		EXPECT_FALSE(braced->isHypnotized());
		EXPECT_EQ(braced->getMaxHealth(), health);
	}
	const auto marker = std::make_shared<Bonus>(hotStatsSuppressionMarker(2));
	const auto hypnotized = std::make_shared<Bonus>(hotStatsHypnotizedMarker());
	const auto extra = std::make_shared<Bonus>(hotStatsHealthBonus());
	braced->addNewBonus(marker);
	braced->addNewBonus(hypnotized);
	braced->addNewBonus(extra);
	EXPECT_EQ(suppressionLevel(*braced), 2);
	EXPECT_TRUE(braced->isHypnotized());
	EXPECT_EQ(braced->getMaxHealth(), health + 17);
	braced->removeBonus(marker);
	braced->removeBonus(hypnotized);
	braced->removeBonus(extra);
	EXPECT_EQ(suppressionLevel(*braced), 0);
	EXPECT_FALSE(braced->isHypnotized());
	EXPECT_EQ(braced->getMaxHealth(), health);
	// A side-wide source reaches the stack through the inherited tree version.
	attackerSideHero->addNewBonus(extra);
	EXPECT_EQ(braced->getMaxHealth(), health + 17);
	attackerSideHero->removeBonus(extra);
	EXPECT_EQ(braced->getMaxHealth(), health);
}

TEST_F(UnitHotStatsAITest, ProjectedAnswersFollowBranchMutators)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = braced->unitId();
	const auto target = child->getForUpdate(id);
	EXPECT_EQ(suppressionLevel(*target), 0);
	EXPECT_EQ(suppressionLevel(*target), 0);
	child->addUnitBonus(id, {hotStatsSuppressionMarker()});
	EXPECT_EQ(suppressionLevel(*target), 1);
	child->removeUnitBonus(id, {hotStatsSuppressionMarker()});
	EXPECT_EQ(suppressionLevel(*target), 0);

	EXPECT_FALSE(target->isHypnotized());
	child->addUnitBonus(id, {hotStatsHypnotizedMarker()});
	EXPECT_TRUE(target->isHypnotized());
	child->removeUnitBonus(id, {hotStatsHypnotizedMarker()});
	EXPECT_FALSE(target->isHypnotized());

	const auto health = target->getMaxHealth();
	EXPECT_EQ(target->getMaxHealth(), health);
	child->addUnitBonus(id, {hotStatsHealthBonus()});
	EXPECT_EQ(target->getMaxHealth(), health + 17);
	child->removeUnitBonus(id, {hotStatsHealthBonus()});
	EXPECT_EQ(target->getMaxHealth(), health);
}

TEST_F(UnitHotStatsAITest, PublicOverlayEditsOnAnyProjectedLayerAreSeenAtOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	EXPECT_EQ(suppressionLevel(*descendant), 0);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*descendant));
	// Public containers are edited without a version bump.
	descendant->bonusesToAdd.push_back(std::make_shared<Bonus>(hotStatsSuppressionMarker()));
	EXPECT_EQ(suppressionLevel(*descendant), 1);
	descendant->bonusesToAdd.clear();
	EXPECT_EQ(suppressionLevel(*descendant), 0);
	parent->bonusesToAdd.push_back(std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), 0)));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*descendant));
	// Same container size, different record.
	parent->bonusesToAdd.front() = std::make_shared<Bonus>(hotStatsSuppressionMarker(2));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*descendant));
	EXPECT_EQ(suppressionLevel(*descendant), 2);
	parent->bonusesToAdd.clear();
	EXPECT_EQ(suppressionLevel(*descendant), 0);
}

TEST_F(UnitHotStatsAITest, ProjectedAnswersFollowCountAndAssignment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, 10, 100));
	const auto target = child->getForUpdate(incoming->unitId());
	const auto health = target->getMaxHealth();
	EXPECT_EQ(suppressionLevel(*target), 0);
	int64_t damage = health * 3;
	target->damage(damage);
	EXPECT_EQ(target->getCount(), 97);
	EXPECT_EQ(target->getMaxHealth(), health);
	target->bonusesToAdd.push_back(std::make_shared<Bonus>(hotStatsSuppressionMarker()));
	EXPECT_EQ(suppressionLevel(*target), 1);
	*target = *incoming; // Whole-state copy: no answer of the old state survives.
	EXPECT_EQ(target->getCount(), 100);
	EXPECT_EQ(target->getMaxHealth(), health);
	EXPECT_EQ(suppressionLevel(*target), 1); // The overlay is not part of the copied state.
	target->bonusesToAdd.clear();
	EXPECT_EQ(suppressionLevel(*target), 0);
}

TEST_F(UnitHotStatsAITest, AnotherThreadComputesWithoutTouchingTheOwnerSlots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = child->getForUpdate(braced->unitId());
	child->addUnitBonus(braced->unitId(), {hotStatsSuppressionMarker()});
	EXPECT_EQ(suppressionLevel(*target), 1);
	int32_t observed = -1;
	std::thread reader([&target, &observed]()
	{
		observed = suppressionLevel(*target);
	});
	reader.join();
	EXPECT_EQ(observed, 1);
	EXPECT_EQ(suppressionLevel(*target), 1);
}
