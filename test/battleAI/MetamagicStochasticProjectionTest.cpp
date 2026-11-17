/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/HeroSpellOutcomeValue.h"
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
	Bonus blessing(BonusDuration::N_TURNS, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 4,
		BonusSourceID(SpellID(SpellID::BLESS)));
	blessing.turnsRemain = 3;
	blessing.spellCasterOwner = PlayerColor(1);
	effect(enemy, blessing);
	const auto health = enemy->getAvailableHealth();
	std::vector<float> ordinary, expert;
	ASSERT_NO_FATAL_FAILURE(score(0, ordinary));
	ASSERT_NO_FATAL_FAILURE(score(3, expert));
	ASSERT_EQ(ordinary.size(), expert.size());
	EXPECT_GT(*std::max_element(expert.begin(), expert.end()), *std::max_element(ordinary.begin(), ordinary.end()));
	EXPECT_EQ(enemy->getAvailableHealth(), health);
	EXPECT_EQ(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLESS))))->size(), 1u);
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
}
