/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../lib/battle/CastleSpecialtyPrayers.h"
#include "../../lib/battle/NewHorizonsFrostMantle.h"
#include "../../lib/battle/QuickStudySpecialty.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/CastleSpecialtyValue.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/ConsecratedArmsValue.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include <chrono>

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

	/// The live hero decision (CBattleAI::attemptHeroAction): the chosen search
	/// profile and its candidate shortlist. Returns the submitted hero requests.
	std::vector<BattleAction> liveHeroChoice(battleSearch::Profile profile, double * milliseconds = nullptr)
	{
		const battleSearch::Scope search(profile);
		heroDecisionPressure::Experiment shortlist;
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
		const auto start = std::chrono::steady_clock::now();
		evaluator.attemptCastingSpell(active, true, nullptr, nullptr, nullptr, nullptr, &shortlist);
		if(milliseconds)
			*milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return callback->heroRequests;
	}

	/// Orrin's level-1 army facing two Ogre stacks, as in the beta AI-versus-AI map:
	/// the Zealots act first, so every shooter is still ready.
	void ogreField()
	{
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ATTACKER, "core:zealot", BattleHex(2, 5), 8, 1000);
		auto * marksmen = place(ATTACKER, "core:marksman", BattleHex(1, 1), 40, 900);
		auto * monks = place(ATTACKER, "core:monk", BattleHex(1, 4), 12, 800);
		auto * archers = place(ATTACKER, "core:archer", BattleHex(1, 7), 40, 700);
		auto * pikemen = place(ATTACKER, "core:pikeman", BattleHex(1, 9), 20, 600);
		enemy = place(DEFENDER, "core:ogre", BattleHex(15, 3), 38, 500);
		auto * second = place(DEFENDER, "core:ogre", BattleHex(15, 7), 21, 400);
		ASSERT_TRUE(active && marksmen && monks && archers && pikemen && enemy && second);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

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

TEST_F(CastleSpecialtyAITest, OrrinIssuesFocusFireWhileHisShootersAreReady)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:orrin", 10));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(2, 5), 5, 1000);
	auto * archers = place(ATTACKER, "core:archer", BattleHex(1, 3), 40, 800);
	auto * marksmen = place(ATTACKER, "core:marksman", BattleHex(1, 7), 40, 700);
	enemy = place(DEFENDER, "core:swordsman", BattleHex(14, 5), 60, 600);
	ASSERT_TRUE(active && archers && marksmen && enemy);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto health = enemy->getAvailableHealth();
	const auto shots = archers->shots.available();
	const auto action = acceptedChoice();
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(action.command, HeroCommand::FOCUS_FIRE);
	if(action.command == HeroCommand::FOCUS_FIRE)
	{
		EXPECT_LT(enemy->getAvailableHealth(), health) << "The volley fires at once";
		EXPECT_EQ(archers->shots.available(), shots - 1);
	}
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

TEST_F(CastleSpecialtyAITest, SanyaConsidersTheSpellHerOpponentCastAndCanCastIt)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:sanya", 10, false));
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	EXPECT_EQ(bestSpellValue(SpellID::BLESS), std::numeric_limits<float>::lowest()) << "Not seen yet";
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(spellAt(DEFENDER, SpellID::BLESS, enemy), PlayerColor(1)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	EXPECT_GT(bestSpellValue(SpellID::BLESS), std::numeric_limits<float>::lowest() / 2) << "The seen spell is a valued candidate";
	const auto action = acceptedChoice();
	if(action.actionType == EActionType::HERO_SPELL && action.spell == SpellID(SpellID::BLESS))
		EXPECT_EQ(quickStudySpecialty::spentCopies(*battle(), *attackerSideHero), 1);
}

TEST_F(CastleSpecialtyAITest, AdelaideRingsAFriendlyStackInFrostMantle)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide", 10, false));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	const auto action = acceptedChoice();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL) << "command " << static_cast<int>(action.command);
	EXPECT_EQ(action.spell, SpellID(SpellID::FROST_RING));
	const auto target = action.getTarget(battle());
	ASSERT_FALSE(target.empty());
	const auto * centre = battle()->battleGetUnitByPos(target.front().hexValue, true);
	ASSERT_NE(centre, nullptr) << "The ring is centred on a stack";
	EXPECT_EQ(centre->unitSide(), ATTACKER) << "A friendly centre is safe and gains the cloak";
	EXPECT_TRUE(centre->hasBonus(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == newHorizonsFrostMantle::CLOAK_STACKING; })));
}

TEST_F(CastleSpecialtyAITest, CuthbertCastsHisStrengthenedShadowSpell)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert", 10, false));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID(SpellID::decode("new-horizons:lifeDrain")),
		"new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	const auto action = acceptedChoice();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL) << "command " << static_cast<int>(action.command);
	EXPECT_EQ(action.spell, SpellID(SpellID::decode("new-horizons:lifeDrain")));
}

