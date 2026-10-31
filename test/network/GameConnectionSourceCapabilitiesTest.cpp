/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include <algorithm>
namespace
{
class Endpoint final : public INetworkConnection
{
public:
	void sendPacket(const std::vector<std::byte> &) override {}
	void setAsyncWritesEnabled(bool) override {}
	void close() override {}
};
}
TEST(GameConnectionSourceCapabilitiesTest, ActualCurrentCapabilityLeavesDefaultCloneAndPersistenceEpochUntouched)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	const auto before = OwnedBonusCaptureCodec::current().version();
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(), before);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), before);
}
TEST(GameConnectionSourceCapabilitiesTest, ExplicitKnown1152IsLocalAndRejectsUnknownShapes)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	const auto capabilities = connection.captureSpecialtySerializationContext(endpoint);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities).version(), ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), ESerializationVersion::CURRENT);
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
TEST(GameConnectionSourceCapabilitiesTest, UnknownEpochRefusesEvenWhenNamedFeatureMaskMatchesKnown1152)
{
	auto endpoint = std::make_shared<Endpoint>(); GameConnection connection(endpoint);
	connection.setSerializationVersion(static_cast<ESerializationVersion>(std::max(static_cast<int32_t>(ESerializationVersion::CURRENT), static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)) + 1));
	EXPECT_THROW(connection.captureSpecialtySerializationContext(endpoint), std::logic_error);
	EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), ESerializationVersion::CURRENT);
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
