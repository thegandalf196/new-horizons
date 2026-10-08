/*
 * OverwhelmingFormulaObstacleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"

#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/MagicalDamageReduction.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../luascript/api/battle/SpellObstacleDescriptor.h"

#include <stdexcept>

namespace
{
JsonNode originalCapture()
{
	JsonNode result;
	result["penetrations"].Vector() = {JsonNode(20), JsonNode(15)};
	result["overwhelmingFormulaSide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
	result["overwhelmingFormulaToken"].String() = "9007199254740993";
	return result;
}

SpellCreatedObstacle hazard(SpellID spell)
{
	SpellCreatedObstacle result;
	result.uniqueID = 7;
	result.ID = spell;
	result.pos = BattleHex(5, 5);
	result.casterSide = BattleSide::ATTACKER;
	result.minimalDamage = 100;
	result.customSize.insert(result.pos);
	result.capturedMdrPenetration = originalCapture();
	return result;
}
}

TEST(OverwhelmingFormulaObstacle, DescriptorAndTriggerProxyRetainOriginalCastIdentityForMineAndFireWall)
{
	for(const SpellID spellId : {SpellID(SpellID::LAND_MINE), SpellID(SpellID::FIRE_WALL)})
	{
		CSpell spell;
		spell.id = spellId;
		scripting::api::SpellObstacleDescriptor descriptor;
		descriptor.spell = &spell;
		descriptor.casterSide = BattleSide::ATTACKER;
		descriptor.pos = BattleHex(5, 5);
		descriptor.capturedMdrPenetration = originalCapture();
		const auto obstacle = descriptor.toObstacle();
		EXPECT_EQ(obstacle.capturedMdrPenetration, originalCapture());
		spells::ObstacleCasterProxy proxy(PlayerColor(0), nullptr, obstacle);
		EXPECT_EQ(proxy.getCapturedMdrPenetration(), originalCapture());
		const auto formula = spells::capturedOverwhelmingFormula(proxy.getCapturedMdrPenetration());
		ASSERT_TRUE(formula);
		EXPECT_EQ(formula->token, uint64_t(9007199254740993));
		EXPECT_EQ(spells::capturedMdrPenetrations(proxy.getCapturedMdrPenetration(), 1),
			(std::vector<int>{20, 15})) << "Formula's dynamic 50% is never baked into the obstacle array";
	}
}

TEST(OverwhelmingFormulaObstacle, JsonAddAndUpdatePreserveCompleteCapture)
{
	for(const auto operation : {BattleChanges::EOperation::ADD, BattleChanges::EOperation::UPDATE})
	{
		auto obstacle = hazard(SpellID::LAND_MINE);
		ObstacleChanges change(obstacle.uniqueID, operation);
		obstacle.toInfo(change, operation);
		SpellCreatedObstacle restored;
		restored.fromInfo(change);
		EXPECT_EQ(restored.uniqueID, obstacle.uniqueID);
		EXPECT_EQ(restored.capturedMdrPenetration, obstacle.capturedMdrPenetration);
	}
}

TEST(OverwhelmingFormulaObstacle, CurrentBinaryRoundTripPreservesCapturedTokenAndContributors)
{
	auto obstacle = hazard(SpellID::FIRE_WALL);
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & obstacle;
	SpellCreatedObstacle restored;
	wire.iser & restored;
	EXPECT_EQ(restored.capturedMdrPenetration, originalCapture());
	const auto formula = spells::capturedOverwhelmingFormula(restored.capturedMdrPenetration);
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->token, uint64_t(9007199254740993));
}

TEST(OverwhelmingFormulaObstacle, OlderBinaryDownSaveRejectsNonemptyCaptureBeforeWriting)
{
	auto obstacle = hazard(SpellID::LAND_MINE);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	EXPECT_THROW(old.oser & obstacle, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(OverwhelmingFormulaObstacle, OlderBinaryAndMissingJsonCaptureLoadAsNull)
{
	auto obstacle = hazard(SpellID::LAND_MINE);
	obstacle.capturedMdrPenetration = JsonNode();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	old.oser & obstacle;
	SpellCreatedObstacle restored;
	restored.capturedMdrPenetration = originalCapture();
	old.iser & restored;
	EXPECT_TRUE(restored.capturedMdrPenetration.isNull());
	ObstacleChanges change(obstacle.uniqueID, BattleChanges::EOperation::ADD);
	obstacle.toInfo(change);
	change.data["obstacle"].Struct().erase("mdrPenetration");
	restored.capturedMdrPenetration = originalCapture();
	restored.fromInfo(change);
	EXPECT_TRUE(restored.capturedMdrPenetration.isNull());
	spells::ObstacleCasterProxy proxy(PlayerColor(0), nullptr, restored);
	EXPECT_TRUE(proxy.getCapturedMdrPenetration().isNull());
}
