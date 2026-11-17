/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/HeroSpellOutcomeValue.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/spells/NewHorizonsMetamagicCore.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/CGameHandler.h"
#include <cmath>
#include <limits>
#include <array>
#include <algorithm>
namespace heroQueueTest
{
namespace
{
class Scores final : public heroActionSelection::Observer
{
public:
	SpellID wanted;
	std::vector<float> values;
	bool failed = false;
	explicit Scores(SpellID spell) : wanted(spell) {}
	void selected(heroActionSelection::SelectionValue) noexcept override {}
	void candidate(heroActionSelection::CandidateValue candidate) noexcept override
	{
		if(candidate.command != HeroCommand::NONE || candidate.spell != wanted) return;
		try { values.push_back(candidate.value); } catch(...) { failed = true; }
	}
};
}
class MetamagicStochasticProjectionTest : public HeroQueueSelectorTest
{
protected:
	SpellID spell;
	void prepare()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		spell = SpellID(SpellID::decode("new-horizons:handOfFate"));
		ASSERT_TRUE(spell.hasValue());
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:chaosMagic"));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		// The generic arena's adjacent100Angels otherwise kill the only enemy
		// before it acts, so the native optional-Hero gate correctly skips all
		// spell scoring. Give this enemy a real turn before that winning attack.
		effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 500, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		ASSERT_EQ(enemy->getInitiative(), 900);
		ASSERT_GT(enemy->getInitiative(), actor->getInitiative());
		ASSERT_LT(enemy->getInitiative(), active->getInitiative());
		environment = std::make_shared<QueueEnvironment>(gameState());
		callback = std::make_shared<QueueCallback>();
		callback->onBattleStarted(battle());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.total();
	}
	void score(int rank, std::vector<float> & values)
	{
		const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
		attackerSideHero->setSecSkillLevel(skill, static_cast<MasteryLevel::Type>(rank), ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:metamagic"), rank);
		spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&cast);
		ASSERT_TRUE(mechanics->canBeCastAt({spells::Destination(enemy)}));
		const auto worldBefore = gameState()->saveToMemory();
		const auto random = wire(*gameHandler->randomizer);
		const auto ambient = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
		evaluator.selectStackAction(active);
		Scores observer(spell);
		evaluator.attemptCastingSpell(active, true, &observer);
		ASSERT_FALSE(observer.failed);
		ASSERT_FALSE(observer.values.empty());
		for(const auto value : observer.values) ASSERT_TRUE(std::isfinite(value));
		values = std::move(observer.values);
		EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
		EXPECT_EQ(wire(*gameHandler->randomizer), random);
		EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), ambient);
	}
};
TEST_F(MetamagicStochasticProjectionTest, HandOfFateEligibleEnemyEnchantmentChangesExpectedUtilityWithoutLiveRolls)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Isolate optional removal from the core's friendly-damage calculation.
	// Immune stacks remain in Hand of Fate's random collateral selection pool.
	for(const auto * unit : {active, actor, waiter})
	{
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
			BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
		ASSERT_TRUE(unit->hasImmunity(spell));
	}
	Bonus blessing(BonusDuration::N_TURNS, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 4,
		BonusSourceID(SpellID(SpellID::BLESS)));
	blessing.turnsRemain = 3;
	blessing.spellCasterOwner = PlayerColor(1);
	effect(enemy, blessing);
	const auto health = enemy->getAvailableHealth();
	std::vector<float> ordinary, unselected, expert;
	ASSERT_NO_FATAL_FAILURE(score(0, ordinary));
	ASSERT_NO_FATAL_FAILURE(score(3, unselected));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:metamagic",
		"new-horizons:metamagic.unravelingMagic", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(score(3, expert));
	ASSERT_EQ(ordinary.size(), expert.size());
	EXPECT_EQ(ordinary, unselected) << "Metamagic core grants no free Unraveling Magic";
	EXPECT_GT(*std::max_element(expert.begin(), expert.end()), *std::max_element(ordinary.begin(), ordinary.end()));
	EXPECT_EQ(enemy->getAvailableHealth(), health);
	EXPECT_EQ(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLESS))))->size(), 1u);
}
TEST_F(MetamagicStochasticProjectionTest, HandOfFateCoreProtectsFriendlyCollateralWithoutRemovalPerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	std::vector<float> ordinary, protectedCollateral;
	ASSERT_NO_FATAL_FAILURE(score(0, ordinary));
	ASSERT_NO_FATAL_FAILURE(score(3, protectedCollateral));
	ASSERT_EQ(ordinary.size(), protectedCollateral.size());
	EXPECT_FALSE(attackerSideHero->hasActivePerk("new-horizons:metamagic",
		"new-horizons:metamagic.unravelingMagic"));
	spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&cast);
	EXPECT_FALSE(newHorizonsMetamagic::singleTargetSpell(*mechanics));
	for(const auto * unit : {active, actor, waiter})
		EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, unit), 40);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, enemy), 100);
	EXPECT_GT(*std::max_element(protectedCollateral.begin(), protectedCollateral.end()),
		*std::max_element(ordinary.begin(), ordinary.end()));
}
TEST(MetamagicStochasticProjectionContractTest, LocalReceiptCannotLaunchCandidateContinuationAndPropagatesUnknown)
{
	const std::array<uint32_t, 2> ids{10, 11};
	int calls = 0;
	const heroSpellProjection::OutcomeValue value = [&](const auto &, auto injuries, bool completed,
		bool localOnly, heroSpellProjection::OutcomeReceipt receipt) -> heroSpellProjection::OutcomeResult
	{
		++calls;
		EXPECT_TRUE(localOnly);
		EXPECT_FALSE(completed);
		EXPECT_TRUE(injuries.empty());
		EXPECT_EQ(std::vector<uint32_t>(receipt.resolved.begin(), receipt.resolved.end()), (std::vector<uint32_t>{10, 11}));
		EXPECT_NE(receipt.kernel, nullptr);
		return {std::numeric_limits<float>::quiet_NaN(), true};
	};
	const heroSpellProjection::OutcomeKernel unused = [](const auto &) { ADD_FAILURE() << "Unknown local result must remain unknown"; return 0; };
	const auto result = heroSpellProjection::evaluateLocalOutcome(value, {}, {}, false, {ids}, unused);
	EXPECT_TRUE(std::isnan(result));
	EXPECT_EQ(calls, 1);
}

