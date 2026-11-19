/*
 * SelectedBonusOverlayReuseTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include <array>

namespace
{
class SelectedBonusOverlayReuseTest : public ProjectedTargetHitFixture
{
protected:
	// A deliberately foreign dynamic type preserves the old copy path. It still
	// retains the actual native source and all its normal query semantics.
	class CopySource final : public StackWithBonuses
	{
	public:
		using StackWithBonuses::StackWithBonuses;
	};
	std::shared_ptr<StackWithBonuses> projected;
	std::shared_ptr<CopySource> foreign;
	std::shared_ptr<StackWithBonuses> reference;

	void start()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		projected = std::make_shared<StackWithBonuses>(child.get(), braced);
		foreign = std::make_shared<CopySource>(child.get(), braced);
		reference = std::make_shared<StackWithBonuses>(child.get(), foreign.get());
	}
	Bonus luck(int value = 2) const
	{
		Bonus result(BonusDuration::N_TURNS, BonusType::LUCK, BonusSource::SPELL_EFFECT,
			value, BonusSourceID(SpellID(SpellID::FORTUNE)));
		result.turnsRemain = 3;
		return result;
	}
	void add(const Bonus & bonus)
	{
		projected->addUnitBonus({bonus});
		reference->addUnitBonus({bonus});
	}
	static std::vector<JsonNode> values(const TConstBonusListPtr & list)
	{
		std::vector<JsonNode> result;
		for(const auto & record : *list)
			result.push_back(record->toJsonNode());
		return result;
	}
	void compare()
	{
		const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto before = std::array{projected->save().toString(), reference->save().toString()};
		for(const bool raw : {false, true})
			for(const auto & selector : {Selector::all, Selector::type()(BonusType::LUCK),
				Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION),
				Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE))})
				EXPECT_EQ(values(projected->getBonusesBeforeCreatureAbilitySuppression(selector, {}, raw)),
					values(reference->getBonusesBeforeCreatureAbilitySuppression(selector, {}, raw)));
		EXPECT_EQ((std::array{projected->save().toString(), reference->save().toString()}), before);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	}
};
}

TEST_F(SelectedBonusOverlayReuseTest, UnrelatedAddedAndSnapshotEffectsShareAbsentMarkerList)
{
	ASSERT_NO_FATAL_FAILURE(start());
	add(luck());
	const auto selector = Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION);
	const auto source = braced->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.absent");
	const auto actual = projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.absent");
	EXPECT_EQ(actual.get(), source.get());
	EXPECT_NE(reference->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.absent").get(), source.get());
	compare();
}

TEST_F(SelectedBonusOverlayReuseTest, SelectedAddedEffectsKeepCopyAndExactOrderedValues)
{
	ASSERT_NO_FATAL_FAILURE(start());
	add(luck());
	const auto selector = Selector::type()(BonusType::LUCK);
	EXPECT_NE(projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.selected").get(),
		braced->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.selected").get());
	compare();
}

TEST_F(SelectedBonusOverlayReuseTest, EmptyProjectionSnapshotStillRemovesInheritedSpellEffect)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const auto native = std::make_shared<Bonus>(luck());
	braced->addNewBonus(native);
	projected->removeUnitBonus(Selector::type()(BonusType::LUCK));
	reference->removeUnitBonus(Selector::type()(BonusType::LUCK));
	ASSERT_TRUE(projected->recipientOverlayView().stackedSnapshot.has_value());
	compare();
	EXPECT_TRUE(projected->getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::LUCK))->empty());
}

TEST_F(SelectedBonusOverlayReuseTest, PointerRemovalDoesNotRemoveEqualIndependentRecord)
{
	ASSERT_NO_FATAL_FAILURE(start());
	Bonus native = luck();
	native.source = BonusSource::ARTIFACT;
	const auto removed = std::make_shared<Bonus>(native);
	const auto retained = std::make_shared<Bonus>(native);
	braced->addNewBonus(removed);
	braced->addNewBonus(retained);
	projected->bonusesToRemove.insert(removed);
	reference->bonusesToRemove.insert(removed);
	compare();
	const auto raw = projected->getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::LUCK), {}, true);
	EXPECT_EQ(std::ranges::count(*raw, removed), 0);
	EXPECT_EQ(std::ranges::count(*raw, retained), 1);
}

TEST_F(SelectedBonusOverlayReuseTest, RefreshIdentityKeepsFullMergeEvenWhenQueryIsUnrelated)
{
	ASSERT_NO_FATAL_FAILURE(start());
	braced->addNewBonus(std::make_shared<Bonus>(luck()));
	Bonus refresh = luck(99);
	refresh.turnsRemain = 5;
	projected->updateUnitBonus({refresh});
	reference->updateUnitBonus({refresh});
	ASSERT_FALSE(projected->recipientOverlayView().updated.empty());
	compare(); // Refresh must not become a new +99 effect or leak all types.
}

TEST_F(SelectedBonusOverlayReuseTest, HeldResultSurvivesNativeAndParentEditsWithoutEpochCache)
{
	ASSERT_NO_FATAL_FAILURE(start());
	add(luck());
	const auto selector = Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION);
	const auto held = projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.held");
	const auto before = values(held);
	Bonus marker(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
	marker.turnsRemain = 3;
	const auto record = std::make_shared<Bonus>(marker);
	braced->addNewBonus(record);
	EXPECT_NE(values(projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.held")), before);
	EXPECT_EQ(values(held), before);
	compare();
	braced->removeBonus(record);
	EXPECT_EQ(values(projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.held")), before);
	EXPECT_EQ(values(held), before);
}

TEST_F(SelectedBonusOverlayReuseTest, SelectedAdditionPredicateRunsOnceAndExceptionsPropagate)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const auto addition = std::make_shared<Bonus>(luck(777));
	projected->bonusesToAdd.push_back(addition);
	int selectedCalls = 0;
	const CSelector selected([&](const Bonus * record)
	{
		if(record == addition.get())
			++selectedCalls;
		return record->type == BonusType::LUCK;
	});
	const auto found = projected->getBonusesBeforeCreatureAbilitySuppression(selected);
	EXPECT_EQ(selectedCalls, 1); // No speculative proof followed by a second merge query.
	EXPECT_EQ(std::ranges::count(*found, addition), 1);
	selectedCalls = 0;
	const CSelector failing([&](const Bonus * record)
	{
		if(record == addition.get())
		{
			++selectedCalls;
			throw std::runtime_error("Selected overlay predicate failure");
		}
		return false;
	});
	EXPECT_THROW(projected->getBonusesBeforeCreatureAbilitySuppression(failing), std::runtime_error);
	EXPECT_EQ(selectedCalls, 1);
}

TEST_F(SelectedBonusOverlayReuseTest, NestedTimedSnapshotMatchesForcedCopyWhileUncapturedParentChangesStayLive)
{
	ASSERT_NO_FATAL_FAILURE(start());
	add(luck());
	const auto nested = std::make_shared<StackWithBonuses>(child.get(), projected.get());
	const auto copyParent = std::make_shared<CopySource>(child.get(), braced);
	copyParent->addUnitBonus({luck()});
	const auto copied = std::make_shared<StackWithBonuses>(child.get(), copyParent.get());
	nested->addUnitBonus({luck(3)});
	copied->addUnitBonus({luck(3)});
	ASSERT_TRUE(nested->recipientOverlayView().stackedSnapshot.has_value());
	ASSERT_TRUE(copied->recipientOverlayView().stackedSnapshot.has_value());
	const auto selector = Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION);
	const auto parentList = projected->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.nested");
	const auto held = nested->getBonusesBeforeCreatureAbilitySuppression(selector, "reuse.nested");
	EXPECT_EQ(held.get(), parentList.get());
	const auto before = values(held);
	EXPECT_EQ(values(copied->getBonusesBeforeCreatureAbilitySuppression(selector)), before);
	Bonus marker(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
	marker.turnsRemain = 3;
	projected->addUnitBonus({marker});
	copyParent->addUnitBonus({marker});
	// Timed Fortune caused descendant construction to capture magical effects.
	// A later parent spell must not enter either independent descendant snapshot.
	EXPECT_NE(values(projected->getBonusesBeforeCreatureAbilitySuppression(selector)), before);
	EXPECT_NE(values(copyParent->getBonusesBeforeCreatureAbilitySuppression(selector)), before);
	EXPECT_EQ(values(nested->getBonusesBeforeCreatureAbilitySuppression(selector)), before);
	EXPECT_EQ(values(copied->getBonusesBeforeCreatureAbilitySuppression(selector)), before);
	// A noncaptured parent record remains live even with those snapshots present.
	Bonus ordinary(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::ARTIFACT,
		7, BonusSourceID(ArtifactID(0)));
	projected->addUnitBonus({ordinary});
	copyParent->addUnitBonus({ordinary});
	const auto luckSelector = Selector::type()(BonusType::LUCK);
	const auto nestedLuck = nested->getBonusesBeforeCreatureAbilitySuppression(luckSelector);
	const auto copiedLuck = copied->getBonusesBeforeCreatureAbilitySuppression(luckSelector);
	EXPECT_EQ(values(nestedLuck), values(copiedLuck));
	EXPECT_EQ(std::ranges::count_if(*nestedLuck, [](const auto & record)
	{
		return record->source == BonusSource::ARTIFACT && record->val == 7;
	}), 1);
	EXPECT_EQ(values(held), before);
}
