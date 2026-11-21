/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../lib/battle/CastleSpecialtyPrayers.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/CastleSpecialtyValue.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/ConsecratedArmsValue.h"

namespace
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

/// The battle AI chooses the specialist's hero action with the real evaluator;
/// the authority must accept every choice (the 0.93.1 release gate).
class CastleSpecialtyAITest : public CastleSpecialtyTest
{
protected:
	/// The real queue selector on the live board; it records the submitted hero request.
	std::vector<BattleAction> heroChoice()
	{
		characterize(false);
		return callback ? callback->heroRequests : std::vector<BattleAction>{};
	}

	/// Exactly one chosen hero action, accepted by the authority.
	BattleAction acceptedChoice()
	{
		const auto chosen = heroChoice();
		EXPECT_EQ(chosen.size(), 1u);
		if(chosen.empty())
			return {};
		EXPECT_TRUE(submit(chosen.front())) << "hero action " << static_cast<int>(chosen.front().actionType);
		return chosen.front();
	}

	/// Crusaders with a damage range face a large Halberdier stack; the weak
	/// Pikeman acts first, so the hero's choice is made before anyone fights.
	void spellArena(bool armyActed = false)
	{
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
		actor = place(ATTACKER, "core:crusader", BattleHex(7, 5), 40, 800);
		waiter = place(ATTACKER, "core:swordsman", BattleHex(3, 5), 30, 600);
		enemy = place(DEFENDER, "core:halberdier", BattleHex(8, 5), 120, 400);
		ASSERT_TRUE(active && actor && waiter && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		// Once the army has acted this round, an Order has nothing left to command,
		// so the hero's choice is between the spells the specialty changes.
		if(armyActed)
			for(auto * unit : {actor, waiter})
			{
				ASSERT_NO_FATAL_FAILURE(activate(unit));
				ASSERT_TRUE(submit(BattleAction::makeDefend(unit)));
			}
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

	/// Every hero action the real selector values, without submitting any of them.
	std::vector<heroActionSelection::CandidateValue> candidateValues()
	{
		struct Values final : heroActionSelection::Observer
		{
			std::vector<heroActionSelection::CandidateValue> rows;
			void candidate(heroActionSelection::CandidateValue value) noexcept override { rows.push_back(value); }
			void selected(heroActionSelection::SelectionValue) noexcept override {}
		} values;
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		DamageCache damage;
		damage.buildDamageCache(child, ATTACKER);
		BattleEvaluator evaluator(environment, callback, child, damage, active, PlayerColor(0), BattleID(0), ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		evaluator.attemptCastingSpell(active, true, &values);
		return values.rows;
	}

	/// The best value the selector gives `spell` (lowest when it is not a candidate).
	float bestSpellValue(SpellID spell)
	{
		float best = std::numeric_limits<float>::lowest();
		for(const auto & row : candidateValues())
			if(row.command == HeroCommand::NONE && row.spell == spell)
				best = std::max(best, row.value);
		return best;
	}

	/// The decision board and damage cache the hero selector itself uses.
	void board()
	{
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		cache.buildDamageCache(parent, ATTACKER);
	}
	DamageCache cache;

	/// The fixture battle has no battle query, so a decided battle is not ended for us.
	bool decided() const
	{
		if(gameState()->currentBattles.empty())
			return true;
		for(const auto side : {ATTACKER, DEFENDER})
			if(battle()->battleGetUnitsIf([side](const battle::Unit * unit)
				{ return unit->unitSide() == side && unit->alive() && !unit->isGhost(); }).empty())
				return true;
		return false;
	}
};
}

TEST_F(CastleSpecialtyAITest, AdelaValuesBlessMoreForBenedictionAndHerBlessIsAccepted)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adela", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	const auto withBenediction = bestSpellValue(SpellID::BLESS);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:valeska")));
	const auto plain = bestSpellValue(SpellID::BLESS);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:adela")));
	ASSERT_GT(plain, std::numeric_limits<float>::lowest() / 2) << "Bless is a valued candidate";
	EXPECT_GT(withBenediction, plain) << "The later prayer on the Blessed stack is credited";
	// Whatever the selector prefers, its choice is accepted; a Bless marks its target.
	const auto action = acceptedChoice();
	if(action.actionType == EActionType::HERO_SPELL && action.spell == SpellID(SpellID::BLESS))
		EXPECT_TRUE(castleSpecialtyPrayers::benedictionRecipient(*battle(), ATTACKER));
}

TEST_F(CastleSpecialtyAITest, LoynisValuesHisLightSpellsAndCastsThemWithAChosenSermonPrayer)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:loynis", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	const auto withSermon = bestSpellValue(SpellID::BLESS);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:valeska")));
	const auto plain = bestSpellValue(SpellID::BLESS);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:loynis")));
	EXPECT_GT(withSermon, plain) << "A Light spell also brings a prayer";
	// The AI's own submission path fills the Sermon prayer and the authority accepts it.
	ASSERT_NO_FATAL_FAILURE(board());
	auto bless = spellAt(ATTACKER, SpellID::BLESS, actor);
	consecratedArmsAI::applyChosenPrayers(*live, parent, cache, bless);
	ASSERT_EQ(bless.prayerChoices.size(), 1u);
	ASSERT_TRUE(submit(bless));
	const auto * recipient = battle()->battleGetUnitByID(bless.prayerRecipient);
	ASSERT_NE(recipient, nullptr);
	EXPECT_TRUE(consecratedArms::activePrayer(*recipient, static_cast<consecratedArms::Prayer>(bless.prayerChoices.front())));
}
#endif