TEST_F(MetamagicStochasticProjectionTest, DebuffRetryUsesCurrentProjectedResistanceWithoutLiveDraws)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::SLOW, "new-horizons:sorceryMagic"));
	const auto metamagic = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
	attackerSideHero->setSecSkillLevel(metamagic, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 50, BonusSourceID()));
	const auto worldBefore = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	const auto base = std::make_shared<HypotheticBattle>(environment.get(), live);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), base);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), base);
	state->addUnitBonus(enemy->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 20, BonusSourceID())});
	EXPECT_EQ(base->battleGetUnitByID(enemy->unitId())->magicResistance(), 50);
	EXPECT_EQ(sibling->battleGetUnitByID(enemy->unitId())->magicResistance(), 50);
	EXPECT_EQ(state->battleGetUnitByID(enemy->unitId())->magicResistance(), 70);
	const auto chance = [&](const auto & board)
	{
		spells::BattleCast cast(board.get(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::SLOW).toSpell());
		const auto mechanics = SpellID(SpellID::SLOW).toSpell()->battleMechanics(&cast);
		const auto * target = board->battleGetUnitByID(enemy->unitId());
		EXPECT_TRUE(mechanics->canBeCastAt({spells::Destination(target)}));
		return SpellTargetEvaluator::applicationChance(mechanics.get(), target);
	};
	// Success = (1-r) + r * .6 * (1-r): .65 at r=.5, .426 at r=.7.
	EXPECT_NEAR(chance(sibling), 0.65, 0.00001);
	EXPECT_NEAR(chance(state), 0.426, 0.00001);
	EXPECT_EQ(enemy->magicResistance(), 50);
	EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}

TEST_F(MetamagicStochasticProjectionTest, AdverseFirstCheckPrecedesOneNormalMetamagicRetry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::SLOW, "new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 50, BonusSourceID()));
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, {true, false});
	const auto worldBefore = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	auto state = std::make_shared<HypotheticBattle>(environment.get(), live);
	spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::SLOW).toSpell());
	const auto mechanics = SpellID(SpellID::SLOW).toSpell()->battleMechanics(&cast);
	// First success .5*.5; failure .75 can earn one .6*.5 normal retry.
	EXPECT_NEAR(SpellTargetEvaluator::applicationChance(mechanics.get(), state->battleGetUnitByID(enemy->unitId())),
		0.475, 0.00001);
	EXPECT_TRUE(state->getBattle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
	EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}
}
