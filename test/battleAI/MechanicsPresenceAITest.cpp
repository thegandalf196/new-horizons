/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ProjectedAdjacencyBonuses.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"

namespace
{
class MechanicsPresenceAITest : public ProjectedTargetHitFixture {};
class ForeignStack final : public StackWithBonuses
{
public:
	using StackWithBonuses::StackWithBonuses;
};

// A transparent test-only outer query adapter. The presence proof itself always
// runs on the exact production StackWithBonuses, not this derived object.
class GuardQueryProbe final : public StackWithBonuses
{
	std::shared_ptr<StackWithBonuses> target;
public:
	mutable size_t queries = 0;
	GuardQueryProbe(const HypotheticBattle * owner, std::shared_ptr<StackWithBonuses> target)
		: StackWithBonuses(owner, target.get()), target(std::move(target)) {}
	bool mayHaveBonusType(BonusType type) const override { return target->mayHaveBonusType(type); }
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = {}) const override
	{
		++queries;
		return target->getAllBonuses(selector, key);
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		++queries;
		return target->getUnstackedBonuses(selector);
	}
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(const CSelector & selector,
		const std::string & key = {}, bool unstacked = false) const override
	{
		++queries;
		return target->getBonusesBeforeCreatureAbilitySuppression(selector, key, unstacked);
	}
};

Bonus suppressionMarker()
{
	return Bonus(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
}
}

TEST_F(MechanicsPresenceAITest, AbsentLegacyGuardsPerformZeroMergedQueriesAndPublicAppendIsImmediate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	ASSERT_TRUE(ProjectedAdjacencyBonuses::canProveTypeAbsence(*target));
	for(const auto type : {BonusType::PHYSICAL_AFFLICTION, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusType::HYPNOTIZED, BonusType::INVINCIBLE}) ASSERT_FALSE(target->mayHaveBonusType(type));
	GuardQueryProbe probe(child.get(), target);
	for(int i = 0; i < 100; ++i)
	{
		EXPECT_FALSE(newHorizonsFrozen::isFrozen(probe));
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 0);
		EXPECT_FALSE(probe.isHypnotized());
		EXPECT_FALSE(probe.isInvincible());
	}
	EXPECT_EQ(probe.queries, 0u);
	const int version = target->treeVersionLocal;
	target->bonusesToAdd.push_back(std::make_shared<Bonus>(suppressionMarker()));
	EXPECT_EQ(target->treeVersionLocal, version); // No invalidation stamp was supplied.
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1);
	EXPECT_GT(probe.queries, 0u);
}

TEST_F(MechanicsPresenceAITest, ParentPublicEditsSnapshotsAndSiblingsKeepIndependentPresence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	auto sibling = std::make_shared<StackWithBonuses>(child.get(), braced);
	EXPECT_FALSE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	parent->bonusesToAdd.push_back(std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), 0)));
	EXPECT_TRUE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_TRUE(descendant->isNewHorizonsFrozen());
	EXPECT_FALSE(sibling->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	auto captured = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	parent->bonusesToAdd.clear();
	EXPECT_FALSE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_FALSE(descendant->isNewHorizonsFrozen());
	EXPECT_TRUE(captured->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_TRUE(captured->isNewHorizonsFrozen());
	captured->freezeDamageScoringBonusSnapshot();
	EXPECT_TRUE(captured->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_FALSE(captured->mayHaveBonusType(BonusType::HYPNOTIZED));
}

TEST_F(MechanicsPresenceAITest, InheritedInactiveLimiterAndNativeInvalidationNeverProveFalseAbsence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	ASSERT_FALSE(target->mayHaveBonusType(BonusType::HYPNOTIZED));
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	marker->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::CONFUSION_PENDING);
	attackerSideHero->addNewBonus(marker);
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::HYPNOTIZED)); // Stale is unknown.
	EXPECT_FALSE(target->isHypnotized()); // Rebuild retains the raw inactive type.
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::HYPNOTIZED));
	attackerSideHero->removeBonus(marker);
	braced->getBonusesBeforeCreatureAbilitySuppression(Selector::all);
	EXPECT_FALSE(target->mayHaveBonusType(BonusType::HYPNOTIZED));
	target->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::HYPNOTIZED)); // Form sources remain unknown.
}

TEST_F(MechanicsPresenceAITest, CustomGetterExtensionsRemainUnknown)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = std::make_shared<ForeignStack>(child.get(), braced);
	auto descendant = std::make_shared<StackWithBonuses>(child.get(), source.get());
	EXPECT_TRUE(source->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_TRUE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_TRUE(source->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	EXPECT_TRUE(descendant->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
}
