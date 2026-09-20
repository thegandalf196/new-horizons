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
#include "../../lib/spells/NewHorizonsMagic.h"

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

TEST(NewHorizonsMagicArrowActionTest, MetamagicOfferPreservesUnconsumedUseAcrossSave)
{
	SideInBattle defaults(nullptr);
	CMemorySerializer oldDefaults;
	oldDefaults.oser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	oldDefaults.iser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	ASSERT_NO_THROW(oldDefaults.oser & defaults);
	SideInBattle restoredDefaults(nullptr);
	ASSERT_NO_THROW(oldDefaults.iser & restoredDefaults);
	EXPECT_FALSE(restoredDefaults.metamagicFirstSpell.hasValue());
	EXPECT_EQ(restoredDefaults.metamagicFirstTargetUnitId, newHorizonsMagic::INVALID_METAMAGIC_TARGET);
	EXPECT_FALSE(restoredDefaults.metamagicFirstCounterspellNegated);

	SideInBattle offered(nullptr);
	offered.metamagicUsesConsumed = 0;
	offered.metamagicPendingCount = 1;
	offered.metamagicFirstSpell = SpellID::HASTE;
	offered.metamagicFirstTargetUnitId = 37;
	offered.metamagicSequenceSpells = {SpellID::HASTE};
	offered.metamagicFirstCounterspellNegated = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & offered);
	SideInBattle restored(nullptr);
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.metamagicUsesConsumed, 0);
	EXPECT_EQ(restored.metamagicPendingCount, 1);
	EXPECT_EQ(restored.metamagicFirstSpell, SpellID::HASTE);
	EXPECT_EQ(restored.metamagicFirstTargetUnitId, 37u);
	ASSERT_EQ(restored.metamagicSequenceSpells.size(), 1u);
	EXPECT_EQ(restored.metamagicSequenceSpells.front(), SpellID::HASTE);
	EXPECT_TRUE(restored.metamagicFirstCounterspellNegated);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	EXPECT_THROW(old.oser & offered, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NewHorizonsMagicArrowActionTest, MetamagicActionsRoundTripAndRejectOlderProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::SLOW;
	action.metamagicFollowup = true;
	action.metamagicGrand = true;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.metamagicFollowup);
	EXPECT_TRUE(restored.metamagicGrand);
	EXPECT_FALSE(restored.metamagicDecline);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	BattleAction decline = BattleAction::makeMetamagicDecline(BattleSide::ATTACKER);
	decline.metamagicManaRefund = 3;
	CMemorySerializer declineWire;
	declineWire.oser.version = ESerializationVersion::CURRENT;
	declineWire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(declineWire.oser & decline);
	BattleAction restoredDecline;
	ASSERT_NO_THROW(declineWire.iser & restoredDecline);
	EXPECT_TRUE(restoredDecline.metamagicDecline);
	EXPECT_EQ(restoredDecline.metamagicManaRefund, 3);
}

TEST(NewHorizonsMagicArrowActionTest, MetamagicCastMetadataRoundTripsAndDefaultsOnOlderProtocol)
{
	BattleSpellCast packet;
	packet.battleID = BattleID(7);
	packet.side = BattleSide::ATTACKER;
	packet.spellID = SpellID::SLOW;
	packet.metamagicFollowup = true;
	packet.metamagicGrand = true;
	packet.metamagicTargetUnitId = 91;
	packet.metamagicManaRefund = 3;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & packet);
	BattleSpellCast restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.metamagicFollowup);
	EXPECT_TRUE(restored.metamagicGrand);
	EXPECT_EQ(restored.metamagicTargetUnitId, 91u);
	EXPECT_EQ(restored.metamagicManaRefund, 3);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	packet.metamagicFollowup = false;
	packet.metamagicGrand = false;
	packet.metamagicTargetUnitId = newHorizonsMagic::INVALID_METAMAGIC_TARGET;
	packet.metamagicManaRefund = 0;
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_FIRE_WALL;
	ASSERT_NO_THROW(oldDefault.oser & packet);
	BattleSpellCast oldDecoded;
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.metamagicFollowup);
	EXPECT_FALSE(oldDecoded.metamagicGrand);
	EXPECT_EQ(oldDecoded.metamagicTargetUnitId, newHorizonsMagic::INVALID_METAMAGIC_TARGET);
	EXPECT_EQ(oldDecoded.metamagicManaRefund, 0);
}
