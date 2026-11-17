/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../AI/BattleAI/ForecastEffectTransitions.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Updaters.h"
#include <array>

namespace
{
using namespace forecastEffects;
constexpr SpellID slow{SpellID::SLOW};
std::shared_ptr<Bonus> timed(BonusType type = BonusType::STACKS_SPEED, int turns = 2)
{
	auto value = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
		-20, BonusSourceID(slow));
	value->turnsRemain = turns;
	return value;
}
RawRecord raw(std::shared_ptr<const Bonus> value, uint64_t occurrence = 0, bool transient = true)
{
	return {value, {77, occurrence}, Bonus::NTurns(value.get()), transient};
}
std::shared_ptr<const State> capture(std::initializer_list<RawRecord> records, Admission admission = Admission())
{
	return State::capture(std::span(records.begin(), records.size()), admission);
}
class ForeignUpdater final : public IUpdater {};
}

TEST(ForecastEffectStateTest, NativeRoundClockOpeningOrdinaryAndFinalExpiryAgree)
{
	auto source = timed();
	CBonusSystemNode native(BonusNodeType::STACK_BATTLE);
	native.addNewBonus(source);
	auto state = capture({raw(source)});
	const auto opening = Transitions::prepareRound(state, true, 1);
	ASSERT_TRUE(opening);
	EXPECT_EQ(opening.receipt->after->find(0)->value->turnsRemain, source->turnsRemain);
	EXPECT_TRUE(opening.receipt->changes.empty());
	state = opening.receipt->after;
	for(int round = 2; round <= 3; ++round)
	{
		native.reduceBonusDurations(CSelector(Bonus::NTurns));
		const auto next = Transitions::prepareRound(state, false, round);
		ASSERT_TRUE(next);
		const auto nativeValues = native.getExportedBonusList();
		EXPECT_EQ(next.receipt->after->values().size(), nativeValues.size());
		if(!nativeValues.empty()) EXPECT_EQ(next.receipt->after->find(0)->value->turnsRemain, nativeValues.front()->turnsRemain);
		state = next.receipt->after;
	}
	EXPECT_TRUE(state->values().empty());
	EXPECT_EQ(state->round(), 3); // final forecast iteration still advances clocks
}

TEST(ForecastEffectStateTest, OwnedSourceSiblingRevisionAndAliasIsolation)
{
	auto source = timed();
	const auto state = capture({raw(source), raw(source)});
	const auto sibling = state;
	ASSERT_EQ(state->values().size(), 1u); // same native occurrence ages once
	source->turnsRemain = 100;
	source->val = 999;
	const auto first = Transitions::prepareRound(state, true, 1);
	ASSERT_TRUE(first);
	const auto next = Transitions::prepareRound(first.receipt->after, false, 2);
	ASSERT_TRUE(next);
	EXPECT_EQ(next.receipt->after->find(0)->value->turnsRemain, 1);
	EXPECT_EQ(sibling->find(0)->value->turnsRemain, 2);
	EXPECT_EQ(sibling->find(0)->value->val, -20);
	EXPECT_TRUE(next.receipt->matches(first.receipt->after));
	EXPECT_FALSE(next.receipt->matches(sibling));
	EXPECT_THROW(capture({raw(source), raw(source, 1)}), std::invalid_argument);
}

TEST(ForecastEffectStateTest, FrozenExtraActivationCannotConsumeNormalSlotOrThaw)
{
	Admission admitted; admitted.round = 2; admitted.frozenApplicationRound = 2;
	auto marker = std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(1)), 2));
	const auto state = capture({raw(marker, 0, false)}, admitted);
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION,
		BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST})
	{
		Activation input; input.reason = reason; input.serial = 10;
		const auto next = Transitions::prepareActivation(state, input);
		ASSERT_TRUE(next);
		EXPECT_TRUE(next.receipt->after->frozen());
		EXPECT_TRUE(next.receipt->blockedExtraActivation);
		EXPECT_FALSE(next.receipt->consumedNormalSlot);
	}
	Activation normal; normal.normalSlotForfeit = true; normal.deferFrozenThaw = true; normal.serial = 11;
	const auto pending = Transitions::prepareActivation(state, normal);
	ASSERT_TRUE(pending);
	EXPECT_TRUE(pending.receipt->after->frozen());
	EXPECT_TRUE(pending.receipt->consumedNormalSlot);
	const auto complete = Transitions::prepareActivationEnd(pending.receipt->after, 11, true, false);
	ASSERT_TRUE(complete);
	EXPECT_FALSE(complete.receipt->after->frozen());
	EXPECT_EQ(complete.receipt->after->frozenApplicationRound(), 2);
	EXPECT_EQ(Transitions::prepareActivationEnd(complete.receipt->after, 11, true, false).refusal, Refusal::Stale);
	const auto died = Transitions::prepareActivationEnd(pending.receipt->after, 11, false, true);
	ASSERT_TRUE(died);
	EXPECT_TRUE(died.receipt->after->frozen()); // native thaw checks alive/non-ghost AFTER ticks
}

