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
#include <limits>

#include "../GameConstants.h"
#include "BattleHex.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "SylvanLuckState.h"
#include "AlternatingHeroActionState.h"
#include "HeroActionAllowanceState.h"
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
	std::optional<HeroOrderState> orderState;
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

	bool hasChainGateState() const
	{
		return chainGateArmed || std::any_of(pendingDemonicGates.begin(), pendingDemonicGates.end(), [](const auto & gate)
		{
			return gate.chainGateAccelerated;
		});
	}

	void init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town);
	const CArmedInstance * getArmy() const;
	const CGHeroInstance * getHero() const;

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CHAIN_GATE) && hasChainGateState())
			throw std::runtime_error("Cannot discard Chain Gate battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_WARCASTING)
			&& warcastingState != AlternatingHeroActionState{})
			throw std::runtime_error("Cannot discard Warcasting battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_ALLOWANCES)
			&& heroActionAllowances != HeroActionAllowanceState{})
			throw std::runtime_error("Cannot discard Hero Action allowance battle state");
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
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS)
			&& (focusFire || activeOrder == HeroCommand::FOCUS_FIRE))
			throw std::runtime_error("Cannot discard New Horizons targeted command state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS) && orderState)
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
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
		{
			h & orderState;
		}
		else if(!h.saving)
		{
			orderState.reset();
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
