/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/ArcaneMarkState.h"
#include "../../lib/battle/PhysicalPoisonSpellOrigin.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"

using battle::ArcaneMarkState;
TEST(NewHorizonsPassiveFactionStateTest, MarksAreSideSpecificSaturatingAndCopiedByValue)
{
	ArcaneMarkState original;
	original.add(BattleSide::ATTACKER, 2);
	original.add(BattleSide::DEFENDER, 1);
	auto copy = original;
	copy.add(BattleSide::ATTACKER, 100);
	EXPECT_EQ(copy.get(BattleSide::ATTACKER), 5);
	EXPECT_EQ(copy.get(BattleSide::DEFENDER), 1);
	EXPECT_EQ(original.get(BattleSide::ATTACKER), 2);
	EXPECT_THROW(copy.add(BattleSide::ATTACKER, -1), std::out_of_range);
	EXPECT_THROW(copy.set(BattleSide::DEFENDER, 6), std::out_of_range);
	EXPECT_THROW(copy.get(static_cast<BattleSide>(2)), std::invalid_argument);
}
TEST(NewHorizonsPassiveFactionStateTest, JsonPresenceIsStrictAndNeitherFieldMasksTheOther)
{
	JsonNode unit;
	EXPECT_FALSE(battle::hasPassiveFactionUnitState(unit));
	unit["state"]["arcaneMarks"] = ArcaneMarkState{}.toJson();
	unit["state"]["demonicGateOpened"].Bool() = true;
	EXPECT_TRUE(battle::hasPassiveFactionUnitState(unit));
	unit["state"]["arcaneMarks"].Vector()[0].Integer() = 1;
	unit["state"]["demonicGateOpened"] = JsonNode();
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"].Struct().erase("demonicGateOpened");
	unit["state"]["arcaneMarks"].Vector()[1].Float() = 1.5;
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"]["arcaneMarks"] = JsonNode();
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
}
TEST(NewHorizonsPassiveFactionStateTest, Explicit1155RoundTripAndOlderWriterPrefixRefusal)
{
	ArcaneMarkState marks;
	marks.set(BattleSide::ATTACKER, 5);
	marks.set(BattleSide::DEFENDER, 2);
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
	bytes.iser.version = bytes.oser.version;
	bytes.oser & marks;
	ArcaneMarkState restored;
	bytes.iser & restored;
	EXPECT_EQ(restored, marks);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	EXPECT_THROW(old.oser & marks, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST(NewHorizonsPassiveFactionStateTest, HistoricalEmptyReadClearsReusedDestination)
{
	ArcaneMarkState empty;
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	bytes.iser.version = bytes.oser.version;
	bytes.oser & empty;
	ArcaneMarkState reused;
	reused.set(BattleSide::ATTACKER, 4);
	bytes.iser & reused;
	EXPECT_FALSE(reused.hasState());
}

TEST(NewHorizonsPassiveFactionStateTest, PoisonSpellOriginIsStrictAndRequiresItsExistingPayload)
{
	JsonNode unit;
	EXPECT_EQ(battle::physicalPoisonSpellOriginFromJson(unit), BattleSide::NONE);
	unit["state"]["physicalPoisonSpellSide"].Integer() = 1;
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
	unit["state"]["physicalPoisonBaseDamage"].Integer() = 10;
	unit["state"]["physicalPoisonActivationsRemaining"].Integer() = 2;
	EXPECT_EQ(battle::physicalPoisonSpellOriginFromJson(unit), BattleSide::DEFENDER);
	unit["state"]["arcaneMarks"] = ArcaneMarkState{}.toJson();
	unit["state"]["arcaneMarks"].Vector()[0].Integer() = 1;
	unit["state"]["physicalPoisonSpellSide"].Integer() = 257;
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"]["physicalPoisonSpellSide"].Float() = 0.5;
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
	unit["state"]["physicalPoisonSpellSide"] = JsonNode();
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
}
