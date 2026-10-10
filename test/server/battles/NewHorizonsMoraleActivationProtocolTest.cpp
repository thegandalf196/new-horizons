/*
 * NewHorizonsMoraleActivationProtocolTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMapInfo.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "FullGameSnapshotTypes.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
UnitChanges markedUpdate()
{
	UnitChanges update(70, UnitChanges::EOperation::UPDATE);
	update.data["state"]["hadMorale"].Bool() = true;
	update.data["state"]["moraleExtraActivation"].Bool() = true;
	return update;
}
template<typename Pack> void rejectsBeforePrefix(Pack & pack)
{
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_IMPLOSION;
	EXPECT_THROW(wire.oser & pack, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
class CorruptMoraleWriter
{
	BinarySerializer & writer;
	JsonNode replacement;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	CorruptMoraleWriter(BinarySerializer & writer, JsonNode replacement)
		: writer(writer), replacement(std::move(replacement)) {}
	bool hasFeature(Version version) const { return writer.hasFeature(version); }
	template<typename T> CorruptMoraleWriter & operator&(T & value) { writer & value; return *this; }
	CorruptMoraleWriter & operator&(JsonNode & value) { writer & replacement; return *this; }
};
class NewHorizonsMoraleActivationProtocolTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(75));
	}
};
}

TEST(NewHorizonsMoraleActivationProtocolRulesTest, EveryOwningUnitEnvelopeRejectsOldWriterBeforePrefix)
{
	auto update = markedUpdate();
	rejectsBeforePrefix(update);
	BattleUnitsChanged units; units.battleID = BattleID(0); units.changedStacks.push_back(update);
	rejectsBeforePrefix(units);
	BattleStackAttacked hit; hit.newState = update;
	rejectsBeforePrefix(hit);
	BattleAttack attack; attack.battleID = BattleID(0); attack.attackerChanges = units;
	rejectsBeforePrefix(attack);
	attack.attackerChanges.changedStacks.clear(); attack.bsa.push_back(hit);
	rejectsBeforePrefix(attack);
	StacksInjured injuries; injuries.battleID = BattleID(0); injuries.stacks.push_back(hit);
	rejectsBeforePrefix(injuries);
}

TEST(NewHorizonsMoraleActivationProtocolRulesTest, CurrentPacketRoundTripPreservesExactOrigin)
{
	auto update = markedUpdate();
	const auto originalPayload = update.data;
	ASSERT_EQ(originalPayload["state"].Struct().size(), 2u);
	// Existing UnitChanges preflights use non-const lookup for these optional
	// histories, materializing null defaults before h & data. Model exactly that
	// normalization; full variant equality still detects any added/lost/retagged value.
	auto expectedPayload = originalPayload;
	expectedPayload["state"]["veteranPhysicalDamageSinceActivation"] = JsonNode();
	expectedPayload["state"]["activationMovementBonus"] = JsonNode();
	BattleUnitsChanged source; source.battleID = BattleID(0); source.changedStacks.push_back(update);
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & source);
	EXPECT_EQ(update.data, originalPayload);
	EXPECT_EQ(source.changedStacks.front().data, expectedPayload);
	BattleUnitsChanged restored;
	ASSERT_NO_THROW(wire.iser & restored);
	ASSERT_EQ(restored.changedStacks.size(), 1u);
	const auto & decoded = restored.changedStacks.front();
	EXPECT_EQ(decoded.data, expectedPayload);
	EXPECT_EQ(decoded.id, update.id);
	EXPECT_EQ(decoded.operation, update.operation);
	EXPECT_EQ(decoded.healthDelta, update.healthDelta);
	EXPECT_EQ(restored.battleID, source.battleID);
	EXPECT_FALSE(restored.rebirthChainConsumption.has_value());
	EXPECT_FALSE(restored.phoenixSparkConsumption.has_value());
	EXPECT_TRUE(decoded.data["state"]["hadMorale"].isBool());
	EXPECT_TRUE(decoded.data["state"]["moraleExtraActivation"].isBool());
	EXPECT_TRUE(battle::hasMoraleActivationState(restored.changedStacks.front().data));
}

TEST(NewHorizonsMoraleActivationProtocolRulesTest, MalformedCurrentReadRejectsBeforeGameplayApply)
{
	for(int corruption = 0; corruption < 3; ++corruption)
	{
		auto source = markedUpdate();
		auto malformed = source.data;
		if(corruption == 0) malformed["state"]["moraleExtraActivation"] = JsonNode(1);
		if(corruption == 1) malformed["state"]["hadMorale"].Bool() = false;
		if(corruption == 2) malformed["state"] = JsonNode(1);
		CMemorySerializer wire;
		CorruptMoraleWriter writer(wire.oser, malformed);
		ASSERT_NO_THROW(source.serialize(writer));
		UnitChanges restored;
		EXPECT_THROW(wire.iser & restored, std::runtime_error);
		// A packet is rejected at decode, before an authoritative visitor receives it.
		EXPECT_TRUE(battle::hasMoraleActivationState(source.data));
	}
}

TEST(NewHorizonsMoraleActivationProtocolRulesTest, OrdinaryOldReadReplacesStaleMarkedPayloadWithAbsentDefault)
{
	UnitChanges source(70, UnitChanges::EOperation::UPDATE);
	source.data["state"]["hadMorale"].Bool() = false;
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_IMPLOSION;
	ASSERT_NO_THROW(wire.oser & source);
	auto restored = markedUpdate();
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_EQ(restored.data, source.data);
	EXPECT_FALSE(battle::hasMoraleActivationState(restored.data));
}

TEST_F(NewHorizonsMoraleActivationProtocolTest, BattleStartRejectsCapturedNonlegacyPercentBeforeOwningPrefixAndNullInfoIsCompatible)
{
	startGame(); startBattle();
	BattleStart source; source.battleID = BattleID(0);
	source.info = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_EQ(source.info->getMoraleExtraDamagePercent(), 75);
	rejectsBeforePrefix(source);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & source);
	current.iser.cb = gameState().get();
	BattleStart currentRestored;
	ASSERT_NO_THROW(current.iser & currentRestored);
	ASSERT_NE(currentRestored.info, nullptr);
	EXPECT_EQ(currentRestored.info->getMoraleExtraDamagePercent(), 75);
	BattleStart empty; empty.battleID = BattleID(0);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_IMPLOSION;
	ASSERT_NO_THROW(wire.oser & empty);
	BattleStart restored;
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_FALSE(restored.info);
}
