/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusDependencyCapture.h"
#include "BonusGraphQuery.h"
#include "RecipientBonusOverlay.h"
namespace bonusDependencies
{
namespace detail
{
/// Cold native inputs only. No node, callback, provider or world owner survives.
struct TerrainWitness
{
	BonusNodeType destination;
	std::optional<FactionID> faction;
	std::optional<TerrainId> adventure, query, battle, resolved;
	std::optional<bool> nativeMembership;
};
struct TerrainWitnesses { std::vector<TerrainWitness> values; };
/// Producer-only capability. General classify/capture deliberately cannot mint it.
class TerrainCaptureCertificate final
{
	TerrainCaptureCertificate() = default;
	friend CapturedModifiers bonusDependencies::captureNativeOverlayModifiers(
		const BonusGraphQuery &, const CBonusSystemNode &, std::span<const recipientBonusOverlay::Layer>);
public:
	Classification classifyMaterialized(const Bonus & raw) const;
	std::shared_ptr<const TerrainWitnesses> witnesses(std::span<const Stage> stages,
		const BonusEvaluationContext & finalContext) const;
};
}
}
