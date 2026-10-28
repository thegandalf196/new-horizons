/*
 * StackWithBonusesQueryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/int3.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/bonuses/BonusList.h"
#include <array>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<HypotheticBattle>);
static_assert(!std::is_copy_assignable_v<HypotheticBattle>);

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

class ForeignBattleSubject final : public CPlayerBattleCallback
{
public:
	using CPlayerBattleCallback::CPlayerBattleCallback;
};

// Carries a legal proxy-base subject for assignment tests; never simulates a battle.
class ReadOnlySubjectProxy final : public BattleProxy
{
public:
	using BattleProxy::BattleProxy;
	BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
	const scripting::Pool & getScriptContextPool() const override { return subject->getBattle()->getScriptContextPool(); }
	std::vector<SpellID> getUsedSpells(BattleSide side) const override { return subject->getBattle()->getUsedSpells(side); }
	uint32_t nextUnitId() const override { return subject->getBattle()->nextUnitId(); }
	int64_t getActualDamage(const DamageRange &, int32_t, vstd::RNG &) const override
	{
		throw std::logic_error("Read-only subject proxy cannot realize damage");
	}
	int3 getLocation() const override { return subject->getBattle()->getLocation(); }
	BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }
	void nextRound() override { rejectMutation(); }
	void nextTurn(uint32_t, BattleUnitTurnReason) override { rejectMutation(); }
	void addUnit(uint32_t, const JsonNode &) override { rejectMutation(); }
	void updateUnit(uint32_t, const JsonNode &, int64_t) override { rejectMutation(); }
	void moveUnit(uint32_t, const BattleHex &) override { rejectMutation(); }
	void removeUnit(uint32_t) override { rejectMutation(); }
	void addUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void setWallState(EWallPart, EWallState) override { rejectMutation(); }
	void addObstacle(const ObstacleChanges &) override { rejectMutation(); }
	void updateObstacle(const ObstacleChanges &) override { rejectMutation(); }
	void removeObstacle(uint32_t) override { rejectMutation(); }
private:
	[[noreturn]] static void rejectMutation() { throw std::logic_error("Read-only subject proxy cannot mutate battle"); }
};
static_assert(!std::is_abstract_v<ReadOnlySubjectProxy>);
static_assert(std::is_copy_assignable_v<BattleProxy>);

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

TEST_F(StackWithBonusesQueryTest, OrdinaryAndForeignRouteViewsRemainEmptyAcrossParentOverlayChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto * unit : battle()->battleGetAllStacks(false))
		(void)unit->counterAttacks.total();
	const auto nativeBefore = braced->acquireState()->save();
	const auto nativeValues = values(braced->getBonusesBeforeCreatureAbilitySuppression(Selector::all));
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto foreign = std::make_shared<ForeignBattleSubject>(battle(), PlayerColor(0));
	const auto foreignBranch = std::make_shared<HypotheticBattle>(environment.get(), foreign);
	for(const auto & branch : {child, foreignBranch})
	{
		const auto sourceOwners = live.use_count();
		for(int query = 0; query != 4; ++query)
		{
			const auto view = branch->initialExportSubject();
			EXPECT_EQ(view.get(), nullptr);
			EXPECT_EQ(view.use_count(), 0);
		}
		EXPECT_EQ(live.use_count(), sourceOwners);
		const auto unit = branch->getForUpdate(braced->unitId());
		EXPECT_EQ(values(unit->getBonusesBeforeCreatureAbilitySuppression(Selector::all)), nativeValues);
	}
	const auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	const auto descendant = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	const auto held = descendant->getBonusesBeforeCreatureAbilitySuppression(Selector::all);
	const auto before = values(held);
	parent->addUnitBonus({addedAttack()});
	EXPECT_FALSE(child->initialExportSubject());
	EXPECT_NE(values(descendant->getBonusesBeforeCreatureAbilitySuppression(Selector::all)), before);
	EXPECT_EQ(values(held), before);
	EXPECT_EQ(braced->acquireState()->save(), nativeBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(StackWithBonusesQueryTest, ExportRouteViewsBindEachForkAndRetainOnlyTheirExistingSharedOwner)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto * unit : battle()->battleGetAllStacks(false))
		(void)unit->counterAttacks.total();
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto nativeBefore = braced->acquireState()->save();
	const auto nativeValues = values(braced->getBonusesBeforeCreatureAbilitySuppression(Selector::all));
	const auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	ASSERT_EQ(allKnowing->battleGetMySide(), BattleSide::ALL_KNOWING);
	const auto origin = InitialExportBattleSubject::capture(allKnowing, *battle());
	const auto parent = std::make_shared<HypotheticBattle>(environment.get(), origin);
	const auto parentView = parent->initialExportSubject();
	ASSERT_NE(parentView, nullptr);
	EXPECT_NE(parentView.get(), origin.get());
	EXPECT_EQ(parentView.use_count(), 2);
	{
		const auto copied = parentView;
		const auto repeated = parent->initialExportSubject();
		EXPECT_EQ(repeated.get(), copied.get());
		EXPECT_FALSE(repeated.owner_before(copied));
		EXPECT_FALSE(copied.owner_before(repeated));
		EXPECT_EQ(parentView.use_count(), 4);
	}
	EXPECT_EQ(parentView.use_count(), 2);
	const auto descendant = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto descendantView = descendant->initialExportSubject();
	ASSERT_NE(descendantView, nullptr);
	EXPECT_NE(descendantView.get(), parentView.get());
	auto last = std::make_shared<HypotheticBattle>(environment.get(), descendant);
	auto lastView = last->initialExportSubject();
	ASSERT_NE(lastView, nullptr);
	EXPECT_NE(lastView.get(), descendantView.get());
	EXPECT_NE(lastView.get(), parentView.get());
	for(const auto & branch : {parent, descendant, last})
	{
		const auto unit = branch->getForUpdate(braced->unitId());
		EXPECT_EQ(values(unit->getBonusesBeforeCreatureAbilitySuppression(Selector::all)), nativeValues);
	}
	const std::weak_ptr<InitialExportBattleSubject> retained = lastView;
	last.reset();
	EXPECT_FALSE(retained.expired());
	EXPECT_GT(lastView->world().size(), 0);
	lastView.reset();
	EXPECT_TRUE(retained.expired());
	EXPECT_EQ(braced->acquireState()->save(), nativeBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(StackWithBonusesQueryTest, RouteGetterTracksBaseSubjectRebindingAndRetiredOwners)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	const auto origin = InitialExportBattleSubject::capture(allKnowing, *battle());
	const auto branch = std::make_shared<HypotheticBattle>(environment.get(), origin);
	const std::weak_ptr<InitialExportBattleSubject> retired = branch->initialExportSubject();
	ASSERT_FALSE(retired.expired());
	const ReadOnlySubjectProxy ordinary(live);
	static_cast<BattleProxy &>(*branch) = ordinary;
	EXPECT_TRUE(retired.expired()); // A cached weak owner must not retain the old object.
	const bool ordinaryClassifiedAsExport = static_cast<bool>(branch->initialExportSubject());
	EXPECT_FALSE(ordinaryClassifiedAsExport); // Never print or dereference a potentially retired view.
	const auto first = InitialExportBattleSubject::capture(allKnowing, *battle());
	const ReadOnlySubjectProxy firstExport(first);
	static_cast<BattleProxy &>(*branch) = firstExport;
	const auto firstView = branch->initialExportSubject();
	EXPECT_EQ(firstView.get(), first.get());
	EXPECT_FALSE(firstView.owner_before(first));
	EXPECT_FALSE(first.owner_before(firstView));
	const auto second = InitialExportBattleSubject::capture(allKnowing, *battle());
	const ReadOnlySubjectProxy secondExport(second);
	static_cast<BattleProxy &>(*branch) = secondExport;
	EXPECT_EQ(branch->initialExportSubject().get(), second.get());
	const ReadOnlySubjectProxy empty(BattleProxy::Subject{});
	static_cast<BattleProxy &>(*branch) = empty;
	const bool nullClassifiedAsExport = static_cast<bool>(branch->initialExportSubject());
	EXPECT_FALSE(nullClassifiedAsExport);
	EXPECT_EQ(branch->initialExportSubject().use_count(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(StackWithBonusesQueryTest, AliasedBaseSubjectsPreservePointerAndControlBlockIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	const auto origin = InitialExportBattleSubject::capture(allKnowing, *battle());
	const auto exported = std::make_shared<HypotheticBattle>(environment.get(), origin);
	const auto view = exported->initialExportSubject();
	ASSERT_NE(view, nullptr);
	const std::shared_ptr<CBattleInfoCallback> aliasOrdinary(view, live.get());
	EXPECT_FALSE(aliasOrdinary.owner_before(view));
	EXPECT_FALSE(view.owner_before(aliasOrdinary));
	EXPECT_NE(aliasOrdinary.get(), static_cast<CBattleInfoCallback *>(view.get()));
	const auto ordinary = std::make_shared<HypotheticBattle>(environment.get(), aliasOrdinary);
	const ReadOnlySubjectProxy positive(view);
	static_cast<BattleProxy &>(*ordinary) = positive;
	EXPECT_EQ(ordinary->initialExportSubject().get(), view.get());
	const ReadOnlySubjectProxy negative(aliasOrdinary);
	static_cast<BattleProxy &>(*exported) = negative;
	const bool aliasClassifiedAsExport = static_cast<bool>(exported->initialExportSubject());
	EXPECT_FALSE(aliasClassifiedAsExport);
	const auto other = std::make_shared<HypotheticBattle>(environment.get(), origin);
	const auto otherView = other->initialExportSubject();
	const auto holder = std::make_shared<std::shared_ptr<InitialExportBattleSubject>>(otherView);
	const std::shared_ptr<CBattleInfoCallback> differentOwner(holder, otherView.get());
	EXPECT_EQ(differentOwner.get(), static_cast<CBattleInfoCallback *>(otherView.get()));
	EXPECT_TRUE(differentOwner.owner_before(otherView) || otherView.owner_before(differentOwner));
	const ReadOnlySubjectProxy rebound(differentOwner);
	static_cast<BattleProxy &>(*other) = rebound;
	const auto oldObjectOwners = otherView.use_count();
	const auto actual = other->initialExportSubject();
	EXPECT_EQ(actual.get(), otherView.get());
	EXPECT_FALSE(actual.owner_before(differentOwner));
	EXPECT_FALSE(differentOwner.owner_before(actual));
	EXPECT_EQ(otherView.use_count(), oldObjectOwners);
	const std::shared_ptr<CBattleInfoCallback> noOwner(std::shared_ptr<void>{}, live.get());
	const auto unowned = std::make_shared<HypotheticBattle>(environment.get(), noOwner);
	EXPECT_FALSE(unowned->initialExportSubject());
	const std::shared_ptr<CBattleInfoCallback> unownedExport(std::shared_ptr<void>{}, view.get());
	const ReadOnlySubjectProxy unownedPositive(unownedExport);
	static_cast<BattleProxy &>(*unowned) = unownedPositive;
	const auto unownedView = unowned->initialExportSubject();
	EXPECT_EQ(unownedView.get(), view.get());
	EXPECT_EQ(unownedView.use_count(), 0);
}

namespace
{
Bonus suppressionMarker(int level, int turns)
{
	Bonus result(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, level, BonusSourceID(SpellID(SpellID::FORGETFULNESS)),
		BonusSubtypeID(), BonusValueType::INDEPENDENT_MAX);
	result.turnsRemain = turns;
	result.statusTags = {BonusStatusTag::DEBUFF};
	return result;
}

// Capture the suppression owner's real query, not a guessed predicate/cache key.
class MarkerQueryProbe final : public StackWithBonuses
{
	std::shared_ptr<StackWithBonuses> target;
public:
	mutable TConstBonusListPtr canonical;
	mutable TConstBonusListPtr copied;
	mutable const CSelector * canonicalSelector = nullptr;

	MarkerQueryProbe(const HypotheticBattle * owner, std::shared_ptr<StackWithBonuses> target)
		: StackWithBonuses(owner, target.get()), target(std::move(target)) {}

	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(const CSelector & selector,
		const std::string & key = "", bool unstacked = false) const override
	{
		canonicalSelector = &selector; // Captured from the actual suppression owner call.
		canonical = target->getBonusesBeforeCreatureAbilitySuppression(selector, key, unstacked);
		const CSelector copy = selector;
		copied = target->getBonusesBeforeCreatureAbilitySuppression(copy, key, unstacked);
		EXPECT_EQ(values(canonical), values(copied));
		return canonical;
	}
};
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryPreservesUnrelatedUpdatesAndRefreshIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	target->addUnitBonus({suppressionMarker(1, 2)});
	target->updateUnitBonus({addedAttack()});
	MarkerQueryProbe probe(child.get(), target);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1);
	ASSERT_NE(probe.canonicalSelector, nullptr);
	const auto held = probe.canonical;
	const auto heldValues = values(held);
	target->updateUnitBonus({suppressionMarker(2, 5)});
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1)
		<< "A duration refresh must retain the existing marker value";
	ASSERT_EQ(probe.canonical->size(), 1u);
	EXPECT_EQ(probe.canonical->front()->turnsRemain, 5);
	EXPECT_EQ(values(held), heldValues);
	const CSelector longDuration([](const Bonus * bonus)
	{
		return bonus->type == BonusType::CREATURE_ABILITY_SUPPRESSION && bonus->turnsRemain >= 4;
	});
	const auto refreshed = target->getBonusesBeforeCreatureAbilitySuppression(longDuration,
		"newHorizonsCreatureAbilitySuppression.marker");
	ASSERT_EQ(refreshed->size(), 1u);
	EXPECT_EQ(refreshed->front()->val, 1);
	EXPECT_EQ(refreshed->front()->turnsRemain, 5);
	const auto attack = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK));
	ASSERT_FALSE(target->getBonusesBeforeCreatureAbilitySuppression(attack)->empty());
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryTracksNestedParentAndDoesNotTrustSameCacheKey)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	parent->updateUnitBonus({addedAttack()});
	const auto target = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	// Direct public edit exercises a live parent route without capturing empty spell effects.
	target->bonusesToUpdate.push_back(std::make_shared<Bonus>(addedAttack()));
	MarkerQueryProbe probe(child.get(), target);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 0);
	parent->addUnitBonus({suppressionMarker(2, 3)});
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 2);
	parent->removeUnitBonus(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION));
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 0);
	const auto alias = std::make_shared<Bonus>(suppressionMarker(1, 3));
	target->bonusesToAdd = {alias, alias};
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1);
	ASSERT_NE(probe.canonicalSelector, nullptr);
	const CSelector copied = *probe.canonicalSelector;
	const auto raw = target->getBonusesBeforeCreatureAbilitySuppression(*probe.canonicalSelector, {}, true);
	const auto rawLegacy = target->getBonusesBeforeCreatureAbilitySuppression(copied, {}, true);
	EXPECT_EQ(values(raw), values(rawLegacy));
	EXPECT_EQ(identities(raw), identities(rawLegacy));
	ASSERT_EQ(raw->size(), 2u);
	EXPECT_EQ(raw->front().get(), raw->back().get());
	EXPECT_EQ(raw->front().get(), alias.get());
	unsigned calls = 0;
	const CSelector throwing([&](const Bonus *) -> bool
	{
		++calls;
		throw std::runtime_error("opaque selector remains observable");
	});
	EXPECT_THROW(target->getBonusesBeforeCreatureAbilitySuppression(throwing,
		"newHorizonsCreatureAbilitySuppression.marker"), std::runtime_error);
	EXPECT_EQ(calls, 1u);
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryKeepsFormFrozenAndForeignFallbacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = std::make_shared<StackWithBonuses>(child.get(), braced);
	ForcedCopyView foreign(*child, *braced);
	for(const auto & view : {target, foreign.projected})
	{
		// A foreign raw query source cannot originate timed effects. Construct
		// every reader first, then expose the documented public query overlay.
		MarkerQueryProbe probe(child.get(), view);
		view->bonusesToAdd.push_back(std::make_shared<Bonus>(suppressionMarker(2, 3)));
		view->bonusesToUpdate.push_back(std::make_shared<Bonus>(addedAttack()));
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 2);
		view->beginBattleForm(creatureByName("core:pikeman"), 2);
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 2);
		view->endBattleForm();
		view->freezeDamageScoringBonusSnapshot();
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 2);
	}
	compare(*target, *foreign.projected, false);
	// Positive selected snapshot must survive after the inherited marker vanishes.
	const auto parent = std::make_shared<StackWithBonuses>(child.get(), braced);
	parent->addUnitBonus({suppressionMarker(2, 3)});
	const auto snapshotTarget = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	snapshotTarget->removeUnitBonus(Selector::type()(BonusType::LUCK));
	const auto snapshot = snapshotTarget->recipientOverlayView().stackedSnapshot;
	ASSERT_TRUE(snapshot);
	ASSERT_TRUE(std::ranges::any_of(*snapshot, [](const Bonus & bonus)
	{ return bonus.type == BonusType::CREATURE_ABILITY_SUPPRESSION && bonus.val == 2; }));
	ASSERT_TRUE(snapshotTarget->bonusesToAdd.empty());
	ASSERT_TRUE(snapshotTarget->bonusesToUpdate.empty());
	MarkerQueryProbe snapshotProbe(child.get(), snapshotTarget);
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(snapshotProbe), 2);
	ASSERT_NE(snapshotProbe.canonicalSelector, nullptr);
	parent->removeUnitBonus(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION));
	ASSERT_TRUE(parent->getBonusesBeforeCreatureAbilitySuppression(*snapshotProbe.canonicalSelector, {}, false)->empty());
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(snapshotProbe), 2);
	ASSERT_EQ(snapshotProbe.canonical->size(), 1u);
	EXPECT_EQ(snapshotProbe.canonical->front()->val, 2);
	EXPECT_EQ(values(snapshotProbe.canonical), values(snapshotProbe.copied));
}

namespace
{
// Legal foreign source with selector-dependent identities: values alone cannot
// justify bypassing the legacy all-query before pointer-based removals.
class MarkerIdentityBearer final : public IBonusBearer
{
	const battle::Unit & source;
public:
	std::shared_ptr<Bonus> allMarker = std::make_shared<Bonus>(suppressionMarker(2, 3));
	std::shared_ptr<Bonus> narrowMarker = std::make_shared<Bonus>(*allMarker);
	bool markerExposed = false;
	explicit MarkerIdentityBearer(const battle::Unit & source) : source(source) {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override
	{
		auto result = std::make_shared<BonusList>();
		const auto native = source.getBonusesBeforeCreatureAbilitySuppression(selector, key);
		for(const auto & bonus : *native)
			result->push_back(bonus);
		const auto unrelated = addedAttack();
		const auto & marker = selector(&unrelated) ? allMarker : narrowMarker;
		if(markerExposed && selector(marker.get()))
			result->push_back(marker);
		return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		return getAllBonuses(selector);
	}
	int32_t getTreeVersion() const override { return source.getTreeVersion(); }
};
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryRetainsForeignPointerRemovalThroughExactParent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	MarkerIdentityBearer foreign(*braced);
	ASSERT_NE(foreign.allMarker.get(), foreign.narrowMarker.get());
	ASSERT_EQ(foreign.allMarker->toJsonNode(), foreign.narrowMarker->toJsonNode());
	battle::CUnitStateDetached source(braced, &foreign);
	source.localInit(child.get());
	source = *braced;
	const auto parent = std::make_shared<StackWithBonuses>(child.get(), &source);
	ASSERT_FALSE(parent->recipientOverlayView().stackedSnapshot);
	ASSERT_FALSE(parent->recipientOverlayView().unstackedSnapshot);
	const auto target = std::make_shared<StackWithBonuses>(child.get(), parent.get());
	MarkerQueryProbe probe(child.get(), target);
	// Timed markers are query inputs here, not originating foreign-source ADDs.
	// Expose them only after all constructor-time provenance checks have finished.
	foreign.markerExposed = true;
	const auto update = std::make_shared<Bonus>(addedAttack());
	parent->bonusesToUpdate.push_back(update);
	target->bonusesToUpdate.push_back(update);
	target->bonusesToRemove.insert(foreign.allMarker);
	const int version = target->treeVersionLocal;
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 0);
	// Same-sized direct identity replacement without a version bump must remain live.
	target->bonusesToRemove.clear();
	target->bonusesToRemove.insert(foreign.narrowMarker);
	EXPECT_EQ(target->treeVersionLocal, version);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 2);
	target->bonusesToRemove.clear();
	target->bonusesToRemove.insert(foreign.allMarker);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 0);
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryRetainsTypedExportRouteAndBothRngs)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Originating spell effects belong to the native source before export capture.
	braced->addNewBonus(std::make_shared<Bonus>(suppressionMarker(1, 2)));
	const auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	const auto origin = InitialExportBattleSubject::capture(allKnowing, *battle());
	const auto branch = std::make_shared<HypotheticBattle>(environment.get(), origin);
	ASSERT_NE(branch->initialExportSubject(), nullptr);
	const auto target = branch->getForUpdate(braced->unitId());
	// This typed slice permits generic OTHER ADD, not spell creation or refresh UPDATE.
	target->addUnitBonus({addedAttack()});
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto nativeBefore = braced->acquireState()->save();
	const auto projectedBefore = target->save();
	const auto traffic = server.startedActions.size();
	MarkerQueryProbe probe(branch.get(), target);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(probe), 1);
	EXPECT_EQ(target->save(), projectedBefore);
	EXPECT_EQ(braced->acquireState()->save(), nativeBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	EXPECT_EQ(server.startedActions.size(), traffic);
}

TEST_F(StackWithBonusesQueryTest, CanonicalSuppressionQueryKeepsFrozenParentReadableAfterOwnerRetires)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	std::shared_ptr<StackWithBonuses> target;
	std::unique_ptr<MarkerQueryProbe> probe;
	std::weak_ptr<HypotheticBattle> retiredOwner;
	{
		const auto sourceOwner = std::make_shared<HypotheticBattle>(environment.get(), live);
		retiredOwner = sourceOwner;
		const auto parent = std::make_shared<StackWithBonuses>(sourceOwner.get(), braced);
		parent->addUnitBonus({suppressionMarker(2, 3)});
		parent->freezeDamageScoringBonusSnapshot();
		target = std::make_shared<StackWithBonuses>(child.get(), parent.get());
		// Keep the child un-frozen so its query must inspect the retained parent.
		target->bonusesToUpdate.push_back(std::make_shared<Bonus>(addedAttack()));
		probe = std::make_unique<MarkerQueryProbe>(child.get(), target);
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*probe), 2);
	}
	ASSERT_TRUE(retiredOwner.expired());
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*probe), 2);
	ASSERT_NE(probe->canonicalSelector, nullptr);
	EXPECT_EQ(values(probe->canonical), values(probe->copied));
}
