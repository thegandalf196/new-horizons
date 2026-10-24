/*
	* NewHorizonsDeepTrancePersistenceTest.cpp, part of VCMI / New Horizons
	* License: GNU General Public License v2.0 or later; see license.txt
	*/
#include "StdInc.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
namespace
{
constexpr auto previous = ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
}
TEST(NewHorizonsDeepTrancePersistence, SideRoundtripAndCopyRetainZeroRecoveryConsumption)
{
	SideInBattle source(nullptr);
	source.deepTranceConsumed = true;
	source.initialAcceptedCombatDay = 7;
	CMemorySerializer wire;
	ASSERT_NO_THROW(source.serialize(wire.oser));
	SideInBattle loaded(nullptr);
	ASSERT_NO_THROW(loaded.serialize(wire.iser));
	EXPECT_TRUE(loaded.deepTranceConsumed);
	EXPECT_EQ(loaded.initialAcceptedCombatDay, 7);
	EXPECT_EQ(loaded.deepTranceRecoveredNormal, 0);
	const auto copy = loaded;
	loaded.deepTranceRecoveredNormal = 3;
	EXPECT_EQ(copy.deepTranceRecoveredNormal, 0);
}
TEST(NewHorizonsDeepTrancePersistence, PreviousReadClearsReusedProvenance)
{
	for(const auto format : {previous, ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS})
	{
		SideInBattle source(nullptr);
		CMemorySerializer wire;
		wire.oser.version = wire.iser.version = format;
		ASSERT_NO_THROW(source.serialize(wire.oser));
		SideInBattle loaded(nullptr);
		loaded.deepTranceConsumed = true;
		loaded.initialAcceptedCombatDay = 3;
		loaded.deepTranceRecoveredNormal = 2;
		ASSERT_NO_THROW(loaded.serialize(wire.iser));
		EXPECT_FALSE(loaded.deepTranceConsumed);
		EXPECT_EQ(loaded.initialAcceptedCombatDay, -1);
		EXPECT_EQ(loaded.deepTranceRecoveredNormal, 0);
	}
}
TEST(NewHorizonsDeepTrancePersistence, LossySideBattleAndEntryWritersRejectBeforePrefix)
{
	for(const auto format : {previous, ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS})
	{
		SideInBattle source(nullptr);
		source.deepTranceConsumed = true;
		CMemorySerializer direct, battle, outer;
		direct.oser.version = battle.oser.version = outer.oser.version = format;
		EXPECT_THROW(source.serialize(direct.oser), std::runtime_error);
		EXPECT_TRUE(direct.extractBuffer().empty());
		BattleStart entry;
		entry.info = std::make_unique<BattleInfo>(nullptr);
		entry.info->getSide(BattleSide::ATTACKER).deepTranceConsumed = true;
		EXPECT_THROW(entry.info->serialize(battle.oser), std::runtime_error);
		EXPECT_TRUE(battle.extractBuffer().empty());
		EXPECT_THROW(entry.serialize(outer.oser), std::runtime_error);
		EXPECT_TRUE(outer.extractBuffer().empty());
	}
}
TEST(NewHorizonsDeepTrancePersistence, MalformedAndOutOfRangeProvenanceRejects)
{
	SideInBattle side(nullptr);
	side.deepTranceRecoveredNormal = 1;
	EXPECT_THROW(side.validateRecoverySpecialtySerialization(true), std::runtime_error);
	side.deepTranceConsumed = true;
	side.initialAcceptedCombatDay = -2;
	EXPECT_THROW(side.validateRecoverySpecialtySerialization(true), std::runtime_error);
	side.initialAcceptedCombatDay = 0;
	side.deepTranceRecoveredNormal = -1;
	EXPECT_THROW(side.validateRecoverySpecialtySerialization(true), std::runtime_error);
}
TEST(NewHorizonsDeepTrancePersistence, LegacyCompletionAddsOnlyActualIndependentRecoveries)
{
	SideInBattle side(nullptr);
	side.initialMana = 5;
	side.manaFurnaceRecoveredNormal = 4;
	side.deepTranceConsumed = true;
	side.deepTranceRecoveredNormal = 3;
	EXPECT_EQ(side.legacySpellPointCompletionCeiling(), 12);
	side.initialMana = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(side.legacySpellPointCompletionCeiling(), std::numeric_limits<int32_t>::max());
}
TEST(NewHorizonsDeepTrancePersistence, HeroReceiptRejectsLossyPrefixAndRetainsFullDayDomain)
{
	for(const auto format : {previous, ESerializationVersion::NEW_HORIZONS_REPLAY_PAYLOAD_VERSIONS})
	{
		CGHeroInstance hero(nullptr);
		EXPECT_EQ(hero.getNewHorizonsLastAcceptedCombatDay(), -1);
		hero.setNewHorizonsLastAcceptedCombatDay(std::numeric_limits<uint32_t>::max());
		EXPECT_NO_THROW(hero.validateRecoverySpecialtyStateSerialization(true));
		EXPECT_THROW(hero.setNewHorizonsLastAcceptedCombatDay(-2), std::runtime_error);
		CMemorySerializer old;
		old.oser.version = format;
		EXPECT_THROW(hero.serialize(old.oser), std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}
