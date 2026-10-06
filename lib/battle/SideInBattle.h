/*
 * SideInBattle.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../GameConstants.h"
#include "BattleHex.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "SylvanLuckState.h"
#include "AdverseCombatRerollState.h"
#include "MoraleSuppressionState.h"
#include "ReducedExtraActivationState.h"
#include "AlternatingHeroActionState.h"
#include "HeroActionAllowanceState.h"
#include "RelentlessAssaultState.h"
#include "../callback/GameCallbackHolder.h"

class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;

struct DLL_LINKAGE SideInBattle : public GameCallbackHolder
{
	struct PendingDemonicGate
	{
		CreatureID creature;
		TQuantity count = 0;
		BattleHex position;
		int32_t arrivalRound = 0;
		uint32_t sourceUnitId = std::numeric_limits<uint32_t>::max();
		// A Chain Gate token may advance exactly this pending Gate to the end of
		// the current round.  Keep the fact explicit instead of inferring it from
		// arrivalRound so later round processing cannot accidentally accelerate a
		// different Gate.
		bool chainGateAccelerated = false;

		auto operator<=>(const PendingDemonicGate &) const = default;

		template <typename Handler> void serialize(Handler & h)
		{
			if(h.saving && chainGateAccelerated && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
				throw std::runtime_error("Cannot discard accelerated Chain Gate state");
			h & creature;
			h & count;
			h & position;
			h & arrivalRound;
			h & sourceUnitId;
			if(h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
				h & chainGateAccelerated;
			else if(!h.saving)
				chainGateAccelerated = false;
		}
	};
	struct GatedDemonicStack
	{
		uint32_t unitId = std::numeric_limits<uint32_t>::max();
		CreatureID creature;
		TQuantity initialCount = 0;

		TQuantity endlessLegionRestoration(TQuantity survivors) const
		{
			return std::max<TQuantity>(0, initialCount - std::max<TQuantity>(0, survivors)) / 2;
		}

		auto operator<=>(const GatedDemonicStack &) const = default;

		template <typename Handler> void serialize(Handler & h)
		{
			h & unitId;
			h & creature;
			h & initialCount;
		}
	};

	using GameCallbackHolder::GameCallbackHolder;

	PlayerColor color = PlayerColor::CANNOT_DETERMINE;
	ObjectInstanceID heroID; //may be empty if army is not commanded by hero
	ObjectInstanceID armyObjectID; //adv. map object with army that participates in battle; may be same as hero

	bool heroCommandUsed = false;
	// Decode-only compatibility slot. New Horizons no longer emits or exposes
	// doctrines; this member remains so old HERO_COMMANDS records retain their
	// wire position while loading.
	HeroCommand activeDoctrine = HeroCommand::NONE;
	HeroCommand activeOrder = HeroCommand::NONE;
	/// Independent round-long snapshots, in issue order. `activeOrder` remains
	/// the latest-command compatibility projection for older callers and saves.
	std::vector<HeroOrderState> orderStates;
	std::optional<FocusFireState> focusFire;
	uint32_t castSpellsCount = 0; //how many spells each side has been cast this turn
	bool temporalFieldUsed = false; // saved once-per-combat Sorcery Mass Slow budget
	bool counterspellArmed = false; // saved Sorcery Counterspell ward, until the next hero action or enemy hero spell
	// Tower Metamagic's combat-use budget and spell-sequence provenance remain
	// separate from the round-long action ledger below. Pending sequence metadata
	// describes an available Spell Action; it does not lock creature actions or
	// expire until the round boundary.
	uint8_t metamagicUsesConsumed = 0;
	uint8_t metamagicPendingCount = 0;
	bool metamagicGrandUsed = false;
	// Historical compatibility bit; sequence rewards no longer use it as a gate.
	bool metamagicFormulaReserveUsed = false;
	bool metamagicCountersequenceArmed = false;
	SpellID metamagicFirstSpell;
	uint32_t metamagicFirstTargetUnitId = std::numeric_limits<uint32_t>::max();
	std::vector<SpellID> metamagicSequenceSpells;
	bool metamagicFirstCounterspellNegated = false;
	std::vector<SpellID> usedSpellsHistory; //every time hero casts spell, it's inserted here -> eagle eye skill
	int32_t enchanterCounter = 0; //tends to pass through 0, so sign is needed
	int32_t initialMana = 0;
	int32_t additionalMana = 0;
	// Keeping new runtime fields at the end avoids shifting preceding offsets.
	// All consumers still require a synchronized rebuild when this struct changes.
	// BattleInfo owns the versioned wire representation.
	int32_t initialNormalSpellPoints = 0;
	int32_t initialBufferSpellPoints = 0;
	int32_t temporaryBufferRemaining = 0;
	bool metamagicSpellBufferUsed = false;
	int32_t bloodrageDamagePercent = 0;
	int32_t bloodrageRank = 0;
	SylvanLuckState sylvanLuck;
	std::map<CreatureID, TQuantity> demonicReserve;
	std::vector<PendingDemonicGate> pendingDemonicGates;
	std::vector<GatedDemonicStack> gatedDemonicStacks;
	// Chain Gate is a single battle-long token.  Keeping it beside the
	// authoritative gated-stack identities makes the token survive a save/load
	// without allowing ordinary stacks to qualify by creature or slot alone.
	bool chainGateArmed = false;
	// Only accepted ordinary hero actions update this alternating readiness.
	AlternatingHeroActionState warcastingState;
	// Authoritative round-long Hero/typed action budget. Creature activations
	// remain outside this ledger; legacy counters above are retained for history
	// and migration only.
	HeroActionAllowanceState heroActionAllowances;
	// Master Gate grants one free Gate opening per side and combat.
	bool masterGateUsed = false;
	// Expert Offense's target streak is shared across all ordinary allied
	// creature activations for this hero side.
	RelentlessAssaultState relentlessAssault;
	// Accepted hero-cast completion persists for the whole battle, not one round.
	bool heroSpellCastCompleted = false;
	// Bit (level - 1) records that an accepted hero cast of that saved spell
	// level completed during this battle. Only the five ordinary spell levels
	// are tracked; creature spells do not enter this history.
	uint8_t completedHeroSpellLevels = 0;
	// Gross Mana paid for accepted hero spells and accepted Counterspell wards.
	// Separate Mana refunds and drains do not change this battle-long ledger.
	int64_t acceptedHeroManaSpent = 0;
	// Expert Luck's once-per-combat adverse stochastic reroll belongs to the
	// harmed side and is independent of attack-strike fortune snapshots.
	AdverseCombatRerollState adverseCombatReroll;
	// Discipline's Rally cancels the first negative Morale trigger for this side.
	MoraleSuppressionState moraleSuppression;
	// Quartermaster's once-per-combat expenditure and reduced activation identity.
	ReducedExtraActivationState reducedExtraActivation;
	// Bloodrage's cap is resolved from the hero's saved skill/perks at battle setup.
	int32_t bloodrageCapPercent = 0;
	// Threshold perks are saved snapshots so player-scoped callbacks and detached AI
	// can evaluate both sides without reading hidden enemy hero inventories.
	int32_t bloodrageSpeedBonus = 0;
	int32_t bloodrageAdditionalRetaliations = 0;
	// Blood Scent's target-sensitive increment is resolved from the saved skill rank/perk.
	int32_t bloodrageLowHealthIncrement = 0;
	// Rage Through Pain's personal increment profile is resolved at battle setup.
	int32_t bloodragePainIncrement = 0;
	// Double Command is a contextual immediate continuation and one combat use.
	DoubleCommandState doubleCommandState;
	// Battle Plan is a once-per-combat round-one Order choice, resolved before
	// the first ordinary Creature Activation.
	PreCombatOrderState preCombatOrderState;
	// Raw Army Value and opponent kind captured before battle mutations begin.
	std::optional<uint64_t> initialArmyValue;
	bool initialArmyIsWandering = false;
	// Movement fatigue captured at battle setup; applies as an additional Morale
	// modifier only for the first combat round.
	int32_t firstRoundMoraleModifier = 0;

	static constexpr uint8_t COMPLETED_HERO_SPELL_LEVELS_MASK =
		static_cast<uint8_t>((1u << GameConstants::SPELL_LEVELS) - 1u);
	static constexpr std::size_t MAX_ORDER_STATES = 8;

	HeroOrderState * findOrder(HeroCommand command)
	{
		const auto it = std::find_if(orderStates.begin(), orderStates.end(), [command](const HeroOrderState & order)
		{
			return order.command == command;
		});
		return it == orderStates.end() ? nullptr : &*it;
	}

	const HeroOrderState * findOrder(HeroCommand command) const
	{
		const auto it = std::find_if(orderStates.begin(), orderStates.end(), [command](const HeroOrderState & order)
		{
			return order.command == command;
		});
		return it == orderStates.end() ? nullptr : &*it;
	}

	std::optional<HeroOrderState> lastOrder() const
	{
		if(orderStates.empty())
			return std::nullopt;
		return orderStates.back();
	}

	static void validateOrderCollection(const std::vector<HeroOrderState> & orders)
	{
		if(orders.size() > MAX_ORDER_STATES)
			throw std::runtime_error("Too many simultaneous New Horizons Hero Orders");
		for(std::size_t index = 0; index < orders.size(); ++index)
		{
			const auto & order = orders[index];
			order.validateShape();
			if(!heroCommands::isActive(order.command))
				throw std::runtime_error("Unsupported command in New Horizons Hero Order collection");
			for(std::size_t previous = 0; previous < index; ++previous)
				if(orders[previous].command == order.command)
					throw std::runtime_error("Duplicate command in New Horizons Hero Order collection");
		}
	}

	void validateOrderStates() const
	{
		validateOrderCollection(orderStates);
	}

	/// Replace the same command's snapshot in place so state-only mutations do
	/// not change issue order. New commands append as the newest issued Order.
	void upsertOrder(const HeroOrderState & order)
	{
		order.validateShape();
		if(!heroCommands::isActive(order.command))
			throw std::runtime_error("Unsupported command in New Horizons Hero Order collection");
		auto updated = orderStates;
		const auto existing = std::find_if(updated.begin(), updated.end(), [&order](const HeroOrderState & current)
		{
			return current.command == order.command;
		});
		if(existing != updated.end())
			*existing = order;
		else
		{
			if(updated.size() >= MAX_ORDER_STATES)
				throw std::runtime_error("Too many simultaneous New Horizons Hero Orders");
			updated.push_back(order);
		}
		validateOrderCollection(updated);
		orderStates = std::move(updated);
	}

	void replaceOrders(const std::vector<HeroOrderState> & orders)
	{
		validateOrderCollection(orders);
		orderStates = orders;
	}

	void validateDoubleCommandState() const
	{
		doubleCommandState.validateShape();
		heroActionAllowances.validateShape();
		const auto doubleGrants = static_cast<uint32_t>(std::count_if(
			heroActionAllowances.grants.begin(), heroActionAllowances.grants.end(), [](const auto & grant)
			{
				return grant.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND;
			}));
		if(doubleCommandState.orderPending())
		{
			if(!doubleCommandState.used || doubleCommandState.issuedRound != heroActionAllowances.currentRound
				|| doubleGrants != 1 || heroActionAllowances.countDoubleCommandOrderGrants(doubleCommandState.issuedRound) != 1)
				throw std::runtime_error("Double Command continuation and allowance grant are inconsistent");
		}
		else if(doubleGrants != 0)
			throw std::runtime_error("Orphaned Double Command allowance grant");
		if(doubleCommandState.secondWindReady()
			&& doubleCommandState.issuedRound != heroActionAllowances.currentRound)
			throw std::runtime_error("Stale Double Command Second Wind continuation");
	}

	void validatePreCombatOrderState() const
	{
		preCombatOrderState.validateShape();
		heroActionAllowances.validateShape();
		if(preCombatOrderState.phase == PreCombatOrderState::Phase::ORDER_REQUIRED)
		{
			if(preCombatOrderState.issuedRound != 1 || heroActionAllowances.currentRound != 1
				|| heroActionAllowances.countBattlePlanOrderGrants(preCombatOrderState.issuedRound) != 1)
				throw std::runtime_error("Battle Plan continuation and allowance grant are inconsistent");
		}
		else if(heroActionAllowances.countBattlePlanOrderGrants(1) != 0)
			throw std::runtime_error("Orphaned Battle Plan allowance grant");
		if(preCombatOrderState.phase == PreCombatOrderState::Phase::AVAILABLE
			&& heroActionAllowances.currentRound > 1)
			throw std::runtime_error("Unresolved Battle Plan state is stale");
	}

	static uint8_t completedHeroSpellLevelBit(int32_t level)
	{
		if(level < 1 || level > GameConstants::SPELL_LEVELS)
			return 0;
		return static_cast<uint8_t>(1u << (level - 1));
	}

	bool hasCompletedHeroSpellLevel(int32_t level) const
	{
		const auto bit = completedHeroSpellLevelBit(level);
		return bit != 0 && (completedHeroSpellLevels & bit) != 0;
	}

	void recordCompletedHeroSpellLevel(int32_t level)
	{
		completedHeroSpellLevels |= completedHeroSpellLevelBit(level);
	}

	bool hasChainGateState() const
	{
		return chainGateArmed || std::any_of(pendingDemonicGates.begin(), pendingDemonicGates.end(), [](const auto & gate)
		{
			return gate.chainGateAccelerated;
		});
	}

	bool hasSacredCommandOrderState() const
	{
		return std::any_of(orderStates.begin(), orderStates.end(), [](const HeroOrderState & order)
		{
			return order.sacredCommandEfficiencyBonusPercent != 0;
		});
	}
	bool hasKnightlySequenceOrderState() const
	{
		return std::any_of(orderStates.begin(), orderStates.end(), [](const HeroOrderState & order)
		{
			return order.knightlySequenceEfficiencyBonusPercent != 0;
		});
	}
	bool hasMandateOfHeavenState() const
	{
		return heroActionAllowances.divineMandateCompletedPairs
			> HeroActionAllowanceState::MAX_DIVINE_MANDATE_COMPLETED_PAIRS_WITHOUT_MANDATE_OF_HEAVEN;
	}

	void init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town);
	const CArmedInstance * getArmy() const;
	const CGHeroInstance * getHero() const;

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving && hasSacredCommandOrderState()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SACRED_COMMAND))
			throw std::runtime_error("Cannot discard Sacred Command battle Order state");
		if(h.saving && hasKnightlySequenceOrderState()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_KNIGHTLY_SEQUENCE))
			throw std::runtime_error("Cannot discard Knightly Sequence battle Order state");
		if(h.saving && hasMandateOfHeavenState()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_MANDATE_OF_HEAVEN))
			throw std::runtime_error("Cannot discard the Mandate of Heaven pair count");
		if(h.saving && firstRoundMoraleModifier != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_FORCED_MARCH))
			throw std::runtime_error("Cannot discard first-round battle Morale modifier");
		if(h.saving && initialArmyIsWandering && !initialArmyValue)
			throw std::runtime_error("Wandering battle army has no initial Army Value snapshot");
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_INITIAL_ARMY_VALUE)
			&& (initialArmyValue.has_value() || initialArmyIsWandering))
			throw std::runtime_error("Cannot discard initial battle Army Value snapshot");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND)
			&& (doubleCommandState != DoubleCommandState{}
				|| std::any_of(heroActionAllowances.grants.begin(), heroActionAllowances.grants.end(), [](const auto & grant)
				{
					return grant.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND;
				})))
			throw std::runtime_error("Cannot discard Double Command battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN)
			&& (preCombatOrderState != PreCombatOrderState{}
				|| std::any_of(heroActionAllowances.grants.begin(), heroActionAllowances.grants.end(), [](const auto & grant)
				{
					return grant.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN;
				})))
			throw std::runtime_error("Cannot discard Battle Plan battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS)
			&& orderStates.size() > 1)
			throw std::runtime_error("Cannot discard simultaneous Hero Orders in an older format");
		if(h.saving)
		{
			validateOrderStates();
			validateDoubleCommandState();
			validatePreCombatOrderState();
		}
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE) && hasChainGateState())
			throw std::runtime_error("Cannot discard Chain Gate battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_WARCASTING)
			&& warcastingState != AlternatingHeroActionState{})
			throw std::runtime_error("Cannot discard Warcasting battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_SYNTHESIS)
			&& warcastingState.hasConsumedBonus)
			throw std::runtime_error("Cannot discard Warcasting consumption history");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_SEQUENCE)
			&& warcastingState.hasRecentActionHistory())
			throw std::runtime_error("Cannot discard Hero Action sequence history");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_ALLOWANCES)
			&& heroActionAllowances != HeroActionAllowanceState{})
			throw std::runtime_error("Cannot discard Hero Action allowance battle state");
		if(h.saving && masterGateUsed && !h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_GATE))
			throw std::runtime_error("Cannot discard Master Gate battle state");
		if(h.saving && relentlessAssault.hasState()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_RELENTLESS_ASSAULT))
			throw std::runtime_error("Cannot discard Relentless Assault battle state");
		if(h.saving && heroSpellCastCompleted
			&& !h.hasFeature(Handler::Version::BATTLE_COMPLETED_HERO_SPELL))
			throw std::runtime_error("Cannot discard completed hero spell battle state");
		if(h.saving && completedHeroSpellLevels != 0
			&& !h.hasFeature(Handler::Version::BATTLE_COMPLETED_HERO_SPELL_LEVELS))
			throw std::runtime_error("Cannot discard completed hero spell level battle state");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_LUCK))
			h & sylvanLuck;
		else if(!h.saving)
			sylvanLuck = {};
		else if(sylvanLuck != SylvanLuckState{})
			throw std::runtime_error("Cannot discard Sylvan Luck battle state");
		if(h.saving && temporalFieldUsed && !h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
			throw std::runtime_error("Cannot discard consumed Temporal Field battle state");
		if(h.saving && counterspellArmed && !h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
			throw std::runtime_error("Cannot discard armed Counterspell battle state");
		if(h.saving && (metamagicUsesConsumed != 0 || metamagicPendingCount != 0 || metamagicGrandUsed
			|| metamagicFormulaReserveUsed || metamagicCountersequenceArmed || metamagicFirstSpell.hasValue()
			|| metamagicFirstTargetUnitId != std::numeric_limits<uint32_t>::max()
			|| !metamagicSequenceSpells.empty() || metamagicFirstCounterspellNegated)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot discard Metamagic battle state");
		if(h.saving && metamagicSpellBufferUsed
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC_REWARDS))
			throw std::runtime_error("Cannot discard consumed Metamagic Spell Buffer state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS)
			&& (focusFire || activeOrder == HeroCommand::FOCUS_FIRE))
			throw std::runtime_error("Cannot discard New Horizons targeted command state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS) && !orderStates.empty())
			throw std::runtime_error("Cannot discard New Horizons canonical Order state");
		h & color;
		h & heroID;
		h & armyObjectID;
		h & castSpellsCount;
		h & usedSpellsHistory;
		h & enchanterCounter;
		h & initialMana;
		h & additionalMana;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
		{
			h & temporalFieldUsed;
		}
		else if(!h.saving)
		{
			temporalFieldUsed = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
		{
			h & counterspellArmed;
		}
		else if(!h.saving)
		{
			counterspellArmed = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
		{
			h & metamagicUsesConsumed;
			h & metamagicPendingCount;
			h & metamagicGrandUsed;
			h & metamagicFormulaReserveUsed;
			h & metamagicCountersequenceArmed;
			h & metamagicFirstSpell;
			h & metamagicFirstTargetUnitId;
			h & metamagicSequenceSpells;
			h & metamagicFirstCounterspellNegated;
			if(!h.saving && (metamagicUsesConsumed > 3 || metamagicPendingCount > 2
				|| metamagicSequenceSpells.size() > 3
				|| (metamagicPendingCount != 0 && (!metamagicFirstSpell.hasValue() || metamagicSequenceSpells.empty()))
				|| (metamagicPendingCount == 0 && (!metamagicSequenceSpells.empty() || metamagicFirstSpell.hasValue()))))
				throw std::runtime_error("Invalid saved Metamagic battle state");
		}
		else if(!h.saving)
		{
			metamagicUsesConsumed = 0;
			metamagicPendingCount = 0;
			metamagicGrandUsed = false;
			metamagicFormulaReserveUsed = false;
			metamagicCountersequenceArmed = false;
			metamagicFirstSpell = SpellID();
			metamagicFirstTargetUnitId = std::numeric_limits<uint32_t>::max();
			metamagicSequenceSpells.clear();
			metamagicFirstCounterspellNegated = false;
		}
		if(h.hasFeature(Handler::Version::HERO_COMMANDS))
		{
			h & heroCommandUsed;
			HeroCommand legacyDoctrine = HeroCommand::NONE;
			h & legacyDoctrine;
			h & activeOrder;
			if(!h.saving)
			{
				// A legacy doctrine may have consumed the old round action, but it
				// must not become current gameplay after a load. Keep that budget
				// bit while dropping only the obsolete Doctrine identity. Preserve
				// the raw Order temporarily so BattleInfo can remove any matching
				// legacy round bonuses before clearing a decode-only Order ID.
				activeDoctrine = HeroCommand::NONE;
			}
		}
		else if(!h.saving)
		{
			heroCommandUsed = false;
			activeDoctrine = HeroCommand::NONE;
			activeOrder = HeroCommand::NONE;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
		{
			h & focusFire;
		}
		else if(!h.saving)
		{
			focusFire.reset();
		}
		std::optional<HeroOrderState> legacyOrderProjection;
		if(h.saving)
			legacyOrderProjection = lastOrder();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
		{
			// Preserve this historical optional slot unchanged. Multiple-order
			// saves append the authoritative collection at the end of this record.
			h & legacyOrderProjection;
			if(!h.saving)
			{
				orderStates.clear();
				if(legacyOrderProjection)
					orderStates.push_back(*legacyOrderProjection);
				validateOrderStates();
			}
		}
		else if(!h.saving)
		{
			orderStates.clear();
			legacyOrderProjection.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DEMONIC_RESERVE))
		{
			h & demonicReserve;
			h & pendingDemonicGates;
			h & gatedDemonicStacks;
		}
		else if(!h.saving)
		{
			demonicReserve.clear();
			pendingDemonicGates.clear();
			gatedDemonicStacks.clear();
		}
		else if(!demonicReserve.empty() || !pendingDemonicGates.empty() || !gatedDemonicStacks.empty())
			throw std::runtime_error("Cannot discard Demonic Gating battle state");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE))
			h & chainGateArmed;
		else if(!h.saving)
			chainGateArmed = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_WARCASTING))
			h & warcastingState;
		else if(!h.saving)
			warcastingState = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_ALLOWANCES))
			h & heroActionAllowances;
		else if(!h.saving)
			heroActionAllowances = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RELENTLESS_ASSAULT))
			h & relentlessAssault;
		else if(!h.saving)
			relentlessAssault = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SPELL_POINTS))
		{
			h & initialNormalSpellPoints;
			h & initialBufferSpellPoints;
			h & temporaryBufferRemaining;
			if(!h.saving && (initialNormalSpellPoints < 0 || initialBufferSpellPoints < 0 || temporaryBufferRemaining < 0))
				throw std::runtime_error("Invalid saved battle Spell Point snapshot");
		}
		else if(h.saving && (initialBufferSpellPoints != 0 || temporaryBufferRemaining != 0))
			throw std::runtime_error("Cannot discard battle Buffer Spell Point state");
		else if(!h.saving)
		{
			initialNormalSpellPoints = std::max<int32_t>(0, initialMana);
			initialBufferSpellPoints = 0;
			temporaryBufferRemaining = 0;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC_REWARDS))
			h & metamagicSpellBufferUsed;
		else if(!h.saving)
			metamagicSpellBufferUsed = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_GATE))
			h & masterGateUsed;
		else if(!h.saving)
			masterGateUsed = false;
		if(h.hasFeature(Handler::Version::BATTLE_COMPLETED_HERO_SPELL))
			h & heroSpellCastCompleted;
		else if(!h.saving)
			heroSpellCastCompleted = false;
		if(h.hasFeature(Handler::Version::BATTLE_COMPLETED_HERO_SPELL_LEVELS))
		{
			h & completedHeroSpellLevels;
			if(!h.saving && (completedHeroSpellLevels & static_cast<uint8_t>(~COMPLETED_HERO_SPELL_LEVELS_MASK)) != 0)
				throw std::runtime_error("Invalid saved completed hero spell levels");
		}
		else if(!h.saving)
			completedHeroSpellLevels = 0;
		if(h.hasFeature(Handler::Version::BATTLE_HERO_MANA_EXPENDITURE))
		{
			h & acceptedHeroManaSpent;
			if(!h.saving && acceptedHeroManaSpent < 0)
				throw std::runtime_error("Invalid saved hero Mana expenditure");
		}
		else if(h.saving && acceptedHeroManaSpent != 0)
			throw std::runtime_error("Cannot discard battle hero Mana expenditure");
		else if(!h.saving)
			acceptedHeroManaSpent = 0;
		h & adverseCombatReroll;
		h & moraleSuppression;
		h & reducedExtraActivation;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MULTIPLE_ORDERS))
		{
			std::vector<HeroOrderState> serializedOrders;
			if(h.saving)
				serializedOrders = orderStates;
			h & serializedOrders;
			if(!h.saving)
			{
				validateOrderCollection(serializedOrders);
				const std::optional<HeroOrderState> collectionProjection = serializedOrders.empty()
					? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(serializedOrders.back());
				if(collectionProjection != legacyOrderProjection)
					throw std::runtime_error("Inconsistent latest Hero Order projection");
				orderStates = std::move(serializedOrders);
			}
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			h & doubleCommandState;
		else if(!h.saving)
			doubleCommandState = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			h & preCombatOrderState;
		else if(!h.saving)
			preCombatOrderState = {};
		if(h.hasFeature(Handler::Version::BATTLE_INITIAL_ARMY_VALUE))
		{
			// BinaryDeserializer supports 32-bit integral values, so preserve the
			// complete runtime uint64_t domain as two explicit wire words.
			std::optional<std::array<uint32_t, 2>> wireInitialArmyValue;
			if(h.saving && initialArmyValue)
				wireInitialArmyValue = std::array<uint32_t, 2>{
					static_cast<uint32_t>(*initialArmyValue),
					static_cast<uint32_t>(*initialArmyValue >> 32)};
			h & wireInitialArmyValue;
			h & initialArmyIsWandering;
			if(!h.saving)
			{
				if(wireInitialArmyValue)
					initialArmyValue = static_cast<uint64_t>((*wireInitialArmyValue)[0])
						| (static_cast<uint64_t>((*wireInitialArmyValue)[1]) << 32);
				else
					initialArmyValue.reset();
				if(initialArmyIsWandering && !initialArmyValue)
					throw std::runtime_error("Wandering battle army has no initial Army Value snapshot");
			}
		}
		else if(!h.saving)
		{
			initialArmyValue.reset();
			initialArmyIsWandering = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FORCED_MARCH))
			h & firstRoundMoraleModifier;
		else if(!h.saving)
			firstRoundMoraleModifier = 0;
		if(!h.saving)
		{
			validateDoubleCommandState();
			validatePreCombatOrderState();
		}
	}

	void clearMetamagicSequence()
	{
		metamagicPendingCount = 0;
		metamagicFirstSpell = SpellID();
		metamagicFirstTargetUnitId = std::numeric_limits<uint32_t>::max();
		metamagicSequenceSpells.clear();
		metamagicFirstCounterspellNegated = false;
		std::erase_if(heroActionAllowances.grants, [](const auto & grant)
		{
			return grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC
				|| grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND;
		});
	}
};
