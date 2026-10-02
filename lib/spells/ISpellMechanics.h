/*
 * ISpellMechanics.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <limits>

#include <vcmi/spells/Magic.h>
#include <vcmi/ServerCallback.h>

#include "../battle/Destination.h"
#include "../battle/BattleSide.h"
#include "../int3.h"
#include "../GameConstants.h"
#include "../bonuses/Bonus.h"

struct Query;
class IBattleState;
class CreatureService;
class CMap;
class IGameInfoCallback;
class CBattleInfoCallback;
class JsonNode;
class CStack;
class CGObjectInstance;
class CGHeroInstance;
class Creature;
class IAdventureSpellEffect;
class MetaString;

namespace spells
{
class Service;
	namespace effects
	{
		class Effect;
	}
}

namespace vstd
{
	class RNG;
}

namespace scripting
{
	class Service;
}

///callback to be provided by server
class DLL_LINKAGE SpellCastEnvironment : public ServerCallback
{
public:
	virtual ~SpellCastEnvironment() = default;

	virtual const CMap * getMap() const = 0;
	virtual const IGameInfoCallback * getCb() const = 0;

	virtual void createBoat(const int3 & visitablePosition, BoatId type, PlayerColor initiator) = 0;
	virtual bool moveHero(ObjectInstanceID hid, int3 dst, EMovementMode mode) = 0;	//TODO: remove
	virtual void showGarrisonDialog(ObjectInstanceID upobj, ObjectInstanceID hid, bool removableUnits, const MetaString & customTitle) = 0;

	virtual void genericQuery(Query * request, PlayerColor color, std::function<void(std::optional<int32_t>)> callback) = 0;//TODO: type safety on query, use generic query packet when implemented
	/// Prepares a per-cast completion callback. It is invoked only if the
	/// adventure spell's effects complete successfully.
	virtual std::function<void()> prepareAdventureSpellCastCompletion(const spells::Caster *, SpellID) { return {}; }
};

namespace spells
{

/// Applies a snapshotted Warcasting percentage to an already identified
/// Spell-Power-derived numerator, then divides with integer truncation.
/// The input must exclude any fixed spell base or level-power component.
DLL_LINKAGE int64_t scaleWarcastingSpellPowerComponent(int64_t numerator, int64_t divisor, int32_t bonusPercent);

/// Scales an identified Spell-Power-derived numerator by an absolute
/// basis-point coefficient and the snapshotted Warcasting percentage. Fixed
/// spell bases and level-power components must stay outside the input.
DLL_LINKAGE int64_t scaleSpellPowerComponentWithCoefficientBasisPoints(int64_t numerator, int32_t divisor,
	int32_t coefficientBasisPoints, int32_t warcastingBonusPercent = 0, int32_t empowerSpellBonusPercent = 0);

class DLL_LINKAGE IBattleCast
{
public:
	using Value = int32_t;
	using Value64 = int64_t;

	using OptionalValue = std::optional<Value>;
	using OptionalValue64 = std::optional<Value64>;

	virtual const CSpell * getSpell() const = 0;
	virtual Mode getMode() const = 0;
	virtual const Caster * getCaster() const = 0;
	virtual const CBattleInfoCallback * getBattle() const = 0;

	virtual OptionalValue getSpellLevel() const = 0;

	virtual OptionalValue getEffectPower() const = 0;
	virtual OptionalValue getEffectDuration() const = 0;
	/// Additional mana selected for a spell-specific cast option.  The default
	/// keeps old callers and non-Sorcery spells unchanged.
	virtual OptionalValue getOvercharge() const { return std::nullopt; }
	/// Optional New Horizons Cure source identity; NONE is the heal-only choice.
	virtual SpellID getCureAffliction() const { return SpellID::NONE; }
	/// Server-side passive effects such as canonical Fire Wall may explicitly
	/// target either side.  Ordinary casts retain their spell-defined smart
	/// targeting when this remains false.
	virtual bool getForceNonSmartTargeting() const { return false; }
	virtual bool getSelectiveDispel() const { return false; }
	virtual bool getMassSlow() const { return false; }
	virtual int32_t getShadowGiftSacrificePercent() const { return 0; }
	/// An immediate additional cast granted by Tower Metamagic.  This is an
	/// authoritative action flag, not a client-side effect hint.
	virtual bool isMetamagicFollowup() const { return false; }
	virtual bool isMetamagicGrand() const { return false; }
	virtual uint32_t getMetamagicTargetUnitId() const { return std::numeric_limits<uint32_t>::max(); }
	virtual int32_t getMetamagicManaRefund() const { return 0; }

	virtual OptionalValue64 getEffectValue() const = 0;
	/// Ward side is set by the authoritative battle action processor when this
	/// hero spell consumes or collapses an armed Counterspell.
	virtual BattleSide getCounterspellSide() const { return BattleSide::NONE; }
	virtual bool isCounterspellNegated() const { return false; }
	/// Exact Mana paid by the armed opposing ward for this accepted cast.
	virtual int32_t getCounterspellManaSpent() const { return 0; }

	virtual bool isForceMassive() const = 0;
};

///all parameters of particular cast event
class DLL_LINKAGE BattleCast : public IBattleCast
{
public:
	bool forceMassive = false; // force this cast to be massive regardless of the spell default

	//normal constructor
	BattleCast(const CBattleInfoCallback * cb_, const Caster * caster_, const Mode mode_, const CSpell * spell_);

	virtual ~BattleCast();

	///IBattleCast
	const CSpell * getSpell() const override;
	Mode getMode() const override;
	const Caster * getCaster() const override;
	const CBattleInfoCallback * getBattle() const override;

	OptionalValue getSpellLevel() const override;

	OptionalValue getEffectPower() const override;
	OptionalValue getEffectDuration() const override;
	OptionalValue getOvercharge() const override;
	SpellID getCureAffliction() const override;
	bool getForceNonSmartTargeting() const override;
	bool getSelectiveDispel() const override;
	bool getMassSlow() const override;
	int32_t getShadowGiftSacrificePercent() const override;
	bool isMetamagicFollowup() const override;
	bool isMetamagicGrand() const override;
	uint32_t getMetamagicTargetUnitId() const override;
	int32_t getMetamagicManaRefund() const override;

	OptionalValue64 getEffectValue() const override;
	BattleSide getCounterspellSide() const override;
	bool isCounterspellNegated() const override;
	int32_t getCounterspellManaSpent() const override;

	bool isForceMassive() const override;

	void setSpellLevel(Value value);

	void setEffectPower(Value value);
	void setEffectDuration(Value value);
	void setOvercharge(Value value);
	void setCureAffliction(SpellID value);
	void setForceNonSmartTargeting(bool value);
	void setSelectiveDispel(bool value);
	void setMassSlow(bool value);
	void setShadowGiftSacrificePercent(int32_t value);
	void setMetamagicFollowup(bool value);
	void setMetamagicGrand(bool value);
	void setMetamagicTargetUnitId(uint32_t value);
	void setMetamagicManaRefund(int32_t value);

	void setEffectValue(Value64 value);
	void setCounterspell(BattleSide wardSide, bool negated, int32_t manaSpent = 0);

	///only apply effects to specified targets
	void applyEffects(ServerCallback * server, const Target & target, bool indirect = false, bool ignoreImmunity = false) const;

	///normal cast
	void cast(ServerCallback * server, Target target);

	///cast evaluation
	void castEval(ServerCallback * server, Target target);

	///cast with silent check for permitted cast
	bool castIfPossible(ServerCallback * server, Target target);

private:
	///spell school level
	OptionalValue magicSkillLevel;

	///actual spell-power affecting effect values
	OptionalValue effectPower;
	///actual spell-power affecting effect duration
	OptionalValue effectDuration;

	///for Archangel-like casting
	OptionalValue64 effectValue;
	///Additional mana selected for a spell-specific cast option.
	OptionalValue overcharge;
	///Optional New Horizons Cure source group selected by the player.
	SpellID cureAffliction = SpellID::NONE;
	bool forceNonSmartTargeting = false;
	bool selectiveDispel = false;
	bool massSlow = false;
	int32_t shadowGiftSacrificePercent = 0;
	bool metamagicFollowup = false;
	bool metamagicGrand = false;
	uint32_t metamagicTargetUnitId = std::numeric_limits<uint32_t>::max();
	int32_t metamagicManaRefund = 0;
	BattleSide counterspellSide = BattleSide::NONE;
	bool counterspellNegated = false;
	int32_t counterspellManaSpent = 0;

	Mode mode;
	const CSpell * spell;
	const CBattleInfoCallback * cb;
	const Caster * caster;
};

class DLL_LINKAGE ISpellMechanicsFactory
{
public:
	virtual ~ISpellMechanicsFactory();

	virtual std::unique_ptr<Mechanics> create(const IBattleCast * event) const = 0;

	static std::unique_ptr<ISpellMechanicsFactory> get(const CSpell * s);

protected:
	const CSpell * spell;

	ISpellMechanicsFactory(const CSpell * s);
};

class DLL_LINKAGE Mechanics : public scripting::ApiRawPointer<Mechanics>
{
public:
	virtual ~Mechanics();

	virtual void forEachEffect(const std::function<bool(const effects::Effect &)> & fn) const
	{ }

	template<class T>
	const T * findEffect() const
	{
		const T * found = nullptr;
		forEachEffect([&found](const effects::Effect & e)
		{
			if(auto p = dynamic_cast<const T *>(&e))
			{
				found = p;
				return true;
			}
			return false;
		});
		return found;
	}

	virtual bool adaptProblem(ESpellCastProblem source, Problem & target) const = 0;
	virtual bool adaptGenericProblem(Problem & target) const = 0;

	virtual BattleHexArray rangeInHexes(const BattleHex & centralHex) const = 0;
	virtual std::vector<const CStack *> getAffectedStacks(const Target & target) const = 0;

	virtual bool canBeCast(Problem & problem) const = 0;
	virtual bool canBeCastAt(const Target & target) const = 0;
	virtual bool canBeCastAt(const Target & target, Problem & problem) const = 0;

	virtual void applyEffects(ServerCallback * server, const Target & targets, bool indirect, bool ignoreImmunity) const = 0;

	virtual void cast(ServerCallback * server, const Target & target) = 0;

	virtual void castEval(ServerCallback * server, const Target & target) = 0;

	virtual bool isReceptive(const battle::Unit * target) const = 0;
	virtual bool wouldResist(const battle::Unit * target) const = 0;

	virtual std::vector<AimType> getTargetTypes() const = 0;

	virtual const Spell * getSpell() const = 0;

	//Cast event facade

	virtual IBattleCast::Value getEffectLevel() const = 0;
	virtual IBattleCast::Value getRangeLevel() const = 0;

	virtual IBattleCast::Value getEffectPower() const = 0;
	virtual int32_t getEffectPowerDivisor() const { return 1; }
	/// Effective saved-rules school-rank coefficient for this spell and caster.
	int32_t getSchoolRankPowerCoefficientPercent() const;
	/// Effective saved-rules School × Spellcraft coefficient, in basis points.
	int32_t getSpellPowerCoefficientBasisPoints() const;
	/// Arcane Focus percentage snapshotted for this hero-cast context. Non-hero
	/// casts and casts after a completed hero spell return zero.
	virtual int32_t getArcaneFocusBonusPercent() const { return 0; }
	/// Combined cast-specific multiplier above the ordinary School × Spellcraft
	/// coefficient. Existing Mechanics implementations retain Arcane Focus only.
	virtual int32_t getCastSpellPowerComponentBonusPercent() const { return getArcaneFocusBonusPercent(); }
	/// Saved-v3 Quicksand's authoritative patch count, or zero for legacy rules
	/// and every other spell.
	int32_t getNewHorizonsQuicksandPatchCount() const;
	bool usesNewHorizonsEarthquake() const;
	int32_t getNewHorizonsEarthquakeParameter(const std::string & name) const;
	int32_t getNewHorizonsEarthquakeSectionCount() const;
	/// New Horizons Land Mine's authoritative selected-hex count, or zero
	/// outside the New Horizons Land Mine profile. Pre-v3 snapshots retain raw
	/// Spell Power; v3 uses the composed saved coefficient.
	int32_t getNewHorizonsLandMinePatchCount() const;
	/// Empower Spell's +25% applies only to the power-derived term.
	int32_t getEmpowerSpellBonusPercent() const;
	/// Resolves a configured chain-effect target count against the saved battle
	/// profile, shared by authoritative casts and target previews/evaluators.
	int32_t getEffectiveChainLength(int32_t configuredLength) const;
	/// Percentage captured from the matching pre-cast Warcasting readiness.
	/// Non-hero casts and Metamagic follow-ups return zero.
	virtual int32_t getWarcastingBonusPercent() const { return 0; }
	virtual IBattleCast::Value getEffectDuration() const = 0;
	/// Applies cast-specific modifiers to a script-supplied fixed duration.
	virtual IBattleCast::Value adjustEffectDuration(IBattleCast::Value baseDuration) const { return baseDuration; }
	virtual bool isSelectiveDispel() const { return false; }
	virtual bool isNewHorizonsCure() const { return false; }
	virtual SpellID getCureAffliction() const { return SpellID::NONE; }
	virtual bool isMassSlow() const { return false; }
	/// Selected Shadow Gift tier and shared preview calculations. Invalid or
	/// legacy casts return zero and cannot acquire the saved-v3 mechanic.
	virtual int32_t getShadowGiftSacrificePercent() const { return 0; }
	int32_t getShadowGiftSacrificeCostBasisPoints() const;
	int32_t getShadowGiftDamageBonusBasisPoints() const;
	/// True only for a saved v3 Storm of Daggers spell entry.
	virtual bool isNewHorizonsStormOfDaggers() const { return false; }
	/// Sets the selected stack count for shared cast/preview calculations. Returns
	/// false when this is not the saved spell or the count is outside 1..5.
	virtual bool setStormOfDaggersTargetCount(int32_t) { return false; }
	/// Shared raw pool projections, before per-target resistance or mitigation.
	virtual int64_t getStormOfDaggersDamagePerTarget(int32_t) const { return 0; }
	virtual int64_t getStormOfDaggersTotalDamage(int32_t) const { return 0; }
	/// True when this cast consumes an additional Metamagic Spell Action.
	/// Exposed on the common Mechanics facade so Lua spell effects can preserve
	/// authoritative cast provenance without depending on BaseMechanics.
	virtual bool isMetamagicFollowup() const { return false; }
	virtual bool usesNewHorizonsMagic() const { return false; }
	/// True only for a saved New Horizons magic-rules v3 battle.
	virtual bool usesNewHorizonsMagicV3() const { return false; }
	/// True only when the saved v3 Quicksand row opts into selected placement.
	virtual bool usesNewHorizonsQuicksandSelectedPlacement() const { return false; }
	/// True only when the saved spell roster contains the Holy Armor feature marker.
	virtual bool usesNewHorizonsMultiplicativeMDR() const { return false; }
	/// True only when resolving the effect target selected by ordinary Magic Mirror.
	virtual bool isMagicMirror() const { return false; }

	virtual IBattleCast::Value64 getEffectValue() const = 0;

	virtual PlayerColor getCasterColor() const = 0;
	virtual BattleSide getCasterSide() const { return casterSide; };
	virtual const CGHeroInstance * getHeroCaster() const = 0;
	virtual const battle::Unit * getUnitCaster() const = 0;

	//Spell facade
	virtual int32_t getSpellIndex() const = 0;
	virtual SpellID getSpellId() const = 0;
	virtual std::string getSpellName() const = 0;
	virtual std::string getCasterNameTextID() const = 0;
	virtual int32_t getSpellLevel() const = 0;
	/// Returns the effective max health of a hypothetical temporary summoned stack,
	/// evaluated with the normal creature/hero bonus and limiter graph without
	/// attaching a live child to either source node.
	int32_t getSummonedCreatureMaxHealth(const Creature * creature, bool natureSummoned) const;

	virtual bool isSmart() const = 0;
	virtual bool isMassive() const = 0;
	virtual bool alwaysHitFirstTarget() const = 0;
	virtual bool requiresClearTiles() const = 0;

	virtual bool isNegativeSpell() const = 0;
	virtual bool isPositiveSpell() const = 0;
	virtual bool isNeutralSpell() const = 0;
	virtual bool isMagicalEffect() const = 0;

	virtual int64_t adjustEffectValue(const battle::Unit * target) const = 0;
	/// Applies only recipient damage modifiers to an already resolved raw hit.
	/// Does not repeat the caster's power, offensive bonuses, or execution rules.
	int64_t adjustRecipientDamage(const battle::Unit * target, int64_t rawDamage) const;
	/// Returns target-adjusted damage before an execute-style threshold override.
	/// Mechanics without such an override use their ordinary adjusted value.
	virtual int64_t adjustEffectValueBeforeExecution(const battle::Unit * target) const
	{
		return adjustEffectValue(target);
	}
	virtual int64_t applySpellBonus(int64_t value, const battle::Unit * target) const = 0;
	virtual int64_t applySpecificSpellBonus(int64_t value) const = 0;
	virtual int64_t calculateRawEffectValue(int32_t basePowerMultiplier, int32_t levelPowerMultiplier) const = 0;
	/// Scales an explicitly Spell-Power-derived numerator before applying its divisor.
	/// Fixed base terms must be added by the caller after this calculation.
	int64_t scaleSpellPowerComponent(int64_t numerator, int32_t divisor = 1) const;
	int64_t scaleSpellPowerComponentWithCoefficient(int64_t numerator, int32_t divisor,
		int32_t coefficientPercent) const;
	int64_t scaleSpellPowerComponentWithCoefficientBasisPoints(int64_t numerator, int32_t divisor,
		int32_t coefficientBasisPoints) const;
	virtual Target canonicalizeTarget(const Target & aim) const = 0;

	//Battle facade
	virtual bool ownerMatches(const battle::Unit * unit) const = 0;
	virtual bool ownerMatches(const battle::Unit * unit, bool sameOwner) const = 0;

	//Global environment facade
	virtual const CreatureService * creatures() const = 0;
	virtual const scripting::Service * scripts() const = 0;
	virtual const Service * spells() const = 0;

	virtual const CBattleInfoCallback * battle() const = 0;
	virtual BattleID getBattleID() const = 0;

	const Caster * caster;

	BattleSide casterSide;

protected:
	Mechanics();
};

class DLL_LINKAGE BaseMechanics : public Mechanics
{
public:
	virtual ~BaseMechanics();

	bool adaptProblem(ESpellCastProblem source, Problem & target) const override;
	bool adaptGenericProblem(Problem & target) const override;

	int32_t getSpellIndex() const override;
	SpellID getSpellId() const override;
	std::string getSpellName() const override;
	std::string getCasterNameTextID() const override;
	int32_t getSpellLevel() const override;

	IBattleCast::Value getEffectLevel() const override;
	IBattleCast::Value getRangeLevel() const override;
	IBattleCast::Value getEffectPower() const override;
	int32_t getEffectPowerDivisor() const override;
	int32_t getWarcastingBonusPercent() const override;
	int32_t getArcaneFocusBonusPercent() const override;
	int32_t getCastSpellPowerComponentBonusPercent() const override;
	IBattleCast::Value getEffectDuration() const override;
	IBattleCast::Value adjustEffectDuration(IBattleCast::Value baseDuration) const override;
	IBattleCast::Value64 getEffectValue() const override;
	IBattleCast::Value getOvercharge() const;
	SpellID getCureAffliction() const override;
	BattleSide getCounterspellSide() const;
	bool isCounterspellNegated() const;
	int32_t getCounterspellManaSpent() const;
	bool isSelectiveDispel() const override;
	bool isNewHorizonsCure() const override;
	bool isMassSlow() const override;
	int32_t getShadowGiftSacrificePercent() const override;
	bool isNewHorizonsStormOfDaggers() const override;
	bool setStormOfDaggersTargetCount(int32_t selectedTargetCount) override;
	int64_t getStormOfDaggersDamagePerTarget(int32_t selectedTargetCount) const override;
	int64_t getStormOfDaggersTotalDamage(int32_t selectedTargetCount) const override;
	bool isMetamagicFollowup() const override;
	bool isMetamagicGrand() const;
	uint32_t getMetamagicTargetUnitId() const;
	int32_t getMetamagicManaRefund() const;
	bool usesNewHorizonsMagic() const override;
	bool usesNewHorizonsMagicV3() const override;
	bool usesNewHorizonsQuicksandSelectedPlacement() const override;
	bool usesNewHorizonsMultiplicativeMDR() const override;
	bool isMagicMirror() const override;

	PlayerColor getCasterColor() const override;
	const CGHeroInstance * getHeroCaster() const override;
	const battle::Unit * getUnitCaster() const override;

	bool isSmart() const override;
	bool isMassive() const override;
	bool requiresClearTiles() const override;
	bool alwaysHitFirstTarget() const override;

	bool isNegativeSpell() const override;
	bool isPositiveSpell() const override;
	bool isNeutralSpell() const override;
	bool isMagicalEffect() const override;

	int64_t adjustEffectValue(const battle::Unit * target) const override;
	int64_t adjustEffectValueBeforeExecution(const battle::Unit * target) const override;
	int64_t applySpellBonus(int64_t value, const battle::Unit * target) const override;
	int64_t applySpecificSpellBonus(int64_t value) const override;
	int64_t calculateRawEffectValue(int32_t basePowerMultiplier, int32_t levelPowerMultiplier) const override;
	Target canonicalizeTarget(const Target & aim) const override;

	bool ownerMatches(const battle::Unit * unit) const override;
	bool ownerMatches(const battle::Unit * unit, bool sameOwner) const override;

	std::vector<AimType> getTargetTypes() const override;

	const CreatureService * creatures() const override;
	const scripting::Service * scripts() const override;
	const Service * spells() const override;

	const CBattleInfoCallback * battle() const override;
	BattleID getBattleID() const override;

protected:
	const CSpell * owner;
	Mode mode;
	int64_t adjustEffectValueImpl(const battle::Unit * target, bool applyExecution) const;
	bool forceNonSmartTargeting = false;
	bool usesNewHorizonsBerserkTargeting() const;
	bool usesNewHorizonsDispelRules() const;

	BaseMechanics(const IBattleCast * event);

private:
    IBattleCast::Value rangeLevel;
	IBattleCast::Value effectLevel;

	///actual spell-power affecting effect values
	IBattleCast::Value effectPower;
	///Matching ordinary-hero Spell Warcasting empowerment captured before cast consumption.
	int32_t warcastingBonusPercent = 0;
	/// First-cast Arcane Focus captured before BattleSpellCast marks completion.
	int32_t arcaneFocusBonusPercent = 0;
	/// Grand Formula's 150% component multiplier, or 100% when unavailable.
	int32_t grandFormulaMultiplierPercent = 100;
	///actual spell-power affecting effect duration
	IBattleCast::Value effectDuration;

	///raw damage/heal amount
	IBattleCast::Value64 effectValue;
	int32_t stormOfDaggersTargetCount = 0;
	///Additional mana selected for a spell-specific cast option.
	IBattleCast::Value overcharge = 0;
	SpellID cureAffliction = SpellID::NONE;
	BattleSide counterspellSide = BattleSide::NONE;
	bool counterspellNegated = false;
	int32_t counterspellManaSpent = 0;
	bool selectiveDispel = false;
	bool massSlow = false;
	int32_t shadowGiftSacrificePercent = 0;
	bool metamagicFollowup = false;
	bool metamagicGrand = false;
	uint32_t metamagicTargetUnitId = std::numeric_limits<uint32_t>::max();
	uint32_t metamagicFirstTargetUnitId = std::numeric_limits<uint32_t>::max();
	bool metamagicFocusedPairingEligible = false;
	int32_t metamagicManaRefund = 0;

	bool forceMassive = false;

	const CBattleInfoCallback * cb;
};

class DLL_LINKAGE IReceptiveCheck
{
public:
	virtual ~IReceptiveCheck() = default;

	virtual bool isReceptive(const Mechanics * m, const battle::Unit * target) const = 0;
};

}// namespace spells

class DLL_LINKAGE AdventureSpellCastParameters
{
public:
	const spells::Caster * caster;
	int3 pos;
};

class DLL_LINKAGE IAdventureSpellMechanics
{
public:
	IAdventureSpellMechanics(const CSpell * s);
	virtual ~IAdventureSpellMechanics() = default;

	virtual bool canBeCast(spells::Problem & problem, const IGameInfoCallback * cb, const spells::Caster * caster) const = 0;
	virtual bool canBeCastAt(spells::Problem & problem, const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const = 0;
	virtual bool adventureCast(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const = 0;
	virtual int getCastsLimit(const spells::Caster * caster, const int3 & mapSize) const = 0;
	virtual int getCastsAlreadyPerformed(const spells::Caster * caster) const = 0;

	static std::unique_ptr<IAdventureSpellMechanics> createMechanics(const CSpell * s);

	virtual bool givesBonus(const spells::Caster * caster, BonusType which) const = 0;

	template<typename EffectType>
	const EffectType * getEffectAs(const spells::Caster * caster) const
	{
		return dynamic_cast<const EffectType *>(getEffect(caster));
	}
protected:
	virtual const IAdventureSpellEffect * getEffect(const spells::Caster * caster) const = 0;

	const CSpell * owner;
};
