/*
 * NewHorizonsConfusionState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../serializer/ESerializationVersion.h"

#include <cstdint>
#include <stdexcept>

class JsonNode;

namespace battle
{
enum class ConfusionBehavior : uint8_t
{
	NONE,
	ATTACK,
	DEFEND,
	WANDER
};

/// One pending forced activation and the target's last actually resolved
/// Confusion behavior. History outlives pending effects and later recasts.
struct DLL_LINKAGE ConfusionState
{
	bool pending = false;
	PlayerColor pendingCaster = PlayerColor::CANNOT_DETERMINE;
	bool pendingConfounder = false;
	ConfusionBehavior previousResolved = ConfusionBehavior::NONE;

	bool hasState() const;
	void validate() const;
	void applyPending(PlayerColor caster, bool confounder);
	/// Consuming a forfeited activation does not manufacture a resolved behavior.
	void clearPending();
	void recordResolved(ConfusionBehavior behavior);
	JsonNode toJson() const;
	static ConfusionState fromJson(const JsonNode & data);
	bool operator==(const ConfusionState &) const = default;

	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_CONFUSION_STATE) && hasState())
			throw std::runtime_error("Cannot discard Confusion pending state or history in an older format");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CONFUSION_STATE))
		{
			h & pending;
			h & pendingCaster;
			h & pendingConfounder;
			h & previousResolved;
			if(!h.saving)
				validate();
		}
		else if(!h.saving)
			*this = {};
	}
};

/// The same strict parser is used by update/wire admission and unit loading.
DLL_LINKAGE ConfusionState confusionStateFromUnitJson(const JsonNode & unit);
DLL_LINKAGE bool hasConfusionState(const JsonNode & unit);
}
