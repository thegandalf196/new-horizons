/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/CStack.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/entities/hero/NewHorizonsPetrifyingBlood.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
JsonNode bloodRules()
{
	JsonNode rules;
	rules["petrifyingBloodSpecialty"]["version"].Integer() = 1;
	return rules;
}
}
TEST(NewHorizonsPetrifyingBloodSerialization, CurrentSettingsRoundTripPreservesCapture)
{
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, bloodRules());
	CMemorySerializer wire;
	settings.serialize(wire.oser);
	GameSettings restored;
	restored.serialize(wire.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), bloodRules());
}
TEST(NewHorizonsPetrifyingBloodSerialization, OldWriterRejectsBeforeSettingsPrefix)
{
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, bloodRules());
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsPetrifyingBloodSerialization, OldAbsentCaptureRoundTripDoesNotFillInstalledPolicy)
{
	GameSettings settings;
	JsonNode historical;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, historical);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	settings.serialize(wire.oser);
	GameSettings restored;
	restored.serialize(wire.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), historical);
	EXPECT_NO_THROW(newHorizonsHeroes::validatePetrifyingBloodSerialization(historical, false));
}
TEST(NewHorizonsPetrifyingBloodSerialization, StrictVersionAndUnknownFieldsAreRejected)
{
	for(int version : {-1, 0, 2})
	{
		auto rules = bloodRules();
		rules["petrifyingBloodSpecialty"]["version"].Integer() = version;
		EXPECT_THROW(newHorizonsHeroes::validatePetrifyingBloodSerialization(rules, true), std::runtime_error);
	}
	auto rules = bloodRules();
	rules["petrifyingBloodSpecialty"]["chance"].Integer() = 100;
	EXPECT_THROW(newHorizonsHeroes::validatePetrifyingBloodSerialization(rules, true), std::runtime_error);
}
TEST(NewHorizonsPetrifyingBloodSerialization, SpentStackReceiptRejectsOldWriterBeforePrefix)
{
	CStack descriptor;
	descriptor.petrifyingBloodRound = 3;
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(descriptor.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsPetrifyingBloodSerialization, NegativeInvalidReceiptRejectsCurrentWriterBeforePrefix)
{
	CStack descriptor;
	descriptor.petrifyingBloodRound = -2;
	CMemorySerializer wire;
	EXPECT_THROW(descriptor.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}

TEST(NewHorizonsPetrifyingBloodSerialization, UnitAndAttackEnvelopesRejectOldWriterBeforePrefix)
{
	UnitChanges change;
	change.data["state"]["petrifyingBloodRound"].Integer() = 3;
	BattleStackAttacked hit;
	hit.newState = change;
	BattleAttack attack;
	attack.bsa.push_back(hit);
	BattleUnitsChanged update;
	update.changedStacks.push_back(change);
	CMemorySerializer unit, nested, outer, envelope;
	unit.oser.version = nested.oser.version = outer.oser.version = envelope.oser.version
		= ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(change.serialize(unit.oser), std::runtime_error);
	EXPECT_TRUE(unit.extractBuffer().empty());
	EXPECT_THROW(hit.serialize(nested.oser), std::runtime_error);
	EXPECT_TRUE(nested.extractBuffer().empty());
	EXPECT_THROW(attack.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	EXPECT_THROW(update.serialize(envelope.oser), std::runtime_error);
	EXPECT_TRUE(envelope.extractBuffer().empty());
}
TEST(NewHorizonsPetrifyingBloodSerialization, BattleAndEntryRejectSpentReceiptBeforePrefix)
{
	BattleStart entry;
	entry.info = std::make_unique<BattleInfo>(nullptr);
	entry.info->stacks.push_back(std::make_unique<CStack>());
	entry.info->stacks.back()->petrifyingBloodRound = 3;
	CMemorySerializer battle, envelope;
	battle.oser.version = envelope.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(entry.info->serialize(battle.oser), std::runtime_error);
	EXPECT_TRUE(battle.extractBuffer().empty());
	EXPECT_THROW(entry.serialize(envelope.oser), std::runtime_error);
	EXPECT_TRUE(envelope.extractBuffer().empty());
}
TEST(NewHorizonsPetrifyingBloodSerialization, UnitReceiptRoundTripAndMalformedPrefixGuard)
{
	UnitChanges source;
	source.data["state"]["petrifyingBloodRound"].Integer() = 3;
	CMemorySerializer current;
	source.serialize(current.oser);
	UnitChanges restored;
	restored.serialize(current.iser);
	EXPECT_EQ(battle::petrifyingBloodRoundFromJson(restored.data), 3);
	source.data["state"]["petrifyingBloodRound"].Float() = 1.5;
	CMemorySerializer malformed;
	EXPECT_THROW(source.serialize(malformed.oser), std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}
