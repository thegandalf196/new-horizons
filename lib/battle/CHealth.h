/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CHealthValueContext.h"
#include "Unit.h"

#include <vector>

class JsonSerializeFormat;

namespace battle
{
class DLL_LINKAGE CHealth final
{
public:
	/// Compact group of surviving creatures with the same current hit points.
	/// Used only when a changing per-creature capacity makes the legacy
	/// first-creature-plus-full-rear-units representation lossy.
	struct DLL_LINKAGE CapacityHealthCohort
	{
		int32_t hitPoints = 0;
		int32_t count = 0;

		void serializeJson(JsonSerializeFormat & handler);
	};
	/// A run of usable casualties with the same death provenance, ordered from
	/// oldest to newest. Temporary one-battle Resurrection keeps the original
	/// run and marks which members are currently restored.
	struct DLL_LINKAGE CasualtyProvenanceCohort
	{
		int32_t count = 0;
		DamageProvenance provenance = DamageProvenance::OTHER;
		int32_t temporarilyRestored = 0;

		void serializeJson(JsonSerializeFormat & handler);
	};

	explicit CHealth(const battle::Unit * Owner);
	CHealth(const CHealth & other) = default;

	/// Capture every native health ledger without retaining its Unit or world.
	static CHealth detached(const CHealth & source, CHealthValueContext context);
	bool isDetached() const noexcept;
	CHealthValueContext valueContext() const;
	/// Change only the forecast owner facts. Capacity preservation/normalization
	/// follows the same explicit sequence as a native Unit capacity change.
	void updateDetachedContext(CHealthValueContext context);

	CHealth & operator=(const CHealth & other);
	/// Exchanges prestaged ledger contents, never their owning Unit pointers.
	void swapPreparedContents(CHealth & prepared) noexcept;

	void init();
	void reset(bool clearUnusableRemains = true);

	void damage(int64_t & amount);
	/// Deal damage while marking the creatures killed by this hit as leaving no
	/// usable remains. The default provenance is OTHER for legacy, nonmagical
	/// damage callers.
	void damage(int64_t & amount, bool destroyRemains);
	/// Shadow Gift sacrifices creature HP directly and cannot be absorbed by
	/// temporary hit points. Other damage paths retain their normal semantics.
	void damage(int64_t & amount, bool destroyRemains, bool bypassTemporaryHitPoints,
		DamageProvenance provenance = DamageProvenance::OTHER, bool trackCasualtyProvenance = true);
	HealInfo heal(int64_t & amount, EHealLevel level, EHealPower power, bool trackCasualtyProvenance = true);

	int32_t getCount() const;
	/// Largest body damage that kills at most this many current creatures, including wounded cohorts.
	int64_t maximumDamageForCasualties(int32_t casualties) const;
	int32_t getFirstHPleft() const;
	int32_t getResurrected() const;
	/// Number of casualties whose remains cannot be restored or harvested.
	int32_t getUnusableRemains() const;
	int32_t getCasualtyCount(DamageProvenance provenance) const;
	bool hasCasualtyProvenanceState() const;
	/// Battle-only hit points consumed before the stack's creature health.
	int64_t getTemporaryHitPoints() const;
	void addTemporaryHitPoints(int64_t amount);
	/// Append full battle-only members without healing existing wounded cohorts.
	bool appendTemporaryCreatures(int32_t count);
	int64_t getCreatureHealthAvailable() const;
	int64_t getTotalHealthOverride() const;
	int64_t getShadowGiftMaximumHealthLost() const;
	void addShadowGiftMaximumHealthLoss(int64_t amount);
	/// Repartition surviving creature HP to a new battle-form capacity without
	/// changing the separate temporary hit-point pool or total HP.
	void repartitionForBattleForm(int32_t newMaximum, int64_t originalTotalHealth, int64_t remainingHealth);
	/// Freeze this ledger at the source creature capacity for casualty, one-battle
	/// resurrection, and unusable-remains provenance.
	void preserveBattleFormProvenance(int32_t sourceMaximum);
	/// Resume ordinary capacity tracking after restoring the source health ledger.
	void releaseBattleFormProvenance(bool preserveCapacityTracking);
	void setTemporaryHitPoints(int64_t amount);
	bool isBattleFormProvenance() const;

	/// returns total remaining health
	int64_t available() const;

	/// returns total initial health
	int64_t total() const;

	void takeResurrected();

	void preserveCapacityHealth();
	void normalizeCapacityHealth(bool preserveCapacityTracking);
	int64_t capacityRegenerationProjectedHeal(int32_t perCreatureHeal) const;
	int64_t consumeCapacityRegeneration(int32_t perCreatureHeal);
	/// Spend only within each survivor's existing regeneration allowance.
	int64_t consumeCapacityRegeneration(int32_t perCreatureHeal, int64_t receivedHealth);
	bool isCapacityHealthTracking() const;

	void serializeJson(JsonSerializeFormat & handler);
private:
	void addResurrected(int32_t amount);
	void addUnusableRemains(int32_t amount);
	void ensureCasualtyProvenanceLedger();
	int32_t removeNewestUsableCasualties(int32_t amount, bool temporary, bool allowNonCasualtySurplus);
	void replaceNewestTemporaryCasualties(int32_t amount, DamageProvenance provenance, bool destroyRemains);
	int32_t casualtyLedgerCount() const;
	int32_t temporarilyRestoredCasualtyCount() const;
	int32_t usableCasualtyDebt() const;
	void validateCasualtyProvenanceLedger() const;
	int64_t creatureHealthAvailable() const;
	void setFromTotal(const int64_t totalHealth);
	void damageCapacityHealth(int64_t amount);
	void addCapacityHealth(int32_t hitPoints, int32_t count);
	void normalizeCapacityHealthCohorts();
	void promoteCapacityHealthFront();
	void healCapacityHealth(int64_t & amount, EHealLevel level);
	void addCapacityHealthHealing(int32_t perCreatureHeal);
	int32_t maximumPerCreature() const;
	int32_t baseAmount() const;
	uint32_t currentMaximumHealth() const;
	int32_t checkedMaximumHealth() const;
	void validateState() const;
	const battle::Unit * owner;
	/// Runtime-only forecast facts; never serialized or exchanged with the ledger.
	CHealthValueContext detachedContext;

	int32_t firstHPleft;
	int32_t fullUnits;
	int32_t resurrected;
	int32_t unusableRemains;
	int64_t temporaryHitPoints;
	int64_t shadowGiftMaximumHealthLost = 0;
	bool capacityHealthTracking = false;
	int32_t capacityHealthMax = 0;
	bool capacityHealthMaxFixed = false;
	int64_t totalHealthOverride = 0;
	std::vector<CapacityHealthCohort> capacityHealthCohorts;
	std::vector<CasualtyProvenanceCohort> casualtyProvenance;
	bool casualtyProvenanceInitialized = false;
};
}
