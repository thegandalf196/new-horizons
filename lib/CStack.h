/*
 * CStack.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once
#include "bonuses/Bonus.h"
#include "bonuses/BonusList.h"
#include "bonuses/CBonusSystemNode.h"
#include "CCreatureHandler.h" //todo: remove
#include "battle/BattleHex.h"
#include "mapObjects/CGHeroInstance.h" // for commander serialization

#include "battle/CUnitState.h"
#include "battle/NewHorizonsBloodrage.h"

#include <utility>

struct BattleStackAttacked;
class BattleInfo;
class CArmedInstance;

//Represents STACK_BATTLE nodes
class DLL_LINKAGE CStack : public CBonusSystemNode, public battle::CUnitState, public battle::IUnitEnvironment
{
private:
	ui32 ID = -1; //unique ID of stack
	CreatureID typeID;
	ui32 baseAmount = -1;

	PlayerColor owner; //owner - player color (255 for neutrals)
	BattleSide side = BattleSide::NONE;

	SlotID slot;  //slot - position in garrison (may be 255 for neutrals/called creatures)
	int64_t battleStartMaximumAggregateHP = 0; //Elemental Rebirth source basis; zero for legacy/ineligible battles
	int64_t rebirthOriginalAggregateHP = 0; //Exact first-generation Rebirth output HP; zero for other stacks

	bool doubleWideCached = false;
	const CCreature * formBonusSource = nullptr; // transient effective native source while polymorphed

public:
	void postDeserialize(const CArmedInstance * army);

	const CStackInstance * base = nullptr; //garrison slot from which stack originates (nullptr for war machines, summoned cres, etc)
	
	BattleHex initialPosition; //position on battlefield; -2 - keep, -3 - lower tower, -4 - upper tower

	CStack(const CStackInstance * base, const PlayerColor & O, int I, BattleSide Side, const SlotID & S);
	CStack(const CStackBasicDescriptor * stack, const PlayerColor & O, int I, BattleSide Side,
		const SlotID & S = SlotID(255), bool hypothetical = false);
	CStack();
	~CStack();

	std::string nodeName() const override;

	// Only the ordinary/delayed queue flags, not a second unit health/state serializer.
	template <typename Handler> void validateRapidResponseSerialization(Handler & h) const
	{
		if((waiting && (!waitedThisTurn || defending)) || (battlecraftWaitMasteryDoubled && !waitedThisTurn))
			throw std::runtime_error("Invalid delayed-activation queue flags");
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RAPID_RESPONSE)
			&& (waiting || waitedThisTurn || movedThisRound || defending || hadMorale
				|| battlecraftWaitBonusUsed || battlecraftWaitMasteryDoubled))
			throw std::runtime_error("Cannot discard existing delayed-activation queue flags");
	}
	void localInit(BattleInfo * battleInfo);
	void afterNewRound(bool isFirstRound = false, bool deferBattleFormRestoration = false, bool pauseBattleForm = false);
	bool acceptsBonus(const Bonus & bonus) const override;
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = {}) const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(
		const CSelector & selector, const std::string & cachingStr = {}, bool unstacked = false) const override;
	std::string getName() const; //plural or singular

	bool canBeHealed() const; //for first aid tent - only harmed stacks that are not war machines
	bool isOnNativeTerrain() const;
	TerrainId getCurrentTerrain() const;

	int32_t unitLevel() const override;
	si32 magicResistance() const override; //include aura of resistance
	std::vector<SpellID> activeSpells() const; //returns vector of active spell IDs sorted by time of cast
	const CGHeroInstance * getMyHero() const; //if stack belongs to hero (directly or was by him summoned) returns hero, nullptr otherwise

	void prepareAttacked(BattleStackAttacked & bsa, vstd::RNG & rand, bool destroyRemains = false,
		battle::DamageProvenance provenance = battle::DamageProvenance::OTHER) const; //requires bsa.damageAmount filled
	static void prepareAttacked(BattleStackAttacked & bsa,
								vstd::RNG & rand,
								const std::shared_ptr<battle::CUnitState> & customState,
								bool destroyRemains = false,
								bool bypassTemporaryHitPoints = false,
								battle::DamageProvenance provenance = battle::DamageProvenance::OTHER); //requires bsa.damageAmount filled

	const CCreature * unitType() const override;
	int32_t unitBaseAmount() const override;
	int64_t getBattleStartMaximumAggregateHP() const override { return battleStartMaximumAggregateHP; }
	int64_t getRebirthOriginalAggregateHP() const override { return rebirthOriginalAggregateHP; }
	/// Capture once after battle-start bonus export. Zero-valued/ineligible stacks remain uncaptured.
	void captureBattleStartMaximumAggregateHP();
	void initializeRebirthOriginalAggregateHP(int64_t originalHP);

	uint32_t unitId() const override;
	BattleSide unitSide() const override;
	PlayerColor unitOwner() const override;
	SlotID unitSlot() const override;
	bool doubleWide() const override { return doubleWideCached;};

	std::string getDescription() const override;
	const BattleInfo * getBattle() const { return battle;}

	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
	int unitFortuneSpeed(const battle::Unit * unit) const override;
	int unitSpeedBonus(const battle::Unit * unit) const override;
	int unitAdditionalRetaliations(const battle::Unit * unit) const override;
	int unitBloodragePainIncrement(const battle::Unit * unit) const override;
	bool unitHasVeteranCohesion(const battle::Unit * unit) const override;
	std::optional<int> unitMagicResistance(const battle::Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit * unit) const override;

	void spendMana(ServerCallback * server, const int spellCost) const override;

	const IBonusBearer* getBonusBearer() const override;
	
	PlayerColor getOwner() const override
	{
		return this->owner;
	}

	template <typename Handler> void validateFrozenSerialization(Handler & h) const
	{
		if(frozenLastAppliedRound() < -1)
			throw std::runtime_error("Invalid Frozen application round");
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN) && frozenLastAppliedRound() != -1)
			throw std::runtime_error("Cannot discard Frozen application receipt in an older stack format");
		for(const auto & bonus : getExportedBonusList())
			if(bonus)
				bonus->validateFrozenSerialization(h);
	}

	template <typename Handler> void validateMoraleActivationSerialization(Handler & h) const
	{
		if(moraleExtraActivation && !hadMorale)
			throw std::runtime_error("Morale activation provenance requires its earned receipt");
		if(h.saving && moraleExtraActivation && !h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE))
			throw std::runtime_error("Cannot discard current Morale activation provenance");
	}

	template <typename Handler> void validateHeroicSpiritSerialization(Handler & h) const
	{
		if(heroicSpiritMoralePending && !heroicSpiritRetaliation)
			throw std::runtime_error("Invalid pending Heroic Spirit receipt");
		if(h.saving && heroicSpiritRetaliation && !h.hasFeature(Handler::Version::NEW_HORIZONS_HEROIC_SPIRIT))
			throw std::runtime_error("Cannot discard Heroic Spirit retaliation in an older stack");
	}

	template <typename Handler> void validateSwiftRebirthSerialization(Handler & h) const
	{
		for(const auto & bonus : getExportedBonusList())
			if(bonus)
				bonus->validateSwiftRebirthSerialization(h);
	}

	template <typename Handler> void validateVeteranCohesionSerialization(Handler & h) const
	{
		if(veteranCohesionEarned && !h.hasFeature(Handler::Version::NEW_HORIZONS_VETERAN_COHESION))
			throw std::runtime_error("Cannot discard Veteran Cohesion receipt in an older stack format");
	}

	template <typename Handler> void validatePlagueSerialization(Handler & h) const
	{
		for(const auto & bonus : getExportedBonusList())
			if(bonus) bonus->validatePlagueSerialization(h);
	}
	template <typename Handler> void validateTrainingSerialization(Handler & h) const
	{
		for(const auto & bonus : getExportedBonusList())
			bonus->validateTrainingSerialization(h);
		if(base)
			base->validateTrainingSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING), h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_COHORTS));
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING))
			for(const auto & bonus : getExportedBonusList())
				if(newHorizonsTraining::isTrainingBonus(bonus.get()))
					throw std::runtime_error("Cannot discard captured training combat bonus");
	}

	template <typename Handler> void validateOpportunistSerialization(Handler & h) const
	{
		if(pursuitMovementRemaining < 0)
			throw std::runtime_error("Invalid post-attack movement allowance");
		if((luckyOwnAttackSequence || pursuitMovementRemaining > 0)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_OPPORTUNIST))
			throw std::runtime_error("Cannot discard an earned own lucky attack sequence");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validateMoraleActivationSerialization(h);
		validateHeroicSpiritSerialization(h);
		if(h.saving)
			validateOpportunistSerialization(h);
		if(h.saving)
			validateTrainingSerialization(h);
		if(h.saving) validatePlagueSerialization(h);
		if(h.saving)
			validateRapidResponseSerialization(h);
		//this assumes that stack objects is newly created
		//CUnitState is not serialized here except for explicit battle-long fields.
		if(h.saving)
			validateVeteranCohesionSerialization(h);
		if(h.saving)
			validateFrozenSerialization(h);
		if(h.saving)
			validateSwiftRebirthSerialization(h);
		if(h.saving && (battlecraftOverwatchReadyRound < -1 || battlecraftOverwatchUsedRound < -1))
			throw std::runtime_error("Invalid Overwatch round marker");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_OVERWATCH)
			&& (battlecraftOverwatchReadyRound != -1 || battlecraftOverwatchUsedRound != -1))
			throw std::runtime_error("Cannot discard Overwatch state in an older format");
		if(h.saving)
			confusionState.validateSerialization(h);
		if(h.saving && getPhantomRoundsRemaining() > 3)
			throw std::runtime_error("Binary stack descriptors cannot preserve extended Phantom lifetime");
		if(h.saving && hasCasualtyProvenanceState())
			throw std::runtime_error("Cannot save magical casualty provenance without its health state");
		if(h.saving && !newHorizonsBloodrage::isValidPersonalIncrement(personalBloodrageIncrement))
			throw std::runtime_error("Invalid personal Bloodrage increment");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& personalBloodrageIncrement != 0)
			throw std::runtime_error("Cannot discard personal Bloodrage state in an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_ELEMENTAL_REBIRTH)
			&& battleStartMaximumAggregateHP != 0)
			throw std::runtime_error("Cannot discard Elemental Rebirth battle-start HP basis in an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP)
			&& rebirthOriginalAggregateHP != 0)
			throw std::runtime_error("Cannot discard Rebirth output original HP in an older format");
		assert(isIndependentNode());
		h & static_cast<CBonusSystemNode&>(*this);
		h & typeID;
		h & ID;
		h & baseAmount;
		h & owner;
		h & slot;
		h & side;
		h & initialPosition;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RAPID_RESPONSE))
		{
			h & waiting;
			h & waitedThisTurn;
			h & movedThisRound;
			h & defending;
			h & hadMorale;
			h & battlecraftWaitBonusUsed;
			h & battlecraftWaitMasteryDoubled;
			if(!h.saving)
				validateRapidResponseSerialization(h);
		}
		else if(!h.saving)
		{
			waiting = waitedThisTurn = movedThisRound = defending = hadMorale = false;
			battlecraftWaitBonusUsed = battlecraftWaitMasteryDoubled = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_OPPORTUNIST))
		{
			h & luckyOwnAttackSequence;
			h & pursuitMovementRemaining;
			if(pursuitMovementRemaining < 0)
				throw std::runtime_error("Invalid saved post-attack movement allowance");
		}
		else if(!h.saving)
		{
			luckyOwnAttackSequence = false;
			pursuitMovementRemaining = 0;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN))
		{
			h & personalBloodrageIncrement;
			if(!h.saving && !newHorizonsBloodrage::isValidPersonalIncrement(personalBloodrageIncrement))
				throw std::runtime_error("Invalid saved personal Bloodrage increment");
		}
		else if(!h.saving)
			personalBloodrageIncrement = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_ELEMENTAL_REBIRTH))
		{
			h & battleStartMaximumAggregateHP;
			if(!h.saving && battleStartMaximumAggregateHP < 0)
				throw std::runtime_error("Invalid saved Elemental Rebirth battle-start HP basis");
		}
		else if(!h.saving)
			battleStartMaximumAggregateHP = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP))
		{
			h & rebirthOriginalAggregateHP;
			if(!h.saving && rebirthOriginalAggregateHP < 0)
				throw std::runtime_error("Invalid saved Rebirth output original HP");
		}
		else if(!h.saving)
			rebirthOriginalAggregateHP = 0;
		h & confusionState;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_OVERWATCH))
		{
			h & battlecraftOverwatchReadyRound;
			h & battlecraftOverwatchUsedRound;
			if(battlecraftOverwatchReadyRound < -1 || battlecraftOverwatchUsedRound < -1)
				throw std::runtime_error("Invalid saved Overwatch round marker");
		}
		else if(!h.saving)
		{
			battlecraftOverwatchReadyRound = -1;
			battlecraftOverwatchUsedRound = -1;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN))
		{
			int32_t restoredRound = frozenLastAppliedRound();
			h & restoredRound;
			if(!h.saving)
				restoreFrozenApplicationRound(restoredRound);
		}
		else if(!h.saving)
			restoreFrozenApplicationRound(-1);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_VETERAN_COHESION))
			h & veteranCohesionEarned;
		else if(!h.saving)
			veteranCohesionEarned = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE))
			h & moraleExtraActivation;
		else if(!h.saving)
			moraleExtraActivation = false;
		if(!h.saving) validateMoraleActivationSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_HEROIC_SPIRIT))
		{
			h & heroicSpiritRetaliation;
			h & heroicSpiritMoralePending;
			if(heroicSpiritMoralePending && !heroicSpiritRetaliation)
				throw std::runtime_error("Invalid saved pending Heroic Spirit receipt");
		}
		else if(!h.saving)
		{
			heroicSpiritRetaliation = false;
			heroicSpiritMoralePending = false;
		}
	}

private:
	void onBattleFormChanged() override;

	const BattleInfo * battle = nullptr; //do not serialize
};

/// Returns effective creature-native bonuses evaluated in a detached, read-only stack context.
/// The source stack supplies rank and battle/army context when available; fallbackArmy supports
/// hypothetical units without a concrete source stack.
DLL_LINKAGE TConstBonusListPtr getBattleFormNativeBonuses(
	const battle::CUnitState & formState,
	const CStack * sourceStack,
	const CArmedInstance * fallbackArmy,
	const CSelector & selector,
	bool unstacked = false);

/// True for a creature-owned native bonus whose source identity is that creature.
DLL_LINKAGE bool isBattleFormNativeBonus(const Bonus * bonus, CreatureID creature);
