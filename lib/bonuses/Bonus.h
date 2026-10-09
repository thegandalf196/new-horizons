/*
 * Bonus.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BonusEnum.h"
#include "BonusCustomTypes.h"
#include "Limiters.h"
#include "../serializer/Serializeable.h"
#include "../texts/MetaString.h"
#include "../filesystem/ResourcePath.h"
#include <vcmi/scripting/ApiTags.h>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

class IBonusBearer;
class IPropagator;
class IUpdater;
class CSelector;
class IGameInfoCallback;
class BonusParameters;
struct Bonus;

namespace newHorizonsFrozen
{
DLL_LINKAGE std::optional<int32_t> markerApplicationRound(const Bonus & bonus);
}

namespace BonusMigration
{
DLL_LINKAGE bool migrateCombatAbility(Bonus & bonus);
}

namespace newHorizonsSwiftRebirth
{
DLL_LINKAGE bool isLifecycleMarker(const Bonus & bonus);
}

using TBonusListPtr = std::shared_ptr<BonusList>;
using TConstBonusListPtr = std::shared_ptr<const BonusList>;
using TPropagatorPtr = std::shared_ptr<const IPropagator>;
using TUpdaterPtr = std::shared_ptr<const IUpdater>;
using TBonusParametersPtr = std::shared_ptr<const BonusParameters>;

/// Struct for handling bonuses of several types. Can be transferred to any hero
struct DLL_LINKAGE Bonus : public std::enable_shared_from_this<Bonus>, public Serializeable, public scripting::ApiCopyable<Bonus>
{
	BonusDuration::Type duration = BonusDuration::PERMANENT; //uses BonusDuration values - 2 bytes
	si32 val = 0;
	si16 turnsRemain = 0; //used if duration is N_TURNS, N_DAYS or ONE_WEEK

	BonusValueType valType = BonusValueType::ADDITIVE_VALUE; // 1 byte
	BonusSource source = BonusSource::OTHER; //source type" uses BonusSource values - what gave that bonus - 1 byte
	BonusSource targetSourceType = BonusSource::OTHER;//Bonuses of what origin this amplifies, uses BonusSource values. Needed for PERCENT_TO_TARGET_TYPE. - 1 byte
	BonusLimitEffect effectRange = BonusLimitEffect::NO_LIMIT; // 1 byte
	BonusType type = BonusType::NONE; //uses BonusType values - says to what is this bonus - 2 bytes

	BonusSubtypeID subtype;
	BonusSourceID sid; //source id: id of object/artifact/spell
	std::string stacking; // bonuses with the same stacking value don't stack (e.g. Angel/Archangel morale bonus)

	TBonusParametersPtr parameters;
	TLimiterPtr limiter;
	TPropagatorPtr propagator;
	TUpdaterPtr updater;
	TUpdaterPtr propagationUpdater;

	ImagePath customIconPath;
	MetaString description;
	PlayerColor bonusOwner = PlayerColor::CANNOT_DETERMINE;
	// Target-relative provenance captured when an effect is applied. Unlike
	// bonusOwner (dynamic aura ownership), this survives control changes.
	bool appliedByEnemy = false;
	// Stable owner of the side that applied a spell effect. Unlike
	// appliedByEnemy, this can be compared with a new recipient after an effect
	// moves between units. Unknown legacy and non-spell bonuses use the sentinel.
	PlayerColor spellCasterOwner = PlayerColor::CANNOT_DETERMINE;
	// Optional status-level metadata. Several component bonuses may represent one
	// status; callers must use statusIdentity to group components when provided.
	std::vector<BonusStatusTag> statusTags;
	std::string statusIdentity;
	static constexpr size_t MAX_STATUS_IDENTITY_LENGTH = 256;
	static bool isValidStatusTag(BonusStatusTag tag)
	{
		return tag == BonusStatusTag::DEBUFF || tag == BonusStatusTag::NON_TRANSFERABLE;
	}
	static bool isValidStatusIdentity(std::string_view identity)
	{
		if(identity.size() > MAX_STATUS_IDENTITY_LENGTH)
			return false;
		for(const unsigned char character : identity)
			if(character < 0x20 || character == 0x7f)
				return false;
		return true;
	}
	bool hasStatusMetadata() const
	{
		return !statusTags.empty() || !statusIdentity.empty();
	}
	bool hasValidStatusMetadata() const
	{
		if(!isValidStatusIdentity(statusIdentity) || (!statusIdentity.empty() && statusTags.empty()))
			return false;
		for(size_t index = 0; index < statusTags.size(); ++index)
		{
			if(!isValidStatusTag(statusTags[index]))
				return false;
			for(size_t previous = 0; previous < index; ++previous)
				if(statusTags[previous] == statusTags[index])
					return false;
		}
		return true;
	}
	static bool isValidSpellCasterOwner(PlayerColor owner)
	{
		return owner.isValidPlayer() || owner == PlayerColor::CANNOT_DETERMINE
			|| owner == PlayerColor::UNFLAGGABLE || owner == PlayerColor::NEUTRAL;
	}

	bool hidden = false;
	void validateConfusionPendingMarker() const;

	Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID sourceID);
	Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID sourceID, BonusSubtypeID subtype);
	Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID sourceID, BonusSubtypeID subtype, BonusValueType ValType);
	Bonus(const Bonus & inst, const BonusSourceID & sourceId);
	Bonus() = default;

	template <typename Handler> void validateFrozenSerialization(Handler & h) const
	{
		if(newHorizonsFrozen::markerApplicationRound(*this)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN))
			throw std::runtime_error("Cannot discard Frozen marker in an older bonus format");
	}

	template <typename Handler> void validateSwiftRebirthSerialization(Handler & h) const
	{
		if(newHorizonsSwiftRebirth::isLifecycleMarker(*this)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SWIFT_REBIRTH))
			throw std::runtime_error("Cannot discard Swift Rebirth lifecycle in an older bonus format");
	}

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving)
			validateSwiftRebirthSerialization(h);
		if(h.saving)
			validateFrozenSerialization(h);
		if(h.saving)
		{
			validateConfusionPendingMarker();
			if(type == BonusType::CONFUSION_PENDING
				&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CONFUSION_MARKER))
				throw std::runtime_error("Cannot discard Confusion pending marker");
		}
		if(h.saving && !isValidSpellCasterOwner(spellCasterOwner))
			throw std::runtime_error("Invalid bonus spell caster owner provenance");
		if(h.saving && appliedByEnemy
			&& !h.hasFeature(Handler::Version::BONUS_EFFECT_HOSTILITY))
			throw std::runtime_error("Cannot discard bonus effect hostility provenance");
		if(h.saving && spellCasterOwner != PlayerColor::CANNOT_DETERMINE
			&& !h.hasFeature(Handler::Version::BONUS_SPELL_CASTER_OWNER))
			throw std::runtime_error("Cannot discard bonus spell caster owner provenance");
		if(h.saving && !hasValidStatusMetadata())
			throw std::runtime_error("Invalid bonus status metadata");
		if(h.saving && hasStatusMetadata()
			&& !h.hasFeature(Handler::Version::BONUS_STATUS_TAGS))
			throw std::runtime_error("Cannot discard bonus status metadata");
		if(h.saving && std::find(statusTags.begin(), statusTags.end(), BonusStatusTag::NON_TRANSFERABLE) != statusTags.end()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_REALITY_WARP_EXCHANGE))
			throw std::runtime_error("Cannot write non-transferable status tag in an older bonus format");
		// TIME_STOP is a new serialized bonus type.  Never emit it through an
		// older handler: doing so would shift/interpret the enum differently in a
		// legacy reader.  A battle without this marker remains fully loadable by
		// older saves because no extra field is introduced in the containing node.
		if(h.saving && type == BonusType::TIME_STOP
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP))
			throw std::runtime_error("Cannot discard New Horizons Time Stop state");
		if(h.saving && type == BonusType::SIEGE_RATING
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SIEGE_RATING))
			throw std::runtime_error("Cannot discard New Horizons Siege rating state");
		if(h.saving && type == BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION))
			throw std::runtime_error("Cannot discard New Horizons Crusade magical reduction state");
		if(h.saving && (type == BonusType::MAXIMUM_LUCK
			|| type == BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS))
			throw std::runtime_error("Cannot discard New Horizons creature probability modifier state");
		if(h.saving && type == BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SHIELD_OF_CHAOS_PHYSICAL_REDUCTION))
			throw std::runtime_error("Cannot discard New Horizons Shield of Chaos physical reduction state");
		if(h.saving && type == BonusType::PHYSICAL_AFFLICTION
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_PHYSICAL_AFFLICTIONS))
			throw std::runtime_error("Cannot discard New Horizons physical-affliction marker state");
		if(h.saving && (type == BonusType::PUPPET_MASTER_CONTROL || type == BonusType::LUCIDITY)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_PUPPET_MASTER_CONTROL))
			throw std::runtime_error("Cannot discard New Horizons Puppet Master control or Lucidity state");
		if(h.saving && type == BonusType::CREATURE_ABILITY_SUPPRESSION
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION))
			throw std::runtime_error("Cannot discard creature ability suppression state");
		if(h.saving && type == BonusType::ELEMENTAL_SPELL_DAMAGE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE))
			throw std::runtime_error("Cannot discard elemental spell damage state");
		if(h.saving && type == BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE))
			throw std::runtime_error("Cannot discard incoming elemental spell damage state");
		if(h.saving && (type == BonusType::PASS_THROUGH || type == BonusType::LONG_REACH)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_TRANSIT_REACH))
			throw std::runtime_error("Cannot discard creature traversal or reach capability");
		if(h.saving && (duration & BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION) != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_ACTIVATION_DURATION))
			throw std::runtime_error("Cannot discard New Horizons creature activation bonus duration");
		h & duration;
		h & type;
		h & subtype;
		h & source;
		h & val;
		h & sid;
		h & description;

		h & customIconPath;
		h & hidden;
		if (h.hasFeature(Handler::Version::BONUS_TRIGGER))
		{
			h & parameters;
		}
		else
		{
			std::vector<int> oldAddInfo;
			h & oldAddInfo;
			convertAddInfo(oldAddInfo);
		}
		h & turnsRemain;
		h & valType;
		h & stacking;
		h & effectRange;
		h & limiter;
		h & propagator;
		h & updater;
		h & propagationUpdater;
		h & targetSourceType;
		if(h.hasFeature(Handler::Version::BONUS_EFFECT_HOSTILITY))
			h & appliedByEnemy;
		else if(!h.saving)
			appliedByEnemy = false;
		if(h.hasFeature(Handler::Version::BONUS_SPELL_CASTER_OWNER))
		{
			h & spellCasterOwner;
			if(!h.saving && !isValidSpellCasterOwner(spellCasterOwner))
				throw std::runtime_error("Invalid bonus spell caster owner provenance");
		}
		else if(!h.saving)
			spellCasterOwner = PlayerColor::CANNOT_DETERMINE;
		if(h.hasFeature(Handler::Version::BONUS_STATUS_TAGS))
		{
			h & statusTags;
			h & statusIdentity;
			if(!h.saving && !hasValidStatusMetadata())
				throw std::runtime_error("Invalid bonus status metadata");
			if(!h.saving && std::find(statusTags.begin(), statusTags.end(), BonusStatusTag::NON_TRANSFERABLE) != statusTags.end()
				&& !h.hasFeature(Handler::Version::NEW_HORIZONS_REALITY_WARP_EXCHANGE))
				throw std::runtime_error("Unsupported non-transferable status tag in older bonus format");
		}
		else if(!h.saving)
		{
			statusTags.clear();
			statusIdentity.clear();
		}

		//old saves stored BATTLE_NO_FLEEING in the slot now used by BATTLE_CAN_FLEE, it blocked retreating unconditionally
		if(!h.saving && !h.hasFeature(Handler::Version::RETREAT_PERMISSION_BONUSES) && type == BonusType::BATTLE_CAN_FLEE)
			val = -GameConstants::BATTLE_RETREAT_BLOCK;

		if (!h.saving && !h.hasFeature(Handler::Version::COMBAT_ABILITY_SCRIPTS))
			BonusMigration::migrateCombatAbility(*this);
		if(!h.saving)
		{
			validateFrozenSerialization(h);
			validateSwiftRebirthSerialization(h);
			validateConfusionPendingMarker();
			if(type == BonusType::CONFUSION_PENDING
				&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CONFUSION_MARKER))
				throw std::runtime_error("Unsupported Confusion pending marker");
		}
	}

	void convertAddInfo(const std::vector<int> & oldAddInfo);

	static bool NDays(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::N_DAYS;
		return set != 0;
	}
	static bool NTurns(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::N_TURNS;
		return set != 0;
	}
	static bool OneDay(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::ONE_DAY;
		return set != 0;
	}
	static bool OneWeek(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::ONE_WEEK;
		return set != 0;
	}
	static bool OneBattle(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::ONE_BATTLE;
		return set != 0;
	}
	static bool Permanent(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::PERMANENT;
		return set != 0;
	}
	static bool UntilGetsTurn(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::STACK_GETS_TURN;
		return set != 0;
	}
	static bool UntilActivationEnds(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::STACK_ACTIVATION;
		return set != 0;
	}
	static bool UntilNextCreatureActivation(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION;
		return set != 0;
	}
	static bool UntilAttack(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_ATTACK;
		return set != 0;
	}
	static bool UntilTakingIndirectDamage(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE;
		return set != 0;
	}
	static bool untilAfterAttackSequence(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE;
		return set != 0;
	}
	static bool UntilBeingAttacked(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_BEING_ATTACKED;
		return set != 0;
	}
	static bool UntilCommanderKilled(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::COMMANDER_KILLED;
		return set != 0;
	}
	static bool UntilOwnAttack(const Bonus *hb)
	{
		auto set = hb->duration & BonusDuration::UNTIL_OWN_ATTACK;
		return set != 0;
	}
	inline bool operator == (const BonusType & cf) const
	{
		return type == cf;
	}
	inline void operator += (const ui32 Val) //no return
	{
		val += Val;
	}

	std::string Description(const IGameInfoCallback * cb, std::optional<si32> customValue = {}) const;
	JsonNode toJsonNode() const;

	std::shared_ptr<Bonus> addLimiter(const TLimiterPtr & Limiter); //returns this for convenient chain-calls
	std::shared_ptr<Bonus> addPropagator(const TPropagatorPtr & Propagator); //returns this for convenient chain-calls
	std::shared_ptr<Bonus> addUpdater(const TUpdaterPtr & Updater); //returns this for convenient chain-calls
	std::shared_ptr<Bonus> addPropagationUpdater(const TUpdaterPtr & Updater); //returns this for convenient chain-calls
};

DLL_LINKAGE std::ostream & operator<<(std::ostream &out, const Bonus &bonus);
