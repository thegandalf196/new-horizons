/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastHeroActionRouting.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

TEST_F(ForecastNativeAdapterTest, UnsupportedHeroClosureStartsReferenceBeforeManaOrEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:aenain"));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto before = *parent->forecastState();
	const auto mana = parent->getProjectedManaAvailable(BattleSide::ATTACKER);
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	PossibleSpellcast candidate;
	candidate.spell = SpellID(SpellID::TELEPORT).toSpell();
	candidate.dest.emplace_back(parent->battleGetAllUnits(false).front());
	const auto decision = forecast::heroAction::classify(*parent, BattleSide::ATTACKER, candidate);
	ASSERT_EQ(decision.route, forecast::heroAction::Route::CurrentNativeReference);
	auto reference = forecast::heroAction::makeBranch(environment.get(), visible, {}, decision.route);
	EXPECT_TRUE(reference->referenceForecastOnly());
	EXPECT_EQ(reference->forecastState(), nullptr);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*reference));
	EXPECT_EQ(reference->getProjectedManaAvailable(BattleSide::ATTACKER), mana);
	for(const auto id : before.ids())
		EXPECT_EQ(reference->battleGetUnitByID(id)->getAvailableHealth(), before.unit(id)->health.available());
	EXPECT_TRUE(parent->forecastState()->sameSnapshot(before));
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, ReferenceDescendantKeepsCurrentManaEffectsAndSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:aenain"));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	// This scenario casts Arrow; Aenain's authored starting spell is Frailty.
	hero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(hero->canCastThisSpell(SpellID(SpellID::MAGIC_ARROW).toSpell()));
	auto reference = forecast::heroAction::makeBranch(environment.get(), visible, {},
		forecast::heroAction::Route::CurrentNativeReference);
	reference->nextRound(); // Enter the actual ordinary Hero allowance round.
	const auto side = BattleSide::ATTACKER;
	const auto initialMana = reference->getProjectedManaAvailable(side);
	ASSERT_TRUE(initialMana);
	PossibleSpellcast candidate;
	candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto units = reference->battleGetAllUnits(false);
	const auto enemyIt = std::ranges::find_if(units, [](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER; });
	ASSERT_NE(enemyIt, units.end());
	const auto * enemy = *enemyIt;
	candidate.dest.emplace_back(enemy);
	ASSERT_TRUE(reference->prepareHeroSpellAllowance(side, candidate.spell->getId()));
	const auto admission = heroActionProjection::admitCandidate(*reference, side, candidate);
	ASSERT_TRUE(admission.admitted);
	ASSERT_TRUE(admission.spellAllowance);
	ASSERT_TRUE(reference->projectAcceptedHeroSpell(side, candidate.spell->getId(), enemy->unitId(), *admission.spellAllowance));
	const auto changedMana = reference->getProjectedManaAvailable(side);
	ASSERT_TRUE(changedMana); ASSERT_LT(*changedMana, *initialMana);
	const auto attacker = std::ranges::find_if(units, [side](const battle::Unit * unit)
	{ return unit->unitSide() == side && unit->unitSlot() == SlotID(0); });
	ASSERT_NE(attacker, units.end());
	const auto id = (*attacker)->unitId();
	Bonus bonus; bonus.type = BonusType::PRIMARY_SKILL; bonus.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	bonus.val = 7; bonus.source = BonusSource::OTHER; bonus.duration = BonusDuration::ONE_BATTLE;
	reference->getForUpdate(id)->addUnitBonus({bonus});
	const auto changedAttack = reference->battleGetUnitByID(id)->getAttack(false);
	auto nested = forecast::heroAction::makeBranch(environment.get(), reference, {},
		forecast::heroAction::Route::Compact); // Inherited reference mode remains authoritative.
	EXPECT_TRUE(nested->referenceForecastOnly());
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*nested));
	EXPECT_EQ(nested->getProjectedManaAvailable(side), changedMana);
	EXPECT_EQ(nested->battleGetUnitByID(id)->getAttack(false), changedAttack);
	EXPECT_EQ(visible->battleGetFightingHero(side)->getManaAvailable(), *initialMana);
}

TEST_F(ForecastNativeAdapterTest, ReferenceFactoryBudgetRefusesBeforeAnySourcePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto beforeMap = parent->stackStates;
	const auto round = parent->battleGetRound();
	battleSearch::WorkAmounts none{};
	auto work = std::make_shared<battleSearch::SearchBudget>(none);
	EXPECT_THROW(forecast::heroAction::makeBranch(environment.get(), parent, work,
		forecast::heroAction::Route::CurrentNativeReference), battleSearch::BudgetExhausted);
	EXPECT_EQ(parent->stackStates, beforeMap);
	EXPECT_EQ(parent->battleGetRound(), round);
	EXPECT_FALSE(parent->referenceForecastOnly());
}
