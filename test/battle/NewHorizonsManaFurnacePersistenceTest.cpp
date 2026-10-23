/*
 * NewHorizonsManaFurnacePersistenceTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
constexpr auto beforeFurnace = ESerializationVersion::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES;
}
TEST(NewHorizonsManaFurnacePersistence, SideRoundtripRetainsActualRecoveryAndOriginalSnapshot)
{
	SideInBattle source(nullptr);
	source.initialMana = source.initialNormalSpellPoints = 5;
	source.manaFurnaceRecoveredNormal = 40;
	CMemorySerializer current;
	ASSERT_NO_THROW(source.serialize(current.oser));
	SideInBattle loaded(nullptr);
	ASSERT_NO_THROW(loaded.serialize(current.iser));
	EXPECT_EQ(loaded.initialMana, 5);
	EXPECT_EQ(loaded.initialNormalSpellPoints, 5);
	EXPECT_EQ(loaded.manaFurnaceRecoveredNormal, 40);
	EXPECT_EQ(loaded.legacySpellPointCompletionCeiling(), 45);
	const auto sibling = source;
	source.manaFurnaceRecoveredNormal = 2;
	EXPECT_EQ(sibling.manaFurnaceRecoveredNormal, 40);
}
TEST(NewHorizonsManaFurnacePersistence, HistoricalReadClearsReusedReceiptAndPreservesOriginalCeiling)
{
	SideInBattle source(nullptr);
	source.initialMana = 5;
	CMemorySerializer previous;
	previous.oser.version = previous.iser.version = beforeFurnace;
	ASSERT_NO_THROW(source.serialize(previous.oser));
	SideInBattle loaded(nullptr);
	loaded.manaFurnaceRecoveredNormal = 40;
	ASSERT_NO_THROW(loaded.serialize(previous.iser));
	EXPECT_EQ(loaded.manaFurnaceRecoveredNormal, 0);
	EXPECT_EQ(loaded.legacySpellPointCompletionCeiling(), 5);
}
TEST(NewHorizonsManaFurnacePersistence, LossyDirectAndEnclosingWritersRejectBeforeAnyPrefix)
{
	SideInBattle side(nullptr);
	side.manaFurnaceRecoveredNormal = 40;
	CMemorySerializer direct;
	direct.oser.version = beforeFurnace;
	EXPECT_THROW(side.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleStart packet;
	packet.battleID = BattleID(0);
	packet.info = std::make_unique<BattleInfo>(nullptr);
	packet.info->getSide(BattleSide::DEFENDER).manaFurnaceRecoveredNormal = 40;
	CMemorySerializer battle, outer;
	battle.oser.version = outer.oser.version = beforeFurnace;
	EXPECT_THROW(packet.info->serialize(battle.oser), std::runtime_error);
	EXPECT_TRUE(battle.extractBuffer().empty());
	EXPECT_THROW(packet.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
}
TEST(NewHorizonsManaFurnacePersistence, InvalidCapturedRecoveryRejectsOnWriteAndRead)
{
	constexpr auto recoveryFormat = ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
	for(const int32_t invalid : {-1, 1000001})
	{
		SideInBattle side(nullptr);
		side.manaFurnaceRecoveredNormal = invalid;
		CMemorySerializer rejected;
		rejected.oser.version = recoveryFormat;
		EXPECT_THROW(side.serialize(rejected.oser), std::runtime_error);
		EXPECT_TRUE(rejected.extractBuffer().empty());
		// Pin the capability whose final field is Mana Furnace. Craft the
		// variable-length suffix using the actual serializer, without
		// bypassing the production reader's validation.
		side.manaFurnaceRecoveredNormal = 0;
		CMemorySerializer valid;
		valid.oser.version = recoveryFormat;
		ASSERT_NO_THROW(side.serialize(valid.oser));
		auto bytes = valid.extractBuffer();
		CMemorySerializer encodedZero;
		encodedZero.oser.version = recoveryFormat;
		int32_t zero = 0;
		encodedZero.oser & zero;
		const auto zeroSuffix = encodedZero.extractBuffer();
		ASSERT_FALSE(zeroSuffix.empty());
		ASSERT_GE(bytes.size(), zeroSuffix.size());
		ASSERT_TRUE(std::equal(zeroSuffix.rbegin(), zeroSuffix.rend(), bytes.rbegin()));
		CMemorySerializer encoded;
		encoded.oser.version = recoveryFormat;
		auto value = invalid;
		encoded.oser & value;
		const auto suffix = encoded.extractBuffer();
		ASSERT_FALSE(suffix.empty());
		bytes.resize(bytes.size() - zeroSuffix.size());
		bytes.insert(bytes.end(), suffix.begin(), suffix.end());
		CMemorySerializer malformed(std::move(bytes));
		malformed.iser.version = recoveryFormat;
		SideInBattle loaded(nullptr);
		EXPECT_THROW(loaded.serialize(malformed.iser), std::runtime_error);
	}
}
TEST(NewHorizonsManaFurnacePersistence, CompletionCeilingUsesCheckedWideArithmeticAndNoInventedRecovery)
{
	SideInBattle side(nullptr);
	side.initialMana = std::numeric_limits<int32_t>::max();
	side.manaFurnaceRecoveredNormal = 40;
	EXPECT_EQ(side.legacySpellPointCompletionCeiling(), std::numeric_limits<int32_t>::max());
	side.initialMana = -5;
	side.manaFurnaceRecoveredNormal = 0;
	EXPECT_EQ(side.legacySpellPointCompletionCeiling(), 0);
}
