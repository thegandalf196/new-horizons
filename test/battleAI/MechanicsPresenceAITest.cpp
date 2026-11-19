/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackProjectionTestAccess.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ProjectedAdjacencyBonuses.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"

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

TEST_F(MechanicsPresenceAITest, AbsentLegacyGuardsPerformZeroMergedQueriesAndControlledAppendIsImmediate)
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
	const int version = StackWithBonusesTestAccess::treeVersion(*target);
	StackWithBonusesTestAccess::appendAdded(*target, std::make_shared<Bonus>(suppressionMarker()));
	EXPECT_EQ(StackWithBonusesTestAccess::treeVersion(*target), version); // The effect-tree version is separate from the presence revision.
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1);
	EXPECT_GT(probe.queries, 0u);
}

TEST_F(MechanicsPresenceAITest, ParentControlledEditsSnapshotsAndSiblingsKeepIndependentPresence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	auto sibling = std::make_shared<StackWithBonuses>(child.get(), braced);
	EXPECT_FALSE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	StackWithBonusesTestAccess::appendAdded(*parent, std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), 0)));
	EXPECT_TRUE(descendant->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_TRUE(descendant->isNewHorizonsFrozen());
	EXPECT_FALSE(sibling->mayHaveBonusType(BonusType::PHYSICAL_AFFLICTION));
	auto captured = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	StackWithBonusesTestAccess::clearAdded(*parent);
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

TEST_F(MechanicsPresenceAITest, OrdinaryOperationsInvalidatePresenceAndRoundExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	EXPECT_FALSE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	ASSERT_TRUE(StackWithBonusesTestAccess::cached(*target));
	const auto before = StackWithBonusesTestAccess::projectionVersion(*target);
	auto marker = suppressionMarker();
	marker.turnsRemain = 1;
	target->addUnitBonus({marker});
	EXPECT_GT(StackWithBonusesTestAccess::projectionVersion(*target), before);
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	marker.turnsRemain = 2;
	target->updateUnitBonus({marker});
	target->advanceTimedRound();
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	target->advanceTimedRound();
	EXPECT_FALSE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	target->addUnitBonus({marker});
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
	target->removeUnitBonus(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION));
	EXPECT_FALSE(target->mayHaveBonusType(BonusType::CREATURE_ABILITY_SUPPRESSION));
}

TEST_F(MechanicsPresenceAITest, QueriesDuringMutationNeverReuseIntermediateSummary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	const Bonus record(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID());
	EXPECT_TRUE(StackWithBonusesTestAccess::queryInsideMutation(*target, record));
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::INVINCIBLE));
}

TEST_F(MechanicsPresenceAITest, QueryPointeeMutationKeepsExactPresenceAfterAliasEscape)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	Bonus record(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	target->addUnitBonus({record});
	ASSERT_FALSE(target->mayHaveBonusType(BonusType::INVINCIBLE));
	const auto held = target->getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::HYPNOTIZED), {}, true);
	ASSERT_EQ(held->size(), 1u);
	ASSERT_TRUE(StackWithBonusesTestAccess::aliasesExposed(*target));
	held->front()->type = BonusType::INVINCIBLE;
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::INVINCIBLE));
	EXPECT_FALSE(target->mayHaveBonusType(BonusType::HYPNOTIZED));
}

TEST_F(MechanicsPresenceAITest, PreparedDraftMutableAliasesRetireBothSharedSummaries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto source = std::make_shared<StackWithBonuses>(child.get(), braced);
	Bonus record(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	source->addUnitBonus({record});
	const auto draft = source->prepareEffectDraft();
	ASSERT_FALSE(source->mayHaveBonusType(BonusType::INVINCIBLE));
	ASSERT_FALSE(draft->mayHaveBonusType(BonusType::INVINCIBLE));
	const auto held = draft->getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::HYPNOTIZED), {}, true);
	ASSERT_EQ(held->size(), 1u);
	ASSERT_TRUE(StackWithBonusesTestAccess::aliasesExposed(*source));
	held->front()->type = BonusType::INVINCIBLE;
	EXPECT_TRUE(source->mayHaveBonusType(BonusType::INVINCIBLE));
	EXPECT_TRUE(draft->mayHaveBonusType(BonusType::INVINCIBLE));
}

TEST_F(MechanicsPresenceAITest, RawProjectionAndFrozenScoringAliasesRemainObservable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool freeze : {false, true})
	{
		const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
		Bonus record(BonusDuration::N_TURNS, BonusType::HYPNOTIZED,
			BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
		record.turnsRemain = 3;
		target->addUnitBonus({record});
		target->removeUnitBonus(Selector::type()(BonusType::LUCK));
		if(freeze)
			target->freezeDamageScoringBonusSnapshot();
		ASSERT_FALSE(target->mayHaveBonusType(BonusType::INVINCIBLE));
		const auto held = target->getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::HYPNOTIZED), {}, true);
		ASSERT_EQ(held->size(), 1u);
		held->front()->type = BonusType::INVINCIBLE;
		EXPECT_TRUE(target->mayHaveBonusType(BonusType::INVINCIBLE)) << freeze;
	}
}

TEST_F(MechanicsPresenceAITest, PublishingPreparedEffectsInvalidatesBothReceivers)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto source = std::make_shared<StackWithBonuses>(child.get(), braced);
	const auto draft = source->prepareEffectDraft();
	ASSERT_FALSE(source->mayHaveBonusType(BonusType::INVINCIBLE));
	const auto before = StackWithBonusesTestAccess::projectionVersion(*source);
	draft->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(draft->mayHaveBonusType(BonusType::INVINCIBLE));
	ASSERT_TRUE(source->preparedEffectSourceMatches(*draft));
	source->publishPreparedEffects(*draft);
	EXPECT_GT(StackWithBonusesTestAccess::projectionVersion(*source), before);
	EXPECT_TRUE(source->mayHaveBonusType(BonusType::INVINCIBLE));
	EXPECT_FALSE(draft->mayHaveBonusType(BonusType::INVINCIBLE));
}

TEST_F(MechanicsPresenceAITest, BorrowedUpdaterGraphNeverBecomesAnOwnedTypeSummary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	Bonus record(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	const auto updater = std::make_shared<CompositeUpdater>();
	record.updater = updater;
	target->addUnitBonus({record});
	ASSERT_FALSE(target->mayHaveBonusType(BonusType::INVINCIBLE));
	ASSERT_FALSE(StackWithBonusesTestAccess::cacheable(*target));
	// The caller retains the same nested updater graph after the Bonus value copy.
	// A depth-limit failure in that graph must become unknown on the next query.
	std::shared_ptr<CompositeUpdater> tail = updater;
	for(int i = 0; i < 33; ++i)
	{
		const auto next = std::make_shared<CompositeUpdater>();
		tail->updaters.push_back(next);
		tail = next;
	}
	EXPECT_TRUE(target->mayHaveBonusType(BonusType::INVINCIBLE));
}

TEST_F(MechanicsPresenceAITest, LayerSummariesPreserveTheCombinedRecordBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto record = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	const auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	StackWithBonusesTestAccess::replaceAdded(*parent, std::vector<std::shared_ptr<Bonus>>(4096, record));
	const auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	StackWithBonusesTestAccess::replaceAdded(*descendant, std::vector<std::shared_ptr<Bonus>>(4096, record));
	EXPECT_FALSE(descendant->mayHaveBonusType(BonusType::INVINCIBLE));
	StackWithBonusesTestAccess::appendAdded(*descendant, record);
	EXPECT_TRUE(descendant->mayHaveBonusType(BonusType::INVINCIBLE));
}
