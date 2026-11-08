/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include <algorithm>
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
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	const auto before = OwnedBonusCaptureCodec::current().version();
	ESerializationVersion observed = ESerializationVersion::NONE;
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint, &observed);
	EXPECT_EQ(observed, ESerializationVersion::CURRENT);
	EXPECT_TRUE(capabilities.passiveFactionSkills);
	EXPECT_EQ(before, ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(observed, capabilities).version(), before);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(), before);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), before);
	PlayerStartsTurn turn;
	turn.player = PlayerColor(2);
	connection.sendPack(turn);
	const auto bytes = endpoint->sent;
	auto decoded = connection.retrievePack(bytes);
	const auto * restored = dynamic_cast<const PlayerStartsTurn *>(decoded.get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->player, turn.player);
	connection.captureSpecialtySerializationContext(endpoint, &observed);
	EXPECT_EQ(observed, ESerializationVersion::CURRENT);
	connection.sendPack(turn);
	EXPECT_EQ(endpoint->sent, bytes);
}
TEST(GameConnectionSourceCapabilitiesTest, ExplicitKnown1153IsLocalAndRejectsUnknownShapes)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(), ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(),
		ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	auto partial = capabilities; partial.ordinaryCombatSpecialties = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(partial), std::invalid_argument);
	newHorizonsHeroes::SpecialtySerializationContext unknown;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(unknown), std::invalid_argument);
}
TEST(GameConnectionSourceCapabilitiesTest, WrongAliasedOrExpiredConnectionCannotIssueSourceCapabilities)
{
	auto endpoint = std::make_shared<Endpoint>(), foreign = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign), std::logic_error);
	std::shared_ptr<INetworkConnection> alias(endpoint.get(), [](INetworkConnection *) {});
	EXPECT_THROW(connection.captureSpecialtySerializationContext(alias), std::logic_error);
	alias.reset(); endpoint.reset();
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign), std::logic_error);
}
TEST(GameConnectionSourceCapabilitiesTest, UnknownEpochRefusesEvenWhenNamedFeatureMaskMatchesKnown1155)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(static_cast<ESerializationVersion>(std::max(static_cast<int32_t>(ESerializationVersion::CURRENT), static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY)) + 1));
	EXPECT_THROW(connection.captureSpecialtySerializationContext(endpoint), std::logic_error);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(),
		ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
}

TEST(GameConnectionSourceCapabilitiesTest, Established1151ReadWriteEpochDoesNotFollowTheDefaultWriterAlias)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(),
		ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_FALSE(capabilities.extendedOrdinarySpecialties);
}

TEST(GameConnectionSourceCapabilitiesTest, Neutral1152SeparatesActualWireEpochFrom1151BonusProfile)
{
	auto sending = std::make_shared<Endpoint>(), receiving = std::make_shared<Endpoint>();
	GameConnection writer(sending), reader(receiving);
	writer.setSerializationVersion(static_cast<ESerializationVersion>(1152));
	reader.setSerializationVersion(static_cast<ESerializationVersion>(1152));
	ESerializationVersion before = ESerializationVersion::NONE, after = ESerializationVersion::NONE;
	const auto source = writer.captureSpecialtySerializationContext(sending, &before);
	HeroLevelUp offer; offer.queryID = QueryID(7); offer.heroId = ObjectInstanceID(3);
	writer.sendPack(offer); const auto bytes = sending->sent;
	auto decoded = reader.retrievePack(bytes);
	const auto target = reader.captureSpecialtySerializationContext(receiving, &after);
	EXPECT_EQ(static_cast<int32_t>(before), 1152); EXPECT_EQ(static_cast<int32_t>(after), 1152);
	EXPECT_EQ(static_cast<int32_t>(OwnedBonusCaptureCodec::fromSourceCapabilities(source).version()), 1151);
	EXPECT_EQ(source, target); EXPECT_FALSE(source.extendedOrdinarySpecialties);
	const auto * restored = dynamic_cast<const HeroLevelUp *>(decoded.get());
	ASSERT_NE(restored, nullptr); EXPECT_EQ(restored->queryID, QueryID(7)); EXPECT_EQ(restored->heroId, ObjectInstanceID(3));
	writer.sendPack(offer); EXPECT_EQ(sending->sent, bytes);
}

