/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "../constants/EntityIdentifiers.h"

/// Metamagic 6 (Double Cast, captured metamagicPolicy 7) per-side combat ledger.
/// The typed Spell grant in HeroActionAllowanceState is the callback-visible
/// signal of an open offer; this record holds its provenance and the per-combat
/// counters that perks read. Battle-side state only: it is never written into
/// formats older than NEW_HORIZONS_DOUBLE_CAST_CHOSEN_PRAYERS.
struct DLL_LINKAGE MetamagicDoubleCastState
{
	static constexpr uint16_t MAX_COUNT = 1000;
	static constexpr size_t MAX_AFFECTED_UNITS = 1024;

	/// Gross uses spent by completed double casts (Crescendo reads this).
	uint16_t usesSpent = 0;
	/// Uses regained by Arcane Cycle; never above usesSpent.
	uint16_t usesRegained = 0;
	/// Completed double casts in this combat (Spellstorm reads this).
	uint16_t completedDoubleCasts = 0;
	/// Open offer: the typed Spell grant it issued, or 0 when no offer is open.
	uint32_t offerGrantId = 0;
	int32_t offerRound = -1;
	int32_t offerActivationSerial = -1;
	SpellID firstSpell = SpellID::NONE;
	/// Sorted, unique unit IDs the first spell affected.
	std::vector<uint32_t> firstSpellAffectedUnits;

	bool operator==(const MetamagicDoubleCastState &) const = default;

	bool offerOpen() const { return offerGrantId != 0; }
	int netUsesSpent() const { return static_cast<int>(usesSpent) - usesRegained; }
	bool defaultState() const { return *this == MetamagicDoubleCastState{}; }

	void closeOffer()
	{
		offerGrantId = 0;
		offerRound = -1;
		offerActivationSerial = -1;
		firstSpell = SpellID::NONE;
		firstSpellAffectedUnits.clear();
	}

	void validate() const;

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_CAST_CHOSEN_PRAYERS))
		{
			if(h.saving && !defaultState())
				throw std::runtime_error("Cannot discard Double Cast battle state in an older format");
			if(!h.saving)
				*this = {};
			return;
		}
		if(h.saving)
			validate();
		h & usesSpent;
		h & usesRegained;
		h & completedDoubleCasts;
		h & offerGrantId;
		h & offerRound;
		h & offerActivationSerial;
		h & firstSpell;
		h & firstSpellAffectedUnits;
		if(!h.saving)
			validate();
	}
};
