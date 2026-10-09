/*
 * NewHorizonsRealityWarpPreview.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/bonuses/BonusParameters.h"
#include <functional>
#include <string>

namespace newHorizonsRealityWarpPreview
{
inline std::string stayReason(newHorizonsRealityWarp::StayReason reason)
{
	using Reason = newHorizonsRealityWarp::StayReason;
	switch(reason)
	{
	case Reason::MOVED: return "moves";
	case Reason::EXCLUDED_EFFECT: return "effect is not transferable";
	case Reason::UNKNOWN_SOURCE: return "original source is unknown";
	case Reason::MISSING_CAPTURE: return "required captured state is missing";
	case Reason::UNSUPPORTED_CONDITION: return "recipient condition is unsupported";
	case Reason::ILLEGAL_RECIPIENT: return "destination cannot receive this effect";
	case Reason::INVALID_SIDECAR: return "captured effect state is invalid";
	case Reason::SIDECAR_CONFLICT: return "destination has conflicting effect state";
	case Reason::INVALID_LINK: return "linked effect is invalid";
	}
	return "effect cannot be transferred";
}

inline bool sameExchange(const newHorizonsRealityWarp::PreparedExchange & first,
	const newHorizonsRealityWarp::PreparedExchange & second)
{
	if(!first.exchange || !second.exchange || first.rejection != second.rejection)
		return false;
	for(size_t index = 0; index < first.exchange->endpoints.size(); ++index)
	{
		const auto & a = first.exchange->endpoints[index];
		const auto & b = second.exchange->endpoints[index];
		if(a.id != b.id || !battle::exactBattleEffectSnapshotEqual(a.expected, b.expected)
			|| !battle::exactBattleEffectSnapshotEqual(a.replacement, b.replacement))
			return false;
	}
	return true;
}

inline std::string text(const newHorizonsRealityWarp::PreparedExchange & prepared,
	const std::function<std::string(uint32_t)> & unitName,
	const std::function<std::string(SpellID)> & spellName)
{
	std::string result = "Reality Warp\nAll spell-effect bundles are listed below. Strength, duration and original caster are preserved. Hostile targets may resist; if either resists, no bundle is exchanged.\n";
	if(prepared.previews.empty())
		result += "\nNeither stack has a spell-effect bundle to exchange (no effect).\n";
	for(const auto & preview : prepared.previews)
	{
		const bool moved = preview.reason == newHorizonsRealityWarp::StayReason::MOVED;
		result += "\n" + unitName(preview.source) + (moved ? " -> " : " (proposed destination: ")
			+ unitName(preview.destination) + (moved ? "" : ")")
			+ ": " + spellName(preview.bundle.spell) + (moved ? " [MOVES]" : " [STAYS]");
		if(!moved)
			result += " — " + stayReason(preview.reason);
		for(const auto & bonus : preview.bundle.bonuses)
		{
			const auto json = bonus.toJsonNode();
			result += "\n  " + json["type"].String() + ": strength " + std::to_string(bonus.val)
				+ "; remaining turns " + std::to_string(bonus.turnsRemain)
				+ "; duration " + (json["duration"].isNull() ? std::string("permanent") : json["duration"].toString())
				+ "; original caster " + std::to_string(bonus.spellCasterOwner.getNum())
				+ "; identity " + bonus.statusIdentity;
			// Preserve visibility of subtype, duration flags, captured strength and
			// typed parameters rather than guessing a spell-specific interpretation.
			result += "\n  Captured effect: " + json.toString();
			if(bonus.parameters)
				result += "\n  Captured parameters: " + bonus.parameters->toJsonNode().toString();
		}
		if(preview.bundle.regeneration)
			result += "\n  Regeneration rate " + std::to_string(preview.bundle.regeneration->rateMillionths)
				+ "; pending healing " + std::to_string(preview.bundle.regeneration->pendingMicroHealth);
		if(preview.bundle.guardianSpirit)
			result += "\n  Guardian Spirit " + std::to_string(preview.bundle.guardianSpirit->hitPointPool)
				+ " HP; " + std::to_string(preview.bundle.guardianSpirit->roundsRemaining) + " rounds";
		if(preview.bundle.capacityRegeneration)
			result += "\n  Regeneration remainder " + std::to_string(preview.bundle.capacityRegeneration->remainderTenths);
		if(preview.bundle.confusion)
			result += "\n  Confusion pending; original caster " + std::to_string(preview.bundle.confusion->caster.getNum())
				+ (preview.bundle.confusion->confounder ? "; Confounder captured" : "; ordinary");
		result += "\n";
	}
	return result + "\nConfirm this complete exchange? A changed battle state requires a fresh preview.";
}
}
