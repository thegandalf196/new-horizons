/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "RecipientBonusOverlay.h"
#include "OwnedBonusPayloads.h"
#include "BonusOutputOrigin.h"
#include <map>
#include <span>
#include <bitset>
namespace bonusDependencies
{
/// Pure finite preflight for accepted local rows BEFORE any effect operation.
DLL_LINKAGE bool supportsRecipientRecord(const Bonus & value);
/// Immutable native RECIPIENT merge program. These are post-collector values,
/// never originating exporters and never passed through another updater/limiter.
class DLL_LINKAGE OwnedModifierOverlay final
{
	std::shared_ptr<const ownedBonusPayloads::Captured> owner;
	std::bitset<static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT)> rawTypes;
	std::vector<recipientBonusOverlay::Layer> layers;
	std::shared_ptr<const uint8_t> originOwner = std::make_shared<const uint8_t>(0);
	OutputOrigins ownedOrigins;
	std::vector<std::vector<OutputOrigin>> sourceRemovals, stackedOrigins;
	struct RemovalReceipt
	{
		std::weak_ptr<const Bonus> source;
		OutputOrigin origin;
	};
	std::vector<RemovalReceipt> removalReceipts;
	void bindOwnedLayers(std::span<const recipientBonusOverlay::Layer> source,
		const std::vector<std::vector<size_t>> & handles, const std::vector<std::vector<size_t>> & localRemovals,
		const std::vector<std::shared_ptr<const Bonus>> & roots);
	std::optional<OutputOrigin> sourceRemoval(const std::shared_ptr<Bonus> & record) const;
public:
	OwnedModifierOverlay(std::span<const recipientBonusOverlay::Layer> source,
		const OutputOrigins & currentOrigins,
		const OwnedModifierOverlay * previousOwnLeaf = nullptr, bool coldSourceProjection = false);
	/// Borrowed results require this immutable program owner through use.
	TConstBonusListPtr apply(const BonusList & nativeStacked,
		OutputOrigins & currentOrigins, bool unstacked = false) const;
	size_t recordCount() const noexcept { return owner->roots().size(); }
	bool mayHaveBonusType(BonusType type) const;
};
}
