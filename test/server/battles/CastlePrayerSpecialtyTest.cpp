/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/CastleSpecialtyPrayers.h"

namespace
{
using namespace consecratedArms;
constexpr auto ATTACKER = BattleSide::ATTACKER;

/// Adela's Benediction of Arms and Loynis's Sermon grant chosen prayers.
class CastlePrayerSpecialtyTest : public CastleSpecialtyTest
{
};
}

TEST_F(CastlePrayerSpecialtyTest, BenedictionGivesTheNextOrderPrayerToTheBlessedStack)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adela", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::BLESS, waiter)));
	ASSERT_EQ(castleSpecialtyPrayers::benedictionRecipient(*battle(), ATTACKER), waiter->unitId());
	ASSERT_NO_FATAL_FAILURE(endRound());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto charge = withPrayer(BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE),
		Prayer::DIVINE_PURPOSE, actor->unitId());
	ASSERT_TRUE(submit(charge));
	const auto chosen = activePrayer(*actor, Prayer::DIVINE_PURPOSE);
	const auto blessed = activePrayer(*waiter, Prayer::DIVINE_PURPOSE);
	ASSERT_TRUE(chosen && blessed);
	// Level 10: +27% potency on the Benediction grant only.
	EXPECT_EQ(blessed->potencyFaith, newHorizonsHeroes::boostedPotencyFaith(chosen->potencyFaith, 27));
	EXPECT_FALSE(castleSpecialtyPrayers::benedictionRecipient(*battle(), ATTACKER)) << "Spent by that Order";
}

TEST_F(CastlePrayerSpecialtyTest, HistoricalAdelaHasNoBenediction)
{
	stripped = {"benedictionOfArms"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:adela", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::BLESS, waiter)));
	EXPECT_FALSE(castleSpecialtyPrayers::benedictionRecipient(*battle(), ATTACKER));
}

TEST_F(CastlePrayerSpecialtyTest, SermonLightSpellGrantsOneChosenPrayer)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:loynis", 20));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto bless = spellAt(ATTACKER, SpellID::BLESS, actor);
	const auto request = choiceRequest(*battle(), bless);
	ASSERT_EQ(request.prayers, 1u);
	ASSERT_TRUE(std::ranges::binary_search(request.recipients, waiter->unitId()));
	auto twoPrayers = withPrayer(bless, Prayer::UNBROKEN_VOW, waiter->unitId());
	twoPrayers.prayerChoices.push_back(static_cast<uint8_t>(Prayer::STEADFAST_SOUL));
	EXPECT_FALSE(submit(twoPrayers));
	auto recipientOnly = bless;
	recipientOnly.prayerRecipient = waiter->unitId();
	EXPECT_FALSE(submit(recipientOnly)) << "A partial choice is refused, not read as a skipped prayer";
	ASSERT_TRUE(submit(withPrayer(bless, Prayer::UNBROKEN_VOW, waiter->unitId())));
	const auto granted = activePrayer(*waiter, Prayer::UNBROKEN_VOW);
	ASSERT_TRUE(granted);
	const auto policy = capturePolicy(attackerSideHero);
	EXPECT_EQ(granted->potencyFaith, newHorizonsHeroes::boostedPotencyFaith(
		potencyFaith(actualFaith(*attackerSideHero), policy), 57));
}

TEST_F(CastlePrayerSpecialtyTest, SkippingSermonsPrayerStillCastsTheSpell)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:loynis", 20));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto bless = spellAt(ATTACKER, SpellID::BLESS, actor);
	ASSERT_EQ(choiceRequest(*battle(), bless).prayers, 1u);
	const int mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(submit(bless)) << "The spell is primary; its prayer is optional";
	EXPECT_TRUE(actor->hasBonusFrom(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(activePrayerCount(*actor), 0);
	EXPECT_EQ(activePrayerCount(*waiter), 0);
}

TEST_F(CastlePrayerSpecialtyTest, OrderWithASkippedPrayerStillSpendsBenediction)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adela", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::BLESS, waiter)));
	ASSERT_NO_FATAL_FAILURE(endRound());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE)));
	EXPECT_EQ(activePrayerCount(*actor), 0);
	EXPECT_EQ(activePrayerCount(*waiter), 0);
	EXPECT_FALSE(castleSpecialtyPrayers::benedictionRecipient(*battle(), ATTACKER)) << "That Order was the next one";
}

TEST_F(CastlePrayerSpecialtyTest, SermonIgnoresNonLightSpellsAndHistoricalLoynis)
{
	stripped = {"sermon"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:loynis", 20));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto bless = spellAt(ATTACKER, SpellID::BLESS, actor);
	EXPECT_EQ(choiceRequest(*battle(), bless).prayers, 0u);
	auto withChoice = bless;
	withChoice.prayerChoices = {0};
	withChoice.prayerRecipient = waiter->unitId();
	EXPECT_FALSE(submit(withChoice)) << "Without Sermon the spell carries no prayer";
	EXPECT_TRUE(submit(bless));
}
