/*
 * OverwhelmingFormulaState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

/// Side-owned combat state. Accepted eligible casts receive a stable token,
/// but the first actual qualifying injury chooses the winning cast. Its later
/// hits, targets and delayed effects keep the same entitlement.
struct DLL_LINKAGE OverwhelmingFormulaState
{
	using CastToken = uint64_t;
	static constexpr CastToken INVALID_CAST_TOKEN = 0;

	CastToken lastCandidateCastToken = INVALID_CAST_TOKEN;
	CastToken winningCastToken = INVALID_CAST_TOKEN;

	bool operator==(const OverwhelmingFormulaState &) const = default;

	bool hasState() const
	{
		return lastCandidateCastToken != INVALID_CAST_TOKEN;
	}

	void reset()
	{
		*this = OverwhelmingFormulaState{};
	}

	/// Call only for an accepted cast whose captured caster/perk eligibility
	/// qualifies. Registration alone never consumes the combat's first use.
	/// Invalid state or exhausted token range returns zero without mutation.
	CastToken registerAcceptedEligibleCast()
	{
		if(!isValid() || lastCandidateCastToken == std::numeric_limits<CastToken>::max())
			return INVALID_CAST_TOKEN;
		return ++lastCandidateCastToken;
	}

	/// A prediction is read-only. Unknown and absent tokens are always inert.
	bool canPenetrate(CastToken token) const
	{
		return isValid() && token != INVALID_CAST_TOKEN && token <= lastCandidateCastToken
			&& (winningCastToken == INVALID_CAST_TOKEN || winningCastToken == token);
	}

	/// Called after authoritative injury, using the target's current MDR at
	/// damage resolution. Immunity, resistance, prevention and zero injury do
	/// not select a winner. The caller classifies hostile magical damage and
	/// whether current reduction applies, preserving fractional MDR values.
	bool claimActualDamage(CastToken token, bool hostileMagicalDamage, int64_t actualInjury, bool hasApplicableMagicalReduction)
	{
		if(!hostileMagicalDamage || actualInjury <= 0 || !hasApplicableMagicalReduction || !canPenetrate(token))
			return false;
		winningCastToken = token;
		return true;
	}

	bool isValid() const
	{
		return winningCastToken <= lastCandidateCastToken;
	}

	void validateShape() const
	{
		if(!isValid())
			throw std::runtime_error("Invalid Overwhelming Formula cast state");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		// The binary serializer deliberately rejects uint64_t fields. Preserve
		// the full token domain with two exactly representable unsigned limbs.
		constexpr auto limbBits = std::numeric_limits<uint32_t>::digits;
		uint32_t candidateHigh = static_cast<uint32_t>(lastCandidateCastToken >> limbBits);
		uint32_t candidateLow = static_cast<uint32_t>(lastCandidateCastToken);
		uint32_t winnerHigh = static_cast<uint32_t>(winningCastToken >> limbBits);
		uint32_t winnerLow = static_cast<uint32_t>(winningCastToken);
		h & candidateHigh;
		h & candidateLow;
		h & winnerHigh;
		h & winnerLow;
		if(!h.saving)
		{
			lastCandidateCastToken = (static_cast<CastToken>(candidateHigh) << limbBits) | candidateLow;
			winningCastToken = (static_cast<CastToken>(winnerHigh) << limbBits) | winnerLow;
			validateShape();
		}
	}
};
