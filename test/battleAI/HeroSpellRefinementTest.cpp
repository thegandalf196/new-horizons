/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/HeroSpellRefinement.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagicArrow.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/CGameHandler.h"
#include <algorithm>
#include <limits>

namespace heroQueueTest
{
class HeroSpellRefinementTest : public HeroQueueSelectorTest
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.total();
	}
	PossibleSpellcast arrow(const CStack * victim, int overcharge = 0)
	{
		PossibleSpellcast candidate;
		candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		candidate.spellOvercharge = overcharge;
		candidate.dest.emplace_back(victim);
		return candidate;
	}
	std::optional<float> scout(const PossibleSpellcast & candidate)
	{
		auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
		DamageCache damage;
		damage.buildDamageCache(state, BattleSide::ATTACKER);
		BattleExchangeEvaluator utility(state, environment, 1, 2);
		return heroDecisionPressure::spellScoutUtility(environment.get(), live, BattleSide::ATTACKER,
			candidate, damage, utility, nullptr);
	}
};

TEST_F(HeroSpellRefinementTest, NativeClassifierRejectsMovementHazardsAndStochasticFamilies)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Slow is now a level-three Sorcery spell. An explicitly granted rank
	// makes this a legal movement-effect control without granting any perks.
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto domain = [&](SpellID spell, const CStack * recipient, bool expectLegal)
	{
		attackerSideHero->addSpellToSpellbook(spell);
		auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
		spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		auto mechanics = spell.toSpell()->battleMechanics(&cast);
		const spells::Target target{spells::Destination(state->battleGetUnitByID(recipient->unitId()))};
		if(expectLegal) EXPECT_TRUE(mechanics->canBeCastAt(target));
		return heroDecisionPressure::spellScoutDomain(*mechanics, target);
	};
	using Domain = heroDecisionPressure::SpellScoutDomain;
	EXPECT_EQ(domain(SpellID::MAGIC_ARROW, enemy, true), Domain::Damage);
	EXPECT_EQ(domain(SpellID::BLESS, actor, true), Domain::NumericTimed);
	// Haste is retired from hero access; Slow provides the legal movement case.
	EXPECT_EQ(domain(SpellID::HASTE, active, false), Domain::Unknown);
	EXPECT_EQ(domain(SpellID::SLOW, enemy, true), Domain::Unknown);
	// An invalid positive-spell aim has no affected recipients. That empty set
	// cannot prove that an arbitrary Timed effect belongs to a numeric domain.
	EXPECT_EQ(domain(SpellID::HASTE, enemy, false), Domain::Unknown);
	for(const auto spell : {SpellID::LAND_MINE, SpellID::BERSERK})
		EXPECT_EQ(domain(spell, enemy, false), Domain::Unknown);
}

TEST_F(HeroSpellRefinementTest, SharedDamageDefenseAndSignedUtilityPreserveWorldAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = gameState()->saveToMemory();
	const auto rng = wire(*gameHandler->randomizer);
	const auto candidate = arrow(enemy);
	const auto ordinary = scout(candidate);
	ASSERT_TRUE(ordinary);
	EXPECT_GT(*ordinary, 0);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	// A real target defense must affect the scout through shared castEval,
	// rather than a nominal spell damage or unweighted HP prior.
	Bonus reduction(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	effect(enemy, reduction);
	const auto protectedValue = scout(candidate);
	ASSERT_TRUE(protectedValue);
	EXPECT_LT(*protectedValue, *ordinary);
}

TEST_F(HeroSpellRefinementTest, TypedAndStochasticCandidatesKeepReferenceAndManaIsNotInvented)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto candidate = arrow(enemy);
	candidate.divineMandateFollowup = true;
	EXPECT_FALSE(scout(candidate));
	candidate.divineMandateFollowup = false;
	candidate.spellHandOfFateExpectedValue = 20;
	EXPECT_FALSE(scout(candidate));
	candidate.spellHandOfFateExpectedValue.reset();
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(scout(candidate));
}

TEST_F(HeroSpellRefinementTest, AffordableOverchargeUsesActualCostAndDoesNotSpendVisibleMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto candidate = arrow(enemy, 1);
	const auto ordinaryCost = live->battleGetSpellCost(candidate.spell, attackerSideHero, 1);
	const auto cost = newHorizonsMagic::magicArrowManaCost(battle()->getMagicRules(),
		SpellID::MAGIC_ARROW, ordinaryCost, 1);
	ASSERT_TRUE(cost);
	setTestSpellPointTotal(attackerSideHero, *cost);
	const auto world = gameState()->saveToMemory();
	EXPECT_TRUE(scout(candidate));
	EXPECT_EQ(gameState()->saveToMemory(), world);
	setTestSpellPointTotal(attackerSideHero, *cost - 1);
	EXPECT_FALSE(scout(candidate));
}

TEST_F(HeroSpellRefinementTest, CommonSpellAndOrderBudgetRetainsOriginalEvaluationOrderAndNoCastBaseline)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto candidate = arrow(enemy);
	const auto value = scout(candidate);
	ASSERT_TRUE(value);
	std::vector<heroDecisionPressure::Ranked> ranked{{0, *value}, {1, *value + 1},
		{2, *value - 1}, {3, *value + 2}, {4, *value + 3}};
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(ranked, 3), (std::vector<size_t>{1, 3, 4}));
	// Ranking cannot submit a harmful action. Finalists retain the ordinary
	// comparison against declining the Hero Action in the real selector.
	std::vector<PossibleSpellcast> finalists(2, candidate);
	finalists[0].command = HeroCommand::RIPOSTE;
	finalists[0].value = -2;
	finalists[1].value = -1;
	const auto selected = heroActionSelection::choose(finalists, 0, false, nullptr);
	ASSERT_TRUE(selected);
	EXPECT_NE(selected->disposition, heroActionSelection::Disposition::PERFORM);
}
TEST_F(HeroSpellRefinementTest, CombinedRosterRefinesAtMostThreeAndUnsupportedSpellKeepsReference)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	active = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), 200);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(13, 5), 200);
	activate(active);
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	std::vector<PossibleSpellcast> candidates{arrow(enemy), arrow(enemy, 1), arrow(enemy, 2)};
	PossibleSpellcast riposte;
	riposte.command = HeroCommand::RIPOSTE;
	candidates.push_back(riposte);
	PossibleSpellcast focus;
	focus.command = HeroCommand::FOCUS_FIRE;
	focus.commandTargets = {enemy->unitId()};
	candidates.push_back(focus);
	auto unsupported = arrow(enemy);
	unsupported.spellHandOfFateExpectedValue = 10; // Existing kernel cannot enter the generic scout.
	candidates.push_back(unsupported);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	DamageCache damage;
	damage.buildDamageCache(state, BattleSide::ATTACKER);
	BattleExchangeEvaluator utility(state, environment, 1, 2);
	heroDecisionPressure::Experiment result;
	result.refinementLimit = 3;
	const auto before = gameState()->saveToMemory();
	const auto rng = wire(*gameHandler->randomizer);
	heroDecisionPressure::prepareShortlist(result, environment.get(), live, BattleSide::ATTACKER,
		candidates, damage, utility);
	ASSERT_GE(result.cheapScores.size(), 4u);
	EXPECT_LE(result.refinedOrdinals.size(), 3u);
	ASSERT_EQ(result.declined.size(), candidates.size());
	EXPECT_FALSE(result.declined.back());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
}

}
