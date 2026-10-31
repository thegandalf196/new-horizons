/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Frame = battle::OwnedUnitBonusFrame;
using Resolver = battle::OwnedUnitCreatureResolver;
using Graph = OwnedRecipientBonusGraph;
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
Resolver::Creature species(int id)
{
	Resolver::Creature result;
	result.id = CreatureID(id); result.faction = FactionID(0); result.level = id;
	result.key = "owned:species" + std::to_string(id); return result;
}
class SeedInfo final : public battle::IUnitInfo
{
	const Resolver & types;
public:
	explicit SeedInfo(const Resolver & types) : types(types) {}
	int32_t unitBaseAmount() const override { return 3; }
	uint32_t unitId() const override { return 17; }
	BattleSide unitSide() const override { return BattleSide::ATTACKER; }
	PlayerColor unitOwner() const override { return PlayerColor(0); }
	SlotID unitSlot() const override { return SlotID(0); }
	const CCreature * unitType() const override { return types.creature(CreatureID(1)); }
	const battle::UnitCreatureResolver * unitCreatureResolver() const override { return &types; }
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override
	{
		return battle::UnitPreparationHealthBasis{30, 30};
	}
};
std::shared_ptr<Bonus> value(BonusType type, int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, amount, BonusSourceID());
}
class SeedView final : public ProjectedBonusView
{
public:
	const battle::Unit * subject = nullptr;
	const battle::Unit * evaluatedUnit() const override { return subject; }
	void onBattleFormChanged(const battle::CUnitState &) const override {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		BonusList raw; raw.push_back(value(BonusType::STACK_HEALTH, subject->creatureId() == CreatureID(2) ? 1 : 10));
		raw.push_back(value(BonusType::STACKS_SPEED, 5));
		auto result = std::make_shared<BonusList>(); raw.getBonuses(*result, selector); return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
TLimiterPtr noLuck()
{
	return std::make_shared<NoneOfLimiter>(std::vector<TLimiterPtr>{std::make_shared<HasAnotherBonusLimiter>(BonusType::LUCK)});
}
Frame::Inputs inputs(bool form = false, bool clone = false)
{
	Frame::Inputs input;
	input.identity = {3, 17, BattleSide::ATTACKER, PlayerColor(0), SlotID(0), CreatureID(1), {30, 30}};
	input.creatures = std::make_shared<Resolver>(std::vector<Resolver::Creature>{species(1), species(2)});
	Graph::NodeInput root, original, alternate;
	root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 3; root.facts.stableStackLevel = 1;
	root.facts.acceptance.emplace(); root.parents = {1, 2};
	auto marker = value(BonusType::LUCK, 1); marker->source = BonusSource::SPELL_EFFECT;
	marker->sid = BonusSourceID(SpellID(SpellID::FORTUNE)); marker->duration = BonusDuration::N_TURNS; marker->turnsRemain = 3;
	auto casts = value(BonusType::CASTS, 0); casts->updater = std::make_shared<TimesStackSizeUpdater>(0, 10000, 1, 1); casts->limiter = noLuck();
	auto mdr = value(BonusType::SPELL_DAMAGE_REDUCTION, 20); mdr->limiter = noLuck();
	root.local = {marker, casts, mdr, value(BonusType::STACKS_SPEED, 5)};
	original.facts.nodeType = alternate.facts.nodeType = BonusNodeType::CREATURE;
	auto hp = value(BonusType::STACK_HEALTH, 10); hp->source = BonusSource::CREATURE_ABILITY; hp->sid = BonusSourceID(CreatureID(1)); hp->updater = std::make_shared<TimesStackSizeUpdater>(10, 10, 1, 1); original.local = {hp};
	hp = value(BonusType::STACK_HEALTH, 1); hp->source = BonusSource::CREATURE_ABILITY; hp->sid = BonusSourceID(CreatureID(2)); hp->updater = std::make_shared<TimesStackSizeUpdater>(1, 1, 1, 1); alternate.local = {hp};
	input.nodes = {root, original, alternate}; input.extraRecords = {marker}; input.sourceLocalSpells = {6};
	for(int id : {1, 2})
	{
		Frame::FormSources source;
		source.full.facts = root.facts; source.full.records = {0, 1, 2, 3};
		source.full.parents = id == 1 ? std::vector<size_t>{1} : std::vector<size_t>{1, 2};
		source.full.facts.creatureContext.emplace();
		source.full.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{CreatureID(id), id, EAlignment::NEUTRAL};
		source.full.facts.creatureContext->faction = FactionID(0);
		source.full.facts.acceptance->metadata.slot = SlotID(0);
		source.native = source.full; source.native.records.clear(); input.forms.emplace(CreatureID(id), source);
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		input.environment.colors.at(side) = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		input.environment.inputs.at(side).emplace(); input.environment.inputs.at(side)->color = *input.environment.colors.at(side);
		input.environment.states.at(side).emplace();
	}
	input.environment.magicRules.emplace(); input.environment.skeletonUpgrades.emplace();
	input.environmentUnits = {nullptr}; input.recipientPosition = 0; // Explicit complete one-unit fixture roster.
	SeedInfo info(*input.creatures); SeedView view; battle::CUnitStateDetached seed(&info, &view); view.subject = &seed;
	seed.localInit(nullptr); seed.position = BattleHex(50); if(form) seed.beginBattleForm(CreatureID(2), 2); seed.cloned = clone;
	input.state = seed.save(); return input;
}
}
TEST(OwnedUnitBonusFrameTest, RawConsumptionReevaluatesConditionalCastsMdrAndSourceLocalSpellProvenance)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	EXPECT_EQ(frame->unit().casts.available(), 0);
	EXPECT_EQ(frame->unit().valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 0);
	const auto * source = dynamic_cast<const ProjectedBonusView *>(frame->unit().getBonusBearer());
	ASSERT_NE(source, nullptr); EXPECT_EQ(source->sourceLocalSpellEffects().size(), 1u);
	frame->removeRecords({0});
	EXPECT_EQ(frame->unit().casts.available(), 3);
	EXPECT_EQ(frame->unit().valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 20);
	EXPECT_TRUE(source->sourceLocalSpellEffects().empty());
	auto forked = frame->fork(); auto repeated = forked->fork();
	EXPECT_EQ(repeated->unit().casts.available(), 3);
	EXPECT_EQ(repeated->unit().getMaxHealth(), 10u); // Typed Spell/Creature payloads are shared owned inputs, never re-encoded.
}
TEST(OwnedUnitBonusFrameTest, MaterializedScratchKeepsFixedScalarAndLedgerAcrossParentChanges)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	auto original = frame->scratch();
	frame->removeRecords({0}); auto consumed = frame->scratch();
	EXPECT_EQ(original.unit().casts.available(), 0);
	EXPECT_EQ(consumed.unit().casts.available(), 3);
	int64_t amount = 10; consumed.unit().damage(amount);
	EXPECT_EQ(consumed.unit().getCount(), 2);
	EXPECT_EQ(consumed.unit().casts.available(), 3); // Fixed PRE-INJURY bonus provider, not damaged count2.
	frame->load(consumed.unit().save());
	EXPECT_EQ(frame->unit().casts.available(), 2); // Current committed scalar frame remains distinct.
	EXPECT_EQ(consumed.unit().casts.available(), 3);
	EXPECT_EQ(original.unit().casts.available(), 0);
}
TEST(OwnedUnitBonusFrameTest, FormedCloneTerminalRestoreUsesOwnedNativeSourceWithoutLibrary)
{
	auto frame = Frame::fromOwnedInputs(inputs(true, true));
	WithoutLibrary absent;
	frame->removeRecords({0});
	ASSERT_EQ(frame->unit().getCount(), 30); ASSERT_EQ(frame->unit().getMaxHealth(), 1u);
	auto scratch = frame->scratch(); int64_t amount = 1; scratch.unit().damage(amount);
	EXPECT_EQ(scratch.unit().getCount(), 0);
	EXPECT_FALSE(scratch.unit().hasBattleForm());
	EXPECT_EQ(scratch.unit().unitType()->getId(), CreatureID(1));
	EXPECT_EQ(scratch.unit().getMaxHealth(), 10u); // Different source form; no temporary native CStack fallback.
	EXPECT_EQ(scratch.unit().casts.available(), 30); // Generic pre-injury scalar contributions stay fixed.
	EXPECT_EQ(scratch.unit().getCasterNameTextID(), "");
	EXPECT_NO_THROW(scratch.unit().save());
	EXPECT_EQ(frame->unit().getCount(), 30); EXPECT_TRUE(frame->unit().hasBattleForm());
}
TEST(OwnedUnitBonusFrameTest, NestedLoansRetainExactAncestryAndRejectSiblingFormQueryBinding)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	frame->removeRecords({0});
	auto nested = [&]() { auto first = frame->scratch(); int64_t amount = 10; first.unit().damage(amount); return first.nested(); }();
	auto sibling = frame->fork();
	EXPECT_THROW(sibling->unit().ownedNativeFormBonuses(nested.unit(), Selector::all, true), std::logic_error);
	frame.reset(); sibling.reset();
	EXPECT_EQ(nested.unit().getCount(), 2); EXPECT_EQ(nested.unit().casts.available(), 3);
	EXPECT_EQ(nested.unit().getMaxHealth(), 10u);
}
TEST(OwnedUnitBonusFrameTest, InvalidRemovalBatchAndMissingFormKeepParentStateUnchanged)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	auto missing = inputs(); missing.recipientPosition = 1;
	EXPECT_THROW(Frame::fromOwnedInputs(std::move(missing)), std::invalid_argument);
	WithoutLibrary absent;
	const auto before = frame->save();
	EXPECT_THROW(frame->removeRecords({0, 999}), std::out_of_range);
	EXPECT_EQ(frame->unit().casts.available(), 0); EXPECT_EQ(frame->save(), before);
	auto invalid = before; invalid["state"]["battleFormCreature"].String() = "unknown";
	EXPECT_THROW(frame->load(invalid), std::logic_error);
	EXPECT_EQ(frame->save(), before);
}
TEST(OwnedUnitBonusFrameTest, NestedLoanDescriptorAndHealthRemainIsolatedAcrossParentFormChanges)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	frame->removeRecords({0});
	auto parent = frame->scratch(); auto ordinaryChild = parent.nested();
	parent.unit().beginBattleForm(CreatureID(2), 1);
	EXPECT_EQ(ordinaryChild.unit().creatureId(), CreatureID(1));
	EXPECT_EQ(ordinaryChild.unit().getCount(), 3);
	EXPECT_EQ(ordinaryChild.unit().getMaxHealth(), 10u);
	auto formedChild = parent.nested();
	ASSERT_EQ(formedChild.unit().creatureId(), CreatureID(2));
	ASSERT_EQ(formedChild.unit().getCount(), 30);
	parent.unit().endBattleForm();
	EXPECT_EQ(formedChild.unit().creatureId(), CreatureID(2));
	EXPECT_EQ(formedChild.unit().getCount(), 30);
	EXPECT_EQ(formedChild.unit().getMaxHealth(), 1u);
	EXPECT_EQ(formedChild.unit().casts.available(), 3);
	EXPECT_EQ(ordinaryChild.unit().getMaxHealth(), 10u);
}
TEST(OwnedUnitBonusFrameTest, SameFormNativeUpdaterRunsCanonicalStagesWithoutDoubleAppend)
{
	auto frame = Frame::fromOwnedInputs(inputs(true));
	WithoutLibrary absent;
	auto scratch = frame->scratch();
	const auto unstacked = scratch.unit().getUnstackedBonuses(Selector::type()(BonusType::STACK_HEALTH));
	ASSERT_EQ(unstacked->size(), 1u); // Each update produces a distinct clone; pointer dedup cannot hide a second evaluation.
	EXPECT_EQ(unstacked->front()->val, 1);
	EXPECT_EQ(scratch.unit().getMaxHealth(), 1u);
}
TEST(OwnedUnitBonusFrameTest, MixedInfoAndForeignBonusProviderCannotBorrowOwnedNativeSource)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	auto sibling = frame->fork();
	battle::CUnitStateDetached mixedView(&frame->unit(), sibling->unit().getBonusBearer());
	EXPECT_THROW(frame->unit().ownedNativeFormBonuses(mixedView, Selector::all, true), std::logic_error);
	battle::CUnitStateDetached mixedUnit(&frame->unit(), &sibling->unit());
	EXPECT_THROW(frame->unit().ownedNativeFormBonuses(mixedUnit, Selector::all, true), std::logic_error);
}