TEST(ForecastEffectStateTest, ActivationReasonsMatchNativePredicatesAndExpireOnce)
{
	auto value = timed(); value->duration = BonusDuration::STACK_GETS_TURN;
	const auto state = capture({raw(value)});
	for(const auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE,
		BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::HERO_COMMAND,
		BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::CRISIS_RESUME})
	{
		Activation input; input.reason = reason; input.serial = 4;
		const auto next = Transitions::prepareActivation(state, input);
		ASSERT_TRUE(next);
		const bool original = !battle::timedEffects::activationContinuation(reason)
			&& reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND;
		EXPECT_EQ(next.receipt->after->values().empty(), original);
	}
}

TEST(ForecastEffectStateTest, EntangleOnlyBreaksOnActualOrdinaryMovementAndRecallPreservesRoots)
{
	Admission admitted; admitted.entangle = SpellID(200); admitted.forceLance = SpellID(201);
	auto entangle = timed(BonusType::BIND_EFFECT); entangle->sid = BonusSourceID(*admitted.entangle);
	auto classic = timed(BonusType::BIND_EFFECT); classic->parameters = std::make_shared<BonusParameters>(int32_t(17));
	auto lance = timed(BonusType::BIND_EFFECT, 1); lance->sid = BonusSourceID(*admitted.forceLance);
	lance->val = 1; lance->stacking = "new-horizons:forceLance";
	const auto state = capture({raw(entangle), raw(classic, 1), raw(lance, 2)}, admitted);
	const auto stayed = Transitions::prepareRemoval(state, Removal::TargetMoved, false);
	ASSERT_TRUE(stayed); EXPECT_EQ(stayed.receipt->after->values().size(), 3u);
	const auto recalled = Transitions::prepareRemoval(state, Removal::RootPreservingRelocation, true);
	ASSERT_TRUE(recalled); EXPECT_EQ(recalled.receipt->after->values().size(), 3u);
	const auto moved = Transitions::prepareRemoval(state, Removal::TargetMoved, true);
	ASSERT_TRUE(moved); EXPECT_EQ(moved.receipt->after->values().size(), 2u);
	EXPECT_EQ(moved.receipt->changes[0].id, 0u);
	const std::array observed{BindObservation{1, false}};
	const auto sourceDied = Transitions::prepareRemoval(moved.receipt->after, Removal::LegacyBindTrigger, false, observed);
	ASSERT_TRUE(sourceDied); EXPECT_EQ(sourceDied.receipt->after->values().size(), 2u); // unparameterized Lance pins group
	const auto opening = Transitions::prepareRound(moved.receipt->after, true, 1);
	ASSERT_TRUE(opening);
	const auto ordinary = Transitions::prepareRound(opening.receipt->after, false, 2);
	ASSERT_TRUE(ordinary); EXPECT_EQ(ordinary.receipt->after->values().size(), 1u); // Lance expired, classic has 1 round
	const auto released = Transitions::prepareRemoval(ordinary.receipt->after, Removal::LegacyBindTrigger, false, observed);
	ASSERT_TRUE(released); EXPECT_TRUE(released.receipt->after->values().empty());
}

TEST(ForecastEffectStateTest, DeathUsesCapturedPersistenceAndStrikeUsesExactNativeDuration)
{
	auto transient = timed(); transient->duration = BonusDuration::UNTIL_ATTACK;
	auto permanentRay = timed(BonusType::PRIMARY_SKILL); permanentRay->duration = BonusDuration::ONE_BATTLE;
	const auto state = capture({raw(transient), raw(permanentRay, 1, false)});
	const auto struck = Transitions::prepareStrike(state, battle::timedEffects::StrikeClock::Attack);
	ASSERT_TRUE(struck); EXPECT_EQ(struck.receipt->after->values().size(), 1u);
	const auto died = Transitions::prepareRemoval(state, Removal::DeathOrResurrection, false);
	ASSERT_TRUE(died); EXPECT_EQ(died.receipt->after->values().size(), 1u);
	EXPECT_EQ(died.receipt->after->values()[0].id, 1u);
}

