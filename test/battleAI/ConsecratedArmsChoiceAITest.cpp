/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/ConsecratedArmsValue.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <array>
#include <span>

namespace heroQueueTest
{
namespace
{
using namespace consecratedArms;

class ConsecratedArmsChoiceAITest : public HeroQueueSelectorTest
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SKILL)),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		ASSERT_TRUE(capturePolicy(attackerSideHero).chosen);
	}

	BattleAction scoredCharge()
	{
		callback = std::make_shared<QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::ATTACKER);
		auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
		consecratedArmsAI::applyChosenPrayers(*live, parent, cache, action);
		return action;
	}

	/// A free and a blocked Marksman; only the blocked one will melee.
	void shooterArena()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		blocked = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(1, 1), 20);
		unblocked = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(1, 9), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(2, 1), 50);
		archer = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(5, 9), 50);
		ASSERT_TRUE(blocked && unblocked && enemy && archer);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	}
	CStack * blocked = nullptr;
	CStack * unblocked = nullptr;
	CStack * archer = nullptr;
};
}

TEST_F(ConsecratedArmsChoiceAITest, DauntlessAssaultIsWorthlessOnAShooterThatWillShoot)
{
	ASSERT_NO_FATAL_FAILURE(shooterArena());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto measured = consecratedArmsAI::measureRecipients(parent, BattleSide::ATTACKER,
		{blocked->unitId(), unblocked->unitId()}, cache);
	ASSERT_EQ(measured.size(), 2u);
	const auto & melee = measured[0].exposure;
	const auto & ranged = measured[1].exposure;
	// Blocked by the adjacent Pikemen: it melees, takes retaliation and strikes back.
	EXPECT_GT(melee.retaliation, 0);
	EXPECT_TRUE(melee.counterStrikes);
	// Free to shoot at the nearest Archers, which will shoot back rather than close in.
	EXPECT_GT(ranged.outgoing, 0);
	EXPECT_EQ(ranged.retaliation, 0);
	EXPECT_FALSE(ranged.counterStrikes);
	Policy chosen; chosen.rank = 1; chosen.chosen = true;
	const std::array dauntless{Prayer::DAUNTLESS_ASSAULT};
	EXPECT_FLOAT_EQ(consecratedArmsAI::chosenRecipientValue(chosen, 200, measured[1].before, ranged, dauntless), 0);
	EXPECT_GT(consecratedArmsAI::chosenRecipientValue(chosen, 200, measured[0].before, melee, dauntless), 0);
	const auto choice = consecratedArmsAI::bestChoice(chosen, 200, std::span(measured).last(1));
	ASSERT_TRUE(choice);
	EXPECT_NE(choice->prayers.front(), Prayer::DAUNTLESS_ASSAULT);
}

TEST_F(ConsecratedArmsChoiceAITest, ScoredChoiceIsLegalDeterministicAndAppliedByTheAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto first = scoredCharge();
	const auto again = scoredCharge();
	EXPECT_EQ(first.prayerRecipient, again.prayerRecipient);
	EXPECT_EQ(first.prayerChoices, again.prayerChoices);
	const auto request = choiceRequest(*battle(), BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_EQ(request.prayers, 1u);
	ASSERT_TRUE(std::ranges::binary_search(request.recipients, first.prayerRecipient));
	ASSERT_EQ(first.prayerChoices.size(), 1u);
	// The Angel deals by far the most damage, so it receives an offensive prayer.
	EXPECT_EQ(first.prayerRecipient, actor->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), first));
	EXPECT_TRUE(activePrayer(*actor, static_cast<Prayer>(first.prayerChoices.front())));
}

TEST_F(ConsecratedArmsChoiceAITest, ExistingChoiceAndRandomPrayerOrdersAreLeftAlone)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback = std::make_shared<QueueCallback>();
	callback->onBattleStarted(battle());
	live = callback->getBattle(BattleID(0));
	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	action.prayerChoices = {static_cast<uint8_t>(Prayer::UNBROKEN_VOW)};
	action.prayerRecipient = waiter->unitId();
	consecratedArmsAI::applyChosenPrayers(*live, nullptr, {}, action);
	EXPECT_EQ(action.prayerRecipient, waiter->unitId());
	EXPECT_EQ(action.prayerChoices, std::vector<uint8_t>{static_cast<uint8_t>(Prayer::UNBROKEN_VOW)});
	auto spell = BattleAction::makeDefend(active);
	consecratedArmsAI::applyChosenPrayers(*live, nullptr, {}, spell);
	EXPECT_TRUE(spell.prayerChoices.empty());
	EXPECT_EQ(spell.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
}
}
