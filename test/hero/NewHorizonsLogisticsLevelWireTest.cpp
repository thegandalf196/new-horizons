/* Part of VCMI; GPL v2 or later, see license.txt. */
#include "StdInc.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

// Supplemental, unregistered tests for the PRIVATE Logistics graph. Not a
// default ABI change or a substitute for the actual level/query journey.
TEST(NewHorizonsLogisticsLevelWire, CurrentPacketPreservesIndependentPreGainSnapshots)
{
	for(bool artillery : {false, true})
		for(bool logistics : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << "artillery=" << artillery << " logistics=" << logistics);
			HeroLevelUp outgoing;
			outgoing.heroId = ObjectInstanceID(7);
			outgoing.player = PlayerColor(0);
			outgoing.artilleryExpertBeforeGain = artillery;
			outgoing.logisticsExpertBeforeGain = logistics;
			outgoing.primaryGains = {4, 3, 2, 1};
			CMemorySerializer memory;
			memory.oser & outgoing;
			HeroLevelUp incoming;
			incoming.artilleryExpertBeforeGain = !artillery;
			incoming.logisticsExpertBeforeGain = !logistics;
			memory.iser & incoming;
			EXPECT_EQ(incoming.heroId, outgoing.heroId);
			EXPECT_EQ(incoming.player, outgoing.player);
			EXPECT_EQ(incoming.primaryGains, outgoing.primaryGains);
			EXPECT_EQ(incoming.artilleryExpertBeforeGain, artillery);
			EXPECT_EQ(incoming.logisticsExpertBeforeGain, logistics);
		}
}

TEST(NewHorizonsLogisticsLevelWire, OlderPacketClearsMissingLogisticsAndRejectsLosingTrueSnapshot)
{
	HeroLevelUp outgoing;
	outgoing.heroId = ObjectInstanceID(7);
	outgoing.player = PlayerColor(0);
	outgoing.artilleryExpertBeforeGain = true;
	outgoing.logisticsExpertBeforeGain = false;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	old.oser & outgoing;
	HeroLevelUp incoming;
	incoming.logisticsExpertBeforeGain = true;
	old.iser & incoming;
	EXPECT_TRUE(incoming.artilleryExpertBeforeGain);
	EXPECT_FALSE(incoming.logisticsExpertBeforeGain);
	outgoing.logisticsExpertBeforeGain = true;
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_CATEGORIES;
	EXPECT_THROW(rejected.oser & outgoing, std::runtime_error);
}
