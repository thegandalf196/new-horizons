/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsRampartSpecialties.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/RampartSpecialtyValue.h"

namespace
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

/// The battle AI sees and uses the revised Rampart specialties where it decides:
/// riders on Uland's Cure and Alagar's ice spells, Gem's free first spell, and the
/// shared damage rules (Giant-Slayer, Use the Ground) in its damage cache.
class RampartSpecialtyAITest : public CastleSpecialtyTest
{
protected:
	std::vector<heroActionSelection::CandidateValue> candidateValues()
	{
		struct Values final : heroActionSelection::Observer
		{
			std::vector<heroActionSelection::CandidateValue> rows;
			void candidate(heroActionSelection::CandidateValue value) noexcept override { rows.push_back(value); }
			void selected(heroActionSelection::SelectionValue) noexcept override {}
		} values;
		board();
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		DamageCache damage;
		damage.buildDamageCache(child, ATTACKER);
		BattleEvaluator evaluator(environment, callback, child, damage, active, PlayerColor(0), BattleID(0), ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		evaluator.attemptCastingSpell(active, true, &values);
		return values.rows;
	}

	float bestSpellValue(SpellID spell)
	{
		float best = std::numeric_limits<float>::lowest();
		for(const auto & row : candidateValues())
			if(row.command == HeroCommand::NONE && row.spell == spell)
				best = std::max(best, row.value);
		return best;
	}

	/// The live selector's submitted hero request, accepted by the authority.
	std::optional<BattleAction> acceptedChoice()
	{
		characterize(false);
		if(!callback || callback->heroRequests.empty())
			return std::nullopt;
		const auto chosen = callback->heroRequests.front();
		EXPECT_TRUE(submit(chosen));
		return chosen;
	}

	void board()
	{
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	}

	int64_t cachedDamage(const CStack * attacker, const CStack * defender)
	{
		board();
		DamageCache cache;
		cache.buildDamageCache(parent, attacker->unitSide());
		return cache.getDamage(parent->battleGetUnitByID(attacker->unitId()), parent->battleGetUnitByID(defender->unitId()), parent);
	}

	/// Crusaders beside a large enemy stack; the weak Pikeman acts first. Once the
	/// army has acted, an Order has nothing left to command.
	void spellArena(bool armyActed = false)
	{
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
		actor = place(ATTACKER, "core:crusader", BattleHex(7, 5), 40, 800);
		waiter = place(ATTACKER, "core:swordsman", BattleHex(3, 5), 30, 600);
		enemy = place(DEFENDER, "core:halberdier", BattleHex(8, 5), 120, 400);
		ASSERT_TRUE(active && actor && waiter && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		if(armyActed)
			for(auto * unit : {actor, waiter})
			{
				ASSERT_NO_FATAL_FAILURE(activate(unit));
				ASSERT_TRUE(submit(BattleAction::makeDefend(unit)));
			}
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}
};
}

TEST_F(RampartSpecialtyAITest, LivingRemedyRaisesTheValueOfUlandsCure)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:uland", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	injure(actor, actor->getAvailableHealth() - 300);
	EXPECT_GT(rampartSpecialtyAI::livingRemedyValue(*battle(), ATTACKER, actor->unitId()), 0.0f);
	const auto withRemedy = bestSpellValue(SpellID::CURE);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:clancy")));
	const auto ordinary = bestSpellValue(SpellID::CURE);
	ASSERT_GT(ordinary, std::numeric_limits<float>::lowest());
	EXPECT_GT(withRemedy, ordinary) << "The Regeneration rider adds to the Cure's own value";
}

TEST_F(RampartSpecialtyAITest, BrittleRaisesTheValueOfAlagarsIceBoltOnADefendingStack)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
	actor = place(ATTACKER, "core:crusader", BattleHex(7, 5), 40, 800);
	enemy = place(DEFENDER, "core:halberdier", BattleHex(8, 5), 120, 1100);
	ASSERT_TRUE(active && actor && enemy);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(submit(BattleAction::makeDefend(enemy), PlayerColor(1)));
	ASSERT_TRUE(enemy->defended());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	board();
	DamageCache damage;
	damage.buildDamageCache(parent, ATTACKER);
	EXPECT_GT(rampartSpecialtyAI::brittleValue(*battle(), parent, ATTACKER, enemy->unitId(), damage), 0.0f);
	const auto withBrittle = bestSpellValue(SpellID::ICE_BOLT);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:clancy")));
	const auto ordinary = bestSpellValue(SpellID::ICE_BOLT);
	ASSERT_GT(ordinary, std::numeric_limits<float>::lowest());
	EXPECT_GT(withBrittle, ordinary);
}

TEST_F(RampartSpecialtyAITest, GemCastsHerFreeFirstSpellWithoutMana)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:gem", 5));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	setTestSpellPointTotal(attackerSideHero, 0);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
	actor = place(ATTACKER, "core:crusader", BattleHex(7, 5), 40, 800);
	enemy = place(DEFENDER, "core:halberdier", BattleHex(8, 5), 120, 400);
	auto * angels = place(DEFENDER, "core:angel", BattleHex(14, 9), 3, 300);
	ASSERT_TRUE(active && actor && enemy && angels);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(actor));
	ASSERT_TRUE(submit(BattleAction::makeDefend(actor)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_LE(battle()->battleGetSpellLevel(SpellID::ICE_BOLT), 2);
	ASSERT_EQ(battle()->battleGetSpellCost(SpellID(SpellID::ICE_BOLT).toSpell(), attackerSideHero), 0);
	EXPECT_GT(bestSpellValue(SpellID::ICE_BOLT), 0.0f) << "The free first spell is a candidate even with no Mana";
	const auto chosen = acceptedChoice();
	ASSERT_TRUE(chosen);
	EXPECT_EQ(chosen->actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(chosen->spell, SpellID(SpellID::ICE_BOLT));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
}

TEST_F(RampartSpecialtyAITest, WithoutSorceressNoManaMeansNoSpellCandidate)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:clancy", 5));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	setTestSpellPointTotal(attackerSideHero, 0);
	ASSERT_NO_FATAL_FAILURE(spellArena(true));
	EXPECT_EQ(bestSpellValue(SpellID::ICE_BOLT), std::numeric_limits<float>::lowest());
}

TEST_F(RampartSpecialtyAITest, GiantSlayerAndUseTheGroundReachTheAIDamageCache)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:coronius"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 5), 30, 1000);
	auto * angels = place(DEFENDER, "core:angel", BattleHex(6, 5), 5, 400);
	ASSERT_TRUE(centaurs && angels);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto boosted = cachedDamage(centaurs, angels);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:clancy")));
	const auto plain = cachedDamage(centaurs, angels);
	ASSERT_GT(plain, 0);
	EXPECT_NEAR(static_cast<double>(boosted) / plain, 1.2, 0.05) << "Target choice sees Giant-Slayer";
	// Mephala: the cache bypasses its pair entries while her position rule can change them.
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:mephala")));
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*battle())) << "No obstacle, nothing to track";
}

TEST_F(RampartSpecialtyAITest, AgainstTheOddsLuckIsVisibleToTheAI)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:melodia", 20));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 5), 10, 1000);
	ASSERT_TRUE(centaurs && place(DEFENDER, "core:angel", BattleHex(12, 5), 10, 400));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	board();
	const auto * seen = parent->battleGetUnitByID(centaurs->unitId());
	ASSERT_NE(seen, nullptr);
	EXPECT_EQ(seen->valOfBonuses(Selector::type()(BonusType::LUCK).And(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == newHorizonsRampart::AGAINST_THE_ODDS_STACKING; }))), 3);
}
#endif
