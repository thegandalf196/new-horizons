/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CStack.h"
#include "../../AI/BattleAI/OwnedStackQuerySource.h"
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Frame = battle::OwnedUnitBonusFrame;
class FixtureEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FixtureEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
struct WithoutLibrary
{
	GameLibrary * before = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = before; }
};
std::shared_ptr<Bonus> value(BonusType type, int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, amount, BonusSourceID());
}
class SeedInfo final : public battle::IUnitInfo
{
	const battle::OwnedUnitCreatureResolver & creatures;
public:
	explicit SeedInfo(const battle::OwnedUnitCreatureResolver & creatures) : creatures(creatures) {}
	int32_t unitBaseAmount() const override { return 3; }
	uint32_t unitId() const override { return 17; }
	BattleSide unitSide() const override { return BattleSide::ATTACKER; }
	PlayerColor unitOwner() const override { return PlayerColor(0); }
	SlotID unitSlot() const override { return SlotID(0); }
	const CCreature * unitType() const override { return creatures.creature(CreatureID(1)); }
	const battle::UnitCreatureResolver * unitCreatureResolver() const override { return &creatures; }
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override { return battle::UnitPreparationHealthBasis{30, 30}; }
};
class SeedBonuses final : public ProjectedBonusView
{
public:
	const battle::Unit * subject = nullptr;
	const battle::Unit * evaluatedUnit() const override { return subject; }
	void onBattleFormChanged(const battle::CUnitState &) const override {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		auto result = std::make_shared<BonusList>();
		for(const auto & record : {value(BonusType::STACK_HEALTH, 10), value(BonusType::STACKS_SPEED, 5)})
			if(selector(record.get())) result->push_back(record);
		return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
std::shared_ptr<Frame> fixtureFrame(const std::vector<std::shared_ptr<Bonus>> & extra = std::vector<std::shared_ptr<Bonus>>(), bool formed = false)
{
	Frame::Inputs input;
	input.identity = {3, 17, BattleSide::ATTACKER, PlayerColor(0), SlotID(0), CreatureID(1), {30, 30}};
	battle::OwnedUnitCreatureResolver::Creature creature;
	creature.id = CreatureID(1); creature.faction = FactionID(0); creature.level = 1; creature.key = "owned:unit";
	auto alternate = creature; alternate.id = CreatureID(2); alternate.key = "owned:alternate";
	input.creatures = std::make_shared<battle::OwnedUnitCreatureResolver>(std::vector{creature, alternate});
	OwnedRecipientBonusGraph::NodeInput root;
	root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 3; root.facts.stableStackLevel = 1;
	root.facts.acceptance.emplace();
	auto casts = value(BonusType::CASTS, 0); casts->updater = std::make_shared<TimesStackSizeUpdater>(0, 10000, 1, 1);
	root.local = {value(BonusType::STACK_HEALTH, 10), value(BonusType::STACKS_SPEED, 5), casts};
	root.local.insert(root.local.end(), extra.begin(), extra.end());
	input.nodes = {root};
	Frame::FormSources source; source.full.facts = root.facts; source.full.records = {0, 1, 2};
	for(size_t index = 3; index < root.local.size(); ++index) source.full.records.push_back(index);
	source.full.facts.creatureContext.emplace();
	source.full.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{CreatureID(1), 1, EAlignment::NEUTRAL};
	source.full.facts.creatureContext->faction = FactionID(0); source.full.facts.acceptance->metadata.slot = SlotID(0);
	source.native = source.full; source.native.records.clear(); input.forms.emplace(CreatureID(1), source);
	source.full.facts.creatureContext->creature->id = CreatureID(2);
	source.native = source.full; source.native.records.clear(); input.forms.emplace(CreatureID(2), source);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		input.environment.colors.at(side) = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		input.environment.inputs.at(side).emplace(); input.environment.inputs.at(side)->color = *input.environment.colors.at(side);
		input.environment.states.at(side).emplace();
	}
	input.environment.magicRules.emplace(); input.environment.skeletonUpgrades.emplace();
	input.peerInputs.emplace(); input.recipientPosition = 0; // Explicit pure one-unit domain, no player/capture grant.
	SeedInfo info(*input.creatures); SeedBonuses bonuses; battle::CUnitStateDetached seed(&info, &bonuses); bonuses.subject = &seed;
	seed.localInit(nullptr); seed.position = BattleHex(50);
	if(formed) seed.beginBattleForm(CreatureID(2), 2);
	input.state = seed.save();
	return Frame::fromOwnedInputs(std::move(input));
}
}
class OwnedStackQuerySourceTest : public HeroCommandFixture {};

