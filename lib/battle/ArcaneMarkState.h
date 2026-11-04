/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <array>
#include <cstdint>
#include "BattleSide.h"
#include <stdexcept>
class JsonNode;

namespace battle
{
/// Permanent combat marks indexed by the original hero battle side. Control,
/// ordinary creature death, Dispel, activation and round expiry never re-key it.
class DLL_LINKAGE ArcaneMarkState
{
	std::array<uint8_t, 2> marks{};
public:
	static constexpr uint8_t MAX_MARKS = 5;
	int32_t get(BattleSide side) const;
	void set(BattleSide side, int32_t count);
	void add(BattleSide side, int32_t count);
	bool hasState() const noexcept { return marks[0] != 0 || marks[1] != 0; }
	JsonNode toJson() const;
	static ArcaneMarkState fromJson(const JsonNode & data);
	bool operator==(const ArcaneMarkState &) const = default;
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		if(hasState() && !h.hasFeature(Handler::Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS))
			throw std::runtime_error("Cannot discard per-hero Arcane Marks in an older format");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS))
		{
			auto decoded = marks;
			h & decoded[0];
			h & decoded[1];
			if(decoded[0] > MAX_MARKS || decoded[1] > MAX_MARKS)
				throw std::runtime_error("Invalid serialized Arcane Mark count");
			marks = decoded;
		}
		else if(!h.saving)
			marks = {};
	}
};
/// Strict presence parsing shared by native loading and packet preflight.
DLL_LINKAGE ArcaneMarkState arcaneMarksFromUnitJson(const JsonNode & unit);
DLL_LINKAGE bool demonicGateOpenedFromUnitJson(const JsonNode & unit);
DLL_LINKAGE bool hasPassiveFactionUnitState(const JsonNode & unit);
}
