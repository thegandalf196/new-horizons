/*
 * PacksForClientBattle.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "NetPacksBase.h"
#include "BattleChanges.h"
#include "PacksForClient.h"
#include "../battle/BattleAction.h"
#include "../battle/AdverseCombatRerollState.h"
#include "../battle/MoraleSuppressionState.h"
#include "../battle/ReducedExtraActivationState.h"
#include "../battle/BattleInfo.h"
#include "../battle/BattleDeploymentState.h"
#include "../battle/BattleHexArray.h"
#include "../battle/BattleUnitTurnReason.h"
#include "../filesystem/ResourcePath.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"
#include "../entities/hero/NewHorizonsNecromancy.h"
#include "../texts/MetaString.h"

class CClient;

class CGHeroInstance;
class CArmedInstance;
class IBattleState;
class BattleInfo;

struct DLL_LINKAGE BattleStart : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	std::unique_ptr<BattleInfo> info;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && info && info->hasCasualtyProvenanceState())
			throw std::runtime_error("Binary BattleStart descriptors cannot preserve casualty health provenance");
		if(h.saving && info && !h.hasFeature(Handler::Version::BATTLE_DEPLOYMENT_PHASES)
			&& info->hasIndependentDeploymentState())
			throw std::runtime_error("Cannot discard independent deployment state from BattleStart");
		if(h.saving && info && !h.hasFeature(Handler::Version::BATTLE_INITIAL_ARMY_VALUE)
			&& info->hasInitialArmyValueState())
			throw std::runtime_error("Cannot discard initial Army Value snapshot from BattleStart");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& info->hasRageThroughPainState())
			throw std::runtime_error("Cannot discard Rage Through Pain battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_SYNTHESIS)
			&& (info->getWarcastingState(BattleSide::ATTACKER).hasConsumedBonus
				|| info->getWarcastingState(BattleSide::DEFENDER).hasConsumedBonus))
			throw std::runtime_error("Cannot discard Warcasting consumption history from BattleStart");
		if(h.saving && info && !h.hasFeature(Handler::Version::BATTLE_FINAL_RELOCATION)
			&& info->getDeploymentState().hasFinalRelocationState())
			throw std::runtime_error("Cannot discard final relocation state from BattleStart");
		if(h.saving && info && !h.hasFeature(Handler::Version::BATTLE_INITIAL_DEPLOYMENT_ORDER)
			&& info->getDeploymentState().hasNonDefaultInitialOrder())
			throw std::runtime_error("Cannot discard initial deployment order from BattleStart");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_RANGED_FOLLOW_UP)
			&& info->hasRangedFollowUpState())
			throw std::runtime_error("Cannot discard ranged follow-up battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE)
			&& info->hasChainGateState())
			throw std::runtime_error("Cannot discard Chain Gate battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_PURSUIT)
			&& info->hasPursuitState())
			throw std::runtime_error("Cannot discard Pursuit battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_CLEAVE)
			&& info->hasCleaveState())
			throw std::runtime_error("Cannot discard Cleave battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_RELENTLESS_ASSAULT)
			&& info->hasRelentlessAssaultState())
			throw std::runtime_error("Cannot discard Relentless Assault battle start state");
		if(h.saving && info && !h.hasFeature(Handler::Version::NEW_HORIZONS_NO_QUARTER)
			&& info->hasNoQuarterState())
			throw std::runtime_error("Cannot discard No Quarter battle start state");
		h & battleID;
		h & info;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleNextRound : public CPackForClient
{
	BattleID battleID = BattleID::NONE;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		assert(battleID != BattleID::NONE);
	}
};

/// Publishes the authoritative completion of one independent deployment phase.
struct DLL_LINKAGE BattleDeploymentPhaseChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleDeploymentState state;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::BATTLE_DEPLOYMENT_PHASES))
			throw std::runtime_error("Deployment phase updates require the current save/network format");
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_INITIAL_DEPLOYMENT_ORDER)
			&& state.hasNonDefaultInitialOrder())
			throw std::runtime_error("Cannot discard initial deployment phase ordering");
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_FINAL_RELOCATION)
			&& state.hasFinalRelocationState())
			throw std::runtime_error("Cannot discard final relocation phase state");
		if(h.saving && (battleID == BattleID::NONE || !state.independent))
			throw std::runtime_error("Invalid independent deployment phase update");
		if(h.saving)
			state.validateShape();
		h & battleID;
		h & state;
		if(battleID == BattleID::NONE || !state.independent)
			throw std::runtime_error("Invalid independent deployment phase update");
		state.validateShape();
	}
};

/// Replicates the complete authoritative Demonic Reserve battle snapshot after
/// server-side changes that are not fully represented by StartAction.
struct DLL_LINKAGE BattleDemonicGatingStateChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	std::map<CreatureID, TQuantity> reserve;
	std::vector<SideInBattle::PendingDemonicGate> pending;
	std::vector<SideInBattle::GatedDemonicStack> gated;
	bool chainGateArmed = false;
	bool masterGateUsed = false;
	std::optional<uint32_t> masterGateContinuationUnitId;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE)
			&& (chainGateArmed || std::any_of(pending.begin(), pending.end(), [](const auto & gate)
			{
				return gate.chainGateAccelerated;
			})))
			throw std::runtime_error("Cannot discard Chain Gate battle state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_GATE)
			&& (masterGateUsed || masterGateContinuationUnitId))
			throw std::runtime_error("Cannot discard Master Gate battle state update");
		h & battleID;
		h & side;
		h & reserve;
		h & pending;
		h & gated;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
			h & chainGateArmed;
		else if(!h.saving)
			chainGateArmed = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_GATE))
		{
			h & masterGateUsed;
			h & masterGateContinuationUnitId;
		}
		else if(!h.saving)
		{
			masterGateUsed = false;
			masterGateContinuationUnitId.reset();
		}
	}
};

/// Replicates one authorized consumption of a side's adverse combat reroll.
/// The perk is enabled in the battle snapshot; this pack can only mark its
/// single available allowance as used.
struct DLL_LINKAGE BattleAdverseRerollStateChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	AdverseCombatRerollState state;

	void visitTyped(ICPackVisitor & visitor) override;

	void validateShape() const
	{
		if(battleID == BattleID::NONE || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			|| !state.enabled || !state.used)
			throw std::runtime_error("Invalid adverse combat reroll state update");
	}

	void validateTransitionFrom(const AdverseCombatRerollState & previous) const
	{
		validateShape();
		// validateShape guarantees an enabled, spent snapshot. The only valid
		// prior state is enabled: either the first consumption advances unused to
		// used, or a replay repeats the already-used snapshot idempotently.
		if(!previous.enabled)
			throw std::runtime_error("Adverse combat reroll update enables or resets its allowance");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_ADVERSE_COMBAT_REROLL))
			throw std::runtime_error(h.saving
				? "Cannot serialize adverse combat reroll update to an older format"
				: "Cannot deserialize adverse combat reroll update from an older format");
		h & battleID;
		h & side;
		h & state;
		validateShape();
	}
};

/// Replicates one authorized cancellation of a side's first negative Morale trigger.
/// The perk is enabled in the battle snapshot; this pack can only mark its allowance as used.
struct DLL_LINKAGE BattleMoraleSuppressionStateChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	MoraleSuppressionState state;

	void visitTyped(ICPackVisitor & visitor) override;

	void validateShape() const
	{
		if(battleID == BattleID::NONE || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			|| (!state.used && !state.roundUsed))
			throw std::runtime_error("Invalid Morale suppression state update");
		state.validate();
	}

	void validateTransitionFrom(const MoraleSuppressionState & previous) const
	{
		validateShape();
		previous.validate();
		// Replayed spent snapshots are harmless; any new update must equal the
		// exact result of one negative trigger, which spends round Unbreakable
		// before battle-long Rally and cannot enable or reset either allowance.
		if(state == previous)
			return;

		auto expected = previous;
		expected.consume(true);
		if(state != expected)
			throw std::runtime_error("Morale suppression update is not a legal single-trigger transition");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RALLY))
			throw std::runtime_error(h.saving
				? "Cannot serialize Rally Morale suppression update to an older format"
				: "Cannot deserialize Rally Morale suppression update from an older format");
		h & battleID;
		h & side;
		h & state;
		validateShape();
	}
};

/// Replicates Quartermaster's once-per-combat expenditure and active reduced-output identity.
struct DLL_LINKAGE BattleReducedExtraActivationStateChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	ReducedExtraActivationState state;

	void visitTyped(ICPackVisitor & visitor) override;

	void validateShape() const
	{
		if(battleID == BattleID::NONE || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
			throw std::runtime_error("Invalid reduced extra activation state update");
		state.validateShape();
	}

	void validateTransitionFrom(const ReducedExtraActivationState & previous) const
	{
		validateShape();
		state.validateTransitionFrom(previous);
	}

	template <typename Handler>
	void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_REDUCED_EXTRA_ACTIVATION))
			throw std::runtime_error(h.saving
				? "Cannot serialize reduced extra activation update to an older format"
				: "Cannot deserialize reduced extra activation update from an older format");
		h & battleID;
		h & side;
		h & state;
		validateShape();
	}
};

struct DLL_LINKAGE BattleSetActiveStack : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	uint32_t stack = 0;
	BattleUnitTurnReason reason;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_PURSUIT))
			throw std::runtime_error("Can not serialize a Pursuit continuation to an older format");
		if(h.saving && reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_RANGED_FOLLOW_UP))
			throw std::runtime_error("Can not serialize a ranged attack continuation to an older format");
		if(reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_REDUCED_EXTRA_ACTIVATION))
			throw std::runtime_error("Can not serialize a reduced extra activation to an older format");
		h & battleID;
		h & stack;
		h & reason;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleCancelled: public CPackForClient
{
	BattleID battleID = BattleID::NONE;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleResultAccepted : public CPackForClient
{
	struct HeroBattleResults
	{
		ObjectInstanceID heroID;
		ObjectInstanceID armyID;
		TExpType exp = 0;

		template <typename Handler> void serialize(Handler & h)
		{
			h & heroID;
			h & armyID;
			h & exp;
		}
	};

	BattleID battleID = BattleID::NONE;
	BattleSideArray<HeroBattleResults> heroResult;
	BattleSide winnerSide;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & heroResult;
		h & winnerSide;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleResult : public Query
{
	BattleID battleID = BattleID::NONE;
	EBattleResult result = EBattleResult::NORMAL;
	BattleSide winner = BattleSide::NONE; //0 - attacker, 1 - defender, 2 - draw
	PlayerColor attacker; //used in case of a draw
	BattleSideArray<std::map<CreatureID, si32>> casualties; //first => casualties of attackers - map crid => number
	BattleSideArray<TExpType> exp{0,0}; //exp for attacker and defender
	BattleSideArray<std::map<CreatureID, si32>> necromancyEligibleCasualties;
	bool necromancyEligibilityCaptured = false;
	BattleSideArray<std::map<CreatureID, si32>> necromancyNonlivingEligibleCasualties;
	BattleSideArray<std::map<CreatureID, si32>> necromancyUndeadEligibleCasualties;
	bool necromancySpecialEligibilityCaptured = false;

	bool hasSpecialNecromancyCasualties() const
	{
		return necromancySpecialEligibilityCaptured
			|| !necromancyNonlivingEligibleCasualties[BattleSide::ATTACKER].empty()
			|| !necromancyNonlivingEligibleCasualties[BattleSide::DEFENDER].empty()
			|| !necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].empty()
			|| !necromancyUndeadEligibleCasualties[BattleSide::DEFENDER].empty();
	}

	bool isSpecialNecromancyCaptureValid() const
	{
		if(!necromancySpecialEligibilityCaptured && hasSpecialNecromancyCasualties())
			return false;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			for(const auto & entry : necromancyNonlivingEligibleCasualties[side])
			{
				if(!entry.first.hasValue() || entry.second < 0)
					return false;
			}
			for(const auto & entry : necromancyUndeadEligibleCasualties[side])
			{
				if(!entry.first.hasValue() || entry.second < 0)
					return false;
			}
		}
		return true;
	}

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !isSpecialNecromancyCaptureValid())
			throw std::runtime_error("Invalid Necromancy special casualty capture");
		if(h.saving && hasSpecialNecromancyCasualties()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES))
			throw std::runtime_error("Cannot write Necromancy special casualty capture to an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY)
			&& (necromancyEligibilityCaptured || !necromancyEligibleCasualties[BattleSide::ATTACKER].empty()
				|| !necromancyEligibleCasualties[BattleSide::DEFENDER].empty()))
			throw std::runtime_error("Cannot write New Horizons Necromancy result to an older format");
		h & battleID;
		h & queryID;
		h & result;
		h & winner;
		h & casualties;
		h & exp;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY))
		{
			h & necromancyEligibleCasualties;
			h & necromancyEligibilityCaptured;
		}
		else if(!h.saving)
		{
			necromancyEligibleCasualties = {};
			necromancyEligibilityCaptured = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES))
		{
			h & necromancyNonlivingEligibleCasualties;
			h & necromancyUndeadEligibleCasualties;
			h & necromancySpecialEligibilityCaptured;
		}
		else if(!h.saving)
		{
			necromancyNonlivingEligibleCasualties = {};
			necromancyUndeadEligibleCasualties = {};
			necromancySpecialEligibilityCaptured = false;
		}
		if(!isSpecialNecromancyCaptureValid())
			throw std::runtime_error("Invalid Necromancy special casualty capture");
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleLogMessage : public CPackForClient, public scripting::ApiSharedPointer<BattleLogMessage>
{
	BattleID battleID = BattleID::NONE;
	std::vector<MetaString> lines;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & lines;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleStackMoved : public CPackForClient, public scripting::ApiSharedPointer<BattleStackMoved>
{
	BattleID battleID = BattleID::NONE;
	ui32 stack = 0;
	BattleHexArray tilesToMove;
	int distance = 0;
	bool teleporting = false;
	
	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & stack;
		h & tilesToMove;
		h & distance;
		h & teleporting;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleUnitsChanged : public CPackForClient, public scripting::ApiSharedPointer<BattleUnitsChanged>
{
	BattleID battleID = BattleID::NONE;
	std::vector<UnitChanges> changedStacks;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& std::ranges::any_of(changedStacks, [](const UnitChanges & change)
				{ return change.hasCasualtyProvenanceState(); }))
			throw std::runtime_error("Cannot discard casualty provenance unit state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RANGED_FOLLOW_UP)
			&& std::ranges::any_of(changedStacks, [](const UnitChanges & change)
			{
				const auto & percent = change.data["state"]["rangedFollowUpDamagePercent"];
				return percent.isNumber() && percent.Float() != 0;
			}))
			throw std::runtime_error("Cannot discard ranged follow-up unit state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_NO_QUARTER)
			&& std::ranges::any_of(changedStacks, [](const UnitChanges & change)
				{ return change.hasNoQuarterMoraleState(); }))
			throw std::runtime_error("Cannot discard No Quarter unit state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& std::ranges::any_of(changedStacks, [](const UnitChanges & change)
				{ return change.hasRageThroughPainState(); }))
			throw std::runtime_error("Cannot discard personal Bloodrage unit state update");
		h & battleID;
		h & changedStacks;
		assert(battleID != BattleID::NONE);
	}
};

struct BattleStackAttacked
{
	ui32 stackAttacked = 0, attackerID = 0;
	ui32 killedAmount = 0;
	int64_t damageAmount = 0;
	UnitChanges newState;
	enum EFlags
	{
		KILLED = 1,
		SECONDARY = 2,
		REBIRTH = 4,
		CLONE_KILLED = 8,
		SPELL_EFFECT = 16,
		GUARDIAN_SPIRIT_EXHAUSTED = 32,
	};
	ui32 flags = 0; //uses EFlags (above)
	SpellID spellID = SpellID::NONE; //only if flag SPELL_EFFECT is set

	bool killed() const//if target stack was killed
	{
		return flags & KILLED || flags & CLONE_KILLED;
	}
	bool cloneKilled() const
	{
		return flags & CLONE_KILLED;
	}
	bool isSecondary() const//if stack was not a primary target (receives no spell effects)
	{
		return flags & SECONDARY;
	}
	///Attacked with spell (SPELL_LIKE_ATTACK)
	bool isSpell() const
	{
		return flags & SPELL_EFFECT;
	}
	bool willRebirth() const//resurrection, e.g. Phoenix
	{
		return flags & REBIRTH;
	}

	template <typename Handler> void serialize(Handler & h)
	{
		const auto & followUpPercent = newState.data["state"]["rangedFollowUpDamagePercent"];
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& newState.hasCasualtyProvenanceState())
			throw std::runtime_error("Cannot discard casualty provenance attack state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RANGED_FOLLOW_UP)
			&& followUpPercent.isNumber() && followUpPercent.Float() != 0)
			throw std::runtime_error("Cannot discard ranged follow-up attack state update");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& newState.hasRageThroughPainState())
			throw std::runtime_error("Cannot discard personal Bloodrage attack state update");
		h & stackAttacked;
		h & attackerID;
		h & newState;
		h & flags;
		h & killedAmount;
		h & damageAmount;
		h & spellID;
	}
	bool operator<(const BattleStackAttacked & b) const
	{
		return stackAttacked < b.stackAttacked;
	}
};

struct DLL_LINKAGE BattleAttack : public CPackForClient
{
	BattleUnitsChanged attackerChanges;
	/// Server-authored post-roll snapshot, shared by every target of the strike.
	BattleSide fortuneSide = BattleSide::NONE;
	std::optional<SylvanLuckState> fortuneState;

	BattleID battleID = BattleID::NONE;
	std::vector<BattleStackAttacked> bsa;
	ui32 stackAttacking = 0;
	ui32 flags = 0; //uses Eflags (below)
	enum EFlags { SHOT = 1, COUNTER = 2, LUCKY = 4, UNLUCKY = 8, BALLISTA_DOUBLE_DMG = 16, DEATH_BLOW = 32, SPELL_LIKE = 64, CUSTOM_ANIMATION = 256};

	BattleHex tile;
	SpellID spellID = SpellID::NONE; //for SPELL_LIKE
	/// Server-authored trigger marker.  The receiver validates the gated-stack
	/// identity and qualifying lethal hit before arming its local token.
	bool chainGateTriggered = false;
	/// Snapshot carried when an eligible primary attack advances the hero-side
	/// Relentless Assault chain. The ordinary activation boundary is replicated
	/// by BattleSetActiveStack and deterministically advances both sides.
	BattleSide relentlessAssaultSide = BattleSide::NONE;
	std::optional<RelentlessAssaultState> relentlessAssaultState;

	bool shot() const//distance attack - decrease number of shots
	{
		return flags & SHOT;
	}
	bool counter() const//is it counterattack?
	{
		return flags & COUNTER;
	}
	bool lucky() const
	{
		return flags & LUCKY;
	}
	bool unlucky() const
	{
		return flags & UNLUCKY;
	}
	bool ballistaDoubleDmg() const //if it's ballista attack and does double dmg
	{
		return flags & BALLISTA_DOUBLE_DMG;
	}
	bool deathBlow() const
	{
		return flags & DEATH_BLOW;
	}
	bool spellLike() const
	{
		return flags & SPELL_LIKE;
	}
	bool playCustomAnimation() const
	{
		return flags & CUSTOM_ANIMATION;
	}

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		const auto hasPersonalBloodrage = [](const UnitChanges & change)
		{
			return change.hasRageThroughPainState();
		};
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& (std::ranges::any_of(attackerChanges.changedStacks, [](const UnitChanges & change)
					{ return change.hasCasualtyProvenanceState(); })
				|| std::ranges::any_of(bsa, [](const BattleStackAttacked & hit)
					{ return hit.newState.hasCasualtyProvenanceState(); })))
			throw std::runtime_error("Cannot discard casualty provenance attack state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& (std::ranges::any_of(attackerChanges.changedStacks, hasPersonalBloodrage)
				|| std::ranges::any_of(bsa, [](const BattleStackAttacked & hit)
					{ return hit.newState.hasRageThroughPainState(); })))
			throw std::runtime_error("Cannot discard personal Bloodrage attack state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_NO_QUARTER)
			&& (std::ranges::any_of(attackerChanges.changedStacks, [](const UnitChanges & change)
					{ return change.hasNoQuarterMoraleState(); })
				|| std::ranges::any_of(bsa, [](const BattleStackAttacked & hit)
					{ return hit.newState.hasNoQuarterMoraleState(); })))
			throw std::runtime_error("Cannot discard No Quarter attack state");
		if(h.saving && chainGateTriggered && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
			throw std::runtime_error("Cannot discard Chain Gate attack state");
		h & battleID;
		h & bsa;
		h & stackAttacking;
		h & flags;
		h & tile;
		h & spellID;
		h & attackerChanges;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_LUCK))
		{
			h & fortuneSide;
			h & fortuneState;
			if(fortuneState && fortuneSide != BattleSide::ATTACKER && fortuneSide != BattleSide::DEFENDER)
				throw std::runtime_error("Invalid fortune packet side");
		}
		else if(h.saving && fortuneState)
			throw std::runtime_error("Cannot discard Sylvan Luck strike state");
		else if(!h.saving)
		{
			fortuneSide = BattleSide::NONE;
			fortuneState.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
			h & chainGateTriggered;
		else if(!h.saving)
			chainGateTriggered = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RELENTLESS_ASSAULT))
		{
			if(h.saving && (relentlessAssaultState.has_value()
				!= (relentlessAssaultSide == BattleSide::ATTACKER || relentlessAssaultSide == BattleSide::DEFENDER)))
				throw std::runtime_error("Invalid Relentless Assault attack snapshot");
			h & relentlessAssaultSide;
			h & relentlessAssaultState;
			if(!h.saving && (relentlessAssaultState.has_value()
				!= (relentlessAssaultSide == BattleSide::ATTACKER || relentlessAssaultSide == BattleSide::DEFENDER)))
				throw std::runtime_error("Invalid Relentless Assault attack snapshot");
		}
		else if(h.saving && (relentlessAssaultState || relentlessAssaultSide != BattleSide::NONE))
			throw std::runtime_error("Cannot discard Relentless Assault attack state");
		else if(!h.saving)
		{
			relentlessAssaultSide = BattleSide::NONE;
			relentlessAssaultState.reset();
		}
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE StartAction : public CPackForClient
{
	StartAction() = default;
	explicit StartAction(BattleAction act)
		: ba(std::move(act))
	{
	}

	BattleID battleID = BattleID::NONE;
	BattleAction ba;
	std::optional<FocusFireState> focusFire;
	std::optional<HeroOrderState> orderState;
	/// When true, the accepted canonical Order is upserted without replacing
	/// other same-side Orders. Legacy writes cannot represent that intent.
	bool preserveOtherOrders = false;
	/// Server-derived Double Command transition for this accepted Order, when any.
	std::optional<DoubleCommandState> doubleCommandState;
	/// Server-derived Battle Plan completion for its accepted opening Order.
	std::optional<PreCombatOrderState> preCombatOrderState;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && preserveOtherOrders
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
			throw std::runtime_error("Cannot discard multi-Order StartAction upsert intent in an older format");
		if(h.saving && doubleCommandState
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			throw std::runtime_error("Cannot discard Double Command StartAction state");
		if(h.saving && preCombatOrderState
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			throw std::runtime_error("Cannot discard Battle Plan StartAction state");
		h & battleID;
		h & ba;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
		{
			h & focusFire;
		}
		else if(h.saving && focusFire)
		{
			throw std::runtime_error("Cannot discard targeted StartAction state");
		}
		else if(!h.saving)
		{
			focusFire.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
		{
			h & orderState;
		}
		else if(h.saving && orderState)
		{
			throw std::runtime_error("Cannot discard canonical Hero Order StartAction state");
		}
		else if(!h.saving)
		{
			orderState.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
		{
			h & preserveOtherOrders;
		}
		else if(!h.saving)
		{
			preserveOtherOrders = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			h & doubleCommandState;
		else if(!h.saving)
			doubleCommandState.reset();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			h & preCombatOrderState;
		else if(!h.saving)
			preCombatOrderState.reset();
		assert(battleID != BattleID::NONE);
	}
};

/// Authoritative replacement for a side's transient canonical Order state.
/// Trigger consumption is server-owned; this packet lets every battle snapshot
/// converge after Charge, Protect, Flank, and Second Wind transitions without
/// smuggling state changes through client presentation effects.
struct DLL_LINKAGE BattleHeroOrderStateChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	/// Legacy projection of the most recently issued Order.
	std::optional<HeroOrderState> state;
	/// Authoritative full same-side Order collection in the multiple-Order format.
	std::optional<std::vector<HeroOrderState>> states;
	/// Authoritative Double Command phase transition, if this update changes it.
	std::optional<DoubleCommandState> doubleCommandState;
	/// Authoritative Battle Plan opening phase transition, if this update changes it.
	std::optional<PreCombatOrderState> preCombatOrderState;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && doubleCommandState
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			throw std::runtime_error("Cannot discard Double Command state update");
		if(h.saving && preCombatOrderState
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			throw std::runtime_error("Cannot discard Battle Plan state update");
		if(h.saving && preCombatOrderState)
			preCombatOrderState->validateShape();
		if(h.saving)
		{
			if(states)
			{
				std::set<HeroCommand> commands;
				for(const auto & order : *states)
				{
					order.validateShape();
					if(!heroCommands::isActive(order.command) || !commands.insert(order.command).second)
						throw std::runtime_error("Invalid or duplicate Order in battle state update");
				}
				const auto projection = states->empty()
					? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(states->back());
				if(state != projection)
					throw std::runtime_error("Legacy Hero Order state is not the latest collection projection");
				if(states->size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
					throw std::runtime_error("Cannot discard multiple active Hero Orders in an older format");
			}
			else if(h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
			{
				throw std::runtime_error("Missing full Hero Order collection in a multi-Order state update");
			}
		}
		h & battleID;
		h & side;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
		{
			h & state;
		}
		else if(h.saving && state)
		{
			throw std::runtime_error("Cannot discard canonical Hero Order state update");
		}
		else if(!h.saving)
		{
			state.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
		{
			h & states;
			if(!h.saving)
			{
				if(!states)
					throw std::runtime_error("Missing full Hero Order collection in a multi-Order state update");
				std::set<HeroCommand> commands;
				for(const auto & order : *states)
				{
					order.validateShape();
					if(!heroCommands::isActive(order.command) || !commands.insert(order.command).second)
						throw std::runtime_error("Invalid or duplicate Order in battle state update");
				}
				const auto projection = states->empty()
					? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(states->back());
				if(state != projection)
					throw std::runtime_error("Legacy Hero Order state is not the latest collection projection");
			}
		}
		else if(!h.saving)
		{
			states = state ? std::vector<HeroOrderState>{*state} : std::vector<HeroOrderState>{};
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			h & doubleCommandState;
		else if(!h.saving)
			doubleCommandState.reset();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			h & preCombatOrderState;
		else if(!h.saving)
			preCombatOrderState.reset();
		assert(battleID != BattleID::NONE);
		assert(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER);
	}
};

struct DLL_LINKAGE EndAction : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	BattleID battleID = BattleID::NONE;
	bool endsFortuneActivation = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_FORTUNE_EFFECTS))
			h & endsFortuneActivation;
		else if(!h.saving)
			endsFortuneActivation = false;
	}
};

struct DLL_LINKAGE BattleSpellCast : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	bool activeCast = true;
	BattleSide side = BattleSide::NONE; //which hero did cast spell
	SpellID spellID; //id of spell
	ui8 manaGained = 0; //mana channeling ability
	BattleHex tile; //destination tile (may not be set in some global/mass spells
	std::vector<ui32> affectedCres; //ids of creatures affected by this spell, in effect order (e.g. chain-lightning hop order); generally used if spell does not set any effect (like dispel or cure)
	std::set<ui32> resistedCres; // creatures that resisted the spell (e.g. Dwarves)
	std::set<ui32> reflectedCres; // creatures that reflected the spell (e.g. Magic Mirror spell)
	si32 casterStack = -1; // -1 if not cated by creature, >=0 caster stack ID
	bool castByHero = true; //if true - spell has been cast by hero, otherwise by a creature
	bool temporalFieldCast = false; // consumes the saved once-per-combat Sorcery Mass Slow budget
	BattleSide counterspellSide = BattleSide::NONE; // ward side consumed or collapsed while this hero spell was attempted
	bool counterspellNegated = false; // the ward had enough mana and suppressed this spell's effects
	bool metamagicFollowup = false; // this spell spends a Metamagic Spell Action without recursively granting another
	bool metamagicGrand = false; // authoritative automatic Grand activation on this accepted follow-up
	uint32_t metamagicTargetUnitId = std::numeric_limits<uint32_t>::max(); // primary target used by sequence perks
	int32_t metamagicManaRefund = 0; // Formula Reserve refund published with the final additional cast
	int32_t paidHeroManaCost = 0; // gross cost paid by the hero for this accepted cast
	int32_t paidCounterspellManaCost = 0; // ward cost paid by counterspellSide for this accepted cast

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && temporalFieldCast && !h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
			throw std::runtime_error("Cannot serialize Temporal Field cast to an older protocol");
		if(h.saving && (counterspellSide != BattleSide::NONE || counterspellNegated)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
			throw std::runtime_error("Cannot serialize Counterspell cast result to an older protocol");
		if(h.saving && (metamagicFollowup || metamagicGrand || metamagicTargetUnitId != std::numeric_limits<uint32_t>::max()
			|| metamagicManaRefund != 0) && !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot serialize Metamagic cast metadata to an older protocol");
		if(h.saving && (paidHeroManaCost != 0 || paidCounterspellManaCost != 0)
			&& !h.hasFeature(Handler::Version::BATTLE_HERO_MANA_EXPENDITURE))
			throw std::runtime_error("Cannot serialize hero Mana expenditure to an older protocol");
		h & battleID;
		h & side;
		h & spellID;
		h & manaGained;
		h & tile;
		h & affectedCres;
		h & resistedCres;
		h & reflectedCres;
		h & casterStack;
		h & castByHero;
		h & activeCast;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
		{
			h & temporalFieldCast;
		}
		else if(!h.saving)
		{
			temporalFieldCast = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
		{
			h & counterspellSide;
			h & counterspellNegated;
		}
		else if(!h.saving)
		{
			counterspellSide = BattleSide::NONE;
			counterspellNegated = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
		{
			h & metamagicFollowup;
			h & metamagicGrand;
			h & metamagicTargetUnitId;
			h & metamagicManaRefund;
		}
		else if(!h.saving)
		{
			metamagicFollowup = false;
			metamagicGrand = false;
			metamagicTargetUnitId = std::numeric_limits<uint32_t>::max();
			metamagicManaRefund = 0;
		}
		if(h.hasFeature(Handler::Version::BATTLE_HERO_MANA_EXPENDITURE))
		{
			h & paidHeroManaCost;
			h & paidCounterspellManaCost;
			if(!h.saving && (paidHeroManaCost < 0 || paidCounterspellManaCost < 0
				|| (paidHeroManaCost > 0 && (!castByHero || !activeCast
					|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)))
				|| (paidCounterspellManaCost > 0
					&& (!castByHero || !activeCast || !counterspellNegated
						|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
						|| (counterspellSide != BattleSide::ATTACKER && counterspellSide != BattleSide::DEFENDER)
						|| counterspellSide == side))))
				throw std::runtime_error("Invalid accepted hero Mana expenditure metadata");
		}
		else if(!h.saving)
		{
			paidHeroManaCost = 0;
			paidCounterspellManaCost = 0;
		}
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE StacksInjured : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	std::vector<BattleStackAttacked> stacks;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& std::ranges::any_of(stacks, [](const BattleStackAttacked & hit)
				{ return hit.newState.hasCasualtyProvenanceState(); }))
			throw std::runtime_error("Cannot discard casualty provenance injury state in an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& std::ranges::any_of(stacks, [](const BattleStackAttacked & hit)
				{ return hit.newState.hasRageThroughPainState(); }))
			throw std::runtime_error("Cannot discard personal Bloodrage injury state in an older format");
		h & battleID;
		h & stacks;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleResultsApplied : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	PlayerColor victor;
	PlayerColor loser;
	ChangeSpells learnedSpells;
	std::vector<BulkMoveArtifacts> movingArtifacts;
	std::vector<GrowUpArtifact> growingArtifacts;
	std::vector<DischargeArtifact> dischargingArtifacts;
	CStackBasicDescriptor raisedStack;
	newHorizonsNecromancy::NecromancyResult necromancy;
	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !necromancy.isSkeletonOutputValid())
			throw std::runtime_error("Invalid Necromancy Skeleton output form");
		if(h.saving && !necromancy.isSpecialCasualtySummaryValid())
			throw std::runtime_error("Invalid negative Necromancy special casualty summary");
		if(h.saving && necromancy.hasSpecialCasualtySummary()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES))
			throw std::runtime_error("Cannot write Necromancy special casualties to an older format");
		if(h.saving && necromancy.skeletonCreature != CreatureID::NONE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SKELETON_FORM))
			throw std::runtime_error("Cannot write Necromancy Skeleton form to an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY)
			&& !necromancy.empty())
			throw std::runtime_error("Cannot write New Horizons Necromancy summary to an older format");
		if(h.saving && necromancy.wightsRaised != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_WIGHTS))
			throw std::runtime_error("Cannot write Necromancy Wights to an older format");
		if(h.saving && necromancy.wightsRaised < 0)
			throw std::runtime_error("Invalid negative Necromancy Wight count");
		h & battleID;
		h & victor;
		h & loser;
		h & learnedSpells;
		h & movingArtifacts;
		h & growingArtifacts;
		h & dischargingArtifacts;
		h & raisedStack;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY))
			h & necromancy;
		else if(!h.saving)
			necromancy = {};
		else if(!necromancy.empty())
			throw std::runtime_error("Cannot write New Horizons Necromancy summary to an older format");
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleEnded : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	PlayerColor victor;
	PlayerColor loser;
	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & victor;
		h & loser;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleObstaclesChanged : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	ObstacleChanges change;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & change;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE CatapultAttack : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	EWallPart attackedPart = EWallPart::INVALID;
	si16 destinationTile = 0;
	// Legacy hit-quality/animation value (0 = miss, 1 = normal, 2 = critical).
	ui8 damageDealt = 0;
	// Canonical New Horizons ruleset-v3 absolute structural damage. Zero means
	// legacy behavior and preserves the old packet semantics.
	ui16 structuralDamage = 0;
	int32_t killedTowerShooter = -1; //unit ID of tower shooter killed by this attack, or -1 if none
	int attacker = -1; //if -1, then a spell caused this

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & attackedPart;
		h & destinationTile;
		h & damageDealt;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CATAPULT_STRUCTURAL_DAMAGE))
			h & structuralDamage;
		else if(!h.saving)
			structuralDamage = 0;
		h & killedTowerShooter;
		h & attacker;
		assert(battleID != BattleID::NONE);
	}
};

struct DLL_LINKAGE BattleSetStackProperty : public CPackForClient
{
	enum BattleStackProperty { CASTS, ENCHANTER_COUNTER, UNBIND, CLONED, HAS_CLONE };

	BattleID battleID = BattleID::NONE;
	int stackID = 0;
	BattleStackProperty which = CASTS;
	int val = 0;
	int absolute = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & stackID;
		h & which;
		h & val;
		h & absolute;
		assert(battleID != BattleID::NONE);
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};

///activated at the beginning of turn
struct DLL_LINKAGE BattleTriggerEffect : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	int stackID = 0;
	BonusType effect = BonusType::NONE;
	int val = 0;
	int additionalInfo = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & stackID;
		h & effect;
		h & val;
		h & additionalInfo;
		assert(battleID != BattleID::NONE);
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};

/// Plays a one-shot animation with an optional sound on the battlefield. Presentation only -
/// changes no game state, so anything that needs a visual for a change it made itself can send it.
struct DLL_LINKAGE BattleAnimationPlayed : public CPackForClient
{
	/// Where one copy of the animation is played. Mirrors battle::Destination - a unit is carried
	/// by id so that playback follows it if it moved since the pack was sent.
	struct DLL_LINKAGE Target
	{
		int32_t unitID = -1;
		BattleHex tile;

		template <typename Handler> void serialize(Handler & h)
		{
			h & unitID;
			h & tile;
		}
	};

	BattleID battleID = BattleID::NONE;
	AnimationPath animation;
	AudioPath sound;
	std::vector<Target> targets;
	float transparency = 1.0f;

	/// Play together with the animations of the next pack instead of on its own, e.g. so that fire
	/// shield flames and the flinch of the burned attacker start on the same frame. Nothing is
	/// played at all unless such a pack follows.
	bool deferred = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & animation;
		h & sound;
		h & targets;
		h & transparency;
		h & deferred;
		assert(battleID != BattleID::NONE);
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};

struct DLL_LINKAGE BattleUpdateGateState : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	EGateState state = EGateState::NONE;
	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & state;
		assert(battleID != BattleID::NONE);
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};
