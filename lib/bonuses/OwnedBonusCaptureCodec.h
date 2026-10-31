/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../serializer/ESerializationVersion.h"
class JsonNode;
namespace newHorizonsHeroes { struct SpecialtySerializationContext; }
/// A local in-process clone codec, NOT permission to persist/network this version.
/// Actual source factory must derive known capabilities from its authoritative captured context.
class DLL_LINKAGE OwnedBonusCaptureCodec final
{
	ESerializationVersion value;
	explicit OwnedBonusCaptureCodec(ESerializationVersion value) : value(value) {}
public:
	static OwnedBonusCaptureCodec current();
	/// Format-only clone capability, NOT Hero-policy/lifetime/native source authority.
	static OwnedBonusCaptureCodec fromSourceCapabilities(const newHorizonsHeroes::SpecialtySerializationContext & capabilities);
	/// Reject unknown/partial capabilities and incompatible captured policy BEFORE cloning.
	static OwnedBonusCaptureCodec fromCapturedSpecialties(const JsonNode & captured,
		const newHorizonsHeroes::SpecialtySerializationContext & capabilities);
	ESerializationVersion version() const noexcept { return value; }
};
