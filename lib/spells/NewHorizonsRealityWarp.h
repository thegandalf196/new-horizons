/*
 * NewHorizonsRealityWarp.h, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"
#include "../battle/BattleEffectExchange.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class CBattleInfoCallback;

namespace newHorizonsRealityWarp
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:realityWarp";
struct DLL_LINKAGE ConfusionPayload
{
	PlayerColor caster = PlayerColor::CANNOT_DETERMINE;
	bool confounder = false;
};
/// State that accompanies a Regeneration bonus bundle on a particular unit.
/// It travels with the spell effects without resolving or advancing the effect.
struct DLL_LINKAGE RegenerationPayload
{
	int32_t rateMillionths = 0;
	int64_t pendingMicroHealth = 0;
};

/// State that accompanies a Guardian Spirit bonus bundle on a particular unit.
struct DLL_LINKAGE GuardianSpiritPayload
{
	int64_t hitPointPool = 0;
	int32_t roundsRemaining = 0;
};

/// Fractional spell-regeneration progress carried by a unit's capacity ledger.
/// Health cohorts and their reference maximum remain outside this payload.
struct DLL_LINKAGE CapacityRegenerationPayload
{
	int32_t remainderTenths = 0;
};

/// A detached snapshot of one applied spell's transferable effects and any
/// sidecar state those effects require. Collection decides eligibility; this
/// planner treats the bundle's contents as opaque and never merges duplicates.
struct DLL_LINKAGE EffectBundle
{
	SpellID spell = SpellID::NONE;
	std::vector<Bonus> bonuses;
	std::optional<RegenerationPayload> regeneration;
	std::optional<GuardianSpiritPayload> guardianSpirit;
	std::optional<CapacityRegenerationPayload> capacityRegeneration;
	std::optional<ConfusionPayload> confusion;
	/// Collection must explicitly opt in only after checking spell/effect identity.
	bool transferable = false;
};

enum class RecipientSide : uint8_t
{
	FIRST,
	SECOND
};

using RecipientLegality = std::function<bool(const EffectBundle &, RecipientSide, PlayerColor)>;

/// Results of one reciprocal, detached effect-bundle exchange.
struct DLL_LINKAGE ExchangePlan
{
	std::vector<EffectBundle> first;
	std::vector<EffectBundle> second;
};

/// Plan a simultaneous exchange from immutable endpoint snapshots. Eligible
/// bundles move only when the destination predicate allows them; illegal and
/// explicitly non-transferable bundles remain at their original endpoint.
/// Stable spell-caster provenance is preserved in every Bonus. For transferred
/// bonuses with known provenance, target-relative hostility is recomputed for
/// the destination owner; legacy unknown provenance retains its saved flag.
DLL_LINKAGE ExchangePlan planExchange(const std::vector<EffectBundle> & firstBundles, PlayerColor firstOwner,
	const std::vector<EffectBundle> & secondBundles, PlayerColor secondOwner,
	const RecipientLegality & recipientAllows);

enum class StayReason : uint8_t
{
	MOVED,
	EXCLUDED_EFFECT,
	UNKNOWN_SOURCE,
	MISSING_CAPTURE,
	UNSUPPORTED_CONDITION,
	ILLEGAL_RECIPIENT,
	INVALID_SIDECAR,
	SIDECAR_CONFLICT,
	INVALID_LINK
};

enum class EndpointRejection : uint8_t
{
	NONE,
	INVALID_ENDPOINT,
	SPELL_LOCKED,
	TIME_STOPPED,
	SNAPSHOT_UNAVAILABLE
};

struct DLL_LINKAGE BundlePreview
{
	uint32_t source = 0;
	uint32_t destination = 0;
	EffectBundle bundle;
	StayReason reason = StayReason::EXCLUDED_EFFECT;
};

struct DLL_LINKAGE PreparedExchange
{
	std::optional<battle::BattleEffectExchange> exchange;
	std::vector<BundlePreview> previews;
	EndpointRejection rejection = EndpointRejection::NONE;
};

/// Collect and plan against both original endpoints without casting or RNG.
/// Empty pairs are valid no-op exchanges; casting admission belongs to callers.
DLL_LINKAGE PreparedExchange prepareExchange(const CBattleInfoCallback & battle, uint32_t first, uint32_t second);
}
