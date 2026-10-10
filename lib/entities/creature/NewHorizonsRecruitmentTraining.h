/*
 * NewHorizonsRecruitmentTraining.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

class CGHeroInstance;
class CGameState;
class BattleInfo;
struct Bonus;
class JsonNode;
namespace newHorizonsTraining
{
inline constexpr auto SKILL = "new-horizons:recruitment";
inline constexpr auto DRILL_SERGEANT = "new-horizons:recruitment.drillSergeant";
inline constexpr auto FIELD_INSTRUCTOR = "new-horizons:recruitment.fieldInstructor";
inline constexpr auto REINFORCEMENT_DRILL = "new-horizons:recruitment.reinforcementDrill";

inline constexpr auto DIPLOMACY = "new-horizons:diplomacy";
inline constexpr auto MERCENARY_CAPTAIN = "new-horizons:diplomacy.mercenaryCaptain";
inline constexpr auto LOYAL_MERCENARIES = "new-horizons:diplomacy.loyalMercenaries";
inline constexpr auto MERCENARY_BONUS = "new-horizons:mercenaryCaptain";
struct DLL_LINKAGE MercenaryOrigin
{
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	int32_t combatsRemaining = 3;
	bool operator==(const MercenaryOrigin &) const = default;
	void validate() const
	{
		if(!hero.hasValue() || combatsRemaining < 0 || combatsRemaining > 3)
			throw std::runtime_error("Invalid Diplomacy cohort origin");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & hero;
		h & combatsRemaining;
		if(!h.saving) validate();
	}
};

/// Whole strategic-stack provenance. Temporary detachment never changes this.
struct DLL_LINKAGE Receipt
{
	int32_t drillDeadline = -1;
	ObjectInstanceID residentRecruiter = ObjectInstanceID::NONE;
	bool fieldPending = false;
	bool fieldTrained = false;
	bool reinforcementPending = false;
	std::vector<MercenaryOrigin> mercenaryOrigins;

	bool operator==(const Receipt &) const = default;
	bool empty() const { return *this == Receipt{}; }
	void validate() const
	{
		ObjectInstanceID previous = ObjectInstanceID::NONE;
		for(const auto & origin : mercenaryOrigins)
		{
			origin.validate();
			if(origin.hero <= previous)
				throw std::runtime_error("Unordered or duplicate Diplomacy cohort origins");
			previous = origin.hero;
		}
		if(drillDeadline < -1 || residentRecruiter.getNum() < -1
			|| ((fieldPending || fieldTrained || reinforcementPending) != residentRecruiter.hasValue()))
			throw std::runtime_error("Invalid recruitment training receipt");
	}
	bool recruitedBy(ObjectInstanceID hero, bool unspent = false) const
	{
		return std::ranges::any_of(mercenaryOrigins, [hero, unspent](const auto & origin)
			{ return origin.hero == hero && (!unspent || origin.combatsRemaining > 0); });
	}
	void admittedThroughDiplomacy(ObjectInstanceID hero)
	{
		if(!hero.hasValue())
			throw std::runtime_error("Invalid Diplomacy recruiting hero");
		if(!recruitedBy(hero))
		{
			const auto position = std::lower_bound(mercenaryOrigins.begin(), mercenaryOrigins.end(), hero,
				[](const auto & origin, auto id) { return origin.hero < id; });
			mercenaryOrigins.insert(position, {hero, 3});
		}
		validate();
	}
	void crossedArmyBoundary()
	{
		residentRecruiter = ObjectInstanceID::NONE;
		fieldPending = fieldTrained = reinforcementPending = false;
	}
	void merge(const Receipt & other)
	{
		validate();
		other.validate();
		if(residentRecruiter.hasValue() && other.residentRecruiter.hasValue()
			&& residentRecruiter != other.residentRecruiter)
			throw std::runtime_error("Cannot merge training receipts from different recruiters");
		drillDeadline = std::max(drillDeadline, other.drillDeadline);
		if(other.residentRecruiter.hasValue())
			residentRecruiter = other.residentRecruiter;
		fieldPending = fieldPending || other.fieldPending;
		fieldTrained = fieldTrained || other.fieldTrained;
		reinforcementPending = reinforcementPending || other.reinforcementPending;
		for(const auto & incoming : other.mercenaryOrigins)
		{
			const auto position = std::lower_bound(mercenaryOrigins.begin(), mercenaryOrigins.end(), incoming.hero,
				[](const auto & origin, auto id) { return origin.hero < id; });
			if(position != mercenaryOrigins.end() && position->hero == incoming.hero)
				position->combatsRemaining = std::max(position->combatsRemaining, incoming.combatsRemaining);
			else
				mercenaryOrigins.insert(position, incoming);
		}
		validate();
	}
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_COHORTS) && !mercenaryOrigins.empty())
			throw std::runtime_error("Diplomacy cohorts require the new save format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING) && !empty())
			throw std::runtime_error("Recruitment training requires the new save format");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING))
		{
			h & drillDeadline;
			h & residentRecruiter;
			h & fieldPending;
			h & fieldTrained;
			h & reinforcementPending;
		}
		else if(!h.saving)
			*this = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_COHORTS))
		{
			// Decode incrementally: never allocate an untrusted vector length.
			uint32_t count = static_cast<uint32_t>(mercenaryOrigins.size());
			h & count;
			if(!h.saving) mercenaryOrigins.clear();
			for(uint32_t index = 0; index < count; ++index)
			{
				MercenaryOrigin origin;
				if(h.saving) origin = mercenaryOrigins[index];
				h & origin;
				if(!h.saving)
				{
					if(!mercenaryOrigins.empty() && origin.hero <= mercenaryOrigins.back().hero)
						throw std::runtime_error("Unordered or duplicate Diplomacy cohort origins");
					mercenaryOrigins.push_back(origin);
				}
			}
		}
		else if(!h.saving) mercenaryOrigins.clear();
		if(!h.saving) validate();
	}
};

struct DLL_LINKAGE StackChange
{
	ObjectInstanceID army = ObjectInstanceID::NONE;
	SlotID slot;
	CreatureID creature = CreatureID::NONE;
	int32_t expectedCount = 0;
	int32_t recruitedCount = 0;
	Receipt previous;
	Receipt next;
	bool operator==(const StackChange &) const = default;
	void validate() const
	{
		previous.validate();
		next.validate();
		if(!army.hasValue() || !slot.validSlot() || creature == CreatureID::NONE
			|| expectedCount < 0 || recruitedCount < 0
			|| static_cast<int64_t>(expectedCount) + recruitedCount > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Invalid training stack transaction");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) { validate(); previous.validateSerialization(h); next.validateSerialization(h); }
		h & army;
		h & slot;
		h & creature;
		h & expectedCount;
		h & recruitedCount;
		h & previous;
		h & next;
		if(!h.saving) validate();
	}
};
struct DLL_LINKAGE WeekChange
{
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	int32_t previous = -1;
	int32_t next = -1;
	bool operator==(const WeekChange &) const = default;
	void validate() const
	{
		if(!hero.hasValue() || previous < -1 || next < 0 || next <= previous)
			throw std::runtime_error("Invalid Reinforcement Drill week transaction");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & hero;
		h & previous;
		h & next;
		if(!h.saving) validate();
	}
};
struct DLL_LINKAGE Batch
{
	std::vector<StackChange> stacks;
	std::vector<WeekChange> weeks;
	bool operator==(const Batch &) const = default;
	bool empty() const { return stacks.empty() && weeks.empty(); }
	void validate() const
	{
		std::set<std::pair<ObjectInstanceID, SlotID>> locations;
		std::set<ObjectInstanceID> heroes;
		for(const auto & change : stacks)
		{
			change.validate();
			if(!locations.emplace(change.army, change.slot).second)
				throw std::runtime_error("Duplicate training stack transaction");
		}
		for(const auto & change : weeks)
		{
			change.validate();
			if(!heroes.insert(change.hero).second)
				throw std::runtime_error("Duplicate training weekly transaction");
		}
	}
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		for(const auto & change : stacks) { change.previous.validateSerialization(h); change.next.validateSerialization(h); }
		if(h.saving && !empty() && !h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING))
			throw std::runtime_error("Cannot discard recruitment training transaction");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING))
		{
			h & stacks;
			h & weeks;
		}
		else if(!h.saving) *this = {};
		if(!h.saving) validate();
	}
};
DLL_LINKAGE Batch captureEntry(const BattleInfo & battle, int32_t day, int32_t week);
DLL_LINKAGE Batch captureCompletion(const BattleInfo & battle, const std::set<ObjectInstanceID> & retainedHeroes);
DLL_LINKAGE void validateBatch(const CGameState & state, const Batch & batch);
DLL_LINKAGE void applyValidatedBatch(CGameState & state, const Batch & batch);
DLL_LINKAGE void addEntryBonuses(BattleInfo & battle, int32_t day, int32_t week);
DLL_LINKAGE bool isTrainingBonus(const Bonus * bonus);
DLL_LINKAGE bool containsTrainingBonus(const JsonNode & node);
DLL_LINKAGE bool containsMercenaryBonus(const JsonNode & node);
DLL_LINKAGE bool isMercenaryBonus(const Bonus * bonus);

DLL_LINKAGE Receipt afterRecruitment(const CGHeroInstance & hero, CreatureID creature,
	int32_t day, const Receipt & previous);
DLL_LINKAGE Receipt afterBattleEntry(const Receipt & previous, int32_t day, bool reinforcement, ObjectInstanceID commandingHero = ObjectInstanceID::NONE);
DLL_LINKAGE Receipt afterCompletedBattle(const Receipt & previous, ObjectInstanceID recruiter);
}
