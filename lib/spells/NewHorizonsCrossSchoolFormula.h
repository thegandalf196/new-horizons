/* NewHorizonsCrossSchoolFormula.h, part of VCMI; GPL v2.0 or later. */
#pragma once
#include "../battle/BattleSide.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

class IBattleInfo;

namespace newHorizonsCrossSchoolFormula
{
constexpr std::string_view PERK = "new-horizons:spellcraft.crossSchoolFormula";
constexpr int BONUS_PERCENT = 10;

/// Membership is resolved only against the battle's saved immutable school catalog.
struct DLL_LINKAGE State
{
	SpellID spell = SpellID::NONE;
	int32_t round = -1;

	bool hasState() const { return round >= 0; }
	void validate() const
	{
		if(round < -1 || (round == -1 ? spell != SpellID::NONE : spell.num < 0))
			throw std::runtime_error("Invalid Cross-School Formula history");
	}
	auto operator<=>(const State &) const = default;
	template<typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(hasState() && !h.hasFeature(Handler::Version::NEW_HORIZONS_CROSS_SCHOOL_FORMULA))
			throw std::runtime_error("Cannot discard Cross-School Formula history");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CROSS_SCHOOL_FORMULA))
		{
			h & spell;
			h & round;
			if(!h.saving)
				validate();
		}
		else if(!h.saving)
			*this = {};
	}
};

struct DLL_LINKAGE Receipt
{
	State before;
	State after;
	auto operator<=>(const Receipt &) const = default;
	void validate() const
	{
		before.validate();
		after.validate();
		if(!after.hasState() || after.round < before.round)
			throw std::runtime_error("Invalid Cross-School Formula cast receipt");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
		{
			validate();
			before.validateSerialization(h);
			after.validateSerialization(h);
		}
		h & before;
		h & after;
		if(!h.saving)
			validate();
	}
};

DLL_LINKAGE bool differentSchools(const std::vector<SpellSchool> &, const std::vector<SpellSchool> &);
DLL_LINKAGE void validateState(const IBattleInfo &, const State &, bool supported = true);
DLL_LINKAGE std::optional<Receipt> acceptedReceipt(const IBattleInfo &, BattleSide, SpellID);
DLL_LINKAGE int bonusPercent(const IBattleInfo &, BattleSide, SpellID);
}
