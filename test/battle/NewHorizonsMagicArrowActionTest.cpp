/*
 * NewHorizonsMagicArrowActionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(NewHorizonsMagicArrowActionTest, OverchargeRoundTripsOnlyOnTheNewProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID(SpellID::MAGIC_ARROW);
	action.spellOvercharge = 4;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.spellOvercharge, 4);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_TARGETED_COMMANDS;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	BattleAction zero = action;
	zero.spellOvercharge = 0;
	CMemorySerializer oldZero;
	oldZero.oser.version = ESerializationVersion::NEW_HORIZONS_TARGETED_COMMANDS;
	oldZero.iser.version = ESerializationVersion::NEW_HORIZONS_TARGETED_COMMANDS;
	ASSERT_NO_THROW(oldZero.oser & zero);
	BattleAction oldDecoded;
	ASSERT_NO_THROW(oldZero.iser & oldDecoded);
	EXPECT_EQ(oldDecoded.spellOvercharge, 0);
}

TEST(NewHorizonsMagicArrowActionTest, SelectiveDispelRoundTripsOnlyOnItsProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID(SpellID::DISPEL);
	action.spellSelectiveDispel = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.spellSelectiveDispel);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PERK_OFFERS;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	action.spellSelectiveDispel = false;
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_PERK_OFFERS;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_PERK_OFFERS;
	ASSERT_NO_THROW(oldDefault.oser & action);
	BattleAction oldDecoded;
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.spellSelectiveDispel);
}

TEST(NewHorizonsMagicArrowActionTest, TemporalFieldRoundTripsOnlyOnItsProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID(SpellID::SLOW);
	action.spellMassSlow = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.spellMassSlow);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	action.spellMassSlow = false;
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL;
	ASSERT_NO_THROW(oldDefault.oser & action);
	BattleAction oldDecoded;
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.spellMassSlow);
}

TEST(NewHorizonsMagicArrowActionTest, TemporalFieldConsumptionStateRoundTripsAndRejectsLossyWrites)
{
	SideInBattle side(nullptr);
	side.temporalFieldUsed = true;
	CMemorySerializer currentSide;
	currentSide.oser.version = ESerializationVersion::CURRENT;
	currentSide.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(currentSide.oser & side);
	SideInBattle restoredSide(nullptr);
	ASSERT_NO_THROW(currentSide.iser & restoredSide);
	EXPECT_TRUE(restoredSide.temporalFieldUsed);

	CMemorySerializer oldSide;
	oldSide.oser.version = ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL;
	EXPECT_THROW(oldSide.oser & side, std::runtime_error);
	EXPECT_TRUE(oldSide.extractBuffer().empty());

	BattleSpellCast packet;
	packet.battleID = BattleID(7);
	packet.side = BattleSide::ATTACKER;
	packet.spellID = SpellID(SpellID::SLOW);
	packet.temporalFieldCast = true;
	CMemorySerializer currentPacket;
	currentPacket.oser.version = ESerializationVersion::CURRENT;
	currentPacket.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(currentPacket.oser & packet);
	BattleSpellCast restoredPacket;
	ASSERT_NO_THROW(currentPacket.iser & restoredPacket);
	EXPECT_TRUE(restoredPacket.temporalFieldCast);

	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL;
	EXPECT_THROW(oldPacket.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
}
