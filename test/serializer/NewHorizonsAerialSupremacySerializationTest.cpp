/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/entities/hero/NewHorizonsAerialSupremacy.h"
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../lib/networkPacks/BattleChanges.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
JsonNode capturedAerial(bool enabled)
{
	JsonNode result;
	result["additionalSpecialties"]["version"].Integer() = 1;
	result["additionalSpecialties"]["aerialSupremacy"]["enabled"].Bool() = enabled;
	return result;
}
newHorizonsAerialSupremacy::Excursion aerialReceipt()
{
	return {1, 2, BattleHex(10, 5), BattleHex(6, 5), BattleHex(5, 5), 1};
}
}
TEST(NewHorizonsAerialSupremacySerialization, UnsupportedEnabledAndDisabledCaptureRejectBeforeSettingsPrefix)
{
	for(const bool enabled : {false, true})
	{
		GameSettings settings;
		settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, capturedAerial(enabled));
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
		EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
TEST(NewHorizonsAerialSupremacySerialization, DirectDescriptorRoundtripAndOldWriterPrefix)
{
	auto receipt = aerialReceipt();
	CMemorySerializer current;
	receipt.serialize(current.oser);
	newHorizonsAerialSupremacy::Excursion loaded;
	loaded.serialize(current.iser);
	EXPECT_EQ(loaded.actor, receipt.actor);
	EXPECT_EQ(loaded.opponent, receipt.opponent);
	EXPECT_EQ(loaded.origin, receipt.origin);
	EXPECT_EQ(loaded.landing, receipt.landing);
	EXPECT_EQ(loaded.target, receipt.target);
	EXPECT_EQ(loaded.round, receipt.round);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
	EXPECT_THROW(receipt.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST(NewHorizonsAerialSupremacySerialization, UnsupportedRoundStateRejectsDirectAndEnclosingPrefix)
{
	UnitChanges change;
	change.id = 1;
	change.data["state"]["aerialRetaliationRound"].Integer() = 1;
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(change);
	for(const bool enclosing : {false, true})
	{
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
		if(enclosing)
		{
			EXPECT_THROW(packet.serialize(old.oser), std::runtime_error);
		}
		else
		{
			EXPECT_THROW(change.serialize(old.oser), std::runtime_error);
		}
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}
TEST(NewHorizonsAerialSupremacySerialization, HistoricalAbsenceAndMalformedPolicyRemainDistinct)
{
	JsonNode historical;
	EXPECT_FALSE(newHorizonsHeroes::aerialSupremacyPolicy(historical));
	EXPECT_NO_THROW(newHorizonsHeroes::validateAerialSupremacySerialization(historical, false));
	auto rules = capturedAerial(true);
	rules["additionalSpecialties"]["aerialSupremacy"]["enabled"].String() = "true";
	EXPECT_THROW(newHorizonsHeroes::validateAerialSupremacySerialization(rules, true), std::runtime_error);
}

TEST(NewHorizonsAerialSupremacySerialization, StackBattleAndStartRejectRoundStateBeforeEveryPrefix)
{
	for(const int round : {-2, 1})
	{
		auto stack = std::make_unique<CStack>();
		stack->aerialRetaliationRound = round;
		CMemorySerializer direct;
		direct.oser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
		EXPECT_THROW(stack->serialize(direct.oser), std::runtime_error);
		EXPECT_TRUE(direct.extractBuffer().empty());
		BattleStart entry;
		entry.info = std::make_unique<BattleInfo>(nullptr);
		entry.info->stacks.push_back(std::move(stack));
		CMemorySerializer battle, outer;
		battle.oser.version = outer.oser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
		EXPECT_THROW(entry.info->serialize(battle.oser), std::runtime_error);
		EXPECT_TRUE(battle.extractBuffer().empty());
		EXPECT_THROW(entry.serialize(outer.oser), std::runtime_error);
		EXPECT_TRUE(outer.extractBuffer().empty());
	}
}

TEST(NewHorizonsAerialSupremacySerialization, ForgedAttackKindRejectsBeforePacketPrefix)
{
	BattleAttack packet;
	packet.battleID = BattleID(0);
	packet.stackAttacking = 1;
	packet.tile = BattleHex(5, 5);
	packet.flags = BattleAttack::COUNTER | BattleAttack::SHOT;
	packet.bsa.emplace_back();
	packet.aerialExcursion = aerialReceipt();
	CMemorySerializer wire;
	EXPECT_THROW(packet.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsAerialSupremacySerialization, UnsupportedDirectReaderRejectsBeforeDescriptorMutation)
{
	auto source = aerialReceipt();
	CMemorySerializer output;
	source.serialize(output.oser);
	CMemorySerializer input(output.extractBuffer());
	input.iser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
	newHorizonsAerialSupremacy::Excursion loaded;
	EXPECT_THROW(loaded.serialize(input.iser), std::runtime_error);
	EXPECT_EQ(loaded.actor, 0);
	EXPECT_EQ(loaded.opponent, 0);
	EXPECT_EQ(loaded.round, -1);
	EXPECT_FALSE(loaded.origin.isValid());
}
