/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/ConsecratedArmsValue.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"

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
};
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
