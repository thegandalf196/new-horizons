/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBonusCaptureCodec.h"
#include "../entities/hero/NewHorizonsSpecialtySerialization.h"
#include <stdexcept>
namespace
{
struct CodecFeatures
{
	using Version = ESerializationVersion;
	Version version;
	bool hasFeature(Version feature) const { return version >= feature; }
};
}
OwnedBonusCaptureCodec OwnedBonusCaptureCodec::current()
{
	using Context = newHorizonsHeroes::SpecialtySerializationContext;
	return fromSourceEpoch(ESerializationVersion::CURRENT, Context::fromHandler(CodecFeatures{ESerializationVersion::CURRENT}));
}
OwnedBonusCaptureCodec OwnedBonusCaptureCodec::fromSourceEpoch(ESerializationVersion epoch,
	const newHorizonsHeroes::SpecialtySerializationContext & capabilities)
{
	using Version = ESerializationVersion;
	Version profile;
	switch(epoch)
	{
	case Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES:
	case Version::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION:
		profile = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
		break;
	case Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES:
	case Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY:
		profile = Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
		break;
	case Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS:
	case Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS: // No new Bonus-only representation.
		profile = Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
		break;
	case Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN:
		profile = Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
		break;
	case Version::NEW_HORIZONS_COMMAND_AUTHORITY:
		profile = Version::NEW_HORIZONS_COMMAND_AUTHORITY;
		break;
	case Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY:
		profile = Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY;
		break;
	default:
		throw std::invalid_argument("Unknown owned bonus source epoch");
	}
	const auto codec = fromSourceCapabilities(capabilities);
	if(codec.version() != profile)
		throw std::invalid_argument("Source epoch and owned bonus capability profile disagree");
	return codec;
}
OwnedBonusCaptureCodec OwnedBonusCaptureCodec::fromSourceCapabilities(
	const newHorizonsHeroes::SpecialtySerializationContext & capabilities)
{
	using Version = ESerializationVersion;
	using Context = newHorizonsHeroes::SpecialtySerializationContext;
	Version version;
	// Explicit known epochs, independent of which writer epoch CURRENT selects.
	if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES}))
		version = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
	else if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES}))
		version = Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	else if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS}))
		version = Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
	else if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN}))
		version = Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	else if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_COMMAND_AUTHORITY}))
		version = Version::NEW_HORIZONS_COMMAND_AUTHORITY;
	else if(capabilities == Context::fromHandler(CodecFeatures{Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY}))
		version = Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY;
	else throw std::invalid_argument("Unknown or partial owned bonus capture capabilities");
	return OwnedBonusCaptureCodec(version);
}
OwnedBonusCaptureCodec OwnedBonusCaptureCodec::fromCapturedSpecialties(const JsonNode & captured,
	const newHorizonsHeroes::SpecialtySerializationContext & capabilities)
{
	const auto codec = fromSourceCapabilities(capabilities);
	newHorizonsHeroes::validateCapturedSpecialtiesSerialization(captured, capabilities);
	return codec;
}
