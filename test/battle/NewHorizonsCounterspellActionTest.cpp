/*
 * NewHorizonsCounterspellActionTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../../lib/battle/SideInBattle.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMagic.h"

TEST(NewHorizonsCounterspellActionTest, CounterCostUsesTheCanonicalMultiplierAndRounding)
{
	EXPECT_EQ(newHorizonsMagic::counterspellCost(0, false), 0);
	EXPECT_EQ(newHorizonsMagic::counterspellCost(4, false), 8);
	EXPECT_EQ(newHorizonsMagic::counterspellCost(4, true), 7);
	EXPECT_EQ(newHorizonsMagic::counterspellCost(5, true), 9);
	EXPECT_EQ(newHorizonsMagic::counterspellCost(11, false), 22);
	EXPECT_EQ(newHorizonsMagic::counterspellCost(11, true), 20);
	EXPECT_THROW(newHorizonsMagic::counterspellCost(-1, false), std::invalid_argument);
}

TEST(NewHorizonsCounterspellActionTest, ArmedStateRoundTripsAndRejectsLossyOldSaves)
{
	SideInBattle armed(nullptr);
	armed.counterspellArmed = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & armed);
	SideInBattle restored(nullptr);
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.counterspellArmed);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	EXPECT_THROW(old.oser & armed, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	SideInBattle defaultState(nullptr);
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	ASSERT_NO_THROW(oldDefault.oser & defaultState);
	SideInBattle oldDecoded(nullptr);
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.counterspellArmed);
}

TEST(NewHorizonsCounterspellActionTest, CastResultRoundTripsAndRejectsLossyOldProtocol)
{
	BattleSpellCast packet;
	packet.battleID = BattleID(7);
	packet.side = BattleSide::DEFENDER;
	packet.spellID = SpellID(SpellID::HASTE);
	packet.counterspellSide = BattleSide::ATTACKER;
	packet.counterspellNegated = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & packet);
	BattleSpellCast restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.counterspellSide, BattleSide::ATTACKER);
	EXPECT_TRUE(restored.counterspellNegated);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	packet.counterspellSide = BattleSide::NONE;
	packet.counterspellNegated = false;
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD;
	ASSERT_NO_THROW(oldDefault.oser & packet);
	BattleSpellCast oldDecoded;
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_EQ(oldDecoded.counterspellSide, BattleSide::NONE);
	EXPECT_FALSE(oldDecoded.counterspellNegated);
}
