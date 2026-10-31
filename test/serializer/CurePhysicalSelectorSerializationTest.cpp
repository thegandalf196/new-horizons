/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/gameState/ReplayLog.h"
#include <algorithm>
#include <array>
#include <string_view>
namespace
{
using Version = ESerializationVersion;
constexpr auto successor = Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
constexpr auto oldCurrent = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
Version beforeFrozen() { return static_cast<Version>(static_cast<int>(Version::NEW_HORIZONS_FROZEN) - 1); }
BattleAction action(std::string kind)
{
	BattleAction result;
	result.side = BattleSide::ATTACKER;
	result.stackNumber = 7;
	result.actionType = EActionType::HERO_SPELL;
	result.spell = SpellID::CURE;
	result.target.push_back({7, BattleHex(3, 5)});
	result.spellCurePhysicalAffliction = std::move(kind);
	return result;
}
MakeAction request(const std::string & kind)
{
	MakeAction result(action(kind));
	result.player = PlayerColor(0);
	result.requestID = 17;
	result.battleID = BattleID(0);
	return result;
}
StartAction announcement(const std::string & kind)
{
	StartAction result(action(kind));
	result.battleID = BattleID(0);
	return result;
}
newHorizonsCrisisCommand::ReturnFrame frame(const std::string & kind)
{
	newHorizonsCrisisCommand::ReturnFrame result;
	result.responder = BattleSide::ATTACKER;
	result.anchor = 7;
	result.grant = 1;
	result.round = 1;
	result.kind = newHorizonsCrisisCommand::ReturnKind::ACTION;
	result.originalActor = -1;
	result.action = action(kind);
	return result;
}
newHorizonsCrisisCommand::State suspension(const std::string & kind)
{
	newHorizonsCrisisCommand::State result;
	result.used[0] = true;
	result.returns.push_back(frame(kind));
	return result;
}
BattleCrisisCommandChanged transition(const std::string & kind)
{
	BattleCrisisCommandChanged result;
	result.battleID = BattleID(0);
	result.state = suspension(kind);
	return result;
}
template<class T> std::vector<std::byte> encode(T & owner, Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	owner.serialize(wire.oser);
	return wire.extractBuffer();
}
template<class T> void rejectPrefix(T & owner, Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	EXPECT_THROW(owner.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
template<class T> T decode(std::vector<std::byte> bytes, Version version)
{
	CMemorySerializer wire(std::move(bytes));
	wire.iser.version = version;
	T result;
	result.serialize(wire.iser);
	return result;
}
template<class T> void rejectedHistoricalRead(T & owner)
{
	const auto bytes = encode(owner, successor);
	for(const auto version : {Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES,
		Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES, oldCurrent})
	{
		SCOPED_TRACE(static_cast<int>(version));
		EXPECT_THROW((void)decode<T>(bytes, version), std::runtime_error);
	}
}
}
TEST(CurePhysicalSelectorSerialization, GraveDustRejectsDirectAndEverySupportedActionOwnerBeforePrefix)
{
	auto direct = action("graveDust");
	auto server = request("graveDust");
	auto client = announcement("graveDust");
	auto returned = frame("graveDust");
	auto state = suspension("graveDust");
	auto update = transition("graveDust");
	ASSERT_NO_THROW(state.validateShape());
	ASSERT_NO_THROW(update.validateShape());
	for(const auto version : {beforeFrozen(), Version::NEW_HORIZONS_FROZEN,
		Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES,
		Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES, oldCurrent})
	{
		SCOPED_TRACE(static_cast<int>(version));
		rejectPrefix(direct, version); rejectPrefix(server, version); rejectPrefix(client, version);
		rejectPrefix(returned, version); rejectPrefix(state, version); rejectPrefix(update, version);
	}
	EXPECT_EQ(direct.spellCurePhysicalAffliction, "graveDust");
	EXPECT_EQ(server.ba, direct);
	EXPECT_EQ(client.ba, direct);
}
TEST(CurePhysicalSelectorSerialization, Exact1152DirectRequestAndAnnouncementRoundtripAndResaveKeepBytes)
{
	auto source = action("graveDust");
	const auto bytes = encode(source, successor);
	auto restored = decode<BattleAction>(bytes, successor);
	EXPECT_EQ(restored, source);
	EXPECT_EQ(encode(restored, successor), bytes);
	auto server = request("graveDust");
	const auto serverBytes = encode(server, successor);
	auto restoredServer = decode<MakeAction>(serverBytes, successor);
	EXPECT_EQ(restoredServer.ba, server.ba);
	EXPECT_EQ(restoredServer.player, server.player);
	EXPECT_EQ(restoredServer.requestID, server.requestID);
	EXPECT_EQ(restoredServer.battleID, server.battleID);
	EXPECT_EQ(encode(restoredServer, successor), serverBytes);
	auto client = announcement("graveDust");
	const auto clientBytes = encode(client, successor);
	auto restoredClient = decode<StartAction>(clientBytes, successor);
	EXPECT_EQ(restoredClient.ba, client.ba);
	EXPECT_EQ(restoredClient.battleID, client.battleID);
	EXPECT_FALSE(restoredClient.focusFire);
	EXPECT_FALSE(restoredClient.orderState);
	EXPECT_EQ(encode(restoredClient, successor), clientBytes);
}
TEST(CurePhysicalSelectorSerialization, HistoricalFieldBearingReadersRefuseGraveDustAcrossAllActionOwners)
{
	auto direct = action("graveDust"); auto server = request("graveDust"); auto client = announcement("graveDust");
	auto returned = frame("graveDust"); auto state = suspension("graveDust"); auto update = transition("graveDust");
	rejectedHistoricalRead(direct); rejectedHistoricalRead(server); rejectedHistoricalRead(client);
	rejectedHistoricalRead(returned); rejectedHistoricalRead(state); rejectedHistoricalRead(update);
}
TEST(CurePhysicalSelectorSerialization, OnlyExactFrozenAndGraveDustNamesAreAdmitted)
{
	for(const std::string kind : {"poison", "disease", "stoneGaze", "GraveDust", "graveDust "})
	{
		auto direct = action(kind); auto server = request(kind); auto client = announcement(kind);
		rejectPrefix(direct, successor); rejectPrefix(server, successor); rejectPrefix(client, successor);
	}
	for(const std::string kind : {"", "frozen", "graveDust"})
	{
		auto direct = action(kind);
		EXPECT_NO_THROW(encode(direct, successor));
	}
}
TEST(CurePhysicalSelectorSerialization, MalformedDecodedNameFailsClosedWithoutInventingGenericAfflictionAdmission)
{
	auto source = action("graveDust");
	auto bytes = encode(source, successor);
	const std::string original = "graveDust", invalid = "stoneGaze";
	ASSERT_EQ(original.size(), invalid.size());
	std::vector<std::byte> text;
	for(const unsigned char ch : original) text.push_back(static_cast<std::byte>(ch));
	const auto at = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
	ASSERT_NE(at, bytes.end());
	ASSERT_EQ(std::search(at + text.size(), bytes.end(), text.begin(), text.end()), bytes.end());
	for(size_t i = 0; i < invalid.size(); ++i) at[i] = static_cast<std::byte>(invalid[i]);
	EXPECT_THROW((void)decode<BattleAction>(bytes, successor), std::runtime_error);
	EXPECT_THROW((void)decode<BattleAction>(bytes, oldCurrent), std::runtime_error);
}
TEST(CurePhysicalSelectorSerialization, PreFrozenReadsClearReusedSelectorsAndFrozenBoundaryRemainsHistorical)
{
	auto empty = action("");
	const auto bytes = encode(empty, beforeFrozen());
	CMemorySerializer wire(bytes);
	wire.iser.version = beforeFrozen();
	auto reused = action("graveDust");
	ASSERT_NO_THROW(reused.serialize(wire.iser));
	EXPECT_TRUE(reused.spellCurePhysicalAffliction.empty());
	EXPECT_EQ(reused, empty);
	EXPECT_EQ(encode(reused, beforeFrozen()), bytes);
	auto server = request("");
	CMemorySerializer serverWire(encode(server, beforeFrozen()));
	serverWire.iser.version = beforeFrozen();
	auto reusedServer = request("graveDust");
	ASSERT_NO_THROW(reusedServer.serialize(serverWire.iser));
	EXPECT_TRUE(reusedServer.ba.spellCurePhysicalAffliction.empty());
	auto frozen = action("frozen");
	rejectPrefix(frozen, beforeFrozen());
	const auto frozenBytes = encode(frozen, Version::NEW_HORIZONS_FROZEN);
	EXPECT_EQ(decode<BattleAction>(frozenBytes, Version::NEW_HORIZONS_FROZEN), frozen);
}
TEST(CurePhysicalSelectorSerialization, EmptyAndFrozen1151BytesStayIdenticalAt1152ForDirectAndOwnedForms)
{
	for(const std::string kind : {"", "frozen"})
	{
		auto direct = action(kind); auto server = request(kind); auto client = announcement(kind);
		auto returned = frame(kind); auto state = suspension(kind); auto update = transition(kind);
		EXPECT_EQ(encode(direct, oldCurrent), encode(direct, successor));
		EXPECT_EQ(encode(server, oldCurrent), encode(server, successor));
		EXPECT_EQ(encode(client, oldCurrent), encode(client, successor));
		EXPECT_EQ(encode(returned, oldCurrent), encode(returned, successor));
		EXPECT_EQ(encode(state, oldCurrent), encode(state, successor));
		EXPECT_EQ(encode(update, oldCurrent), encode(update, successor));
	}
}
TEST(CurePhysicalSelectorSerialization, CrisisOwnedActionShapeIsRetainedAndCurrentResaveIsExact)
{
	auto source = suspension("graveDust");
	ASSERT_NO_THROW(source.validateShape()); // Existing shape already permits completed HERO_SPELL actions.
	const auto bytes = encode(source, successor);
	auto restored = decode<newHorizonsCrisisCommand::State>(bytes, successor);
	EXPECT_EQ(restored.returns, source.returns);
	EXPECT_EQ(restored.used, source.used);
	EXPECT_EQ(encode(restored, successor), bytes);
	source.returns.front().action.actionType = EActionType::RETREAT;
	EXPECT_THROW(source.validateShape(), std::runtime_error); // No new gameplay or shape legality.
}
TEST(CurePhysicalSelectorSerialization, ExistingUnrelatedWritePreflightStillRejectsBeforeDirectPayload)
{
	auto source = action("");
	source.spellOvercharge = 1;
	const auto before = source;
	const auto old = static_cast<Version>(static_cast<int>(Version::NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE) - 1);
	rejectPrefix(source, old);
	EXPECT_EQ(source, before);
}
TEST(CurePhysicalSelectorSerialization, ReplayAnnouncementUsesDeclaredCodecWithoutApplyingAction)
{
	auto source = announcement("graveDust");
	const auto bytes = encode(source, successor);
	auto restored = decode<StartAction>(bytes, successor);
	EXPECT_EQ(restored.battleID, source.battleID);
	EXPECT_EQ(restored.ba, source.ba);
	EXPECT_EQ(encode(restored, successor), bytes);

	// The reserved codec is testable without activating the default replay writer.
	ASSERT_EQ(Version::CURRENT, oldCurrent);
	EXPECT_THROW(ReplayPackSerializer::write(source), std::runtime_error);
	auto legacy = announcement("frozen");
	const auto control = ReplayPackSerializer::write(legacy);
	EXPECT_EQ(control.version, Version::CURRENT);
	auto decoded = ReplayPackSerializer::read(control, nullptr);
	const auto * replayed = dynamic_cast<StartAction *>(decoded.get());
	ASSERT_NE(replayed, nullptr);
	EXPECT_EQ(replayed->ba, legacy.ba);

	// Keep the actual replay-reader negative using its real pointer envelope.
	CMemorySerializer explicitWire;
	explicitWire.oser.version = successor;
	const CPackForClient * pointer = &source;
	explicitWire.oser & pointer;
	ReplayPayload historical(explicitWire.extractBuffer(), oldCurrent);
	EXPECT_THROW(ReplayPackSerializer::read(historical, nullptr), std::runtime_error);
}