TEST(OwnedUnitBonusFrameTest, DerivedScratchUsesRetainedEnvironmentWithoutAdmittingForeignOccurrences)
{
	auto frame = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	frame->removeRecords({0});
	auto scratch = frame->scratch(); auto child = scratch.nested();
	EXPECT_EQ(scratch.unit().getCasterOwner(), PlayerColor(0));
	EXPECT_EQ(scratch.unit().magicResistance(), 0);
	EXPECT_EQ(child.unit().magicResistance(), 0);
	EXPECT_EQ(scratch.unit().getMovementRange(0), frame->unit().getMovementRange(0));
	EXPECT_EQ(scratch.unit().getInitiative(0), frame->unit().getInitiative(0));
	EXPECT_NO_THROW(scratch.unit().necromancyCombatPassives());
	int64_t amount = 10; scratch.unit().damage(amount); // Veteran/pain validate the derived subject even with zero policies.
	EXPECT_EQ(scratch.unit().getCount(), 2);
	EXPECT_EQ(child.unit().getCount(), 3);
	frame.reset();
	EXPECT_EQ(child.unit().magicResistance(), 0);
	EXPECT_EQ(child.unit().casts.available(), 3);
}

TEST(OwnedUnitBonusFrameTest, CopiedLegacyMoraleLimitsPreserveZeroWithoutLibraryFallback)
{
	auto values = inputs(); values.environment.legacyMoraleLimits = std::pair<int32_t, int32_t>{-2, 4};
	auto frame = Frame::fromOwnedInputs(std::move(values));
	values = inputs(); values.environment.legacyMoraleLimits = std::pair<int32_t, int32_t>{0, 0};
	auto zero = Frame::fromOwnedInputs(std::move(values));
	WithoutLibrary absent;
	auto scratch = frame->scratch(); auto child = scratch.nested();
	EXPECT_EQ(frame->unit().moraleValWithBonus(10), 4);
	EXPECT_EQ(scratch.unit().moraleValWithBonus(-10), -2);
	EXPECT_EQ(child.unit().moraleValWithBonus(10), 4);
	EXPECT_EQ(zero->unit().moraleValWithBonus(10), 0);
	EXPECT_EQ(zero->unit().moraleValWithBonus(-10), 0);
}
TEST(OwnedUnitBonusFrameTest, UnknownOrInvalidLegacyMoraleLimitsRefuseOnlyWhenNeeded)
{
	auto unknown = Frame::fromOwnedInputs(inputs());
	auto values = inputs(); values.environment.legacyMoraleLimits = std::pair<int32_t, int32_t>{1, 3};
	auto invalid = Frame::fromOwnedInputs(std::move(values));
	WithoutLibrary absent;
	EXPECT_THROW(unknown->unit().moraleValWithBonus(1), std::logic_error);
	EXPECT_THROW(invalid->unit().moraleValWithBonus(1), std::invalid_argument);
	EXPECT_EQ(invalid->unit().casts.available(), 0); // Unrelated canonical queries retain their old admission/order.
}

