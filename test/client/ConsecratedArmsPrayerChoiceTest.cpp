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
	EXPECT_FALSE(selection.chooseRecipient(7)); // Prayer first, then recipient.
	ASSERT_TRUE(selection.togglePrayer(Prayer::DIVINE_PURPOSE));
	ASSERT_TRUE(selection.togglePrayer(Prayer::UNBROKEN_VOW));
	EXPECT_EQ(selection.chosenPrayers(), std::vector<Prayer>{Prayer::UNBROKEN_VOW});
	EXPECT_FALSE(selection.complete());
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
	ASSERT_TRUE(selection.togglePrayer(Prayer::HEAVENS_JUDGMENT));
	EXPECT_TRUE(selection.prayersComplete());
	EXPECT_FALSE(selection.canToggle(Prayer::DIVINE_PURPOSE));
	EXPECT_FALSE(selection.togglePrayer(Prayer::DIVINE_PURPOSE));
	EXPECT_TRUE(selection.togglePrayer(Prayer::STEADFAST_SOUL)); // Deselect frees a slot.
	EXPECT_FALSE(selection.prayersComplete());
	ASSERT_TRUE(selection.togglePrayer(Prayer::DAUNTLESS_ASSAULT));
	ASSERT_TRUE(selection.chooseRecipient(5));
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

TEST(ConsecratedArmsPrayerChoice, SkippedPrayerSendsThePlainAction)
{
	BattleAction action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::RIPOSTE);
	action.prayerChoices = {3};
	action.prayerRecipient = 9;
	const auto plain = consecratedArmsPrayerChoice::withoutPrayer(action);
	EXPECT_TRUE(plain.prayerChoices.empty());
	EXPECT_EQ(plain.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
	EXPECT_EQ(plain.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(plain.command, HeroCommand::RIPOSTE);
}

TEST(ConsecratedArmsPrayerChoice, RecipientClickGrantsOnlyToAFreshlyEligibleStack)
{
	using consecratedArmsPrayerChoice::RecipientClick;
	Selection chosen(1, {3, 7, 9});
	ASSERT_TRUE(chosen.togglePrayer(Prayer::HEAVENS_JUDGMENT));
	const auto plain = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);

	auto action = plain;
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 8, request(1, {3, 7, 9}), action),
		RecipientClick::REFUSE); // Not a recipient of the Order.
	EXPECT_TRUE(action.prayerChoices.empty());
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 7, request(1, {3, 9}), action),
		RecipientClick::REFUSE); // Died or left since the prayer was chosen.
	EXPECT_EQ(action.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
	auto summoned = plain;
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 12, request(1, {3, 12}), summoned),
		RecipientClick::SEND_WITH_PRAYER); // Became a recipient since then.
	EXPECT_EQ(summoned.prayerRecipient, 12u);
	ASSERT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 9, request(1, {3, 7, 9}), action),
		RecipientClick::SEND_WITH_PRAYER);
	EXPECT_EQ(action.prayerChoices, std::vector<uint8_t>{static_cast<uint8_t>(Prayer::HEAVENS_JUDGMENT)});
	EXPECT_EQ(action.prayerRecipient, 9u);

	// Nobody can receive it any more, or the Order changed: the Order goes ahead without it.
	action = plain;
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 7, request(1, {}), action),
		RecipientClick::SEND_WITHOUT_PRAYER);
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 7, request(0, {}), action),
		RecipientClick::SEND_WITHOUT_PRAYER);
	EXPECT_TRUE(action.prayerChoices.empty());
	EXPECT_EQ(action.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
}

TEST(ConsecratedArmsPrayerChoice, AntiphonalRecipientClickNeedsBothPrayers)
{
	using consecratedArmsPrayerChoice::RecipientClick;
	Selection chosen(2, {4, 5});
	ASSERT_TRUE(chosen.togglePrayer(Prayer::DIVINE_PURPOSE));
	auto action = BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::BRACE);
	EXPECT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 5, request(2, {4, 5}), action),
		RecipientClick::SEND_WITHOUT_PRAYER);
	ASSERT_TRUE(chosen.togglePrayer(Prayer::STEADFAST_SOUL));
	ASSERT_EQ(consecratedArmsPrayerChoice::resolveRecipientClick(chosen, 5, request(2, {4, 5}), action),
		RecipientClick::SEND_WITH_PRAYER);
	EXPECT_EQ(action.prayerChoices, (std::vector<uint8_t>{static_cast<uint8_t>(Prayer::DIVINE_PURPOSE),
		static_cast<uint8_t>(Prayer::STEADFAST_SOUL)}));
	EXPECT_EQ(action.prayerRecipient, 5u);
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
