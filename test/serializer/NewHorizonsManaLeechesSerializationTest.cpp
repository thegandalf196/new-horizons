/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsManaLeeches.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace
{
JsonNode captured(int amount = 1)
{
	JsonNode result;
	result["additionalSpecialties"]["version"].Integer() = 1;
	result["additionalSpecialties"]["manaLeeches"]["manaPerDamagingAttackSequence"].Integer() = amount;
	return result;
}
}
TEST(NewHorizonsManaLeechesSerialization, CapabilityRejectsOldWritersBeforePrefixIncludingZero)
{
	static_assert(ESerializationVersion::NEW_HORIZONS_MANA_LEECHES > ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE);
	for(const int amount : {0, 1})
	{
		GameSettings settings;
		settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured(amount));
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
		EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
TEST(NewHorizonsManaLeechesSerialization, CurrentSettingsRoundtripAndHistoricalAbsence)
{
	GameSettings settings;
	const auto rules = captured();
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer wire;
	settings.serialize(wire.oser);
	CMemorySerializer reader(wire.extractBuffer());
	GameSettings loaded;
	loaded.serialize(reader.iser);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	JsonNode historical;
	const auto before = historical;
	EXPECT_NO_THROW(newHorizonsHeroes::validateManaLeechesSerialization(historical, false));
	EXPECT_EQ(historical, before);
}
TEST(NewHorizonsManaLeechesSerialization, MalformedRowRejectsCurrentFormat)
{
	auto rules = captured(-1);
	EXPECT_THROW(newHorizonsHeroes::validateManaLeechesSerialization(rules, true), std::runtime_error);
	rules = captured();
	rules["additionalSpecialties"]["manaLeeches"]["unknown"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateManaLeechesSerialization(rules, true), std::runtime_error);
}

TEST(NewHorizonsManaLeechesSerialization, OldReaderRejectsWithoutInstallingCapturedRow)
{
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured());
	CMemorySerializer output;
	source.serialize(output.oser);
	CMemorySerializer input(output.extractBuffer());
	input.iser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
	GameSettings loaded;
	const auto before = loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_THROW(loaded.serialize(input.iser), std::runtime_error);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), before);
}

TEST(NewHorizonsManaLeechesSerialization, RecoveryReceiptRoundtripCopyAndLegacyCeiling)
{
	SideInBattle side(nullptr);
	side.initialMana = 5;
	side.manaLeechesRecoveredNormal = 3;
	CMemorySerializer wire;
	side.serialize(wire.oser);
	SideInBattle loaded(nullptr);
	loaded.serialize(wire.iser);
	EXPECT_EQ(loaded.manaLeechesRecoveredNormal, 3);
	EXPECT_EQ(loaded.legacySpellPointCompletionCeiling(), 8);
	const auto copy = loaded;
	loaded.manaLeechesRecoveredNormal = 1;
	EXPECT_EQ(copy.manaLeechesRecoveredNormal, 3);
	loaded.initialMana = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(loaded.legacySpellPointCompletionCeiling(), std::numeric_limits<int32_t>::max());
}
TEST(NewHorizonsManaLeechesSerialization, OldSideReadClearsReusedRecoveryAndNegativeRejects)
{
	SideInBattle source(nullptr), loaded(nullptr);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
	source.serialize(wire.oser);
	loaded.manaLeechesRecoveredNormal = 9;
	loaded.serialize(wire.iser);
	EXPECT_EQ(loaded.manaLeechesRecoveredNormal, 0);
	loaded.manaLeechesRecoveredNormal = -1;
	EXPECT_THROW(loaded.validateManaLeechesSerialization(true), std::runtime_error);
}
TEST(NewHorizonsManaLeechesSerialization, OldSideBattleAndEntryWritersRejectBeforePrefix)
{
	SideInBattle side(nullptr);
	side.manaLeechesRecoveredNormal = 1;
	BattleStart entry;
	entry.info = std::make_unique<BattleInfo>(nullptr);
	entry.info->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal = 1;
	CMemorySerializer direct, battle, outer;
	direct.oser.version = battle.oser.version = outer.oser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
	EXPECT_THROW(side.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	EXPECT_THROW(entry.info->serialize(battle.oser), std::runtime_error);
	EXPECT_TRUE(battle.extractBuffer().empty());
	EXPECT_THROW(entry.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
}
TEST(NewHorizonsManaLeechesSerialization, TypedRecoveryPackRetainsIdentityAndRejectsOldWire)
{
	SetMana source(ObjectInstanceID(3), SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, 1);
	CMemorySerializer current;
	source.serialize(current.oser);
	SetMana loaded;
	loaded.serialize(current.iser);
	EXPECT_EQ(loaded.operation, source.operation);
	EXPECT_EQ(loaded.amount, 1);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer oldReader(current.extractBuffer());
	oldReader.iser.version = ESerializationVersion::NEW_HORIZONS_NEUTRAL_ARMY_SIZE;
	EXPECT_THROW(loaded.serialize(oldReader.iser), std::runtime_error);
}
