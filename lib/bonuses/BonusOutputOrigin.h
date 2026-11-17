/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Bonus.h"
#include <map>
namespace bonusDependencies
{
/// Opaque identity of an owned originating output, never an export capability.
/// Owner is a pure identity token; no source pointer/provider is retained.
struct OutputOrigin
{
	std::shared_ptr<const uint8_t> owner;
	uint32_t row = 0;
	bool operator==(const OutputOrigin &) const = default;
	bool operator<(const OutputOrigin & other) const noexcept
	{
		if(owner.owner_before(other.owner)) return true;
		if(other.owner.owner_before(owner)) return false;
		return row < other.row;
	}
};
using OutputOrigins = std::map<const Bonus *, OutputOrigin>;
}