TEST(ForecastEffectStateTest, UnsupportedRejectedBeforePlanAndBudgetRefusalLeavesSourceUntouched)
{
	for(const auto type : {BonusType::POISON, BonusType::REBIRTH, BonusType::HP_REGENERATION,
		BonusType::HYPNOTIZED, BonusType::COMBAT_EVENT_TRIGGER})
		EXPECT_THROW(capture({raw(timed(type))}), std::invalid_argument);
	auto updated = timed(); updated->updater = std::make_shared<ForeignUpdater>();
	EXPECT_THROW(capture({raw(updated)}), std::invalid_argument);
	const auto state = capture({raw(timed())});
	battleSearch::SearchBudget empty({0, 0, 0, 0, 0});
	EXPECT_EQ(Transitions::prepareRound(state, true, 1, &empty).refusal, Refusal::Exhausted);
	EXPECT_EQ(state->revision(), 0u); EXPECT_EQ(state->find(0)->value->turnsRemain, 2);
	EXPECT_EQ(Transitions::prepareRound(state, false, 1).refusal, Refusal::Stale);
	EXPECT_EQ(Transitions::prepareStrike({}, battle::timedEffects::StrikeClock::Attack).refusal, Refusal::Unsupported);
}

TEST(ForecastEffectStateTest, NumericCountdownDoesNotDirtyStatsUntilActualExpiry)
{
	const auto state = capture({raw(timed(BonusType::STACK_HEALTH))}, Admission{{}, {}, {}, 1, -1});
	const auto aged = Transitions::prepareRound(state, false, 2);
	ASSERT_TRUE(aged);
	EXPECT_EQ(aged.receipt->dirty, Dirty::None);
	const auto expired = Transitions::prepareRound(aged.receipt->after, false, 3);
	ASSERT_TRUE(expired);
	EXPECT_TRUE(contains(expired.receipt->dirty, Dirty::HealthCapacity));
	EXPECT_TRUE(contains(expired.receipt->dirty, Dirty::UnitStats));
}

TEST(ForecastEffectStateTest, FocusRowsAreOwnedButNeverDoubleConsumedByGenericStrike)
{
	Admission admitted; admitted.focusMagic = SpellID(204);
	auto focus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NONE,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(*admitted.focusMagic));
	focus->stacking = "new-horizons:focusMagic";
	const auto state = capture({raw(focus)}, admitted);
	const auto attacked = Transitions::prepareStrike(state, battle::timedEffects::StrikeClock::Attack);
	ASSERT_TRUE(attacked);
	EXPECT_TRUE(attacked.receipt->changes.empty());
	EXPECT_EQ(attacked.receipt->after->values()[0].value->val, 3);
	const auto died = Transitions::prepareRemoval(state, Removal::DeathOrResurrection, false);
	ASSERT_TRUE(died); EXPECT_TRUE(died.receipt->after->values().empty());
}

TEST(ForecastEffectStateTest, NativeSi16DurationArithmeticPreservesAllSentinelsWithoutPartialThrows)
{
	for(const int16_t value : {int16_t(-32768), int16_t(-1), int16_t(0), int16_t(1), int16_t(32767)})
	{
		const auto native = static_cast<int16_t>(value - 1);
		EXPECT_EQ(battle::timedEffects::nextTurns(value), native);
		const auto state = capture({raw(timed(BonusType::STACKS_SPEED, value))}, Admission{{}, {}, {}, 1, -1});
		const auto next = Transitions::prepareRound(state, false, 2);
		ASSERT_TRUE(next);
		if(native <= 0) EXPECT_TRUE(next.receipt->after->values().empty());
		else EXPECT_EQ(next.receipt->after->find(0)->value->turnsRemain, native);
	}
}

TEST(ForecastEffectStateTest, AcceptedNativeFrozenApplicationAndShatterNeverRollOrUseDamageAmount)
{
	Admission admitted; admitted.round = 3;
	const auto source = capture({}, admitted);
	auto frozen = std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(1)), 3));
	const std::array incoming{raw(frozen, 7, false)};
	const auto applied = Transitions::prepareApplication(source, {}, incoming, 3);
	ASSERT_TRUE(applied);
	EXPECT_FALSE(source->frozen()); EXPECT_TRUE(applied.receipt->after->frozen());
	EXPECT_TRUE(contains(applied.receipt->dirty, Dirty::Control));
	frozen->parameters.reset(); // owned marker survives original outcome mutation
	EXPECT_TRUE(applied.receipt->after->values()[0].value->parameters);
	const auto rejectedHit = Transitions::prepareFrozenShatter(applied.receipt->after, false);
	ASSERT_TRUE(rejectedHit); EXPECT_TRUE(rejectedHit.receipt->after->frozen());
	// Native qualifiesForShatter has no damage > 0 condition. A qualified
	// accepted zero-damage creature hit removes the same captured marker.
	const auto acceptedHit = Transitions::prepareFrozenShatter(applied.receipt->after, true);
	ASSERT_TRUE(acceptedHit); EXPECT_FALSE(acceptedHit.receipt->after->frozen());
	EXPECT_EQ(acceptedHit.receipt->after->frozenApplicationRound(), 3);
	EXPECT_TRUE(applied.receipt->after->frozen());
}
