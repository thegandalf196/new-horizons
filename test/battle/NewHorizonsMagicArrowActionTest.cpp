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
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMagic.h"

TEST(NewHorizonsShadowGiftMechanicsTest, SharedTierFormulaAndNoFreeSacrifice)
{
	using namespace newHorizonsShadowGift;
	EXPECT_TRUE(isValidSacrificePercent(10));
	EXPECT_TRUE(isValidSacrificePercent(20));
	EXPECT_TRUE(isValidSacrificePercent(30));
	EXPECT_FALSE(isValidSacrificePercent(0));
	EXPECT_FALSE(isValidSacrificePercent(15));

	EXPECT_EQ(getSacrificeCostBasisPoints(10, false), 1000);
	EXPECT_EQ(getSacrificeCostBasisPoints(10, true), 750);
	EXPECT_EQ(getSacrificeHealthAmount(1000, 1000), 100);
	EXPECT_EQ(getSacrificeHealthAmount(1000, 750), 75);
	EXPECT_EQ(getSacrificeHealthAmount(9, 750), 1);
	EXPECT_EQ(getSacrificeHealthAmount(1, 1000), 0);
	EXPECT_EQ(getSacrificeHealthAmount(2, 10000), 1);

	EXPECT_EQ(getDamageBonusBasisPoints(10, 0, 10000), 1250);
	EXPECT_EQ(getDamageBonusBasisPoints(10, 100, 10000), 1750);
	EXPECT_EQ(getDamageBonusBasisPoints(10, 100, 14500), 1975);
	EXPECT_EQ(getDamageBonusBasisPoints(10, 100, 14500, 25), 2156);
	EXPECT_THROW(getDamageBonusBasisPoints(15, 100, 10000), std::invalid_argument);
}

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

TEST(NewHorizonsShadowGiftMechanicsTest, SelectedTierRoundTripsOnlyOnItsProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::SLOW; // The serializer transports the optional tier; server validates spell identity.
	action.spellShadowGiftSacrificePercent = 20;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.spellShadowGiftSacrificePercent, 20);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_QUICKSAND;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	action.spellShadowGiftSacrificePercent = 0;
	CMemorySerializer oldDefault;
	oldDefault.oser.version = ESerializationVersion::NEW_HORIZONS_QUICKSAND;
	oldDefault.iser.version = ESerializationVersion::NEW_HORIZONS_QUICKSAND;
	ASSERT_NO_THROW(oldDefault.oser & action);
	BattleAction oldDecoded;
	ASSERT_NO_THROW(oldDefault.iser & oldDecoded);
	EXPECT_EQ(oldDecoded.spellShadowGiftSacrificePercent, 0);

	action.spellShadowGiftSacrificePercent = 15;
	CMemorySerializer invalid;
	invalid.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalid.oser & action, std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());
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
