/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../Global.h"
#include "../serializer/ESerializationVersion.h"
#include <stdexcept>
class JsonNode;
namespace battle
{
/// Nonmagical Wait concealment and its one delayed-activation shot. Attack
/// history and Wait use last for this round; no creature/hero identity is stored.
struct DLL_LINKAGE ForestAmbushState
{
	bool attacked = false;
	bool waited = false;
	bool concealed = false;
	bool shotPending = false;

	bool hasState() const;
	void validate() const;
	void acceptWait(bool entitled);
	void receiveAttack();
	void beginActivation(bool delayed);
	void finishActivation();
	void finishStrike(bool ranged, bool physical);
	void retire();
	JsonNode toJson() const;
	static ForestAmbushState fromJson(const JsonNode & data);
	bool operator==(const ForestAmbushState &) const = default;

	template<typename Handler> void validateSerialization(const Handler & h) const
	{
		validate();
		if(hasState() && !h.hasFeature(Handler::Version::NEW_HORIZONS_FOREST_AMBUSH))
			throw std::runtime_error("Cannot discard Forest Ambush state in an older format");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FOREST_AMBUSH))
		{
			h & attacked;
			h & waited;
			h & concealed;
			h & shotPending;
			if(!h.saving)
				validateSerialization(h);
		}
		else if(!h.saving)
			*this = {};
	}
};
DLL_LINKAGE ForestAmbushState forestAmbushStateFromUnitJson(const JsonNode & unit);
}
