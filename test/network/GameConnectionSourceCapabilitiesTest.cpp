/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>
namespace
{
class Endpoint final : public INetworkConnection
{
public:
	std::vector<std::byte> sent;
	void sendPacket(const std::vector<std::byte> & packet) override { sent = packet; }
	void setAsyncWritesEnabled(bool) override {}
	void close() override {}
};
}
TEST(GameConnectionSourceCapabilitiesTest, ActualCurrentCapabilityLeavesDefaultCloneAndPersistenceEpochUntouched)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	const auto profile = OwnedBonusCaptureCodec::current().version();
	ESerializationVersion observed = ESerializationVersion::NONE;
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint, &observed);
	EXPECT_EQ(observed, ESerializationVersion::CURRENT);
	EXPECT_TRUE(capabilities.passiveFactionSkills);
	EXPECT_EQ(profile, ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(observed, capabilities).version(), profile);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(), profile);
	PlayerStartsTurn turn; turn.player = PlayerColor(2);
	connection.sendPack(turn); const auto bytes = endpoint->sent;
	auto decoded = connection.retrievePack(bytes);
	const auto * restored = dynamic_cast<const PlayerStartsTurn *>(decoded.get());
	ASSERT_NE(restored, nullptr); EXPECT_EQ(restored->player, turn.player);
	connection.captureSpecialtySerializationContext(endpoint, &observed);
	EXPECT_EQ(observed, ESerializationVersion::CURRENT);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), profile);
	connection.sendPack(turn); EXPECT_EQ(endpoint->sent, bytes);
}

TEST(GameConnectionSourceCapabilitiesTest, HistoricalMemoryProfilesRemainExplicitWithoutLiveConnectionAdmission)
{
	// Pure Bonus-copy codec fixtures do not admit an old live protocol or save payload.
	for(const auto [wire, profile] : {std::pair{1151, 1151}, {1152, 1151}, {1153, 1153},
		{1154, 1153}, {1155, 1155}, {1156, 1155}, {1157, 1157}, {1158, 1158},
		{1159, 1159}, {1160, 1159}, {1161, 1159}})
	{
		CMemorySerializer memory; memory.oser.version = static_cast<ESerializationVersion>(wire);
		const auto context = newHorizonsHeroes::SpecialtySerializationContext::fromHandler(memory.oser);
		EXPECT_EQ(static_cast<int32_t>(OwnedBonusCaptureCodec::fromSourceEpoch(memory.oser.version, context).version()), profile);
		EXPECT_EQ(static_cast<int32_t>(OwnedBonusCaptureCodec::fromSourceCapabilities(context).version()), profile);
		EXPECT_EQ(static_cast<int32_t>(memory.oser.version), wire);
	}
}

TEST(GameConnectionSourceCapabilitiesTest, PartialUnknownAndMismatchedBonusProfilesReject)
{
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	const auto capabilities = newHorizonsHeroes::SpecialtySerializationContext::fromHandler(memory.oser);
	auto partial = capabilities; partial.ordinaryCombatSpecialties = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(partial), std::invalid_argument);
	newHorizonsHeroes::SpecialtySerializationContext unknown;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(unknown), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(ESerializationVersion::CURRENT, capabilities), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(static_cast<ESerializationVersion>(1163), capabilities), std::invalid_argument);
	memory.iser.version = ESerializationVersion::CURRENT;
	auto incompleteCurrent = newHorizonsHeroes::SpecialtySerializationContext::fromHandler(memory.iser);
	incompleteCurrent.extendedOrdinarySpecialties = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(incompleteCurrent), std::invalid_argument);
}

TEST(GameConnectionSourceCapabilitiesTest, WrongAliasedOrExpiredConnectionCannotIssueSourceCapabilities)
{
	auto endpoint = std::make_shared<Endpoint>(), foreign = std::make_shared<Endpoint>();
	GameConnection connection(endpoint);
	std::shared_ptr<INetworkConnection> samePointerDifferentOwner(endpoint.get(), [](INetworkConnection *) {});
	std::shared_ptr<INetworkConnection> sameOwnerDifferentPointer(endpoint, foreign.get());
	for(const auto & invalid : std::vector<std::shared_ptr<INetworkConnection>>{
		foreign, samePointerDifferentOwner, sameOwnerDifferentPointer, {}})
	{
		ESerializationVersion observed = ESerializationVersion::NONE;
		EXPECT_THROW(connection.captureSpecialtySerializationContext(invalid, &observed), std::logic_error);
		EXPECT_EQ(observed, ESerializationVersion::NONE);
	}
	samePointerDifferentOwner.reset(); sameOwnerDifferentPointer.reset(); endpoint.reset();
	ESerializationVersion observed = ESerializationVersion::NONE;
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
}

TEST(GameConnectionSourceCapabilitiesTest, UnsupportedLiveEpochRejectsBeforeChangingWireOrSourceCapabilities)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	PlayerStartsTurn turn; turn.player = PlayerColor(2);
	connection.sendPack(turn); const auto bytes = endpoint->sent;
	const auto original = connection.captureSpecialtySerializationContext(endpoint);
	for(const int epoch : {0, 893, 1151, 1152, 1153, 1154, 1155, 1156, 1157, 1158, 1159, 1160, 1161, 1163})
	{
		SCOPED_TRACE(epoch);
		EXPECT_THROW(connection.setSerializationVersion(static_cast<ESerializationVersion>(epoch)), std::runtime_error);
		EXPECT_EQ(endpoint->sent, bytes);
		ESerializationVersion observed = ESerializationVersion::NONE;
		EXPECT_EQ(connection.captureSpecialtySerializationContext(endpoint, &observed), original);
		EXPECT_EQ(observed, ESerializationVersion::CURRENT);
		connection.sendPack(turn); EXPECT_EQ(endpoint->sent, bytes);
	}
}

TEST(GameConnectionSourceCapabilitiesTest, IndependentCurrentWriterReaderKeepWireAndBonusProfilesSeparate)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto sending = std::make_shared<Endpoint>(), receiving = std::make_shared<Endpoint>();
	GameConnection writer(sending), reader(receiving);
	writer.setSerializationVersion(ESerializationVersion::CURRENT);
	reader.setSerializationVersion(ESerializationVersion::CURRENT);
	ESerializationVersion before = ESerializationVersion::NONE, after = ESerializationVersion::NONE;
	const auto source = writer.captureSpecialtySerializationContext(sending, &before);
	HeroLevelUp offer; offer.reachedLevel = 3; offer.optionalRerollAvailable = true;
	offer.queryID = QueryID(7); offer.replacesQueryID = QueryID(6); offer.heroId = ObjectInstanceID(3);
	writer.sendPack(offer); const auto bytes = sending->sent;
	auto decoded = reader.retrievePack(bytes);
	const auto target = reader.captureSpecialtySerializationContext(receiving, &after);
	EXPECT_EQ(before, ESerializationVersion::CURRENT); EXPECT_EQ(after, ESerializationVersion::CURRENT);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(before, source).version(), ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(after, target).version(), ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY);
	EXPECT_EQ(source, target);
	const auto * restored = dynamic_cast<const HeroLevelUp *>(decoded.get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->reachedLevel, 3); EXPECT_EQ(restored->queryID, QueryID(7));
	EXPECT_EQ(restored->heroId, ObjectInstanceID(3)); EXPECT_TRUE(restored->optionalRerollAvailable);
	EXPECT_EQ(restored->replacesQueryID, QueryID(6));
	writer.sendPack(offer); EXPECT_EQ(sending->sent, bytes);
}