TEST(OwnedUnitBonusFrameTest, OrdinarySnapshotWithoutFormFieldsReplacesFormedFixedProviderNativeSource)
{
	auto formed = Frame::fromOwnedInputs(inputs(true));
	auto ordinary = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	auto scratch = formed->scratch(); auto snapshot = ordinary->save();
	auto & state = snapshot["state"].Struct();
	for(auto it = state.begin(); it != state.end();)
		if(it->first.starts_with("battleForm")) it = state.erase(it); else ++it;
	scratch.unit().load(snapshot);
	EXPECT_FALSE(scratch.unit().hasBattleForm());
	EXPECT_EQ(scratch.unit().creatureId(), CreatureID(1));
	EXPECT_EQ(scratch.unit().getMaxHealth(), 10u);
	EXPECT_EQ(scratch.unit().getCount(), 3);
	const auto health = scratch.unit().getUnstackedBonuses(Selector::type()(BonusType::STACK_HEALTH));
	ASSERT_EQ(health->size(), 1u); EXPECT_EQ(health->front()->val, 10);
	EXPECT_NO_THROW(scratch.unit().save());
	EXPECT_EQ(formed->unit().creatureId(), CreatureID(2));
	EXPECT_EQ(formed->unit().getMaxHealth(), 1u);
}
TEST(OwnedUnitBonusFrameTest, PositionLimiterUsesFixedFrameUntilCurrentStateIsCommitted)
{
	auto values = inputs();
	auto limited = value(BonusType::STACKS_SPEED, 7);
	BattleHexArray hexes; hexes.insert(BattleHex(50));
	limited->limiter = std::make_shared<UnitOnHexLimiter>(hexes);
	// Append raw payload as an explicit recipient-local extra; do not alter any inherited source stage.
	values.extraRecords.push_back(limited);
	for(auto & [id, form] : values.forms) form.full.records.push_back(7);
	auto unbound = Graph::fromOwnedInputs(values.nodes, 0, values.extraRecords);
	const auto unboundQuery = values.forms.at(CreatureID(1)).full;
	auto frame = Frame::fromOwnedInputs(std::move(values));
	WithoutLibrary absent;
	EXPECT_THROW(unbound->queryRoot(Selector::all, false, unboundQuery), std::logic_error);
	EXPECT_EQ(frame->unit().valOfBonuses(BonusType::STACKS_SPEED), 12);
	auto scratch = frame->scratch(); scratch.unit().setPosition(BattleHex(51));
	EXPECT_EQ(scratch.unit().valOfBonuses(BonusType::STACKS_SPEED), 12); // Pre-injury provider retains hex50.
	frame->load(scratch.unit().save());
	EXPECT_EQ(frame->unit().valOfBonuses(BonusType::STACKS_SPEED), 5);
	EXPECT_EQ(scratch.unit().valOfBonuses(BonusType::STACKS_SPEED), 12);
}


