/*
 * HeroOrderWarcast.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace heroCommands
{
/// Warcasting: a recipient that carried its side's beneficial spell when the
/// Order was committed, with its captured share of the Order effect.
struct DLL_LINKAGE WarcastRecipient
{
	uint32_t unitId = std::numeric_limits<uint32_t>::max();
	int16_t percent = 0;

	bool operator==(const WarcastRecipient &) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & unitId;
		h & percent;
	}
};

/// Plain Order snapshot captured once at commit and never recomputed.
struct DLL_LINKAGE WarcastSnapshot
{
	static constexpr uint8_t KINDLED = 1;
	static constexpr uint8_t OPENING = 2;
	static constexpr uint8_t WARDED = 4;
	static constexpr uint8_t STEEL = 8;
	static constexpr uint8_t RIDERS = 15;
	static constexpr int16_t MAX_PERCENT = 200;
	/// Sorted by unit identity.
	std::vector<WarcastRecipient> recipients;
	/// Rider perks active for this Order's warcast recipients.
	uint8_t riders = 0;
	/// Commander's Meditation restored Mana when this Order was issued.
	bool meditation = false;
	/// Unbroken Command placed the warcast recipients in the carry ledger.
	bool unbroken = false;

	bool operator==(const WarcastSnapshot &) const = default;
	bool empty() const { return recipients.empty() && riders == 0 && !meditation && !unbroken; }
	int percentFor(uint32_t unitId) const
	{
		if(recipients.empty())
			return 0;
		const auto it = std::lower_bound(recipients.begin(), recipients.end(), unitId,
			[](const WarcastRecipient & entry, uint32_t id) { return entry.unitId < id; });
		return it != recipients.end() && it->unitId == unitId ? it->percent : 0;
	}

	/// Appended after every older Order field; older formats read it inert.
	template <typename Handler> void serializeAppended(Handler & h)
	{
		if(h.saving && !empty() && !h.hasFeature(Handler::Version::NEW_HORIZONS_WARCAST))
			throw std::runtime_error("Cannot discard Warcasting Order snapshot");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_WARCAST))
		{
			h & recipients;
			h & riders;
			h & meditation;
			h & unbroken;
		}
		else if(!h.saving)
			*this = WarcastSnapshot();
	}
};
}