TEST(GameConnectionSourceCapabilitiesTest, ReservedProfilesObserveExactEpochAndNeverGuessUnknown)
{
	for(const auto [wire, profile] : {std::pair{1151, 1151}, {1152, 1151}, {1153, 1153}, {1154, 1153}, {1155, 1155}, {1156, 1155}})
	{
		auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
		connection.setSerializationVersion(static_cast<ESerializationVersion>(wire));
		ESerializationVersion observed = ESerializationVersion::NONE;
		const auto context = connection.captureSpecialtySerializationContext(endpoint, &observed);
		EXPECT_EQ(static_cast<int32_t>(observed), wire);
		EXPECT_EQ(static_cast<int32_t>(OwnedBonusCaptureCodec::fromSourceCapabilities(context).version()), profile);
	}
}

TEST(GameConnectionSourceCapabilitiesTest, FailedEpochCaptureLeavesSentinelAndExactOwnerChecksIntact)
{
	auto endpoint = std::make_shared<Endpoint>(), foreign = std::make_shared<Endpoint>();
	GameConnection connection(endpoint); connection.setSerializationVersion(static_cast<ESerializationVersion>(1152));
	ESerializationVersion observed = ESerializationVersion::NONE;
	std::shared_ptr<INetworkConnection> separateOwner(endpoint.get(), [](INetworkConnection *) {});
	std::shared_ptr<INetworkConnection> separatePointer(endpoint, foreign.get());
	for(const auto & invalid : std::vector<std::shared_ptr<INetworkConnection>>{foreign, separateOwner, separatePointer, {}})
	{
		EXPECT_THROW(connection.captureSpecialtySerializationContext(invalid, &observed), std::logic_error);
		EXPECT_EQ(observed, ESerializationVersion::NONE);
	}
	connection.setSerializationVersion(static_cast<ESerializationVersion>(static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN) + 1));
	EXPECT_THROW(connection.captureSpecialtySerializationContext(endpoint, &observed), std::invalid_argument);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	separateOwner.reset(); separatePointer.reset(); endpoint.reset();
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
}

TEST(GameConnectionSourceCapabilitiesTest, Recognized1154KeepsExactOptionalWireEnvelopeAnd1153BonusProfile)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	const auto extended = connection.captureSpecialtySerializationContext(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	HeroLevelUp offer; offer.reachedLevel = 6; offer.queryID = QueryID(9); offer.replacesQueryID = QueryID(8);
	connection.sendPack(offer);
	const auto before = endpoint->sent;
	ESerializationVersion observedEpoch = ESerializationVersion::NONE;
	const auto optional = connection.captureSpecialtySerializationContext(endpoint, &observedEpoch);
	EXPECT_EQ(observedEpoch, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	EXPECT_EQ(optional, extended);
	EXPECT_TRUE(optional.extendedOrdinarySpecialties);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(optional).version(),
		ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	connection.sendPack(offer);
	EXPECT_EQ(endpoint->sent, before); // Capture must not rewrite the actual transport epoch.
	auto decoded = connection.retrievePack(endpoint->sent);
	const auto * restored = dynamic_cast<const HeroLevelUp *>(decoded.get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->reachedLevel, 6); EXPECT_EQ(restored->queryID, QueryID(9));
	EXPECT_EQ(restored->replacesQueryID, QueryID(8)); EXPECT_FALSE(restored->optionalRerollAvailable);
}

TEST(GameConnectionSourceCapabilitiesTest, Recognized1154DoesNotRelaxExactConnectionOwnership)
{
	auto endpoint = std::make_shared<Endpoint>(), foreign = std::make_shared<Endpoint>();
	GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign), std::logic_error);
	std::shared_ptr<INetworkConnection> samePointerDifferentOwner(endpoint.get(), [](INetworkConnection *) {});
	EXPECT_THROW(connection.captureSpecialtySerializationContext(samePointerDifferentOwner), std::logic_error);
	std::shared_ptr<INetworkConnection> sameOwnerDifferentPointer(endpoint, foreign.get());
	EXPECT_THROW(connection.captureSpecialtySerializationContext(sameOwnerDifferentPointer), std::logic_error);
	EXPECT_THROW(connection.captureSpecialtySerializationContext({}), std::logic_error);
	samePointerDifferentOwner.reset(); sameOwnerDifferentPointer.reset(); endpoint.reset();
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign), std::logic_error);
}

