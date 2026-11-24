/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsAcademySpecialties.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/HeroActionSelection.h"

namespace
{
constexpr auto ACADEMY_AI_ATTACKER = BattleSide::ATTACKER;
constexpr auto ACADEMY_AI_DEFENDER = BattleSide::DEFENDER;

/// The battle AI sees the revised Academy specialties through the shared rules it
/// already forecasts with: Hardened and Serpent's Embrace in its damage cache, and
/// Storm Lord and Practice Makes Perfect in its spell forecasts. No search branch.
class AcademySpecialtyAITest : public CastleSpecialtyTest
{
protected:
	void academyBoard()
	{
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	}

	int64_t academyCached(DamageCache & cache, const CStack * attacker, const CStack * defender)
	{
		return cache.getDamage(parent->battleGetUnitByID(attacker->unitId()), parent->battleGetUnitByID(defender->unitId()), parent);
	}

	/// Crusaders beside a large enemy stack; the weak Pikeman acts first.
	void academySpellArena()
	{
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ACADEMY_AI_ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
		actor = place(ACADEMY_AI_ATTACKER, "core:crusader", BattleHex(7, 5), 40, 800);
		waiter = place(ACADEMY_AI_ATTACKER, "core:swordsman", BattleHex(3, 5), 30, 600);
		enemy = place(ACADEMY_AI_DEFENDER, "core:halberdier", BattleHex(8, 5), 120, 400);
		ASSERT_TRUE(active && actor && waiter && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

	float academyBestSpellValue(SpellID spell)
	{
		struct Values final : heroActionSelection::Observer
		{
			std::vector<heroActionSelection::CandidateValue> rows;
			void candidate(heroActionSelection::CandidateValue value) noexcept override { rows.push_back(value); }
			void selected(heroActionSelection::SelectionValue) noexcept override {}
		} values;
		academyBoard();
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		DamageCache damage;
		damage.buildDamageCache(child, ACADEMY_AI_ATTACKER);
		BattleEvaluator evaluator(environment, callback, child, damage, active, PlayerColor(0), BattleID(0),
			ACADEMY_AI_ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		evaluator.attemptCastingSpell(active, true, &values);
		float best = std::numeric_limits<float>::lowest();
		std::string seen;
		for(const auto & row : values.rows)
		{
			seen += std::to_string(row.spell.getNum()) + "/" + std::to_string(static_cast<int>(row.command)) + " ";
			if(row.command == HeroCommand::NONE && row.spell == spell)
				best = std::max(best, row.value);
		}
		if(best == std::numeric_limits<float>::lowest())
			ADD_FAILURE() << "No candidate for spell " << spell.getNum() << "; candidates: " << seen;
		return best;
	}
};
}

TEST_F(AcademySpecialtyAITest, HardenedReachesTheAIDamageCacheAndAStaleEntryIsNotReused)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:neela"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * pikemen = place(ACADEMY_AI_ATTACKER, "core:pikeman", BattleHex(7, 5), 200, 300);
	auto * angels = place(ACADEMY_AI_DEFENDER, "core:angel", BattleHex(8, 5), 5, 1000);
	ASSERT_TRUE(pikemen && angels);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	academyBoard();
	DamageCache cache;
	cache.buildDamageCache(parent, ACADEMY_AI_DEFENDER);
	const auto plain = academyCached(cache, angels, pikemen);
	ASSERT_GT(plain, 0);
	// Two hits mark her stack with the level 1 total of 10%.
	SetStackEffect mark;
	mark.battleID = BattleID(0);
	auto marker = newHorizonsAcademy::hardenedMarker(attackerSideHero, *pikemen);
	ASSERT_TRUE(marker);
	marker->val = 100;
	mark.toAdd.emplace_back(pikemen->unitId(), std::vector<Bonus>{*marker});
	gameHandler->sendAndApply(mark);
	academyBoard();
	const auto hardened = academyCached(cache, angels, pikemen);
	EXPECT_NEAR(static_cast<double>(hardened) / plain, 0.9, 0.02) << "The marked stack is recomputed, not served stale";
}

TEST_F(AcademySpecialtyAITest, SerpentsEmbraceBoundDamageReachesTheAIDamageCache)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:fafner", 20));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * nagas = place(ACADEMY_AI_ATTACKER, "core:naga", BattleHex(7, 5), 10, 1000);
	auto * pikemen = place(ACADEMY_AI_DEFENDER, "core:pikeman", BattleHex(8, 5), 500, 300);
	ASSERT_TRUE(nagas && pikemen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	academyBoard();
	DamageCache cache;
	cache.buildDamageCache(parent, ACADEMY_AI_ATTACKER);
	const auto plain = academyCached(cache, nagas, pikemen);
	ASSERT_NO_FATAL_FAILURE(activate(nagas));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(nagas, pikemen, nagas->getPosition())));
	ASSERT_TRUE(pikemen->hasBonusOfType(BonusType::BIND_EFFECT));
	academyBoard();
	const auto bound = academyCached(cache, nagas, pikemen);
	EXPECT_NEAR(static_cast<double>(bound) / plain, 1.4, 0.05) << "+40% at level 20 against the target it holds";
}

TEST_F(AcademySpecialtyAITest, StormLordRaisesTheAIValueOfSolmyrsLightningBolt)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:solmyr"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::LIGHTNING_BOLT, "new-horizons:havocMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 400, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(academySpellArena());
	const float level1 = academyBestSpellValue(SpellID::LIGHTNING_BOLT);
	attackerSideHero->level = 20;
	const float level20 = academyBestSpellValue(SpellID::LIGHTNING_BOLT);
	ASSERT_GT(level1, 0.0f);
	EXPECT_GT(level20, level1) << "The forecast reads the stronger Spell Power term";
}

TEST_F(AcademySpecialtyAITest, PracticeRaisesTheAIValueOfAstralsPracticedSpell)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:astral", 30));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::LIGHTNING_BOLT, "new-horizons:havocMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 400, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(academySpellArena());
	const float unpracticed = academyBestSpellValue(SpellID::LIGHTNING_BOLT);
	newHorizonsHeroes::setSpellPractice(*attackerSideHero, SpellID(SpellID::LIGHTNING_BOLT), 60);
	const float practiced = academyBestSpellValue(SpellID::LIGHTNING_BOLT);
	ASSERT_GT(unpracticed, 0.0f);
	EXPECT_GT(practiced, unpracticed);
}
#endif
