/*
 * CUnitState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "BattleUnitTurnReason.h"
#include "NewHorizonsConfusionState.h"
#include "Unit.h"
#include "../bonuses/BonusCache.h"

#include <vector>

class JsonSerializeFormat;
class JsonNode;
class UnitChanges;

namespace vstd
{
	class RNG;
}

namespace battle
{
class CUnitState;
/// Validate the typed JSON receipt, including absent legacy state.
DLL_LINKAGE bool hasVeteranCohesionState(const JsonNode & unitSnapshot);

class DLL_LINKAGE CAmmo
{
public:
	explicit CAmmo(const battle::Unit * Owner, CSelector totalSelector);

	CAmmo & operator=(const CAmmo & other);
	CAmmo & operator=(CAmmo && other) = delete;

	int32_t available() const;
	bool canUse(int32_t amount = 1) const;
	virtual bool isLimited() const;
	virtual void reset();
	virtual int32_t total() const;
	virtual void use(int32_t amount = 1);

	virtual void serializeJson(JsonSerializeFormat & handler);
protected:
	int32_t used;
	const battle::Unit * owner;
	BonusValueCache totalProxy;
};

class DLL_LINKAGE CShots : public CAmmo
{
public:
	explicit CShots(const battle::Unit * Owner);
	CShots & operator=(const CShots & other);

	bool isLimited() const override;
	int32_t total() const override;

	void setEnv(const IUnitEnvironment * env_);
private:
	const IUnitEnvironment * env;

	BonusValueCache shooter;
};

class DLL_LINKAGE CCasts : public CAmmo
{
public:
	explicit CCasts(const battle::Unit * Owner);
};

class DLL_LINKAGE CRetaliations : public CAmmo
{
public:
	explicit CRetaliations(const battle::Unit * Owner);
	CRetaliations & operator=(const CRetaliations & other);

	bool isLimited() const override;
	int32_t total() const override;
	void reset() override;
	void setEnv(const IUnitEnvironment * env_);

	void serializeJson(JsonSerializeFormat & handler) override;
private:
	mutable int32_t totalCache;
	const IUnitEnvironment * env;

	BonusValueCache noRetaliation;
	BonusValueCache unlimited;
};

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
	int32_t getFirstHPleft() const;
	int32_t getResurrected() const;
	/// Number of casualties whose remains cannot be restored or harvested.
	int32_t getUnusableRemains() const;
	int32_t getCasualtyCount(DamageProvenance provenance) const;
	bool hasCasualtyProvenanceState() const;
	/// Battle-only hit points consumed before the stack's creature health.
	int64_t getTemporaryHitPoints() const;
	void addTemporaryHitPoints(int64_t amount);
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
	const battle::Unit * owner;

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

class DLL_LINKAGE CUnitState : public Unit
{
public:
	bool cloned;
	bool defending;
	bool drainedMana;
	bool fear;
	bool hadMorale;
	bool castSpellThisTurn;
	bool ghost;
	bool ghostPending;
	bool movedThisRound;
	/// Pending forced activation and battle-long Confusion result history.
	/// Ordinary round/activation boundaries do not clear this value implicitly.
	ConfusionState confusionState;
	/// Movement still available during a Pursuit/Vanish movement-only continuation.
	/// A positive value means this unit is in the movement-only tail of its
	/// current activation; it must never grant another attack.
	int32_t pursuitMovementRemaining;
	/// Cleave may create at most one automatic follow-up strike in a genuine
	/// creature activation. Hero actions and same-activation continuations keep it.
	bool cleaveUsedThisActivation;
	/// A ranged follow-up damage multiplier, persisted until used or declined.
	/// Zero means no pending shot; values from 1 to 100 are valid percentages.
	int32_t rangedFollowUpDamagePercent = 0;
	/// Round in which this stack last used Archery's once-per-round Counterfire.
	int32_t archeryCounterfireRound = -1;
	/// Accepted Wait arms Overwatch until the next genuine activation. Usage is
	/// independent of Counterfire and survives readiness expiry within a round.
	int32_t battlecraftOverwatchReadyRound = -1;
	int32_t battlecraftOverwatchUsedRound = -1;
	/// Round in which this stack first spent Deadeye on an ordinary ranged shot.
	int32_t archeryDeadeyeRound = -1;
	/// Global activation serial in which this stack first triggered Suppression.
	int32_t archerySuppressionActivationSerial = -1;
	/// Global activation serial in which this stack spent Rain of Arrows.
	int32_t archeryRainOfArrowsActivationSerial = -1;
	/// Crossfire provenance is tracked per battle side on the damaged stack.
	/// A new round invalidates both sets lazily via archeryCrossfireRound.
	int32_t archeryCrossfireRound = -1;
	std::vector<uint32_t> archeryCrossfireAttackers;
	std::vector<uint32_t> archeryCrossfireDefenders;
	/// Number of accepted activations remaining before No Quarter's morale penalty ends.
	int32_t noQuarterMoraleActivationsRemaining;
	/// One personal Bloodrage increment earned the first time this unit crosses below half HP.
	int32_t personalBloodrageIncrement = 0;
	/// Battle-long one-shot receipt; healing, death and round changes never rearm it.
	bool veteranCohesionEarned = false;
	/// Tenths of a hit point per creature carried between capacity-regeneration activations.
	int32_t capacityRegenerationRemainderTenths = 0;
	bool timeStopTurnConsumedFlag;
	/// Cast-time Regeneration mark rate in millionths; 1,000,000 is 100%.
	int32_t regenerationRateMillionths;
	/// Fixed-point HP marked for Regeneration, preserving fractions across hits.
	int64_t regenerationPendingMicroHealth;
	bool summoned;
	bool natureSummoned;
	bool waiting;
	bool waitedThisTurn; //"waited()" that stays true for full turn after wait - needed as UI button hackfix
	/// Whether the one-shot Battlecraft Wait damage bonus has already been spent this round.
	/// The availability is the conjunction of waitedThisTurn and !battlecraftWaitBonusUsed.
	bool battlecraftWaitBonusUsed;
	/// Whether this accepted Wait won Battlefield Mastery's per-side round award.
	/// The normal Wait lifetime remains authoritative; this only doubles its rank term.
	bool battlecraftWaitMasteryDoubled = false;
	/// Round in which this stack received Battlecraft's Pre-emptive Strike.
	/// Unlike Bulwark's Defend-scoped marker, this is not reset by Defend.
	int32_t battlecraftPreemptiveStrikeRound = -1;
	/// Temporary Speed granted only for this unit's delayed activation after Waiting.
	/// Cleared when the authoritative Creature Activation ends; unlike a timed bonus,
	/// it does not affect initiative or later activations.
	int32_t getActivationMovementBonus() const { return activationMovementBonus; }
	void setActivationMovementBonus(int32_t value);
	void setRangedFollowUpDamagePercent(int32_t value);
	/// Creature Defense supplied by the authoritative Defend action.  This is
	/// recorded explicitly because duration alone is not provenance: another
	/// temporary effect may also use STACK_GETS_TURN.
	int32_t defensiveStanceMeleeBonus;
	int32_t defensiveStanceRangedBonus;
	/// Whether this Defend stance won Battlefield Mastery's per-side round award.
	/// It expires at the same next-activation boundary as the Defend stance.
	bool battlecraftDefendMasteryDoubled = false;
	/// A lethal retaliation was prevented by Last Stand during this activation.
	/// Turn flow consumes it to suppress remaining attacks/continuations; a real
	/// next activation clears it deterministically in BattleInfo::nextTurn.
	bool armorerLastStandEndedActivation = false;
	/// A passive Last Stand Defend stance has transient CUnitState provenance
	/// which compact binary BattleInfo stack descriptors do not preserve.
	bool armorerLastStandDefending = false;
	/// Whether this Defend stance has already spent Bulwark's first-melee-attack reaction.
	bool bulwarkPreemptiveUsed;
	/// Whether Mire Grip has already applied its activation-scoped Speed penalty.
	bool bulwarkMireGripApplied;
	/// Physical creature damage received while Defending, pending Swamp Renewal.
	/// Preserved across round boundaries until the stack's next activation.
	int64_t bulwarkDefendPhysicalDamage;
	/// Actual creature HP lost to physical creature damage since this stack's last activation.
	int64_t veteranPhysicalDamageSinceActivation = 0;
	/// Round in which this stack first received Immovable's Defend reduction.
	int32_t bulwarkImmovableRound;
	/// Round in which this stack first received Armorer Bastion's physical-attack reduction.
	int32_t armorerBastionRound;
	/// Round in which this Defending stack first reflected a melee hit for Toxic Spines.
	int32_t bulwarkToxicSpinesRound;
	/// Saved physical Poison potency and remaining real-activation ticks (not SPELL_EFFECT bonuses).
	int64_t physicalPoisonBaseDamage;
	int32_t physicalPoisonActivationsRemaining;
	int32_t physicalPoisonSourceStackId;
	/// Separate Guardian Spirit buffer; only physical creature damage can consume it.
	int64_t guardianSpiritHitPoints = 0;
	int32_t guardianSpiritRoundsRemaining = 0;

	CCasts casts;
	CRetaliations counterAttacks;
	CHealth health;
	CShots shots;

	///id of alive clone of this stack clone if any
	si32 cloneID;

	///position on battlefield; -2 - keep, -3 - lower tower, -4 - upper tower
	BattleHex position;

	CUnitState();

	CUnitState(const CUnitState & other) = delete;
	CUnitState(CUnitState && other) = delete;

	CUnitState & operator= (const CUnitState & other);
	CUnitState & operator= (CUnitState && other) = delete;

	bool doubleWide() const override;

	int32_t creatureIndex() const override;
	CreatureID creatureId() const override;
	int32_t creatureLevel() const override;
	int32_t creatureCost() const override;
	int32_t creatureIconIndex() const override;

	int32_t getCasterUnitId() const override;

	int32_t getSpellSchoolLevel(const spells::Spell * spell, SpellSchool * outSelectedSchool = nullptr) const override;
	int32_t getEffectLevel(const spells::Spell * spell) const override;

	int64_t getSpellBonus(const spells::Spell * spell, int64_t base, const Unit * affectedStack) const override;
	int64_t getSpecificSpellBonus(const spells::Spell * spell, int64_t base) const override;

	int32_t getEffectPower(const spells::Spell * spell) const override;
	int32_t getEnchantPower(const spells::Spell * spell) const override;
	int64_t getEffectValue(const spells::Spell * spell) const override;
	int64_t getEffectRange(const spells::Spell * spell) const override;

	PlayerColor getCasterOwner() const override;
	const CGHeroInstance * getHeroCaster() const override;
	std::string getCasterNameTextID() const override;
	void getCastDescription(const spells::Spell * spell, const battle::Units & attacked, MetaString & text) const override;
	int32_t manaLimit() const override;

	bool ableToRetaliate() const override;
	bool alive() const override;
	bool isGhost() const override;
	bool isFrozen() const override;
	bool isNewHorizonsFrozen() const;
	int32_t frozenLastAppliedRound() const;
	/// Restore a validated binary receipt without attempting a new application.
	void restoreFrozenApplicationRound(int32_t round);
	/// Record successful application before publishing its physical marker.
	void recordFrozenApplication(int32_t round);
	/// Commit only the prevalidated recipient-local application stamp.
	void commitPreparedFrozenApplication(const CUnitState & prepared) noexcept;
	bool isValidTarget(bool allowDead = false) const override;

	bool isHypnotized() const override;
	bool isInvincible() const override;
	bool isTimeStopped() const override;

	bool isClone() const override;
	bool hasClone() const override;

	bool canCast() const override;
	bool isCaster() const override;
	bool canShootBlocked() const override;
	bool canShoot() const override;
	bool isShooter() const override;

	int32_t getKilled() const override;
	/// Magical casualties that remain part of the casualty total, including
	/// those temporarily restored for one battle.
	int32_t getMagicalCasualties() const;
	bool hasCasualtyProvenanceState() const;
	int32_t getCount() const override;
	int32_t getFirstHPleft() const override;
	int32_t getUnusableRemains() const override;
	int32_t getPersonalBloodrageIncrement() const override { return personalBloodrageIncrement; }
	int64_t getAvailableHealth() const override;
	int64_t getSurvivingMissingHealth() const override;
	int64_t getTotalHealth() const override;
	int64_t getShadowGiftCurrentHealth() const override;
	int64_t getShadowGiftMaximumHealth() const override;
	int64_t getShadowGiftMaximumHealthLost() const override;
	int64_t getPhantomIntegrity() const override;
	int64_t getPhantomInitialIntegrity() const override;
	int64_t getGuardianSpiritHitPoints() const override;
	int32_t getGuardianSpiritRoundsRemaining() const override;
	int32_t magicResistance() const override;
	uint32_t getMaxHealth() const override;

	/// Install the transient Phantom Army durability profile after the stack has
	/// completed normal battlefield initialization.
	void initializePhantomProfile(int64_t integrity, int32_t duration);

	/// True while a replacement creature form is active.
	bool hasBattleForm() const;
	/// Remaining form lifetime; zero after reversion. Time Stop pauses this value.
	int32_t getBattleFormRoundsRemaining() const { return battleFormRoundsRemaining; }
	bool isBattleFormRestorationPending() const { return battleFormRestorationPending; }
	void deferBattleFormRestoration();
	/// Effective battle creature, falling back to the stable source species.
	CreatureID battleFormCreature() const;
	/// Original source creature identity, retained after form expiry.
	CreatureID battleFormOriginalCreature() const;
	/// True when this state has form/provenance payload requiring the new save contract.
	bool hasBattleFormState() const;
	void beginBattleForm(CreatureID creature, int32_t rounds);
	void endBattleForm();
	/// Synchronous invalidation hook for effective creature bonuses and caches.
	virtual void onBattleFormChanged();
	int32_t getBattleFormViewRevision() const;

	BattleHex getPosition() const override;
	void setPosition(const BattleHex & hex) override;
	int32_t getInitiative(int turn = 0) const override;
	uint8_t getRangedFullDamageDistance() const;
	uint8_t getShootingRangeDistance() const;

	ui32 getMovementRange(int turn) const override;
	ui32 getMovementRange() const override;

	bool canMove(int turn = 0) const override;
	bool defended(int turn = 0) const override;
	bool moved(int turn = 0) const override;
	bool timeStopTurnConsumed() const override;
	bool willMove(int turn = 0) const override;
	bool waited(int turn = 0) const override;
	bool battlecraftWaitBonusAvailable() const override;

	std::shared_ptr<Unit> acquire() const override;
	std::shared_ptr<CUnitState> acquireState() const override;

	BattlePhases::Type battleQueuePhase(int turn) const override;

	int getTotalAttacks(bool ranged) const override;

	int getMinDamage(bool ranged) const override;
	int getMaxDamage(bool ranged) const override;

	int getAttack(bool ranged) const override;
	int getDefense(bool ranged) const override;
	int getDefenseIgnoringDefensiveStance(bool ranged) const override;

	JsonNode save() override;
	void load(const JsonNode & data) override;

	void damage(int64_t & amount) override;
	void damage(int64_t & amount, bool destroyRemains);
	void damage(int64_t & amount, bool destroyRemains, DamageProvenance provenance);
	void damageShadowGiftSacrifice(int64_t & amount);
	void addShadowGiftMaximumHealthLoss(int64_t amount);
	HealInfo heal(int64_t & amount, EHealLevel level, EHealPower power) override;

	void localInit(const IUnitEnvironment * env_);
	void serializeJson(JsonSerializeFormat & handler);

	FactionID getFactionID() const override;

	/// Finalize ammunition/retaliation use after an attack.  Physical attacks also
	/// spend the one-shot Battlecraft Wait bonus, if this stack earned it.
	void afterAttack(bool ranged, bool counter, bool physical = true);

	/// Record the authoritative Wait action and arm Battlecraft's one-shot bonus.
	void afterWait();

	void afterNewRound(bool isFirstRound = false, bool deferBattleFormRestoration = false, bool pauseBattleForm = false);

	void afterGetsTurn(BattleUnitTurnReason reason);

	/// Non-mutating near-term Regeneration forecast, clamped to surviving wounds.
	int64_t regenerationProjectedHeal() const;
	/// Preserve current creature HP when a per-creature health capacity is raised.
	/// Rear survivors become grouped health cohorts; this never heals or resurrects.
	void preserveCreatureHealthOnCapacityIncrease();
	/// Normalize tracked health after a capacity bonus changes or expires.
	void normalizeCapacityHealth();
	/// Commit recipient-local capacity projection without allocating or loading
	/// packet-provided health. All validation occurred on the detached projection.
	void commitPreparedCapacityHealth(CUnitState & prepared) noexcept;
	/// Exact aggregate HP the next genuine capacity-regeneration activation would restore.
	int64_t capacityRegenerationProjectedHeal() const;
	/// Apply the same projected capacity-regeneration tick and advance its tenths carry.
	int64_t consumeCapacityRegeneration();
	/// Original max HP captured for an active capacity effect, or current max when inactive.
	int32_t getCapacityHealthReferenceMax() const;
	/// Clear the non-compounding max-health baseline after its source effects are gone.
	void clearCapacityHealthReference();
	/// Mark actual new wounds on the top surviving creature using the saved rate.
	void recordRegenerationWounds(int64_t newWoundHealth);
	/// Consume all marks at activation start, returning only healable surviving wounds.
	int64_t consumeRegenerationMarks();

	bool archeryCrossfireAvailable(BattleSide side, uint32_t currentShooter, int32_t round) const;
	void archeryRecordCrossfireDamage(BattleSide side, uint32_t shooter, int32_t round);

	void makeGhost();

	void onRemoved();

private:
	void damageInternal(int64_t & amount, bool destroyRemains, bool bypassTemporaryHitPoints,
		DamageProvenance provenance = DamageProvenance::OTHER);
	std::pair<int32_t, int32_t> getMoraleLimits() const override;
	const IUnitEnvironment * env = nullptr;
	int32_t activationMovementBonus = 0;
	int32_t frozenAppliedRound = -1;
	int64_t phantomInitialIntegrity = 0;
	int64_t phantomIntegrity = 0;
	int32_t phantomRoundsRemaining = 0;
	int64_t phantomShadowGiftMaximumHealthLost = 0;
	int32_t capacityHealthReferenceMax = 0;
	CHealth battleFormOriginalHealth;
	CreatureID battleFormCreatureId = CreatureID(-1);
	CreatureID battleFormOriginalCreatureId = CreatureID(-1);
	int32_t battleFormRoundsRemaining = 0;
	bool battleFormRestorationPending = false;
	int32_t battleFormOriginalMaxHealth = 0;
	int32_t battleFormOriginalCount = 0;
	int32_t battleFormInitiativeSnapshot = 0;
	bool battleFormInitiativeSnapshotActive = false;
	int32_t battleFormOriginalCapacityHealthReferenceMax = 0;
	int32_t battleFormOriginalCapacityRegenerationRemainderTenths = 0;
	int32_t battleFormViewRevision = 0;

	BonusCachePerTurn initiativeBasePerTurn;
	BonusCachePerTurn initiativeBasePresencePerTurn;
	BonusCachePerTurn initiativePercentPerTurn;
	BonusCachePerTurn initiativeFlatPerTurn;
	BonusCachePerTurn stackSpeedPerTurn;
	BonusCachePerTurn movementRangePerTurn;
	BonusCachePerTurn immobilizedPerTurn;
	UnitBonusValuesProxy bonusCache;

	void reset();
};

/// Check a serialized Unit::save() snapshot for nonlegacy magical casualty
/// provenance before forwarding it through an older wire format.
DLL_LINKAGE bool hasCasualtyProvenanceState(const JsonNode & unitSnapshot);
/// Validates raw Overwatch round markers without modifying a unit. Missing
/// legacy fields mean inactive; noninteger/out-of-range markers are rejected.
DLL_LINKAGE bool hasOverwatchState(const JsonNode & unitSnapshot);

class DLL_LINKAGE CUnitStateDetached final : public CUnitState
{
public:
	explicit CUnitStateDetached(const IUnitInfo * unit_, const IBonusBearer * bonus_);

	CUnitStateDetached & operator= (const CUnitState & other);

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(
		const CSelector & selector, const std::string & cachingStr = {}, bool unstacked = false) const override;

	int32_t getTreeVersion() const override;

	uint32_t unitId() const override;
	BattleSide unitSide() const override;

	const CCreature * unitType() const override;
	PlayerColor unitOwner() const override;

	SlotID unitSlot() const override;

	int32_t unitBaseAmount() const override;
	int64_t getBattleStartMaximumAggregateHP() const override;
	int64_t getRebirthOriginalAggregateHP() const override;

	void spendMana(ServerCallback * server, const int spellCost) const override;

private:
	const IUnitInfo * unit;
	const IBonusBearer * bonus;
};

}