TEST(OwnedUnitBonusFrameTest, AdjacencyUsesOwnedCurrentOccurrencesAndLazyCanonicalTraits)
{
	auto peerInputs = inputs(); peerInputs.identity.id = 18;
	auto peer = Frame::fromOwnedInputs(std::move(peerInputs));
	{
		WithoutLibrary absent;
		auto position = peer->scratch(); position.unit().setPosition(BattleHex(94)); peer->load(position.unit().save());
	}
	auto values = inputs(); values.state["state"]["position"].Integer() = 93; values.environmentUnits.push_back(std::shared_ptr<const battle::Unit>(peer, &peer->unit()));
	auto aura = value(BonusType::STACKS_SPEED, 7);
	auto adjacent = std::make_shared<UnitAdjacentLimiter>(); adjacent->targetUnit = CreatureID(77);
	adjacent->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	adjacent->filter.nativeTrait = AdjacentUnitFilter::NativeTrait::LIVING; adjacent->filter.includeUpgrades = true;
	aura->limiter = adjacent; values.extraRecords.push_back(aura);
	for(auto & [id, form] : values.forms)
	{
		form.full.records.push_back(7); form.full.facts.adjacencyUnits.emplace(); form.full.facts.adjacencyTraits.emplace();
		form.full.facts.adjacencyTraits->emplace(CreatureID(1), OwnedBonusEvaluationFacts::AdjacentCreature{false, true, {{CreatureID(77), true}}});
	}
	auto captured = Frame::fromOwnedInputs(values);
	for(auto & [id, form] : values.forms) form.full.facts.adjacencyTraits.reset();
	auto unknown = Frame::fromOwnedInputs(values);
	// Raw authoring changes precede the final capture; copied old contexts remain independent.
	adjacent->filter = {}; adjacent->targetUnit = CreatureID(1);
	auto historical = Frame::fromOwnedInputs(std::move(values));
	WithoutLibrary absent;
	auto scratch = captured->scratch();
	ASSERT_TRUE(captured->unit().getSurroundingHexes().contains(peer->unit().getPosition()));
	EXPECT_EQ(captured->unit().valOfBonuses(BonusType::STACKS_SPEED), 12);
	EXPECT_EQ(scratch.unit().valOfBonuses(BonusType::STACKS_SPEED), 12);
	EXPECT_THROW(unknown->unit().valOfBonuses(BonusType::STACKS_SPEED), std::logic_error);
	EXPECT_EQ(historical->unit().valOfBonuses(BonusType::STACKS_SPEED), 12);
	EXPECT_EQ(captured->unit().valOfBonuses(BonusType::STACKS_SPEED), 12); // Deep payloads unaffected by input mutation.
}
TEST(OwnedUnitBonusFrameTest, CopiedLuckLimitsUseCanonicalClampAndRetainedScratchEnvironmentWithoutLibrary)
{
	for(const int amount : {10, -10})
	{
		auto values = inputs(); values.environment.legacyLuckLimits = std::pair<int32_t, int32_t>{-2, 4};
		values.extraRecords.push_back(value(BonusType::LUCK, amount));
		for(auto & [id, form] : values.forms) form.full.records.push_back(7);
		auto frame = Frame::fromOwnedInputs(std::move(values));
		WithoutLibrary absent;
		auto scratch = frame->scratch(); auto child = scratch.nested();
		const int expected = amount > 0 ? 4 : -2;
		EXPECT_EQ(frame->unit().luckVal(), expected); EXPECT_EQ(scratch.unit().luckVal(), expected);
		frame.reset(); EXPECT_EQ(child.unit().luckVal(), expected);
	}
	auto values = inputs(); values.environment.legacyLuckLimits = std::pair<int32_t, int32_t>{0, 0};
	auto zero = Frame::fromOwnedInputs(std::move(values));
	auto unknown = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	EXPECT_EQ(zero->unit().luckVal(), 0);
	EXPECT_THROW(unknown->unit().luckVal(), std::logic_error);
}