TEST_F(CastleSpecialtyAITest, EdricBattleAIActionsAreAcceptedWhileGriffinsCounterShots)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:edric", 10));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * griffins = place(ATTACKER, "core:griffin", BattleHex(1, 5), 30, 100);
	ASSERT_NE(place(ATTACKER, "core:pikeman", BattleHex(2, 2), 10, 200), nullptr);
	auto * archers = place(DEFENDER, "core:archer", BattleHex(15, 3), 30, 900);
	auto * marksmen = place(DEFENDER, "core:marksman", BattleHex(15, 7), 20, 800);
	ASSERT_TRUE(griffins && archers && marksmen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto shooterHealth = archers->getAvailableHealth() + marksmen->getAvailableHealth();
	for(int activation = 0; activation < 16 && !decided(); ++activation)
	{
		const auto * unit = battle()->battleActiveUnit();
		ASSERT_NE(unit, nullptr);
		const auto side = unit->unitSide();
		const auto player = battle()->sideToPlayer(side);
		auto queue = std::make_shared<heroQueueTest::QueueCallback>();
		queue->onBattleStarted(battle());
		auto world = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		const auto * stack = battle()->battleGetStackByID(unit->unitId());
		ASSERT_NE(stack, nullptr);
		BattleAction action;
		{
			BattleEvaluator evaluator(world, queue, stack, player, BattleID(0), side, 1.0f, 2);
			ASSERT_NO_THROW(action = evaluator.selectStackAction(stack));
		}
		ASSERT_TRUE(submit(action, player)) << "activation " << activation << " action " << static_cast<int>(action.actionType);
	}
	const auto after = (archers->alive() ? archers->getAvailableHealth() : 0) + (marksmen->alive() ? marksmen->getAvailableHealth() : 0);
	EXPECT_LT(after, shooterHealth) << "Shots at the Griffins are answered by flying counters";
}
TEST_F(CastleSpecialtyAITest, SpecialtyRidersValueTheirTargetsAndOnlyForTheirHero)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adela", 10));
	ASSERT_NO_FATAL_FAILURE(spellArena());
	ASSERT_NO_FATAL_FAILURE(board());
	// Benediction: the engaged Crusaders gain more from a later prayer than the idle Swordsmen.
	const auto engaged = castleSpecialtyAI::benedictionValue(*live, parent, ATTACKER, actor->unitId(), cache);
	const auto idle = castleSpecialtyAI::benedictionValue(*live, parent, ATTACKER, waiter->unitId(), cache);
	EXPECT_GT(engaged, 0.0f);
	EXPECT_GT(idle, 0.0f);
	EXPECT_EQ(castleSpecialtyAI::sermonValue(*live, parent, ATTACKER, cache), 0.0f) << "Sermon is Loynis's";
	EXPECT_EQ(castleSpecialtyAI::frostMantleValue(*live, parent, ATTACKER, actor->unitId(), cache), 0.0f);
	EXPECT_EQ(castleSpecialtyAI::volleyValue(*live, parent, ATTACKER, enemy->unitId(), 10, cache), 0.0f);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:loynis")));
	EXPECT_GT(castleSpecialtyAI::sermonValue(*live, parent, ATTACKER, cache), 0.0f);
	EXPECT_EQ(castleSpecialtyAI::benedictionValue(*live, parent, ATTACKER, actor->unitId(), cache), 0.0f);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:adelaide")));
	EXPECT_GT(castleSpecialtyAI::frostMantleValue(*live, parent, ATTACKER, actor->unitId(), cache), 0.0f)
		<< "The Halberdiers will strike the cloaked Crusaders";
	EXPECT_EQ(castleSpecialtyAI::frostMantleValue(*live, parent, ATTACKER, enemy->unitId(), cache), 0.0f);
}

TEST_F(CastleSpecialtyAITest, OrrinsVolleyIsWorthMoreBeforeHisShootersAct)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:orrin", 10));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(2, 5), 5, 1000);
	auto * archers = place(ATTACKER, "core:archer", BattleHex(1, 3), 40, 800);
	enemy = place(DEFENDER, "core:swordsman", BattleHex(14, 5), 60, 600);
	ASSERT_TRUE(active && archers && enemy);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_NO_FATAL_FAILURE(board());
	const auto ready = castleSpecialtyAI::volleyValue(*live, parent, ATTACKER, enemy->unitId(), 10, cache);
	EXPECT_GT(ready, 0.0f);
	// Once the archers have shot, issuing Focus Fire! brings no volley.
	ASSERT_NO_FATAL_FAILURE(activate(archers));
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(archers, enemy)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_NO_FATAL_FAILURE(board());
	EXPECT_EQ(castleSpecialtyAI::volleyValue(*live, parent, ATTACKER, enemy->unitId(), 10, cache), 0.0f);
}

