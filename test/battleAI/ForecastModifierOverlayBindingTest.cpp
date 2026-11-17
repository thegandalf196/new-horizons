/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/ForecastNativeAdapter.h"
#include "../../AI/BattleAI/FocusMagicProjection.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"

namespace
{
class ForecastModifierOverlayBindingTest : public ForecastNativeAdapterTest
{
protected:
	std::shared_ptr<HypotheticBattle> child, sibling;
	uint32_t actorId = 0;
	void TearDown() override
	{
		child.reset(); sibling.reset();
		ForecastNativeAdapterTest::TearDown();
	}
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(ForecastNativeAdapterTest::prepare());
		if(parent->battleGetRound() == 0) parent->nextRound(); // Start ordinary clocks before applying one-turn effects.
		bool found = false;
		for(const auto * unit : parent->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::ATTACKER && unit->alive())
			{ actorId = unit->unitId(); found = true; break; }
		ASSERT_TRUE(found);
		ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *child);
		forecast::NativeAdapter::forkExchange(*parent, *sibling);
		ASSERT_TRUE(child->forecastState()); ASSERT_TRUE(sibling->forecastState());
		EXPECT_EQ(child->forecastState()->unit(actorId)->currentModifiers,
			sibling->forecastState()->unit(actorId)->currentModifiers);
	}
	auto nativeObservation() { return gameState()->saveToMemory(); }
	auto siblingObservation() { return sibling->getForUpdate(actorId)->save(); }

	void apply(const std::vector<Bonus> & removed, const std::vector<Bonus> & added)
	{
		using namespace forecast;
		auto target = child->getForUpdate(actorId); ASSERT_TRUE(target);
		const auto priorRecipe = child->forecastState() ? child->forecastState()->unit(target->unitId())->currentModifiers : nullptr;
		ASSERT_NE(child->forecastState(), nullptr);
		auto draft = *child->forecastState();
		const auto receipt = NativeAdapter::prepareEffectApplication(draft, *child, target->unitId(), removed, added);
		auto staged = NativeAdapter::prepareEffectDraft(*target); ASSERT_TRUE(staged);
		ASSERT_TRUE(NativeAdapter::stageEffects(draft, *child, target->unitId(), receipt, *staged));
		ASSERT_TRUE(draft.unit(target->unitId())->currentModifiers);
		ASSERT_TRUE(draft.unit(target->unitId())->modifierNumbers);
		if(priorRecipe) EXPECT_NE(draft.unit(target->unitId())->currentModifiers, priorRecipe);
		NativeAdapter::publishEffects(*child, std::move(draft), *target, *staged);
		EXPECT_EQ(child->getForUpdate(target->unitId()).get(), target.get());
		EXPECT_EQ(parent->forecastState()->unit(actorId)->currentModifiers,
			sibling->forecastState()->unit(actorId)->currentModifiers);
		EXPECT_NE(child->forecastState()->unit(actorId)->currentModifiers,
			sibling->forecastState()->unit(actorId)->currentModifiers);
	}
	void expectNativeParity()
	{
		const auto unit = child->getForUpdate(actorId); ASSERT_TRUE(unit);
		const auto * state = child->forecastState(); ASSERT_NE(state, nullptr);
		for(bool ranged : {false, true})
		{
			const auto expected = nativeDamage::prepareUnit(*unit, ranged); ASSERT_TRUE(expected);
			const auto * actual = state->prepared(unit->unitId(), ranged); ASSERT_NE(actual, nullptr);
			const auto * row = state->unit(unit->unitId()); ASSERT_TRUE(row->modifierNumbers);
			for(const auto * prepared : {actual, &row->modifierNumbers->prepared[ranged ? 1 : 0]})
			{
				const auto & a = prepared->unit; const auto & e = expected->unit;
				EXPECT_EQ(a.bonuses, e.bonuses); EXPECT_EQ(a.values, e.values);
				EXPECT_EQ(a.minDamage, e.minDamage); EXPECT_EQ(a.maxDamage, e.maxDamage);
				EXPECT_EQ(a.count, e.count); EXPECT_EQ(a.attack, e.attack); EXPECT_EQ(a.defense, e.defense);
				EXPECT_EQ(a.firstHealth, e.firstHealth); EXPECT_EQ(a.maxHealth, e.maxHealth);
				EXPECT_EQ(a.phantomIntegrity, e.phantomIntegrity); EXPECT_EQ(a.baseAmount, e.baseAmount);
				EXPECT_EQ(a.availableHealth, e.availableHealth); EXPECT_EQ(a.shooter, e.shooter);
				EXPECT_EQ(a.turret, e.turret); EXPECT_EQ(a.creatureKey, e.creatureKey);
				EXPECT_EQ(prepared->physicalDefense, expected->physicalDefense);
				EXPECT_EQ(prepared->enemyDefenseReduction, expected->enemyDefenseReduction);
				EXPECT_EQ(prepared->enemyAttackReduction, expected->enemyAttackReduction);
				EXPECT_EQ(prepared->attackReduction, expected->attackReduction);
				EXPECT_EQ(prepared->damageBoost, expected->damageBoost); EXPECT_EQ(prepared->petrification, expected->petrification);
				EXPECT_EQ(prepared->shooting, ranged);
				const auto & leadership = state->definition(unit->unitId())->leadership;
				if(leadership.known) EXPECT_EQ(prepared->leadershipEfficiency, leadership.active
					? newHorizonsHeroes::squareRootLeadershipEfficiency(unit->getCount(), leadership.capacity) : 1.0);
			}
		}
	}
	void casualty()
	{
		auto target = child->getForUpdate(actorId); ASSERT_GT(target->getCount(), 1);
		const auto before = child->forecastState()->unit(target->unitId())->currentModifiers;
		const auto oldCount = target->getCount();
		int64_t amount = target->getFirstHPleft();
		ASSERT_GT(amount, 0);
		ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *target, amount, battle::DamageProvenance::OTHER, nullptr));
		EXPECT_LT(target->getCount(), oldCount);
		EXPECT_EQ(child->forecastState()->unit(target->unitId())->currentModifiers, before);
		ASSERT_NO_FATAL_FAILURE(expectNativeParity());
	}
};
TEST_F(ForecastModifierOverlayBindingTest, AcceptedTimedApplyExpiryThenCasualtyUsesCurrentRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto outside = nativeObservation(); const auto siblingBefore = siblingObservation();
	const auto target = child->getForUpdate(actorId); const auto originalAttack = target->getAttack(false);
	Bonus timed(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 5,
		BonusSourceID(SpellID(SpellID::BLOODLUST)));
	timed.subtype = BonusSubtypeID(PrimarySkill::ATTACK); timed.turnsRemain = 1;
	ASSERT_NO_FATAL_FAILURE(apply({}, {timed}));
	EXPECT_GT(target->getAttack(false), originalAttack); ASSERT_NO_FATAL_FAILURE(expectNativeParity());
	const auto appliedRecipe = child->forecastState()->unit(target->unitId())->currentModifiers;
	ASSERT_NO_FATAL_FAILURE(apply({timed}, {}));
	EXPECT_NE(child->forecastState()->unit(target->unitId())->currentModifiers, appliedRecipe);
	EXPECT_EQ(target->getAttack(false), originalAttack); ASSERT_NO_FATAL_FAILURE(expectNativeParity());
	ASSERT_NO_FATAL_FAILURE(casualty());
	EXPECT_EQ(target->getAttack(false), originalAttack);
	EXPECT_EQ(nativeObservation(), outside); EXPECT_EQ(siblingObservation(), siblingBefore);
}
TEST_F(ForecastModifierOverlayBindingTest, FocusLastChargeThenCasualtyCannotRestoreShooterRows)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto outside = nativeObservation(); const auto siblingBefore = siblingObservation();
	const auto target = child->getForUpdate(actorId);
	ASSERT_FALSE(target->isShooter());
	const auto spell = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL)); ASSERT_TRUE(spell.hasValue());
	ASSERT_NO_FATAL_FAILURE(apply({}, newHorizonsFocusMagic::enchantment(*target, spell)));
	ASSERT_TRUE(newHorizonsFocusMagic::active(*target)); ASSERT_TRUE(target->isShooter());
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		const auto consumption = newHorizonsFocusMagic::consume(*target);
		ASSERT_FALSE(consumption.remove.empty());
		ASSERT_NO_FATAL_FAILURE(apply(consumption.remove, consumption.add));
		EXPECT_EQ(newHorizonsFocusMagic::charges(*target), remaining);
		ASSERT_NO_FATAL_FAILURE(expectNativeParity());
	}
	ASSERT_FALSE(target->isShooter());
	ASSERT_NO_FATAL_FAILURE(casualty());
	EXPECT_FALSE(target->isShooter()); EXPECT_FALSE(newHorizonsFocusMagic::active(*target));
	EXPECT_EQ(nativeObservation(), outside); EXPECT_EQ(siblingObservation(), siblingBefore);
}
TEST_F(ForecastModifierOverlayBindingTest, FutureRoundExpiryRebindsBeforeLaterCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto outside = nativeObservation();
	const auto target = child->getForUpdate(actorId);
	const auto baseline = target->getAttack(false);
	Bonus timed(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 5,
		BonusSourceID(SpellID(SpellID::BLOODLUST)));
	timed.subtype = BonusSubtypeID(PrimarySkill::ATTACK); timed.turnsRemain = 1;
	ASSERT_NO_FATAL_FAILURE(apply({}, {timed}));
	const auto oldRecipe = child->forecastState()->unit(target->unitId())->currentModifiers;
	ASSERT_GT(target->getAttack(false), baseline);
	child->nextRound(); // Actual timed owner's future-context stage/prefix/publication.
	ASSERT_NE(child->forecastState()->unit(target->unitId())->currentModifiers, oldRecipe);
	EXPECT_EQ(target->getAttack(false), baseline);
	ASSERT_NO_FATAL_FAILURE(expectNativeParity());
	ASSERT_NO_FATAL_FAILURE(casualty());
	EXPECT_EQ(nativeObservation(), outside);
}
TEST_F(ForecastModifierOverlayBindingTest, StaleOrForeignPreparedSourceCannotCaptureCurrentRecipes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto target = child->getForUpdate(actorId);
	auto other = sibling->getForUpdate(actorId);
	auto staged = forecast::NativeAdapter::prepareEffectDraft(*target); ASSERT_TRUE(staged);
	EXPECT_FALSE(other->capturePreparedModifierFacts(*staged));
	auto future = std::make_shared<HypotheticBattle>(environment.get(), child);
	auto unregistered = forecast::NativeAdapter::prepareEffectDraft(*target, future.get());
	EXPECT_FALSE(target->capturePreparedModifierFacts(*unregistered)); // Future map must own exact staged pointer.
	Bonus addition(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 1,
		BonusSourceID(SpellID(SpellID::BLOODLUST)));
	addition.subtype = BonusSubtypeID(PrimarySkill::ATTACK); addition.turnsRemain = 1;
	target->addUnitBonus({addition}); // Canonical source version changes; the old receipt is stale.
	EXPECT_FALSE(target->capturePreparedModifierFacts(*staged));
}
}
