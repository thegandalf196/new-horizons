/*
 * HeroActionAllowanceState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "HeroCommand.h"

/// Value-only ledger for the flexible Hero Action and typed Spell/Order actions.
/// Creature activations are deliberately outside this ledger.
/// Action-kind eligibility is represented here; payload-specific restrictions
/// (such as spell school or a distinct Order ID) remain a future extension seam.
struct DLL_LINKAGE HeroActionAllowanceState
{
	enum class ActionKind : uint8_t
	{
		SPELL,
		ORDER
	};

	enum class AllowanceKind : uint8_t
	{
		HERO,
		SPELL,
		ORDER
	};

	enum class GrantSource : uint8_t
	{
		ROUND,
		METAMAGIC,
		METAMAGIC_GRAND,
		PERK,
		ARTIFACT,
		OTHER,
		DOUBLE_COMMAND,
		BATTLE_PLAN
	};

	struct DLL_LINKAGE Grant
	{
		uint32_t id = 0;
		AllowanceKind allowance = AllowanceKind::HERO;
		GrantSource source = GrantSource::OTHER;
		int32_t grantedRound = -1;
		int32_t expiryRound = -1;

		bool operator==(const Grant &) const = default;

		template <typename Handler> void serialize(Handler & h)
		{
			if(h.saving && source == GrantSource::DOUBLE_COMMAND
				&& !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
				throw std::runtime_error("Cannot discard Double Command allowance grant");
			if(h.saving && source == GrantSource::BATTLE_PLAN
				&& !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
				throw std::runtime_error("Cannot discard Battle Plan allowance grant");
			if(h.saving)
				validateShape();
			h & id;
			h & allowance;
			h & source;
			h & grantedRound;
			h & expiryRound;
			if(!h.saving)
				validateShape();
		}

		void validateShape() const
		{
			if(id == 0 || !isValid(allowance) || !isValid(source)
				|| grantedRound < 0 || expiryRound < grantedRound
				|| (source == GrantSource::ROUND
					&& (allowance != AllowanceKind::HERO || expiryRound != grantedRound))
				|| ((source == GrantSource::METAMAGIC || source == GrantSource::METAMAGIC_GRAND)
					&& allowance != AllowanceKind::SPELL)
				|| (source == GrantSource::DOUBLE_COMMAND
					&& (allowance != AllowanceKind::ORDER || expiryRound != grantedRound))
				|| (source == GrantSource::BATTLE_PLAN
					&& (allowance != AllowanceKind::ORDER || grantedRound != 1 || expiryRound != 1)))
				throw std::runtime_error("Invalid Hero Action allowance grant shape");
		}
	};

	struct DLL_LINKAGE Selection
	{
		uint32_t grantId = 0;
		AllowanceKind allowance = AllowanceKind::HERO;
		GrantSource source = GrantSource::OTHER;
		int32_t expiryRound = -1;

		bool operator==(const Selection &) const = default;
	};

	struct DLL_LINKAGE Receipt
	{
		uint32_t grantId = 0;
		ActionKind action = ActionKind::SPELL;
		AllowanceKind allowance = AllowanceKind::HERO;
		GrantSource source = GrantSource::OTHER;
		int32_t round = -1;

		bool operator==(const Receipt &) const = default;
	};

	struct DLL_LINKAGE Counts
	{
		uint32_t heroActions = 0;
		uint32_t orderActions = 0;
		uint32_t spellActions = 0;

		bool operator==(const Counts &) const = default;
	};

	int32_t currentRound = -1;
	uint32_t nextGrantId = 1;
	std::vector<Grant> grants;

	bool operator==(const HeroActionAllowanceState &) const = default;

	/// Starts a round once, expiring old grants and issuing exactly one flexible
	/// Hero allowance. Repeating the same round is intentionally idempotent.
	void resetForRound(int32_t round)
	{
		validateShape();
		if(round < 0 || round < currentRound)
			throw std::invalid_argument("Invalid Hero Action allowance round");
		if(round == currentRound)
			return;

		auto next = *this;
		std::erase_if(next.grants, [round](const Grant & grant)
		{
			return grant.expiryRound < round;
		});
		next.currentRound = round;
		next.appendGrant(AllowanceKind::HERO, GrantSource::ROUND, round);
		next.validateShape();
		*this = std::move(next);
	}

	/// Adds a source-specific allowance. The base ROUND grant is created only by
	/// resetForRound so repeated setup cannot manufacture extra Hero Actions.
	uint32_t grantAllowance(AllowanceKind allowance, GrantSource source, int32_t expiryRound)
	{
		validateShape();
		if(currentRound < 0 || !isValid(allowance) || !isValid(source)
			|| source == GrantSource::ROUND || expiryRound < currentRound
			|| ((source == GrantSource::METAMAGIC || source == GrantSource::METAMAGIC_GRAND)
				&& allowance != AllowanceKind::SPELL))
			throw std::invalid_argument("Invalid Hero Action allowance grant");

		auto next = *this;
		const auto id = next.appendGrant(allowance, source, expiryRound);
		next.validateShape();
		*this = std::move(next);
		return id;
	}

	/// Selects without spending. Typed allowances are preferred to flexible Hero
	/// allowances; within a class the earliest expiry and then lowest stable ID win.
	std::optional<Selection> eligibleAllowance(ActionKind action, int32_t round) const
	{
		validateShape();
		validateQuery(action, round);
		if(currentRound < 0 || round < currentRound)
			throw std::invalid_argument("Hero Action allowance query is outside the active ledger rounds");

		const Grant * selected = nullptr;
		for(const auto & grant : grants)
		{
			if(grant.expiryRound < round || !canPay(grant.allowance, action))
				continue;
			if(!selected || selectionKey(grant, action) < selectionKey(*selected, action))
				selected = &grant;
		}
		if(!selected)
			return {};
		return Selection{selected->id, selected->allowance, selected->source, selected->expiryRound};
	}

	/// Commits exactly the currently preferred queried allowance. A stale ID,
	/// wrong action kind, or wrong round returns no receipt and changes no state.
	std::optional<Receipt> consumeAllowance(uint32_t grantId, ActionKind action, int32_t round)
	{
		validateShape();
		validateQuery(action, round);
		if(round != currentRound)
			return {};

		const auto selected = eligibleAllowance(action, round);
		if(!selected || selected->grantId != grantId)
			return {};

		const auto it = std::find_if(grants.begin(), grants.end(), [grantId](const Grant & grant)
		{
			return grant.id == grantId;
		});
		if(it == grants.end())
			return {};

		const Receipt receipt{it->id, action, it->allowance, it->source, round};
		grants.erase(it);
		return receipt;
	}

	/// Counts unexpired grants as three non-overlapping pools. A flexible Hero
	/// allowance appears only in heroActions, never again as Spell/Order capacity.
	Counts remainingCounts(int32_t round) const
	{
		validateShape();
		if(currentRound < 0 || round < currentRound || round < 0)
			throw std::invalid_argument("Invalid Hero Action allowance count round");

		Counts result;
		for(const auto & grant : grants)
		{
			if(grant.expiryRound < round)
				continue;
			switch(grant.allowance)
			{
			case AllowanceKind::HERO:
				++result.heroActions;
				break;
			case AllowanceKind::ORDER:
				++result.orderActions;
				break;
			case AllowanceKind::SPELL:
				++result.spellActions;
				break;
			default:
				throw std::runtime_error("Invalid Hero Action allowance kind");
			}
		}
		return result;
	}

	void validateShape() const
	{
		if(currentRound < -1 || nextGrantId == 0 || (currentRound == -1 && !grants.empty()))
			throw std::runtime_error("Invalid Hero Action allowance ledger shape");

		uint32_t previousId = 0;
		uint32_t currentRoundBaseGrants = 0;
		for(const auto & grant : grants)
		{
			grant.validateShape();
			if(grant.id <= previousId || grant.id >= nextGrantId
				|| grant.grantedRound > currentRound || grant.expiryRound < currentRound)
				throw std::runtime_error("Invalid Hero Action allowance grant ordering or lifetime");
			previousId = grant.id;
			if(grant.source == GrantSource::ROUND && grant.grantedRound == currentRound)
				++currentRoundBaseGrants;
		}
		if(currentRoundBaseGrants > 1)
			throw std::runtime_error("Duplicate base Hero Action allowance");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND)
			&& std::any_of(grants.begin(), grants.end(), [](const Grant & grant)
			{
				return grant.source == GrantSource::DOUBLE_COMMAND;
			}))
			throw std::runtime_error("Cannot discard Double Command allowance grant");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN)
			&& std::any_of(grants.begin(), grants.end(), [](const Grant & grant)
			{
				return grant.source == GrantSource::BATTLE_PLAN;
			}))
			throw std::runtime_error("Cannot discard Battle Plan allowance grant");
		if(h.saving)
			validateShape();
		h & currentRound;
		h & nextGrantId;
		h & grants;
		if(!h.saving)
			validateShape();
	}

	uint32_t countDoubleCommandOrderGrants(int32_t round) const
	{
		validateShape();
		if(round < 0 || currentRound != round)
			return 0;
		return static_cast<uint32_t>(std::count_if(grants.begin(), grants.end(), [round](const Grant & grant)
		{
			return grant.source == GrantSource::DOUBLE_COMMAND
				&& grant.allowance == AllowanceKind::ORDER
				&& grant.grantedRound == round && grant.expiryRound == round;
		}));
	}

	uint32_t countBattlePlanOrderGrants(int32_t round) const
	{
		validateShape();
		if(round < 0 || currentRound != round)
			return 0;
		return static_cast<uint32_t>(std::count_if(grants.begin(), grants.end(), [round](const Grant & grant)
		{
			return grant.source == GrantSource::BATTLE_PLAN
				&& grant.allowance == AllowanceKind::ORDER
				&& grant.grantedRound == round && grant.expiryRound == round;
		}));
	}

private:
	static bool isValid(ActionKind value)
	{
		return value == ActionKind::SPELL || value == ActionKind::ORDER;
	}

	static bool isValid(AllowanceKind value)
	{
		return value == AllowanceKind::HERO || value == AllowanceKind::SPELL || value == AllowanceKind::ORDER;
	}

	static bool isValid(GrantSource value)
	{
		return value == GrantSource::ROUND || value == GrantSource::METAMAGIC
			|| value == GrantSource::METAMAGIC_GRAND || value == GrantSource::PERK
			|| value == GrantSource::ARTIFACT || value == GrantSource::OTHER
			|| value == GrantSource::DOUBLE_COMMAND || value == GrantSource::BATTLE_PLAN;
	}

	static bool canPay(AllowanceKind allowance, ActionKind action)
	{
		return allowance == AllowanceKind::HERO
			|| (allowance == AllowanceKind::SPELL && action == ActionKind::SPELL)
			|| (allowance == AllowanceKind::ORDER && action == ActionKind::ORDER);
	}

	static std::tuple<uint8_t, int32_t, uint32_t> selectionKey(const Grant & grant, ActionKind action)
	{
		const bool immediateOrder = action == ActionKind::ORDER
			&& (grant.source == GrantSource::DOUBLE_COMMAND || grant.source == GrantSource::BATTLE_PLAN);
		const bool specialized = grant.allowance != AllowanceKind::HERO;
		return std::tuple(immediateOrder ? uint8_t{0} : specialized ? uint8_t{1} : uint8_t{2},
			grant.expiryRound, grant.id);
	}

	static void validateQuery(ActionKind action, int32_t round)
	{
		if(!isValid(action) || round < 0)
			throw std::invalid_argument("Invalid Hero Action allowance query");
	}

	uint32_t appendGrant(AllowanceKind allowance, GrantSource source, int32_t expiryRound)
	{
		if(nextGrantId == std::numeric_limits<uint32_t>::max())
			throw std::overflow_error("Hero Action allowance grant ID exhausted");

		Grant grant;
		grant.id = nextGrantId;
		grant.allowance = allowance;
		grant.source = source;
		grant.grantedRound = currentRound;
		grant.expiryRound = expiryRound;
		grants.push_back(grant);
		++nextGrantId;
		return grant.id;
	}
};

/// Contextual continuation granted by Double Command. This is separate from
/// ordinary round-long allowances because the extra Order must be issued
/// immediately after the triggering Hero Order; a primary Second Wind stores
/// its creature activation until the continuation has resolved.
struct DLL_LINKAGE DoubleCommandState
{
	static constexpr uint32_t INVALID_UNIT_ID = HeroOrderState::INVALID_UNIT_ID;

	enum class Phase : uint8_t
	{
		NONE,
		ORDER_REQUIRED,
		SECOND_WIND_READY
	};

	bool used = false;
	Phase phase = Phase::NONE;
	HeroCommand firstOrder = HeroCommand::NONE;
	int32_t issuedRound = -1;
	uint32_t anchorStackId = INVALID_UNIT_ID;
	uint32_t deferredSecondWindTargetUnitId = INVALID_UNIT_ID;

	bool orderPending() const { return phase == Phase::ORDER_REQUIRED; }
	bool secondWindReady() const { return phase == Phase::SECOND_WIND_READY; }

	bool operator==(const DoubleCommandState &) const = default;

	/// Starts only for an accepted flexible-Hero Order receipt. Caller supplies
	/// the server-authoritative perk snapshot and the active stack/target IDs.
	bool begin(
		const HeroActionAllowanceState::Receipt & receipt,
		bool activePerk,
		HeroCommand order,
		int32_t round,
		uint32_t anchor,
		uint32_t deferredSecondWindTarget = INVALID_UNIT_ID)
	{
		validateShape();
		if(!activePerk || used || phase != Phase::NONE
			|| receipt.action != HeroActionAllowanceState::ActionKind::ORDER
			|| receipt.allowance != HeroActionAllowanceState::AllowanceKind::HERO
			|| receipt.round != round || round < 0 || !heroCommands::isActive(order)
			|| anchor == INVALID_UNIT_ID
			|| (order == HeroCommand::SECOND_WIND) == (deferredSecondWindTarget == INVALID_UNIT_ID))
			return false;

		used = true;
		phase = Phase::ORDER_REQUIRED;
		firstOrder = order;
		issuedRound = round;
		anchorStackId = anchor;
		deferredSecondWindTargetUnitId = deferredSecondWindTarget;
		return true;
	}

	/// Resolves the mandatory distinct Order. A Second Wind primary leaves a
	/// short-lived continuation phase so the original activation can resume.
	void completeFollowup(HeroCommand secondOrder)
	{
		validateShape();
		if(phase != Phase::ORDER_REQUIRED || !heroCommands::isActive(secondOrder)
			|| secondOrder == firstOrder)
			throw std::invalid_argument("Invalid Double Command follow-up Order");
		if(firstOrder == HeroCommand::SECOND_WIND)
			phase = Phase::SECOND_WIND_READY;
		else
			clearContext();
		validateShape();
	}

	/// Exhausts the mandatory continuation when no distinct legal Order remains.
	void exhaustPendingOrder()
	{
		validateShape();
		if(phase != Phase::ORDER_REQUIRED)
			throw std::invalid_argument("No Double Command Order is pending");
		if(firstOrder == HeroCommand::SECOND_WIND)
			phase = Phase::SECOND_WIND_READY;
		else
			clearContext();
		validateShape();
	}

	/// Returns the held Second Wind target and clears its continuation context.
	std::optional<uint32_t> consumeSecondWindContinuation()
	{
		validateShape();
		if(phase != Phase::SECOND_WIND_READY)
			return {};
		const auto target = deferredSecondWindTargetUnitId;
		clearContext();
		validateShape();
		return target;
	}

	void validateShape() const
	{
		const bool contextClear = firstOrder == HeroCommand::NONE && issuedRound == -1
			&& anchorStackId == INVALID_UNIT_ID && deferredSecondWindTargetUnitId == INVALID_UNIT_ID;
		if(phase == Phase::NONE)
		{
			if(!contextClear)
				throw std::runtime_error("Invalid cleared Double Command state");
			return;
		}
		if(!used || !heroCommands::isActive(firstOrder) || issuedRound < 0
			|| anchorStackId == INVALID_UNIT_ID)
			throw std::runtime_error("Invalid active Double Command state");
		if(phase == Phase::ORDER_REQUIRED)
		{
			if((firstOrder == HeroCommand::SECOND_WIND)
				!= (deferredSecondWindTargetUnitId != INVALID_UNIT_ID))
				throw std::runtime_error("Invalid pending Double Command target");
			return;
		}
		if(phase == Phase::SECOND_WIND_READY && firstOrder == HeroCommand::SECOND_WIND
			&& deferredSecondWindTargetUnitId != INVALID_UNIT_ID)
			return;
		throw std::runtime_error("Invalid Double Command phase");
	}

	/// Verifies the bounded follow-up/exhaustion/Second Wind-clear transitions
	/// used by state-change packets. Starting a continuation belongs to StartAction.
	void validateTransitionFrom(const DoubleCommandState & previous) const
	{
		previous.validateShape();
		validateShape();
		if(*this == previous)
			return;
		if(previous.orderPending())
		{
			auto expected = previous;
			expected.exhaustPendingOrder();
			if(*this == expected)
				return;
		}
		else if(previous.secondWindReady())
		{
			auto expected = previous;
			if(expected.consumeSecondWindContinuation() && *this == expected)
				return;
		}
		throw std::runtime_error("Invalid Double Command state transition");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && *this != DoubleCommandState{}
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
			throw std::runtime_error("Cannot discard Double Command state");
		if(h.saving)
			validateShape();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_COMMAND))
		{
			h & used;
			h & phase;
			h & firstOrder;
			h & issuedRound;
			h & anchorStackId;
			h & deferredSecondWindTargetUnitId;
			if(!h.saving)
				validateShape();
		}
		else if(!h.saving)
			*this = {};
	}

private:
	void clearContext()
	{
		phase = Phase::NONE;
		firstOrder = HeroCommand::NONE;
		issuedRound = -1;
		anchorStackId = INVALID_UNIT_ID;
		deferredSecondWindTargetUnitId = INVALID_UNIT_ID;
	}
};

/// Saved state for the once-per-combat Battle Plan opening choice. The
/// one-time availability is resolved from the selected perk during battle setup;
/// an outstanding choice is bound to its non-activation stack anchor.
struct DLL_LINKAGE PreCombatOrderState
{
	static constexpr uint32_t INVALID_UNIT_ID = HeroOrderState::INVALID_UNIT_ID;

	enum class Phase : uint8_t
	{
		NOT_GRANTED,
		AVAILABLE,
		ORDER_REQUIRED,
		COMPLETED
	};

	Phase phase = Phase::NOT_GRANTED;
	int32_t issuedRound = -1;
	uint32_t anchorStackId = INVALID_UNIT_ID;

	bool operator==(const PreCombatOrderState &) const = default;
	bool orderPending() const { return phase == Phase::ORDER_REQUIRED; }
	bool isUnresolved() const { return phase == Phase::AVAILABLE || orderPending(); }

	void beginOrderRequired(int32_t round, uint32_t anchor)
	{
		validateShape();
		if(phase != Phase::AVAILABLE || round != 1 || anchor == INVALID_UNIT_ID)
			throw std::invalid_argument("Invalid Battle Plan opening Order");
		phase = Phase::ORDER_REQUIRED;
		issuedRound = round;
		anchorStackId = anchor;
		validateShape();
	}

	void complete()
	{
		validateShape();
		if(phase != Phase::AVAILABLE && phase != Phase::ORDER_REQUIRED)
			throw std::invalid_argument("Battle Plan is not available to complete");
		phase = Phase::COMPLETED;
		issuedRound = -1;
		anchorStackId = INVALID_UNIT_ID;
		validateShape();
	}

	void validateShape() const
	{
		const bool noContext = issuedRound == -1 && anchorStackId == INVALID_UNIT_ID;
		if(phase == Phase::NOT_GRANTED || phase == Phase::AVAILABLE || phase == Phase::COMPLETED)
		{
			if(!noContext)
				throw std::runtime_error("Invalid inactive Battle Plan state");
			return;
		}
		if(phase == Phase::ORDER_REQUIRED && issuedRound == 1 && anchorStackId != INVALID_UNIT_ID)
			return;
		throw std::runtime_error("Invalid pending Battle Plan state");
	}

	void validateTransitionFrom(const PreCombatOrderState & previous) const
	{
		previous.validateShape();
		validateShape();
		if(*this == previous)
			return;
		if(previous.phase == Phase::AVAILABLE)
		{
			auto expected = previous;
			if(phase == Phase::ORDER_REQUIRED)
			{
				expected.beginOrderRequired(issuedRound, anchorStackId);
				if(*this == expected)
					return;
			}
			expected = previous;
			expected.complete();
			if(*this == expected)
				return;
		}
		else if(previous.phase == Phase::ORDER_REQUIRED)
		{
			auto expected = previous;
			expected.complete();
			if(*this == expected)
				return;
		}
		throw std::runtime_error("Invalid Battle Plan state transition");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && *this != PreCombatOrderState{}
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
			throw std::runtime_error("Cannot discard Battle Plan state");
		if(h.saving)
			validateShape();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_PLAN))
		{
			h & phase;
			h & issuedRound;
			h & anchorStackId;
			if(!h.saving)
				validateShape();
		}
		else if(!h.saving)
			*this = {};
	}
};

/// Shared value-only accepted Hero spell transition used by authoritative
/// execution and hypothetical battle projections. Spell identities and target
/// sequence metadata remain caller-owned; this commits allowance provenance
/// and Metamagic source counters together.
struct DLL_LINKAGE HeroSpellAllowanceTransition
{
	struct DLL_LINKAGE Result
	{
		HeroActionAllowanceState::Receipt receipt;
		bool chargedMetamagicUse = false;
		bool activatedGrand = false;
		std::vector<uint32_t> grantedGrantIds;
		uint8_t pendingMetamagicGrants = 0;
	};

	/// Reports the automatic Grand outcome for a candidate extra spell. Grand
	/// is available only for the first extra spell of the third Metamagic use.
	static bool activatesGrand(
		bool metamagicFollowup,
		uint8_t metamagicPendingCount,
		size_t sequenceSpellCount,
		uint8_t metamagicUsesConsumed,
		uint8_t metamagicRank,
		bool grandPerkEnabled,
		bool metamagicGrandUsed)
	{
		return metamagicFollowup
			&& metamagicPendingCount == 1
			&& sequenceSpellCount == 1
			&& metamagicUsesConsumed == 2
			&& metamagicRank >= 3
			&& grandPerkEnabled
			&& !metamagicGrandUsed;
	}

	/// Commits an already accepted HERO-mode spell. `selectionGrantId` must be
	/// the currently preferred spell-paying grant; validation/preflight remains
	/// the caller's responsibility. The protocol follow-up flag is meaningful
	/// only for Metamagic grants, not every future typed spell source. `grand`
	/// is the expected server-derived outcome. Failure is atomic and leaves
	/// every argument unchanged.
	static std::optional<Result> commitAcceptedCast(
		HeroActionAllowanceState & ledger,
		uint32_t selectionGrantId,
		int32_t round,
		bool metamagicFollowup,
		bool grand,
		uint8_t metamagicRank,
		bool grandPerkEnabled,
		uint8_t & metamagicUsesConsumed,
		uint8_t & metamagicPendingCount,
		bool & metamagicGrandUsed,
		size_t sequenceSpellCount)
	{
		using Ledger = HeroActionAllowanceState;
		using Allowance = Ledger::AllowanceKind;
		using Source = Ledger::GrantSource;
		using Action = Ledger::ActionKind;

		if(round < 0 || round != ledger.currentRound || metamagicRank > 3
			|| metamagicUsesConsumed > 3 || metamagicPendingCount > 1)
			return {};

		const auto outstandingMetaGrants = countPendingMetamagicGrants(ledger, round);
		if(outstandingMetaGrants != metamagicPendingCount)
			return {};

		const auto selection = ledger.eligibleAllowance(Action::SPELL, round);
		if(!selection || selection->grantId != selectionGrantId)
			return {};

		const bool metaSource = selection->source == Source::METAMAGIC;
		const bool grandSource = selection->source == Source::METAMAGIC_GRAND;
		if((metaSource || grandSource) != metamagicFollowup
			|| (grand && (!metaSource || !metamagicFollowup)))
			return {};

		const bool activatesGrand = HeroSpellAllowanceTransition::activatesGrand(
			metamagicFollowup, metamagicPendingCount, sequenceSpellCount,
			metamagicUsesConsumed, metamagicRank, grandPerkEnabled, metamagicGrandUsed);
		if(grand != activatesGrand)
			return {};

		if(selection->allowance == Allowance::HERO)
		{
			if(metamagicFollowup || grand || metamagicPendingCount != 0 || sequenceSpellCount != 0)
				return {};
		}
		else if(selection->allowance == Allowance::SPELL)
		{
			if(metaSource)
			{
				if(metamagicPendingCount != 1 || sequenceSpellCount != 1 || metamagicUsesConsumed >= metamagicRank)
					return {};
			}
			else if(grandSource)
			{
				if(grand || !metamagicGrandUsed || metamagicPendingCount != 1 || sequenceSpellCount != 2)
					return {};
			}
			else if(metamagicFollowup || grand)
				return {};
		}
		else
			return {};

		auto nextLedger = ledger;
		const auto receipt = nextLedger.consumeAllowance(selectionGrantId, Action::SPELL, round);
		if(!receipt)
			return {};

		Result result;
		result.receipt = *receipt;
		uint8_t nextUsesConsumed = metamagicUsesConsumed;
		uint8_t nextPendingCount = metamagicPendingCount;
		bool nextGrandUsed = metamagicGrandUsed;

		if(receipt->allowance == Allowance::HERO)
		{
			// A base-Hero-paid cast reserves (but does not charge) one use until its
			// optional Spell Action is accepted. An outstanding token cannot recurse.
			if(metamagicRank > nextUsesConsumed)
			{
				result.grantedGrantIds.push_back(nextLedger.grantAllowance(
					Allowance::SPELL, Source::METAMAGIC, round));
				nextPendingCount = 1;
			}
		}
		else if(metaSource)
		{
			++nextUsesConsumed; // charged only when the first Metamagic extra cast is accepted
			nextPendingCount = 0;
			if(activatesGrand)
			{
				result.grantedGrantIds.push_back(nextLedger.grantAllowance(
					Allowance::SPELL, Source::METAMAGIC_GRAND, round));
				nextPendingCount = 1;
				nextGrandUsed = true;
				result.activatedGrand = true;
			}
			result.chargedMetamagicUse = true;
		}
		else if(grandSource)
		{
			nextPendingCount = 0;
		}

		if(countPendingMetamagicGrants(nextLedger, round) != nextPendingCount)
			return {};

		nextLedger.validateShape();
		ledger = std::move(nextLedger);
		metamagicUsesConsumed = nextUsesConsumed;
		metamagicPendingCount = nextPendingCount;
		metamagicGrandUsed = nextGrandUsed;
		result.pendingMetamagicGrants = nextPendingCount;
		return result;
	}

private:
	static uint8_t countPendingMetamagicGrants(const HeroActionAllowanceState & ledger, int32_t round)
	{
		uint32_t count = 0;
		for(const auto & grant : ledger.grants)
			if(grant.allowance == HeroActionAllowanceState::AllowanceKind::SPELL
				&& (grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC
					|| grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND)
				&& grant.expiryRound >= round)
				++count;
		if(count > std::numeric_limits<uint8_t>::max())
			throw std::runtime_error("Metamagic allowance count overflow");
		return static_cast<uint8_t>(count);
	}
};
