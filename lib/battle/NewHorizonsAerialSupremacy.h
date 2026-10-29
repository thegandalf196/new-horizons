/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BattleHex.h"
#include <optional>
#include <stdexcept>
class CBattleInfoCallback;
class JsonNode;
namespace battle { class Unit; }
namespace newHorizonsAerialSupremacy
{
/// A physical ordinary counter's presentation, never a movement request.
/// The original authoritative footprint and ordinary damage context remain fixed.
struct DLL_LINKAGE Excursion
{
	uint32_t actor = 0;
	uint32_t opponent = 0;
	BattleHex origin;
	BattleHex landing;
	BattleHex target;
	int32_t round = -1;
	void validateShape() const;
	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_AERIAL_SUPREMACY))
			throw std::runtime_error("Aerial excursion requires its captured capability");
		if(h.saving) validateShape();
		h & actor; h & opponent; h & origin; h & landing; h & target; h & round;
		if(!h.saving) validateShape();
	}
};
/// Missing historical state is unused; malformed numeric state always rejects.
DLL_LINKAGE bool hasRoundState(const JsonNode & snapshot);
/// One bounded reachability query and stable board-order landing selection.
/// Every result admits one normal physical counter; it never moves either unit.
DLL_LINKAGE std::optional<Excursion> prepare(const CBattleInfoCallback & battle,
	const battle::Unit * actor, const battle::Unit * opponent);
/// Validate an accepted typed receipt without accepting a different target/hex.
DLL_LINKAGE bool matches(const CBattleInfoCallback & battle, const Excursion & excursion);
DLL_LINKAGE void validate(const CBattleInfoCallback & battle, const Excursion & excursion);
}
