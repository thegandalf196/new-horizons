/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusDependencyFacts.h"
#include "BonusDependencyQueryFrame.h"
#include "RecipientLayerSource.h"
#include "BonusGraphQuery.h"
#include "RecipientBonusOverlay.h"
#include "../combatScripts/NativePreparedBonuses.h"
#include <array>
namespace bonusDependencies
{
class OwnedModifierOverlay;
struct NumericFrame
{
private:
	std::shared_ptr<const uint8_t> captureIdentity;
	int warfare = 0;
	friend class CapturedModifiers;
public:
	std::vector<Values> scalar;
	std::array<nativeDamage::PreparedUnit, 2> prepared;
};
/// Immutable cold-resolved root. Recipes own approved rules; mutable branches
/// retain only scalar frames and native PreparedUnit values. No query owner lives here.
struct OccurrenceIdentity
{
	uint32_t exporter, rawRecord, outputAlias;
	BonusType rawType;
};
class DLL_LINKAGE CapturedModifiers final
{
	std::shared_ptr<const uint8_t> rawOriginIdentity = std::make_shared<const uint8_t>(0);
	std::shared_ptr<const uint8_t> leafBaseIdentity;
	std::shared_ptr<const uint8_t> captureIdentity = std::make_shared<const uint8_t>(0);
	std::vector<std::shared_ptr<const OwnedModifierOverlay>> overlays;
	std::shared_ptr<const std::vector<Recipe>> recipes = std::make_shared<const std::vector<Recipe>>();
	std::shared_ptr<const std::vector<OccurrenceIdentity>> identities = std::make_shared<const std::vector<OccurrenceIdentity>>();
	struct Materialized
	{
		std::vector<Values> scalar;
		TConstBonusListPtr stacked, unstacked;
		std::map<uint32_t, std::shared_ptr<Bonus>> aliases;
		OutputOrigins origins;
		std::bitset<nativeDamage::bonusTypeCount> dirty;
	};
	std::optional<Materialized> materialize(int count, Change changes, const std::vector<Values> * previous) const;
	std::optional<QueryFrameBinding> queryBinding;
	std::shared_ptr<const ownedBonusPayloads::Captured> localSpellOwner;
	std::vector<Bonus> localSpells;
	friend DLL_LINKAGE CapturedModifiers captureNativeOverlayModifiers(const BonusGraphQuery &, const CBonusSystemNode &, std::span<const recipientBonusOverlay::Layer>);
public:
	/// Cold value binding under the caller's exact source receipt; this method
	/// establishes no native admission. Exact source-local spells are not queries.
	std::shared_ptr<const CapturedModifiers> bindQueries(const QueryFrameBinding & binding,
		std::span<const Bonus> exactLocalSpellEffects) const;
	/// Root/child sealing: value lease for exactly the source frame installed in
	/// origBearer. Caller binds it atomically with the fixed-base native receipt.
	std::shared_ptr<const RecipientLayerSource> bindRecipientLayerSource(
		std::shared_ptr<const OwnedBonusQueryFrame> exactSourceFrame) const;
	bool ownsRecipientLayerSource(const RecipientLayerSource & source) const noexcept;
	/// One native recipient DELTA layer relative to exactBefore, never a repeated
	/// complete ancestry. Called on FIXED base; exactBefore may be the base or
	/// its previous own-leaf result. Caller validates operation/source receipt.
	std::shared_ptr<const CapturedModifiers> appendRecipientLayer(const recipientBonusOverlay::Layer & layer,
		std::span<const Bonus> exactLocalSpellEffects, const OwnedBonusQueryFrame & exactBefore,
		const RecipientLayerSource * fixedSource = nullptr) const;
	bool ownsQueryFrame(const OwnedBonusQueryFrame & frame) const noexcept;
	std::shared_ptr<const OwnedBonusQueryFrame> refreshQueries(const QueryFrameBinding & binding,
		Change changes, std::shared_ptr<const OwnedBonusQueryFrame> previous = nullptr) const;
	/// Dependencies are checked BEFORE compact mutation; Count refresh cannot
	/// change max-health, form, acceptance state, source roster or source clocks.
	bool canRefresh(Change changes) const noexcept;
	/// Native exporter occurrences only; recipient values have a separate owned role.
	/// This slice/effective presence NEVER replaces A3 conservative raw type masks.
	std::span<const OccurrenceIdentity> occurrences() const noexcept { return *identities; }
	/// Refresh-boundary native reduction. Base identity/health/count and Warfare
	/// are explicit current inputs from the existing shared core helpers.
	std::optional<NumericFrame> refresh(int currentCount, Change changes,
		const std::array<nativeDamage::PreparedUnit, 2> & base, int warfare,
		const NumericFrame * previous = nullptr) const;
};
/// Actual raw local/inherited rows include dormant and materialized propagated
/// records. Uses shared native collector with compile-time no-op observation
/// in ordinary collection. Must run inside CanonicalSourceAdmission's lease.
DLL_LINKAGE CapturedModifiers captureNativeOverlayModifiers(const BonusGraphQuery & graph, const CBonusSystemNode & recipient,
	std::span<const recipientBonusOverlay::Layer> layers);
DLL_LINKAGE CapturedModifiers captureNativeModifiers(const BonusGraphQuery & graph, const CBonusSystemNode & recipient);
}
