/*
 * StackWithBonusesQueryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/bonuses/BonusList.h"
#include <array>

namespace
{
// Same native pre-suppression values, but deliberately NOT an admitted source
// kind: production must retain its original private-copy merge for this view.
class ForeignBearer final : public IBonusBearer
{
	const battle::Unit & source;
public:
	explicit ForeignBearer(const battle::Unit & source) : source(source) {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override
	{
		return source.getBonusesBeforeCreatureAbilitySuppression(selector, key, false);
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		return source.getBonusesBeforeCreatureAbilitySuppression(selector, {}, true);
	}
	int32_t getTreeVersion() const override { return source.getTreeVersion(); }
};

struct ForcedCopyView
{
	ForeignBearer bearer;
	battle::CUnitStateDetached source;
	std::shared_ptr<StackWithBonuses> projected;
	ForcedCopyView(HypotheticBattle & battle, const battle::CUnitState & original)
		: bearer(original), source(&original, &bearer)
	{
		source.localInit(&battle);
		source = original;
		projected = std::make_shared<StackWithBonuses>(&battle, &source);
	}
};

std::vector<JsonNode> values(const TConstBonusListPtr & list)
{
	std::vector<JsonNode> result;
	for(const auto & bonus : *list)
		result.push_back(bonus->toJsonNode());
	return result;
}

std::vector<const Bonus *> identities(const TConstBonusListPtr & list)
{
	std::vector<const Bonus *> result;
	for(const auto & bonus : *list)
		result.push_back(bonus.get());
	return result;
}

Bonus addedAttack()
{
	return Bonus(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
		3, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
}

class StackWithBonusesQueryTest : public ProjectedTargetHitFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		ProjectedTargetHitFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void compare(StackWithBonuses & actual, StackWithBonuses & reference,
		bool sameRecords = true)
	{
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto before = std::array{actual.save().toString(), reference.save().toString()};
		const auto traffic = server.startedActions.size();
		for(const bool raw : {false, true})
			for(const auto & selector : {Selector::all,
				Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)),
				Selector::type()(BonusType::LUCK)})
			{
				const auto found = actual.getBonusesBeforeCreatureAbilitySuppression(selector, {}, raw);
				const auto expected = reference.getBonusesBeforeCreatureAbilitySuppression(selector, {}, raw);
				EXPECT_EQ(values(found), values(expected)); // Includes list order/duplicates.
				if(sameRecords)
				{
					EXPECT_EQ(identities(found), identities(expected));
				}
			}
		EXPECT_EQ((std::array{actual.save().toString(), reference.save().toString()}), before);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ(server.startedActions.size(), traffic);
	}
};
}

TEST_F(StackWithBonusesQueryTest, EmptyNativeOverlaySharesStableOwnerAndMatchesForcedCopy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto projected = std::make_shared<StackWithBonuses>(child.get(), braced);
	ForcedCopyView reference(*child, *braced);
	const auto layer = projected->recipientOverlayView();
	ASSERT_TRUE(layer.added.empty() && layer.updated.empty() && layer.removed.empty());
	ASSERT_FALSE(layer.stackedSnapshot);
	ASSERT_FALSE(layer.unstackedSnapshot);
	ASSERT_FALSE(layer.veteranCohesionEarned);
	const auto inherited = braced->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.native");
	const auto found = projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.native");
	const auto copied = reference.projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.native");
	EXPECT_EQ(found.get(), inherited.get());
	EXPECT_NE(copied.get(), inherited.get());
	compare(*projected, *reference.projected);
}

TEST_F(StackWithBonusesQueryTest, HeldNativeMembershipSurvivesCacheReplacementAndRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto projected = std::make_shared<StackWithBonuses>(child.get(), braced);
	const auto before = projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.held");
	const auto beforeValues = values(before);
	const auto bonus = std::make_shared<Bonus>(addedAttack());
	braced->addNewBonus(bonus);
	const auto added = projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.held");
	const auto addedValues = values(added);
	EXPECT_NE(addedValues, beforeValues);
	EXPECT_EQ(values(before), beforeValues);
	braced->removeBonus(bonus); // Changes membership, not a shared record's value.
	EXPECT_EQ(values(projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "empty.held")), beforeValues);
	EXPECT_EQ(values(added), addedValues);
	EXPECT_EQ(values(before), beforeValues);
}

TEST_F(StackWithBonusesQueryTest, ProjectedParentLifetimeAndLaterOverlayDoNotChangeHeldLists)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	const auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	std::weak_ptr<StackWithBonuses> weak = parent;
	const auto held = descendant->getBonusesBeforeCreatureAbilitySuppression(Selector::all);
	const auto original = values(held);
	parent->addUnitBonus({addedAttack()});
	const auto parentAdded = descendant->getBonusesBeforeCreatureAbilitySuppression(Selector::all);
	const auto parentValues = values(parentAdded);
	EXPECT_NE(parentValues, original);
	EXPECT_EQ(values(held), original);
	parent.reset();
	ASSERT_FALSE(weak.expired()); // descendant owns its projected source.
	descendant->addUnitBonus({addedAttack()});
	EXPECT_NE(values(descendant->getBonusesBeforeCreatureAbilitySuppression(Selector::all)), parentValues);
	EXPECT_EQ(values(parentAdded), parentValues);
	EXPECT_EQ(values(held), original);
}

TEST_F(StackWithBonusesQueryTest, AddedRemovedAndEngagedSnapshotsRetainCopyPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto projected = std::make_shared<StackWithBonuses>(child.get(), braced);
	// A derived projected source forces the legacy-copy path while retaining
	// exact source-local spell provenance through its native CStack ancestor.
	class ProvenanceSource final : public StackWithBonuses
	{
	public:
		using StackWithBonuses::StackWithBonuses;
	};
	const auto referenceSource = std::make_shared<ProvenanceSource>(child.get(), braced);
	const auto reference = std::make_shared<StackWithBonuses>(child.get(), referenceSource.get());
	const auto inherited = referenceSource->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "snapshot.copy");
	const auto copied = reference->getBonusesBeforeCreatureAbilitySuppression(Selector::all, "snapshot.copy");
	EXPECT_EQ(values(inherited), values(copied));
	EXPECT_NE(inherited.get(), copied.get()); // Positive fallback witness before mutation.
	projected->addUnitBonus({addedAttack()});
	reference->addUnitBonus({addedAttack()});
	compare(*projected, *reference, false);
	const auto removeAttack = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK));
	projected->removeUnitBonus(removeAttack);
	reference->removeUnitBonus(removeAttack);
	ASSERT_TRUE(projected->recipientOverlayView().stackedSnapshot);
	ASSERT_TRUE(projected->recipientOverlayView().unstackedSnapshot);
	compare(*projected, *reference, false);
	projected->freezeDamageScoringBonusSnapshot();
	reference->freezeDamageScoringBonusSnapshot();
	compare(*projected, *reference, false);
}

TEST_F(StackWithBonusesQueryTest, VeteranAndCreatureFormKeepTheirExistingFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto projected = std::make_shared<StackWithBonuses>(child.get(), braced);
	ForcedCopyView reference(*child, *braced);
	projected->veteranCohesionEarned = true;
	reference.projected->veteranCohesionEarned = true;
	compare(*projected, *reference.projected, false);
	projected->veteranCohesionEarned = false;
	reference.projected->veteranCohesionEarned = false;
	projected->beginBattleForm(creatureByName("core:pikeman"), 2);
	reference.projected->beginBattleForm(creatureByName("core:pikeman"), 2);
	ASSERT_TRUE(projected->hasBattleFormState());
	compare(*projected, *reference.projected, false);
	projected->endBattleForm();
	reference.projected->endBattleForm();
	compare(*projected, *reference.projected, false);
}

TEST_F(StackWithBonusesQueryTest, ThrowingSelectorLeavesStatesBothRngsAndHeldMembershipAlone)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto projected = std::make_shared<StackWithBonuses>(child.get(), braced);
	ForcedCopyView reference(*child, *braced);
	const auto held = projected->getBonusesBeforeCreatureAbilitySuppression(Selector::all);
	const auto original = values(held);
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto before = std::array{projected->save().toString(), reference.projected->save().toString()};
	for(const auto & view : {projected, reference.projected})
	{
		unsigned calls = 0;
		const CSelector throwing([&](const Bonus *) -> bool
		{
			++calls;
			throw std::runtime_error("expected selector failure");
		});
		EXPECT_THROW(view->getBonusesBeforeCreatureAbilitySuppression(throwing), std::runtime_error);
		EXPECT_EQ(calls, 1u);
	}
	EXPECT_EQ(values(held), original);
	EXPECT_EQ((std::array{projected->save().toString(), reference.projected->save().toString()}), before);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
}
