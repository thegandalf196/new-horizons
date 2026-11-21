/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/ConsecratedArmsPrayerChoice.h"
#include "../../client/battle/CurrentFactionSkillPresentation.h"

namespace
{
using consecratedArms::Prayer;
using consecratedArmsPrayerChoice::Selection;

consecratedArms::ChoiceRequest request(size_t prayers, std::vector<uint32_t> recipients)
{
	consecratedArms::ChoiceRequest result;
	result.prayers = prayers;
	result.recipients = std::move(recipients);
	return result;
}
}

TEST(ConsecratedArmsPrayerChoice, SinglePrayerIsARadioChoiceAndRecipientFollowsIt)
{
	Selection selection(1, {3, 7, 9});
	EXPECT_FALSE(selection.chosenRecipient());
	EXPECT_EQ(consecratedArmsPrayerChoice::promptText(selection), "Choose one prayer.");
	EXPECT_FALSE(selection.chooseRecipient(7)); // Prayer first, then recipient.
	ASSERT_TRUE(selection.togglePrayer(Prayer::DIVINE_PURPOSE));
	ASSERT_TRUE(selection.togglePrayer(Prayer::UNBROKEN_VOW));
	EXPECT_EQ(selection.chosenPrayers(), std::vector<Prayer>{Prayer::UNBROKEN_VOW});
	EXPECT_FALSE(selection.complete());
	EXPECT_EQ(consecratedArmsPrayerChoice::promptText(selection), "Choose the stack that receives the prayer.");
	EXPECT_FALSE(selection.chooseRecipient(8)); // Not an Order recipient.
	ASSERT_TRUE(selection.chooseRecipient(7));
	EXPECT_TRUE(selection.complete());

	BattleAction action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(selection.apply(action, request(1, {3, 7, 9})));
	EXPECT_EQ(action.prayerChoices, std::vector<uint8_t>{static_cast<uint8_t>(Prayer::UNBROKEN_VOW)});
	EXPECT_EQ(action.prayerRecipient, 7u);
}

TEST(ConsecratedArmsPrayerChoice, AntiphonalNeedsTwoDifferentPrayersAndBlocksAThird)
{
	Selection selection(2, {4, 5});
	ASSERT_TRUE(selection.togglePrayer(Prayer::STEADFAST_SOUL));
	EXPECT_EQ(consecratedArmsPrayerChoice::promptText(selection), "Choose one more prayer.");
	ASSERT_TRUE(selection.togglePrayer(Prayer::HEAVENS_JUDGMENT));
	EXPECT_TRUE(selection.prayersComplete());
	EXPECT_FALSE(selection.canToggle(Prayer::DIVINE_PURPOSE));
	EXPECT_FALSE(selection.togglePrayer(Prayer::DIVINE_PURPOSE));
	EXPECT_TRUE(selection.togglePrayer(Prayer::STEADFAST_SOUL)); // Deselect frees a slot.
	EXPECT_FALSE(selection.prayersComplete());
	ASSERT_TRUE(selection.togglePrayer(Prayer::DAUNTLESS_ASSAULT));
	EXPECT_EQ(consecratedArmsPrayerChoice::promptText(selection), "Choose the stack that receives the prayers.");
	ASSERT_TRUE(selection.chooseRecipient(5));
	EXPECT_EQ(consecratedArmsPrayerChoice::promptText(selection), "Ready. Confirm to issue the Order.");
	BattleAction action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(selection.apply(action, request(2, {4, 5})));
	EXPECT_EQ(action.prayerChoices, (std::vector<uint8_t>{static_cast<uint8_t>(Prayer::HEAVENS_JUDGMENT),
		static_cast<uint8_t>(Prayer::DAUNTLESS_ASSAULT)}));
}

TEST(ConsecratedArmsPrayerChoice, SingleRecipientIsPreselectedAndStaleRequestsAreRefused)
{
	Selection selection(1, {11});
	EXPECT_EQ(selection.chosenRecipient(), std::optional<uint32_t>(11));
	ASSERT_TRUE(selection.togglePrayer(Prayer::VICARIOUS_REDEMPTION));
	BattleAction action = BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::BRACE);
	// The recipient died, the Order became illegal, or the policy changed.
	EXPECT_FALSE(selection.apply(action, request(1, {12})));
	EXPECT_FALSE(selection.apply(action, request(0, {})));
	EXPECT_FALSE(selection.apply(action, request(2, {11})));
	EXPECT_TRUE(action.prayerChoices.empty());
	EXPECT_EQ(action.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
}

TEST(ConsecratedArmsPrayerChoice, SkillReadoutDescribesChosenOrRandomPrayersByCapturedPolicy)
{
	using passiveFactionSkillPresentation::consecratedText;
	consecratedArms::Policy chosen{1, 0, 0, 0};
	chosen.chosen = true;
	const auto chosenText = consecratedText(chosen, 100);
	ASSERT_TRUE(chosenText);
	EXPECT_NE(chosenText->details.find("one prayer of your choice to one Order recipient you choose"), std::string::npos);
	EXPECT_EQ(chosenText->details.find("random"), std::string::npos);
	const auto randomText = consecratedText(consecratedArms::Policy{1, 0, 0, 0}, 100);
	ASSERT_TRUE(randomText);
	EXPECT_NE(randomText->details.find("one random prayer to its recipients"), std::string::npos);
	EXPECT_EQ(passiveFactionSkillPresentation::prayerSummary(Prayer::UNBROKEN_VOW),
		std::string("Limits casualties from a single hit."));
}
