/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/PhysicalPoisonSpellOrigin.h"
#include "../../lib/battle/UnitStateEffectReceipts.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"






TEST(NewHorizonsPassiveFactionStateTest, PoisonSpellOriginIsStrictAndRequiresItsExistingPayload)
{
	JsonNode unit;
	EXPECT_EQ(battle::physicalPoisonSpellOriginFromJson(unit), BattleSide::NONE);
	unit["state"]["physicalPoisonSpellSide"].Integer() = 1;
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
	unit["state"]["physicalPoisonBaseDamage"].Integer() = 10;
	unit["state"]["physicalPoisonActivationsRemaining"].Integer() = 2;
	EXPECT_EQ(battle::physicalPoisonSpellOriginFromJson(unit), BattleSide::DEFENDER);
	unit["state"]["physicalPoisonSpellSide"].Integer() = 257;
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"]["physicalPoisonSpellSide"].Float() = 0.5;
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
	unit["state"]["physicalPoisonSpellSide"] = JsonNode();
	EXPECT_THROW(battle::physicalPoisonSpellOriginFromJson(unit), std::runtime_error);
}

TEST(NewHorizonsPassiveFactionStateTest, DemonicGatePresenceIsStrict)
{
	JsonNode unit;
	EXPECT_FALSE(battle::hasPassiveFactionUnitState(unit));
	unit["state"]["demonicGateOpened"].Bool() = true;
	EXPECT_TRUE(battle::hasPassiveFactionUnitState(unit));
	unit["state"]["demonicGateOpened"].Integer() = 1;
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"]["demonicGateOpened"] = JsonNode();
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error);
	unit["state"]["demonicGateOpened"].Bool() = true;
	unit["state"]["physicalPoisonSpellSide"].Integer() = 257;
	EXPECT_THROW(battle::hasPassiveFactionUnitState(unit), std::runtime_error)
		<< "A valid gate receipt cannot mask malformed Poison origin";
}
