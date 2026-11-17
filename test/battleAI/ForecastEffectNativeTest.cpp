/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ForecastEffectTransitions.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/RoundBonusDuration.h"
#include "../../lib/battle/UnitTransitionSidecars.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../lib/CRandomGenerator.h"

namespace
{
class ForecastEffectNativeTest : public ProjectedTargetHitFixture
{
protected:
	void prepare()
	{
		startGame(); startBattle(); beginCombat();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	std::shared_ptr<const forecastEffects::State> capture(CStack & unit,
		std::optional<SpellID> entangle = std::nullopt, std::optional<SpellID> lance = std::nullopt)
	{
		std::vector<forecastEffects::RawRecord> records;
		const auto round = battle::roundTimedBonuses(battle()->getMagicRules());
		for(const auto & record : unit.getExportedBonusList())
			if(record->source == BonusSource::SPELL_EFFECT || newHorizonsFrozen::markerApplicationRound(*record))
				records.push_back({record, {unit.unitId(), records.size()}, round(record.get()),
					battle::isTransientSpellEffect(record.get())});
		return forecastEffects::State::capture(records, {entangle, lance, {}, battle()->getRound(), unit.frozenLastAppliedRound()});
	}
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;
};
}

TEST_F(ForecastEffectNativeTest, NativeFrozenForfeitDeferredCompletionMatchesOwnedAndPreservesSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 20);
	SetStackEffect frozen; frozen.battleID = battle()->getBattleID();
	frozen.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{newHorizonsFrozen::marker(
		BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
	gameHandler->sendAndApply(frozen);
	const auto owned = capture(*unit);
	auto native = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), native);
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION})
	{
		native->nextTurn(unit->unitId(), reason);
		forecastEffects::Activation input; input.reason = reason; input.serial = 3;
		const auto prepared = forecastEffects::Transitions::prepareActivation(owned, input);
		ASSERT_TRUE(prepared);
		EXPECT_EQ(prepared.receipt->after->frozen(), newHorizonsFrozen::isFrozen(*native->getForUpdate(unit->unitId())));
	}
	native->nextTurnPreservingForfeit(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	forecastEffects::Activation input; input.serial = 4; input.normalSlotForfeit = true; input.deferFrozenThaw = true;
	const auto pending = forecastEffects::Transitions::prepareActivation(owned, input);
	ASSERT_TRUE(pending);
	EXPECT_EQ(pending.receipt->after->frozen(), newHorizonsFrozen::isFrozen(*native->getForUpdate(unit->unitId())));
	native->completeForfeitedActivation(unit->unitId());
	const auto completed = forecastEffects::Transitions::prepareActivationEnd(pending.receipt->after, 4, true, false);
	ASSERT_TRUE(completed);
	EXPECT_EQ(completed.receipt->after->frozen(), newHorizonsFrozen::isFrozen(*native->getForUpdate(unit->unitId())));
	EXPECT_EQ(completed.receipt->after->frozenApplicationRound(), native->getForUpdate(unit->unitId())->frozenLastAppliedRound());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*unit));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*sibling->getForUpdate(unit->unitId())));
}

TEST_F(ForecastEffectNativeTest, NativeActualMoveClearsEntangleButKeepsClassicBindAndFixedLance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 20);
	const auto entangle = SpellID(SpellID::decode("new-horizons:entangle"));
	const auto lance = SpellID(SpellID::decode("new-horizons:forceLance"));
	Bonus roots(BonusDuration::N_TURNS, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, 1, BonusSourceID(entangle));
	roots.turnsRemain = 3;
	Bonus classic = roots; classic.sid = BonusSourceID(SpellID(SpellID::BIND));
	classic.parameters = std::make_shared<BonusParameters>(int32_t(17));
	unit->addNewBonus(std::make_shared<Bonus>(roots));
	unit->addNewBonus(std::make_shared<Bonus>(classic));
	unit->addNewBonus(std::make_shared<Bonus>(newHorizonsSorcerySpells::forceLancePin(lance)));
	const auto before = capture(*unit, entangle, lance);
	battle()->moveUnit(unit->unitId(), BattleHex(leftHex + GameConstants::BFIELD_WIDTH));
	const auto prepared = forecastEffects::Transitions::prepareRemoval(before, forecastEffects::Removal::TargetMoved, true);
	ASSERT_TRUE(prepared);
	const auto actual = unit->getAllBonuses(Selector::type()(BonusType::BIND_EFFECT));
	EXPECT_EQ(prepared.receipt->after->values().size(), actual->size());
	EXPECT_EQ(prepared.receipt->changes.size(), 1u);
	EXPECT_EQ(prepared.receipt->changes[0].before->sid, BonusSourceID(entangle));
	const auto after = capture(*unit, entangle, lance);
	const auto preserved = forecastEffects::Transitions::prepareRemoval(after, forecastEffects::Removal::RootPreservingRelocation, true);
	ASSERT_TRUE(preserved); EXPECT_TRUE(preserved.receipt->changes.empty());
}

TEST_F(ForecastEffectNativeTest, OwnedEffectPreparationLeavesLiveWorldAndBothRandomSourcesUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 20);
	Bonus speed(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		-20, BonusSourceID(SpellID(SpellID::SLOW)));
	speed.turnsRemain = 2;
	unit->addNewBonus(std::make_shared<Bonus>(speed));
	const auto rng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaults = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto liveBefore = projectedHitTest::serialized(*unit);
	const auto before = capture(*unit);
	const auto after = forecastEffects::Transitions::prepareRound(before, before->round() == 0, before->round() + 1);
	ASSERT_TRUE(after);
	const auto strike = forecastEffects::Transitions::prepareStrike(after.receipt->after,
		battle::timedEffects::StrikeClock::Attack);
	ASSERT_TRUE(strike);
	EXPECT_EQ(projectedHitTest::serialized(*unit), liveBefore);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaults);
}

TEST_F(ForecastEffectNativeTest, FreezingTouchPotentialRolePersistsForDeadBodyButCurrentEligibilityDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"), BattleHex(leftHex), 20);
	ASSERT_GT(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 0);
	EXPECT_TRUE(newHorizonsFrozen::hasFreezingTouchRole(*unit, battle()->getMagicRules()));
	auto dead = unit->acquireState();
	auto injury = dead->getAvailableHealth();
	dead->damage(injury);
	ASSERT_FALSE(dead->alive());
	EXPECT_TRUE(newHorizonsFrozen::hasFreezingTouchRole(*dead, battle()->getMagicRules()));
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*dead, battle()->getMagicRules()));
	auto disabled = battle()->getMagicRules();
	disabled["creatureAbilities"]["freezingTouchChancePercent"].Integer() = 0;
	EXPECT_FALSE(newHorizonsFrozen::hasFreezingTouchRole(*unit, disabled));
	EXPECT_FALSE(newHorizonsFrozen::hasFreezingTouchRole(*dead, disabled));
}