TEST(OwnedUnitBonusFrameTest, PositiveNecromancyLuckUsesCopiedSourceOnlyAtExplanationStage)
{
	const SecondarySkill necromancy(SecondarySkill::decode("new-horizons:necromancy"));
	ASSERT_TRUE(necromancy.hasValue());
	auto values = inputs(); values.environment.legacyLuckLimits = std::pair<int32_t, int32_t>{-4, 4};
	values.environment.skeletonUpgrades = std::set<CreatureID>{CreatureID(1)};
	values.environment.inputs.at(BattleSide::ATTACKER)->necromancy.luckBonus = 2;
	values.environment.necromancyLuckSource = necromancy;
	auto frame = Frame::fromOwnedInputs(values); TConstBonusListPtr explanation;
	values.environment.necromancyLuckSource.reset();
	auto unknown = Frame::fromOwnedInputs(std::move(values));
	WithoutLibrary absent;
	EXPECT_EQ(frame->unit().luckValAndBonusList(explanation), 3);
	ASSERT_FALSE(explanation->empty()); EXPECT_EQ(explanation->back()->sid, BonusSourceID(necromancy));
	EXPECT_NO_THROW(unknown->unit().getMovementRange(0));
	EXPECT_NO_THROW(unknown->unit().magicResistance());
	EXPECT_THROW(unknown->unit().luckVal(), std::logic_error);
}
TEST(OwnedUnitBonusFrameTest, CanonicalValueCopyKeepsExactPrivateBindingsAcrossForkAndLoanRetirement)
{
	auto parent = Frame::fromOwnedInputs(inputs());
	WithoutLibrary absent;
	parent->removeRecords({0});
	auto first = parent->fork(); auto second = first->fork();
	for(const auto & frame : {parent, first, second})
	{
		const auto * view = dynamic_cast<const ProjectedBonusView *>(frame->unit().getBonusBearer());
		ASSERT_NE(view, nullptr);
		EXPECT_EQ(view->evaluatedUnit(), &frame->unit());
	}
	parent.reset(); first.reset();
	EXPECT_EQ(second->unit().casts.available(), 3);
	auto scratch = second->scratch(); auto nested = scratch.nested();
	second.reset();
	int64_t damage = 10; scratch.unit().damage(damage);
	EXPECT_EQ(scratch.unit().getCount(), 2);
	EXPECT_EQ(nested.unit().getCount(), 3);
	EXPECT_EQ(scratch.unit().casts.available(), 3);
	EXPECT_EQ(nested.unit().casts.available(), 3);
	EXPECT_EQ(nested.unit().getMaxHealth(), 10u);
}
