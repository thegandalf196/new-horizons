/*
 * SylvanLuckStatusPresentationTest.cpp, part of VCMI / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/battle/SylvanLuckState.h"

namespace
{
using namespace newHorizonsBattleStatus;

constexpr uint32_t SOURCE_UNIT_ID = 101;
constexpr uint32_t RECIPIENT_UNIT_ID = 202;
constexpr uint32_t OTHER_UNIT_ID = 303;

std::string tooltip(const SylvanLuckStackStatus & status)
{
	return sylvanLuckStackTooltip(status, "Sylvan Luck", "Serendipity", "Forest's Favor",
		"Shared Fortune", "Cascading Fortune", "Fortunate Aim");
}
}

TEST(SylvanLuckStatusPresentationTest, InactiveAndDisabledStateIsOmitted)
{
	const SylvanLuckState noTemporaryEffects;
	const auto inactive = makeSylvanLuckStackStatus(noTemporaryEffects, RECIPIENT_UNIT_ID, 0, true, false);
	EXPECT_TRUE(inactive.skillPresent);
	EXPECT_FALSE(inactive.active());
	EXPECT_TRUE(tooltip(inactive).empty());

	SylvanLuckState active;
	active.sharedFortune = true;
	active.sharedUnits.insert(RECIPIENT_UNIT_ID);
	const auto disabled = makeSylvanLuckStackStatus(active, RECIPIENT_UNIT_ID, 3, false, false);
	EXPECT_FALSE(disabled.skillPresent);
	EXPECT_FALSE(disabled.active());
	EXPECT_EQ(disabled.sharedFortuneLuck, 0);
	EXPECT_TRUE(tooltip(disabled).empty());
}

TEST(SylvanLuckStatusPresentationTest, PerStackLuckGiftUsesActualRecipientAndActivationExpiry)
{
	SylvanLuckState fortune;
	fortune.sharedFortune = true;
	EXPECT_FALSE(fortune.recordStrike(SOURCE_UNIT_ID, true, false));
	fortune.finishPositiveStrike({RECIPIENT_UNIT_ID}, false);

	const auto recipient = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 4, true, false);
	ASSERT_TRUE(recipient.active());
	EXPECT_EQ(recipient.effectiveCappedLuck, 4);
	EXPECT_EQ(recipient.sharedFortuneLuck, 1);
	EXPECT_EQ(recipient.cascadingFortuneLuck, 0);
	EXPECT_EQ(recipient.forestFavorSpeed, 0);
	const auto sharedTooltip = tooltip(recipient);
	EXPECT_NE(sharedTooltip.find("Shared Fortune"), std::string::npos);
	EXPECT_NE(sharedTooltip.find("+1 temporary Luck"), std::string::npos);
	EXPECT_NE(sharedTooltip.find("next activation"), std::string::npos);
	EXPECT_NE(sharedTooltip.find("ordinary attack: +4"), std::string::npos);

	const auto otherStack = makeSylvanLuckStackStatus(fortune, OTHER_UNIT_ID, 2, true, false);
	EXPECT_EQ(otherStack.sharedFortuneLuck, 0);
	EXPECT_EQ(otherStack.effectiveCappedLuck, 2);
	StackInfoStatusSnapshot giftSnapshot;
	giftSnapshot.sylvanLuck = recipient;

	// The gift remains through the recipient's current activation and is removed
	// when that stack begins its next activation.
	fortune.beginActivation(RECIPIENT_UNIT_ID, true);
	const auto expired = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 4, true, false);
	EXPECT_EQ(expired.sharedFortuneLuck, 0);
	StackInfoStatusSnapshot expiredSnapshot;
	expiredSnapshot.sylvanLuck = expired;
	EXPECT_NE(giftSnapshot, expiredSnapshot);
}

TEST(SylvanLuckStatusPresentationTest, FirstPositiveEligibilityAndForestFavorSpeedFollowStateTransitions)
{
	SylvanLuckState fortune;
	fortune.serendipity = true;
	fortune.forestsFavor = true;
	fortune.fortunateAim = true;

	const auto ready = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 5, true, true);
	EXPECT_EQ(ready.effectiveCappedLuck, 5);
	EXPECT_TRUE(ready.serendipityChanceOnlyReady);
	EXPECT_TRUE(ready.forestFavorReady);
	EXPECT_TRUE(ready.fortunateAimConditional);
	const auto readyTooltip = tooltip(ready);
	EXPECT_NE(readyTooltip.find("ordinary attack: +5"), std::string::npos);
	EXPECT_NE(readyTooltip.find("Serendipity"), std::string::npos);
	EXPECT_NE(readyTooltip.find("trigger chance only"), std::string::npos);
	EXPECT_NE(readyTooltip.find("Forest's Favor"), std::string::npos);
	EXPECT_NE(readyTooltip.find("remainder of that activation"), std::string::npos);
	EXPECT_NE(readyTooltip.find("Fortunate Aim"), std::string::npos);
	EXPECT_NE(readyTooltip.find("only when this shooter attacks the active Focus Fire target"), std::string::npos);

	const auto notFocusedTarget = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 5, true, false);
	EXPECT_FALSE(notFocusedTarget.fortunateAimConditional);

	EXPECT_FALSE(fortune.recordStrike(RECIPIENT_UNIT_ID, true, false));
	const auto afterPositive = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 5, true, true);
	EXPECT_FALSE(afterPositive.serendipityChanceOnlyReady);
	EXPECT_FALSE(afterPositive.forestFavorReady);
	EXPECT_EQ(afterPositive.forestFavorSpeed, 2);
	const auto speedTooltip = tooltip(afterPositive);
	EXPECT_NE(speedTooltip.find("+2 Speed"), std::string::npos);
	EXPECT_NE(speedTooltip.find("until the next creature activation begins"), std::string::npos);

	fortune.endActivation();
	const auto afterActivation = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 5, true, true);
	EXPECT_EQ(afterActivation.forestFavorSpeed, 0);
	EXPECT_FALSE(afterActivation.forestFavorReady);
}

TEST(SylvanLuckStatusPresentationTest, CascadingFortuneAppliesOnlyToTheNextFriendlyActivation)
{
	SylvanLuckState fortune;
	fortune.cascadingFortune = true;
	EXPECT_FALSE(fortune.recordStrike(SOURCE_UNIT_ID, true, false));
	fortune.finishPositiveStrike({}, true);
	EXPECT_TRUE(fortune.cascadingPending);

	fortune.beginActivation(RECIPIENT_UNIT_ID, true);
	const auto recipient = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 6, true, false);
	EXPECT_EQ(recipient.cascadingFortuneLuck, 3);
	EXPECT_NE(tooltip(recipient).find("Cascading Fortune"), std::string::npos);
	EXPECT_NE(tooltip(recipient).find("+3 temporary Luck"), std::string::npos);
	EXPECT_NE(tooltip(recipient).find("current activation"), std::string::npos);

	const auto otherStack = makeSylvanLuckStackStatus(fortune, OTHER_UNIT_ID, 1, true, false);
	EXPECT_EQ(otherStack.cascadingFortuneLuck, 0);

	fortune.endActivation();
	const auto expired = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 6, true, false);
	EXPECT_EQ(expired.cascadingFortuneLuck, 0);
	EXPECT_EQ(expired.forestFavorSpeed, 0);
}

TEST(SylvanLuckStatusPresentationTest, GamblerAndChainReadinessTrackAttackAndRoundTransitions)
{
	SylvanLuckState fortune;
	fortune.gambler = true;
	fortune.chainOfFortune = true;

	const auto beforeAttack = makeSylvanLuckStackStatus(fortune, SOURCE_UNIT_ID, 5, true, false);
	EXPECT_TRUE(beforeAttack.gamblerAttackReady);
	EXPECT_FALSE(beforeAttack.chainFortuneReady);
	const auto gamblerTooltip = tooltip(beforeAttack);
	EXPECT_NE(gamblerTooltip.find("Gambler"), std::string::npos);
	EXPECT_NE(gamblerTooltip.find("+3 attack Luck is ready"), std::string::npos);
	EXPECT_NE(gamblerTooltip.find("first attack this round"), std::string::npos);

	EXPECT_FALSE(fortune.recordStrike(SOURCE_UNIT_ID, true, false));
	const auto sourceAfterPositive = makeSylvanLuckStackStatus(fortune, SOURCE_UNIT_ID, 2, true, false);
	EXPECT_FALSE(sourceAfterPositive.gamblerAttackReady);
	EXPECT_FALSE(sourceAfterPositive.chainFortuneReady);

	const auto nextFriendlyStack = makeSylvanLuckStackStatus(fortune, RECIPIENT_UNIT_ID, 1, true, false);
	EXPECT_FALSE(nextFriendlyStack.gamblerAttackReady);
	EXPECT_TRUE(nextFriendlyStack.chainFortuneReady);
	const auto chainTooltip = tooltip(nextFriendlyStack);
	EXPECT_NE(chainTooltip.find("Chain of Fortune"), std::string::npos);
	EXPECT_NE(chainTooltip.find("different friendly stack's next attack"), std::string::npos);
	EXPECT_NE(chainTooltip.find("consumes the gift"), std::string::npos);

	// The next different stack's attack consumes the carried Chain of Fortune
	// benefit. Gambler becomes available again only at the next round boundary.
	EXPECT_FALSE(fortune.recordStrike(RECIPIENT_UNIT_ID, false, false));
	EXPECT_FALSE(makeSylvanLuckStackStatus(fortune, OTHER_UNIT_ID, 0, true, false).chainFortuneReady);
	fortune.nextRound();
	const auto nextRound = makeSylvanLuckStackStatus(fortune, SOURCE_UNIT_ID, 5, true, false);
	EXPECT_TRUE(nextRound.gamblerAttackReady);
	EXPECT_FALSE(nextRound.chainFortuneReady);
}