TEST_F(OwnedStackQuerySourceTest, ExplicitOwnedBindingKeepsInitialHealthAndQueriesCurrentCountWithoutNativeProvider)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	FixtureEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto owner = std::make_shared<HypotheticBattle>(&environment, callback);
	auto captured = fixtureFrame(); const auto capturedBefore = captured->save();
	auto privateFrame = captured->fork();
	WithoutLibrary absent;
	auto source = std::make_shared<StackWithBonuses>(owner.get(), privateFrame);
	EXPECT_EQ(source->getAvailableHealth(), 30); EXPECT_EQ(source->getMaxHealth(), 10u);
	EXPECT_EQ(source->casts.available(), 3); EXPECT_EQ(source->getMovementRange(), 5);
	EXPECT_EQ(source->unitCreatureResolver(), privateFrame->unit().unitCreatureResolver());
	EXPECT_FALSE(source->isBonusCacheable());
	int64_t damage = 12; source->damage(damage);
	EXPECT_EQ(source->getAvailableHealth(), 18); EXPECT_EQ(source->casts.available(), 2);
	EXPECT_EQ(captured->save(), capturedBefore);
	OwnedStackQuerySource mapping(*owner, *source, privateFrame);
	EXPECT_THROW(mapping.unitHasAmmoCart(&captured->unit()), std::invalid_argument);
	EXPECT_EQ(captured->save(), capturedBefore);
}

TEST_F(OwnedStackQuerySourceTest, OwnedNestedUnitsAndFixedLoansSurviveParentChangesAndBranchRelease)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	FixtureEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto owner = std::make_shared<HypotheticBattle>(&environment, callback);
	auto captured = fixtureFrame();
	WithoutLibrary absent;
	auto parent = std::make_shared<StackWithBonuses>(owner.get(), captured->fork());
	int64_t damage = 12; parent->damage(damage);
	auto child = std::make_shared<StackWithBonuses>(owner.get(), static_cast<const battle::Unit *>(parent.get()));
	auto sibling = std::make_shared<StackWithBonuses>(owner.get(), static_cast<const battle::Unit *>(parent.get()));
	const auto siblingBefore = sibling->save();
	damage = 10; child->damage(damage);
	EXPECT_EQ(child->getAvailableHealth(), 8); EXPECT_EQ(child->casts.available(), 1);
	EXPECT_EQ(parent->getAvailableHealth(), 18); EXPECT_EQ(parent->casts.available(), 2);
	EXPECT_EQ(sibling->save(), siblingBefore); EXPECT_EQ(sibling->casts.available(), 2);
	auto retained = child->acquireState(); auto retainedUnit = child->acquire();
	child.reset(); parent.reset(); sibling.reset(); owner.reset(); callback.reset(); captured.reset();
	EXPECT_EQ(retained->getAvailableHealth(), 8); EXPECT_EQ(retained->getMaxHealth(), 10u);
	EXPECT_EQ(retained->casts.available(), 1); EXPECT_EQ(retained->getMovementRange(), 5);
	EXPECT_EQ(retainedUnit->getAvailableHealth(), 8); EXPECT_EQ(retainedUnit->getMovementRange(), 5);
}

TEST_F(OwnedStackQuerySourceTest, NestedOwnedDescriptorsRestoreOriginalFormWithoutNativeLookup)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	FixtureEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto owner = std::make_shared<HypotheticBattle>(&environment, callback);
	auto captured = fixtureFrame({}, true);
	WithoutLibrary absent;
	auto parent = std::make_shared<StackWithBonuses>(owner.get(), captured->fork());
	auto stateChild = std::make_shared<StackWithBonuses>(owner.get(), static_cast<const battle::CUnitState *>(parent.get()));
	auto unitChild = std::make_shared<StackWithBonuses>(owner.get(), static_cast<const battle::Unit *>(parent.get()));
	EXPECT_EQ(stateChild->unitType()->getId(), CreatureID(2));
	EXPECT_EQ(unitChild->unitType()->getId(), CreatureID(2));
	auto restored = captured->scratch();
	restored.unit().endBattleForm();
	// Descriptor selection after canonical scalar restoration only; this does
	// not grant the separate current-form source-domain transition capability.
	static_cast<battle::CUnitState &>(*stateChild) = restored.unit();
	static_cast<battle::CUnitState &>(*unitChild) = restored.unit();
	EXPECT_FALSE(stateChild->hasBattleForm()); EXPECT_FALSE(unitChild->hasBattleForm());
	EXPECT_EQ(stateChild->unitType()->getId(), CreatureID(1));
	EXPECT_EQ(unitChild->unitType()->getId(), CreatureID(1));
	EXPECT_EQ(stateChild->battleFormOriginalCreature(), CreatureID(1));
	EXPECT_EQ(unitChild->battleFormOriginalCreature(), CreatureID(1));
}