TEST_F(CastleSpecialtyAITest, OrrinFocusesFireAgainstSeveralStacksInEverySearchProfile)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:orrin"));
	ASSERT_NO_FATAL_FAILURE(ogreField());
	// Quick search refines one unscouted Order (Charge) plus the best scouted ones;
	// the volley must rank Focus Fire! among those, not leave it unevaluated.
	for(const auto profile : {battleSearch::Profile::BeamOneTwoActors, battleSearch::Profile::Full})
	{
		// Rough cost of the whole hero decision: the fastest of three identical runs.
		double milliseconds = std::numeric_limits<double>::max();
		std::vector<BattleAction> chosen;
		for(int run = 0; run < 3; ++run)
		{
			double elapsed = 0;
			chosen = liveHeroChoice(profile, &elapsed);
			milliseconds = std::min(milliseconds, elapsed);
		}
		RecordProperty(profile == battleSearch::Profile::Full ? "fullMs" : "quickMs", std::to_string(milliseconds));
		std::cout << "Orrin hero decision " << (profile == battleSearch::Profile::Full ? "Full" : "Quick")
			<< ": " << milliseconds << " ms\n";
		ASSERT_EQ(chosen.size(), 1u);
		EXPECT_EQ(chosen.front().actionType, EActionType::HERO_COMMAND);
		EXPECT_EQ(chosen.front().command, HeroCommand::FOCUS_FIRE) << "command " << static_cast<int>(chosen.front().command);
	}
	const auto action = liveHeroChoice(battleSearch::Profile::BeamOneTwoActors);
	ASSERT_EQ(action.size(), 1u);
	const auto health = enemy->getAvailableHealth() + battle()->battleGetUnitByPos(BattleHex(15, 7), true)->getAvailableHealth();
	ASSERT_TRUE(submit(action.front()));
	const auto after = enemy->getAvailableHealth() + battle()->battleGetUnitByPos(BattleHex(15, 7), true)->getAvailableHealth();
	EXPECT_LT(after, health) << "The volley fires at once";
}

TEST_F(CastleSpecialtyAITest, OrdinaryHeroOrdersAreAcceptedWithoutAVolley)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:valeska"));
	ASSERT_NO_FATAL_FAILURE(ogreField());
	ASSERT_NO_FATAL_FAILURE(board());
	EXPECT_EQ(castleSpecialtyAI::volleyValue(*live, parent, ATTACKER, enemy->unitId(), 10, cache), 0.0f);
	const auto chosen = liveHeroChoice(battleSearch::Profile::BeamOneTwoActors);
	ASSERT_EQ(chosen.size(), 1u);
	std::cout << "Ordinary hero order " << static_cast<int>(chosen.front().command) << "\n";
	EXPECT_TRUE(submit(chosen.front()));
}

TEST_F(CastleSpecialtyAITest, AdelaideCloaksAnIsolatedThreatenedStack)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide", 10, false));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	// Ice Elementals shrug off Frost Ring, so no ring can hurt them; their charge
	// at the lone Crusaders is what the cloak answers (or the chill slows).
	active = place(ATTACKER, "core:pikeman", BattleHex(1, 1), 1, 1000);
	actor = place(ATTACKER, "core:crusader", BattleHex(5, 5), 20, 800);
	enemy = place(DEFENDER, "core:iceElemental", BattleHex(9, 5), 30, 400);
	ASSERT_TRUE(active && actor && enemy);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	for(const auto profile : {battleSearch::Profile::BeamOneTwoActors, battleSearch::Profile::Full})
	{
		const auto chosen = liveHeroChoice(profile);
		ASSERT_EQ(chosen.size(), 1u);
		ASSERT_EQ(chosen.front().actionType, EActionType::HERO_SPELL) << "command " << static_cast<int>(chosen.front().command);
		EXPECT_EQ(chosen.front().spell, SpellID(SpellID::FROST_RING));
	}
	const auto action = acceptedChoice();
	ASSERT_EQ(action.spell, SpellID(SpellID::FROST_RING));
	const auto * centre = battle()->battleGetUnitByPos(action.getTarget(battle()).front().hexValue, true);
	ASSERT_NE(centre, nullptr) << "The ring is centred on a stack";
	EXPECT_EQ(centre, actor) << "The threatened Crusaders take the cloak";
	EXPECT_TRUE(actor->hasBonus(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == newHorizonsFrostMantle::CLOAK_STACKING; })));
}
#endif
