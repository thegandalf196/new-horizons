/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/GameLibrary.h"
namespace
{
using Version = ESerializationVersion;
auto capabilities(Version version)
{
	CMemorySerializer codec; codec.oser.version = version;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(codec.oser);
}
JsonNode captured(bool extended)
{
	JsonNode result; result["additionalSpecialties"]["version"].Integer() = 1;
	auto & policy = result["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["version"].Integer() = 1; policy["closeQuartersDrill"].Integer() = policy["marshSharpshooter"].Integer() = 1;
	for(const auto * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"}) policy[key].Integer() = 0;
	if(extended) policy["cinderReprisalDamagePercent"].Integer() = 0; // Presence incl0 requires the exact reserved capability.
	return result;
}
std::shared_ptr<Bonus> burn()
{
	const auto hero = HeroTypeID(HeroTypeID::decode("core:rashka"));
	return std::make_shared<Bonus>(newHorizonsCreatureBurn::marker(hero, 1, 7));
}
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
}
TEST(OwnedBonusCaptureCodecTest, Explicit1152CloneAdmitsCanonicalBurnWithoutChangingDefaultPersistence)
{
	const auto codec = OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES));
	ASSERT_EQ(codec.version(), Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	auto marker = burn();
	if(Version::CURRENT < Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)
		EXPECT_THROW(ownedBonusPayloads::copy({marker}), std::runtime_error);
	auto copy = ownedBonusPayloads::copy({marker, marker}, codec);
	ASSERT_EQ(copy->roots().size(), 2u); EXPECT_EQ(copy->roots()[0], copy->roots()[1]); EXPECT_NE(copy->roots()[0], marker);
	EXPECT_EQ(copy->roots()[0]->val, 7);
	CMemorySerializer ordinary;
	EXPECT_EQ(ordinary.oser.version, Version::CURRENT); EXPECT_EQ(ordinary.iser.version, Version::CURRENT);
	if(Version::CURRENT < Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)
	{
		EXPECT_THROW(ordinary.oser & *marker, std::runtime_error);
		EXPECT_TRUE(ordinary.extractBuffer().empty()); // Existing direct writer rejects before its first field.
	}
}
TEST(OwnedBonusCaptureCodecTest, UnknownPartialOrIncompatibleCapturedCapabilitiesRejectBeforeClone)
{
	newHorizonsHeroes::SpecialtySerializationContext unknown;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(false), unknown), std::invalid_argument);
	unknown.extendedOrdinarySpecialties = true;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(false), unknown), std::invalid_argument);
	const auto current = OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(false), capabilities(Version::CURRENT));
	EXPECT_EQ(current.version(), Version::CURRENT);
	if(Version::CURRENT < Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)
		EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), capabilities(Version::CURRENT)), std::runtime_error);
	auto malformed = captured(true); malformed["additionalSpecialties"]["ordinaryCreatureSpecialties"]["cinderReprisalDamagePercent"].Integer() = -1;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(malformed, capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)), std::runtime_error);
}
TEST(OwnedBonusCaptureCodecTest, ExplicitGraphOwns1152RawMarkerAndQueriesAfterLibraryIsAbsent)
{
	const auto codec = OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES));
	auto marker = burn();
	OwnedRecipientBonusGraph::NodeInput input; input.local = {marker};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0, {}, codec);
	marker.reset();
	WithoutLibrary absent;
	const auto result = graph->query(Selector::type()(BonusType::NONE));
	ASSERT_EQ(result.size(), 1u); EXPECT_EQ(result.front()->val, 7);
}

TEST(OwnedBonusCaptureCodecTest, Stable1151RemainsTruthfulAndRefusesExtendedPayloadRegardlessOfCurrentWriter)
{
	const auto legacy = capabilities(Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	const auto codec = OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(false), legacy);
	EXPECT_EQ(codec.version(), Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), legacy), std::runtime_error);
	auto marker = burn();
	EXPECT_THROW(ownedBonusPayloads::copy({marker}, codec), std::runtime_error);
	CMemorySerializer writer;
	writer.oser.version = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
	EXPECT_THROW(writer.oser & *marker, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	auto partial = legacy; partial.ordinaryCombatSpecialties = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(partial), std::invalid_argument);
}
