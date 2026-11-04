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
TEST(OwnedBonusCaptureCodecTest, Explicit1153CloneAdmitsCanonicalBurnWithoutChangingDefaultPersistence)
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
	EXPECT_EQ(current.version(), OwnedBonusCaptureCodec::current().version());
	if(Version::CURRENT < Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)
		EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), capabilities(Version::CURRENT)), std::runtime_error);
	auto malformed = captured(true); malformed["additionalSpecialties"]["ordinaryCreatureSpecialties"]["cinderReprisalDamagePercent"].Integer() = -1;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(malformed, capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)), std::runtime_error);
}
TEST(OwnedBonusCaptureCodecTest, ExplicitGraphOwns1153RawMarkerAndQueriesAfterLibraryIsAbsent)
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

TEST(OwnedBonusCaptureCodecTest, ExplicitKnownWireEpochsSelectConcreteBonusProfiles)
{
	const auto ordinary = capabilities(Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	const auto extended = capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	const auto optional = capabilities(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	const auto calibration = capabilities(Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION);
	const auto passive = capabilities(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	EXPECT_EQ(optional, extended); // Quick Study adds no Bonus/specialty bit.
	EXPECT_EQ(calibration, ordinary); // Neutral calibration also adds no Bonus/specialty bit.
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES, ordinary).version(),
		Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, extended).version(),
		Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, optional).version(),
		Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION, calibration).version(),
		Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS, passive).version(),
		Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	CMemorySerializer writer;
	EXPECT_EQ(writer.oser.version, Version::CURRENT); EXPECT_EQ(writer.iser.version, Version::CURRENT);
	if(Version::CURRENT == Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES
		|| Version::CURRENT == Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION)
		EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	else if(Version::CURRENT == Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES
		|| Version::CURRENT == Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY)
		EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	else if(Version::CURRENT == Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS)
		EXPECT_EQ(OwnedBonusCaptureCodec::current().version(), Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	else
		EXPECT_THROW(OwnedBonusCaptureCodec::current(), std::invalid_argument);
}

TEST(OwnedBonusCaptureCodecTest, WireEpochAndNamedBonusProfileMustBothMatch)
{
	const auto ordinary = capabilities(Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	const auto extended = capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES, extended), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, ordinary), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, ordinary), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION, extended), std::invalid_argument);
	auto partial = extended; partial.extendedOrdinarySpecialties = false; partial.adjacencyFilters = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, partial), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NONE, ordinary), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES, ordinary), std::invalid_argument);
}

TEST(OwnedBonusCaptureCodecTest, IdenticalBonusMaskDoesNotAuthorizeAnUnknownWireEpoch)
{
	const auto unknown = static_cast<Version>(static_cast<int32_t>(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS) + 1);
	const auto mask = capabilities(unknown);
	EXPECT_EQ(mask, capabilities(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceCapabilities(mask).version(), Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(unknown, mask), std::invalid_argument);
}

TEST(OwnedBonusCaptureCodecTest, OptionalQuickStudySourceUsesExistingBonusCloneRepresentation)
{
	const auto mask = capabilities(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	const auto codec = OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, mask);
	EXPECT_EQ(codec.version(), Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), mask).version(),
		Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	auto raw = burn(); const auto copy = ownedBonusPayloads::copy({raw, raw}, codec);
	ASSERT_EQ(copy->roots().size(), 2u);
	EXPECT_EQ(copy->roots()[0], copy->roots()[1]); EXPECT_NE(copy->roots()[0], raw);
	EXPECT_EQ(copy->roots()[0]->val, 7);
}

TEST(OwnedBonusCaptureCodecTest, Calibration1152Preserves1151BonusRepresentationAndRefusesObsoleteExtendedMask)
{
	EXPECT_EQ(static_cast<int32_t>(Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION), 1152);
	EXPECT_EQ(static_cast<int32_t>(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES), 1153);
	EXPECT_EQ(static_cast<int32_t>(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY), 1154);
	const auto mask = capabilities(Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION);
	const auto codec = OwnedBonusCaptureCodec::fromSourceEpoch(static_cast<Version>(1152), mask);
	EXPECT_EQ(static_cast<int32_t>(codec.version()), 1151);
	EXPECT_FALSE(mask.extendedOrdinarySpecialties);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(static_cast<Version>(1152),
		capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromCapturedSpecialties(captured(true), mask), std::runtime_error);
	auto marker = burn();
	EXPECT_THROW(ownedBonusPayloads::copy({marker}, codec), std::runtime_error);
}

TEST(OwnedBonusCaptureCodecTest, QuickStudyProfileDoesNotSynthesizeExtendedSpecialtyCaptures)
{
	const auto optional = capabilities(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	ASSERT_TRUE(optional.extendedOrdinarySpecialties); // Representation support, not captured activation.
	auto absent = captured(false); const auto before = absent;
	const auto codec = OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, optional);
	EXPECT_EQ(codec.version(), Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromCapturedSpecialties(absent, optional).version(), codec.version());
	EXPECT_EQ(absent, before);
	EXPECT_FALSE(absent["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().contains("cinderReprisalDamagePercent"));
	const auto partial = captured(true); // A single explicit optional coefficient is canonical too.
	EXPECT_EQ(OwnedBonusCaptureCodec::fromCapturedSpecialties(partial, optional).version(), codec.version());
	EXPECT_EQ(partial["additionalSpecialties"]["ordinaryCreatureSpecialties"]["cinderReprisalDamagePercent"].Integer(), 0);
}

TEST(OwnedBonusCaptureCodecTest, Passive1155EpochCannotUseHistorical1153Or1154Capabilities)
{
	const auto passive = capabilities(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
	const auto extended = capabilities(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES);
	const auto optional = capabilities(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	EXPECT_NE(passive, extended);
	EXPECT_NE(passive, optional);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, passive), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY, passive), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS, extended), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS, optional), std::invalid_argument);
}
