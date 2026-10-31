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
OwnedBonusCaptureCodec OwnedBonusCaptureCodec::current() { return OwnedBonusCaptureCodec(ESerializationVersion::CURRENT); }
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