TEST(GameConnectionSourceCapabilitiesTest, SuccessfulEpochObservationDistinguishesAllRecognizedWireProfiles)
{
	for(const auto epoch : {ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION,
		ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY})
	{
		auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
		connection.setSerializationVersion(epoch);
		ESerializationVersion observed = ESerializationVersion::NONE;
		const auto context = connection.captureSpecialtySerializationContext(endpoint, &observed);
		EXPECT_EQ(observed, epoch);
		if(epoch == ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES
			|| epoch == ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION)
			EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(context).version(), ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
		else
			EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(context).version(), ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	}
}

TEST(GameConnectionSourceCapabilitiesTest, FailedEpochObservationNeverWritesTheCallerOutput)
{
	auto endpoint = std::make_shared<Endpoint>(), foreign = std::make_shared<Endpoint>();
	GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	ESerializationVersion observed = ESerializationVersion::NONE;
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	EXPECT_THROW(connection.captureSpecialtySerializationContext({}, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	std::shared_ptr<INetworkConnection> separateOwner(endpoint.get(), [](INetworkConnection *) {});
	EXPECT_THROW(connection.captureSpecialtySerializationContext(separateOwner, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	std::shared_ptr<INetworkConnection> differentPointer(endpoint, foreign.get());
	EXPECT_THROW(connection.captureSpecialtySerializationContext(differentPointer, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	connection.setSerializationVersion(static_cast<ESerializationVersion>(static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN) + 1));
	EXPECT_THROW(connection.captureSpecialtySerializationContext(endpoint, &observed), std::invalid_argument);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
	separateOwner.reset(); differentPointer.reset(); endpoint.reset();
	EXPECT_THROW(connection.captureSpecialtySerializationContext(foreign, &observed), std::logic_error);
	EXPECT_EQ(observed, ESerializationVersion::NONE);
}

TEST(GameConnectionSourceCapabilitiesTest, Independent1154WriterReaderObserveRealWireAndSeparateBonusProfiles)
{
	auto sending = std::make_shared<Endpoint>(), receiving = std::make_shared<Endpoint>();
	GameConnection writer(sending), reader(receiving);
	writer.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	reader.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	ESerializationVersion before = ESerializationVersion::NONE, after = ESerializationVersion::NONE;
	const auto source = writer.captureSpecialtySerializationContext(sending, &before);
	HeroLevelUp offer; offer.reachedLevel = 3; offer.optionalRerollAvailable = true; offer.queryID = QueryID(7);
	writer.sendPack(offer);
	auto decoded = reader.retrievePack(sending->sent);
	const auto target = reader.captureSpecialtySerializationContext(receiving, &after);
	EXPECT_EQ(before, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	EXPECT_EQ(after, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(source).version(), ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(target).version(), ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(source, target);
	const auto * restored = dynamic_cast<const HeroLevelUp *>(decoded.get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->reachedLevel, 3); EXPECT_EQ(restored->queryID, QueryID(7));
	EXPECT_TRUE(restored->optionalRerollAvailable); EXPECT_EQ(restored->replacesQueryID, QueryID::NONE);
}

TEST(GameConnectionSourceCapabilitiesTest, PassiveFactionEpochHasAnExplicitCompleteOwnedCopyProfile)
{
	auto endpoint = std::make_shared<Endpoint>();
	GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint);
	EXPECT_TRUE(capabilities.passiveFactionSkills);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(),
		ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	auto incomplete = capabilities;
	incomplete.extendedOrdinarySpecialties = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(incomplete), std::invalid_argument);
}
