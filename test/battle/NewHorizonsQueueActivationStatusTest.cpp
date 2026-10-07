/*
 * NewHorizonsQueueActivationStatusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsQueueActivationStatus.h"
#include "../../lib/battle/BattleUnitTurnReason.h"

namespace
{
using namespace newHorizonsQueueActivationStatus;
using Reason = BattleUnitTurnReason;

constexpr uint32_t activeUnit = 17;
constexpr int32_t currentRound = 3;

}

TEST(NewHorizonsQueueActivationStatusTest, ClassifiesImplementedExtraActivationOriginsOnly)
{
	EXPECT_EQ(classify(Reason::MORALE, false), Origin::MORALE);
	EXPECT_EQ(classify(Reason::REDUCED_EXTRA_ACTIVATION, false), Origin::QUARTERMASTER);
	EXPECT_EQ(classify(Reason::HERO_COMMAND, true), Origin::SECOND_WIND);
	EXPECT_EQ(classify(Reason::HERO_COMMAND, false), Origin::NORMAL);
	EXPECT_EQ(classify(Reason::TURN_QUEUE, true), Origin::NORMAL);
	EXPECT_EQ(classify(Reason::AUTOMATIC_ACTION, true), Origin::NORMAL);
	EXPECT_EQ(classify(Reason::HERO_SPELLCAST, false), Origin::NORMAL);
}

TEST(NewHorizonsQueueActivationStatusTest, SameActivationReturnsPreserveCurrentOrigin)
{
	const std::array continuations{
		Reason::HERO_SPELLCAST,
		Reason::UNIT_SPELLCAST,
		Reason::ACTION_REJECTED,
		Reason::HERO_COMMAND,
		Reason::MASTER_GATE_CONTINUATION,
		Reason::PURSUIT_CONTINUATION,
		Reason::RANGED_ATTACK_CONTINUATION};

	Status status;
	update(status, activeUnit, currentRound, Reason::MORALE, false);
	ASSERT_EQ(status.origin, Origin::MORALE);

	for(const auto reason : continuations)
	{
		EXPECT_TRUE(isSameActivationContinuation(reason));
		update(status, activeUnit, currentRound, reason, false);
		EXPECT_EQ(status.activeUnitId, activeUnit);
		EXPECT_EQ(status.round, currentRound);
		EXPECT_EQ(status.origin, Origin::MORALE) << "reason=" << static_cast<int>(reason);
	}

	update(status, activeUnit, currentRound, Reason::HERO_COMMAND, true);
	EXPECT_EQ(status.origin, Origin::SECOND_WIND)
		<< "Only a Hero Command with the actual active Second Wind recipient replaces the origin.";
}

TEST(NewHorizonsQueueActivationStatusTest, NewOrdinaryActivationRoundOrUnitClearsOldOrigin)
{
	Status status;
	update(status, activeUnit, currentRound, Reason::REDUCED_EXTRA_ACTIVATION, false);
	ASSERT_EQ(status.origin, Origin::QUARTERMASTER);

	update(status, activeUnit, currentRound, Reason::TURN_QUEUE, false);
	EXPECT_EQ(status.origin, Origin::NORMAL);

	update(status, activeUnit, currentRound, Reason::MORALE, false);
	ASSERT_EQ(status.origin, Origin::MORALE);
	update(status, activeUnit, currentRound, Reason::AUTOMATIC_ACTION, false);
	EXPECT_EQ(status.origin, Origin::NORMAL);

	update(status, activeUnit, currentRound, Reason::MORALE, false);
	ASSERT_EQ(status.origin, Origin::MORALE);
	update(status, activeUnit, currentRound + 1, Reason::TURN_QUEUE, false);
	EXPECT_EQ(status.round, currentRound + 1);
	EXPECT_EQ(status.origin, Origin::NORMAL);

	update(status, activeUnit + 1, currentRound + 1, Reason::RANGED_ATTACK_CONTINUATION, false);
	EXPECT_EQ(status.origin, Origin::NORMAL)
		<< "An ordinary different-unit queue activation must not inherit another unit's marker.";
}

TEST(NewHorizonsQueueActivationStatusTest, MarksOnlyTheMatchingCurrentQueueEntry)
{
	Status status;
	update(status, activeUnit, currentRound, Reason::MORALE, false);

	EXPECT_TRUE(marksCurrentEntry(status, activeUnit, 0));
	EXPECT_FALSE(marksCurrentEntry(status, activeUnit, 1));
	EXPECT_FALSE(marksCurrentEntry(status, activeUnit + 1, 0));

	update(status, activeUnit, currentRound, Reason::TURN_QUEUE, false);
	EXPECT_FALSE(marksCurrentEntry(status, activeUnit, 0));

	status = Status{};
	EXPECT_FALSE(marksCurrentEntry(status, activeUnit, 0));
}
