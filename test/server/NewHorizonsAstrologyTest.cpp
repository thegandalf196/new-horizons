/*
 * NewHorizonsAstrologyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 */
#include "StdInc.h"

#include "../../lib/gameState/NewHorizonsAstrology.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(NewHorizonsAstrologyWire, PreviewResultRoundTripsWithTheNewSaveFeature)
{
	NewTurn outgoing;
	outgoing.nextAstrologyWeek = {
		EWeekType::BONUS_GROWTH,
		CreatureID(3),
		7
	};

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	NewTurn incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.nextAstrologyWeek, outgoing.nextAstrologyWeek);
}

TEST(NewHorizonsAstrologyWire, OlderFormatsRejectAnAuthoredPreviewInsteadOfDiscardingIt)
{
	NewTurn outgoing;
	outgoing.nextAstrologyWeek = {
		EWeekType::PLAGUE,
		CreatureID::NONE,
		0
	};

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MYSTIC_POND_RESULTS;
	EXPECT_THROW(old.oser & outgoing, std::runtime_error);
}

TEST(NewHorizonsAstrologyState, FirstWeekIsOnlyTheUnknownSentinel)
{
	AstrologyWeek preview;
	EXPECT_FALSE(preview.known());

	preview.type = EWeekType::NORMAL;
	EXPECT_TRUE(preview.known());
}

TEST(NewHorizonsCastleGateWire, DailyUsageRoundTripsToTheClientMirror)
{
	SetNewHorizonsCastleGateState outgoing;
	outgoing.hid = ObjectInstanceID(42);
	outgoing.lastUseDay = 17;

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	SetNewHorizonsCastleGateState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.hid, outgoing.hid);
	EXPECT_EQ(incoming.lastUseDay, outgoing.lastUseDay);
}
