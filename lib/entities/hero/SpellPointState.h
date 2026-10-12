/*
 * SpellPointState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../GameConstants.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
/// Stores ordinary capacity-bound Spell Points and capacity-independent Buffer
/// Spell Points. Maximum Spell Points are supplied to mutations because that
/// capacity is derived from the owning hero and is not state owned here.
class DLL_LINKAGE SpellPointState final
{
public:
	int32_t getNormal() const noexcept
	{
		return normal;
	}

	int32_t getBuffer() const noexcept
	{
		return buffer;
	}

	int64_t getTotal() const noexcept
	{
		return static_cast<int64_t>(normal) + buffer;
	}

	struct DLL_LINKAGE Refund
	{
		int32_t normal = 0;
		int32_t permanentBuffer = 0;
		int32_t temporaryBuffer = 0;
	};

	/// Undo a percentage of an actual charge, in reverse charge order. Rounding
	/// occurs once on the whole cost, never separately on the provenance pools.
	static Refund refundForSpent(int32_t normalSpent, int32_t permanentBufferSpent,
		int32_t temporaryBufferSpent, int32_t percent)
	{
		if(normalSpent < 0 || permanentBufferSpent < 0 || temporaryBufferSpent < 0
			|| percent < 0 || percent > 100)
			throw std::invalid_argument("Invalid Spell Point refund provenance");
		int64_t remaining = (static_cast<int64_t>(normalSpent) + permanentBufferSpent
			+ temporaryBufferSpent) * percent / 100;
		Refund result;
		result.normal = static_cast<int32_t>(std::min<int64_t>(normalSpent, remaining));
		remaining -= result.normal;
		result.permanentBuffer = static_cast<int32_t>(std::min<int64_t>(permanentBufferSpent, remaining));
		remaining -= result.permanentBuffer;
		result.temporaryBuffer = static_cast<int32_t>(std::min<int64_t>(temporaryBufferSpent, remaining));
		return result;
	}

	/// Set Normal Spell Points to an absolute value. Values are clamped to the
	/// nonnegative capacity; increasing capacity never fills the new space.
	void setNormal(int32_t value, int32_t maximum) noexcept
	{
		normal = std::clamp(value, int32_t{0}, nonnegative(maximum));
	}

	/// Restore Normal Spell Points without changing Buffer. A negative amount is
	/// rejected without changing state. The current Normal pool is first clamped
	/// in case capacity has fallen since the previous mutation.
	bool restoreNormal(int32_t amount, int32_t maximum) noexcept
	{
		if(amount < 0)
			return false;

		clampNormal(maximum);
		const int32_t room = nonnegative(maximum) - normal;
		normal += std::min(amount, room);
		return true;
	}

	/// Grant Buffer Spell Points independently of Normal capacity. Additions
	/// saturate at INT32_MAX; a negative grant is rejected without mutation.
	bool grantBuffer(int32_t amount) noexcept
	{
		if(amount < 0)
			return false;

		const int64_t sum = static_cast<int64_t>(buffer) + amount;
		buffer = static_cast<int32_t>(std::min<int64_t>(sum, std::numeric_limits<int32_t>::max()));
		return true;
	}

	/// Remove only Buffer, for effects such as clearing unused temporary battle
	/// points. This must not fall through to spending from the Normal pool.
	bool removeBuffer(int64_t amount) noexcept
	{
		if(amount < 0 || amount > buffer)
			return false;
		buffer -= static_cast<int32_t>(amount);
		return true;
	}

	/// Enforce the current Normal capacity. Buffer is never affected.
	void clampNormal(int32_t maximum) noexcept
	{
		normal = std::min(normal, nonnegative(maximum));
	}

	/// Replace the stored pools from a snapshot, clamping only Normal to the
	/// supplied capacity. Negative serialized pool values are rejected and leave
	/// the existing state unchanged. Capacity is often unavailable while a hero
	/// is being deserialized, so callers may pass INT32_MAX and clamp after attach.
	bool restoreSnapshot(int32_t normalValue, int32_t bufferValue, int32_t maximum) noexcept
	{
		if(normalValue < 0 || bufferValue < 0)
			return false;

		normal = std::min(normalValue, nonnegative(maximum));
		buffer = bufferValue;
		return true;
	}

	/// Spend one nonnegative cost, consuming Buffer before Normal. Insufficient
	/// funds or a negative cost leave both pools unchanged.
	bool spend(int64_t cost) noexcept
	{
		if(cost < 0 || cost > getTotal())
			return false;

		const int32_t spentFromBuffer = static_cast<int32_t>(std::min<int64_t>(buffer, cost));
		buffer -= spentFromBuffer;
		normal -= static_cast<int32_t>(cost - spentFromBuffer);
		return true;
	}

	template <typename Handler>
	void serialize(Handler & h)
	{
		int32_t normalValue = normal;
		int32_t bufferValue = buffer;
		h & normalValue;
		h & bufferValue;
		if(!h.saving && (normalValue < 0 || bufferValue < 0))
			throw std::runtime_error("Invalid negative Spell Point pool in saved state");
		if(!h.saving)
		{
			normal = normalValue;
			buffer = bufferValue;
		}
	}

private:
	static int32_t nonnegative(int32_t value) noexcept
	{
		return std::max(value, int32_t{0});
	}

	int32_t normal = 0;
	int32_t buffer = 0;
};
}
