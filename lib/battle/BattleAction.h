/*
 * BattleAction.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "Destination.h"
#include "HeroCommand.h"
#include "../GameConstants.h"

#include <utility>

class CBattleInfoCallback;

namespace battle
{
	class Unit;
}

/// A struct which handles battle actions like defending, walking,... - represents a creature stack in a battle
class DLL_LINKAGE BattleAction
{
public:
	BattleSide side; //who made this action
	ui32 stackNumber; //stack ID, -1 left hero, -2 right hero,
	EActionType actionType; //use ActionType enum for values

	SpellID spell;
	/// Selected Sorcery Magic Arrow charge level, not an additional Mana amount.
	/// Zero selects the ordinary cast; the saved rules determine each level's full price.
	si32 spellOvercharge = 0;
	/// Requests the target-aware Selective Dispel mode. The server validates
	/// the spell, hero and saved perk before accepting this optional mode.
	bool spellSelectiveDispel = false;
	/// Selects a saved New Horizons Cure affliction by its SPELL_EFFECT source
	/// identity. NONE means heal-only and is validated against current target state.
	SpellID spellCureAffliction = SpellID::NONE;
	/// Explicit physical status choice; no creature-source ID is reinterpreted as a spell.
	std::string spellCurePhysicalAffliction;
	/// Player-selected Purify effects. Each pair is (unit ID, source SpellID); SpellID::NONE
	/// is the named physical-Poison choice sentinel. Other IDs select complete SPELL_EFFECT groups.
	/// Repeated units are allowed up to the caster's per-stack Spell Power cap.
	std::vector<std::pair<int32_t, SpellID>> spellPurifyChoices;
	/// Requests the once-per-combat Sorcery Temporal Field variant of Slow.
	/// The server validates perk ownership, availability and cost.
	bool spellMassSlow = false;
	/// Request only: spent by the authority when the first declared strike occurs.
	bool perfectMoment = false;
	/// Canonical New Horizons Fire Wall placement direction.  The action's
	/// target contains the selected start hex; the server derives and validates
	/// the remaining two line hexes from this direction before casting.
	BattleHex::EDir spellFireWallDirection = BattleHex::NONE;
	/// Canonical New Horizons Shadow Gift sacrifice tier (10, 20 or 30 percent).
	/// Zero means no tier was selected and is invalid for an authoritative Gift cast.
	si32 spellShadowGiftSacrificePercent = 0;
	HeroCommand command = HeroCommand::NONE;
	/// Real Inferno reserve stack selected for Demonic Gating. The entire
	/// currently available stack is committed; no creatures are created.
	CreatureID gatingCreature;
	/// Marks a Skirmisher move-and-shoot command, distinct from ordinary melee
	/// WALK_AND_ATTACK so the server can validate the selected firing hex.
	bool archerySkirmisherAttack = false;
	/// Consecrated Arms 4: chosen prayer ordinals (one, or two with Antiphonal
	/// Prayer) and their single recipient among the Order's recipients.
	static constexpr uint32_t NO_PRAYER_RECIPIENT = std::numeric_limits<uint32_t>::max();
	std::vector<uint8_t> prayerChoices;
	uint32_t prayerRecipient = NO_PRAYER_RECIPIENT;

	BattleAction();
	bool operator==(const BattleAction &) const = default;
	/// Completes a native no-action activation without moving the stack.
	static BattleAction makeNoAction(const battle::Unit * stack);
	static BattleAction makeHeroCommand(BattleSide side, HeroCommand command);
	static BattleAction makeTargetedHeroCommand(BattleSide side, HeroCommand command, uint32_t targetUnitId);
	static BattleAction makePairedHeroCommand(BattleSide side, HeroCommand command, uint32_t firstUnitId, uint32_t secondUnitId);

	static BattleAction makeHeal(const battle::Unit * healer, const battle::Unit * healed);
	static BattleAction makeDefend(const battle::Unit * stack);
	static BattleAction makeWait(const battle::Unit * stack);
	static BattleAction makeMeleeAttack(const battle::Unit * stack, const BattleHex & destination, const BattleHex & attackFrom, bool returnAfterAttack = true);
	static BattleAction makeMeleeAttack(const battle::Unit* stack, const battle::Unit* target, const BattleHex& attackFrom, bool returnAfterAttack = true);
	static BattleAction makeShotAttack(const battle::Unit * shooter, const battle::Unit * target);
	static BattleAction makeWalkAndCast(const battle::Unit * stack, const BattleHex & castFrom, const battle::Unit * target, const SpellID & spellID);
	static BattleAction makeCreatureSpellcast(const battle::Unit * stack, const battle::Target & target, const SpellID & spellID);
	static BattleAction makeMove(const battle::Unit * stack, const BattleHex & dest);
	static BattleAction makeEndOFTacticPhase(BattleSide side);
	static BattleAction makeRetreat(BattleSide side);
	static BattleAction makeSurrender(BattleSide side);

	bool isTacticsAction() const;
	bool isUnitAction() const;
	bool isSpellAction() const;
	bool isBattleEndAction() const;
	std::string toString() const;

	void aimToHex(const BattleHex & destination);
	void aimToUnit(const battle::Unit * destination);

	battle::Target getTarget(const CBattleInfoCallback * cb) const;
	void setTarget(const battle::Target & target_);

	/// Non-writing closed-kind admission; requestEnvelope retains historical request diagnostics.
	void validatePhysicalCureSerialization(bool frozenSupported, bool graveDustSupported,
		bool requestEnvelope = false) const;

	template <typename Handler> void validateWriteSerialization(Handler & h) const
	{
		if(!spellCurePhysicalAffliction.empty())
			validatePhysicalCureSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN),
				h.hasFeature(Handler::Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES));
		if(h.saving && perfectMoment && !h.hasFeature(Handler::Version::NEW_HORIZONS_PERFECT_MOMENT))
			throw std::runtime_error("Cannot serialize Perfect Moment to an older protocol");
		if(h.saving && command == HeroCommand::FOCUS_FIRE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
			throw std::runtime_error("Cannot serialize targeted command to an older protocol");
		if(h.saving && (command == HeroCommand::RIPOSTE || command == HeroCommand::BRACE
			|| command == HeroCommand::PROTECT || command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
			throw std::runtime_error("Cannot serialize canonical Order to an older protocol");
		if(h.saving && spellOvercharge != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE))
			throw std::runtime_error("Cannot serialize Magic Arrow overcharge to an older protocol");
		if(h.saving && spellSelectiveDispel
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SELECTIVE_DISPEL))
			throw std::runtime_error("Cannot serialize Selective Dispel to an older protocol");
		if(h.saving && spellCureAffliction != SpellID::NONE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CURE_AFFLICTION))
			throw std::runtime_error("Cannot serialize Cure affliction selection to an older protocol");
		if(h.saving && !spellPurifyChoices.empty()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_PURIFY))
			throw std::runtime_error("Cannot serialize Purify effect selection to an older protocol");
		if(h.saving && spellMassSlow
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
			throw std::runtime_error("Cannot serialize Temporal Field to an older protocol");
		if(h.saving && spellFireWallDirection != BattleHex::NONE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_FIRE_WALL))
			throw std::runtime_error("Cannot serialize Fire Wall direction to an older protocol");
		if(h.saving && spell == SpellID(SpellID::LAND_MINE)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_LAND_MINE))
			throw std::runtime_error("Cannot serialize multi-hex Land Mine action to an older protocol");
		if(h.saving && spell == SpellID(SpellID::QUICKSAND)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_QUICKSAND))
			throw std::runtime_error("Cannot serialize multi-hex Quicksand action to an older protocol");
		if(h.saving && archerySkirmisherAttack
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_ARCHERY_SKIRMISHER))
			throw std::runtime_error("Cannot serialize Skirmisher attack metadata to an older protocol");
		if(h.saving && (!prayerChoices.empty() || prayerRecipient != NO_PRAYER_RECIPIENT)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_CAST_CHOSEN_PRAYERS))
			throw std::runtime_error("Cannot serialize chosen prayers to an older protocol");
		if(h.saving && spellShadowGiftSacrificePercent != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SHADOW_GIFT))
			throw std::runtime_error("Cannot serialize Shadow Gift choice to an older protocol");
		if(h.saving && spellShadowGiftSacrificePercent != 0
			&& spellShadowGiftSacrificePercent != 10 && spellShadowGiftSacrificePercent != 20
			&& spellShadowGiftSacrificePercent != 30)
			throw std::runtime_error("Invalid Shadow Gift sacrifice choice");
	}

	template <typename Handler> void serializePrayerChoices(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_CAST_CHOSEN_PRAYERS))
		{
			if(!h.saving)
			{
				prayerChoices.clear();
				prayerRecipient = NO_PRAYER_RECIPIENT;
			}
			return;
		}
		h & prayerChoices;
		h & prayerRecipient;
		if(!h.saving && prayerChoices.size() > 2)
			throw std::runtime_error("Too many chosen prayers");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateWriteSerialization(h);
		h & side;
		h & stackNumber;
		h & actionType;
		h & spell;
		h & target;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PERFECT_MOMENT))
			h & perfectMoment;
		else if(!h.saving)
			perfectMoment = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE))
			h & spellOvercharge;
		else if(!h.saving)
			spellOvercharge = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SELECTIVE_DISPEL))
			h & spellSelectiveDispel;
		else if(!h.saving)
			spellSelectiveDispel = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CURE_AFFLICTION))
		{
			h & spellCureAffliction;
		}
		else if(!h.saving)
		{
			spellCureAffliction = SpellID::NONE;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
		{
			h & spellMassSlow;
		}
		else if(!h.saving)
		{
			spellMassSlow = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FIRE_WALL))
		{
			h & spellFireWallDirection;
		}
		else if(!h.saving)
		{
			spellFireWallDirection = BattleHex::NONE;
		}
		if(h.hasFeature(Handler::Version::HERO_COMMANDS))
		{
			h & command;
		}
		else if(!h.saving)
		{
			command = HeroCommand::NONE;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DEMONIC_RESERVE))
			h & gatingCreature;
		else if(!h.saving)
			gatingCreature = CreatureID();
		else if(gatingCreature.hasValue())
			throw std::runtime_error("Cannot serialize Demonic Gating to an older protocol");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_ARCHERY_SKIRMISHER))
			h & archerySkirmisherAttack;
		else if(!h.saving)
			archerySkirmisherAttack = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SHADOW_GIFT))
			h & spellShadowGiftSacrificePercent;
		else if(!h.saving)
			spellShadowGiftSacrificePercent = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PURIFY))
			h & spellPurifyChoices;
		else if(!h.saving)
			spellPurifyChoices.clear();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN))
			h & spellCurePhysicalAffliction;
		else if(!h.saving)
			spellCurePhysicalAffliction.clear();
		serializePrayerChoices(h);
		if(!h.saving && !spellCurePhysicalAffliction.empty())
			validatePhysicalCureSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN),
				h.hasFeature(Handler::Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES));
		if(!h.saving && command == HeroCommand::FOCUS_FIRE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
			throw std::runtime_error("Targeted command requires the new protocol");
		if(!h.saving && spell == SpellID(SpellID::LAND_MINE)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_LAND_MINE))
			throw std::runtime_error("Multi-hex Land Mine action requires the new protocol");
		if(!h.saving && spell == SpellID(SpellID::QUICKSAND)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_QUICKSAND))
			throw std::runtime_error("Multi-hex Quicksand action requires the new protocol");
		if(!h.saving && spellShadowGiftSacrificePercent != 0
			&& spellShadowGiftSacrificePercent != 10 && spellShadowGiftSacrificePercent != 20
			&& spellShadowGiftSacrificePercent != 30)
			throw std::runtime_error("Invalid Shadow Gift sacrifice choice");
		if(!h.saving && (command == HeroCommand::RIPOSTE || command == HeroCommand::BRACE
			|| command == HeroCommand::PROTECT || command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
			throw std::runtime_error("Canonical Order requires the new protocol");
	}

	struct DestinationInfo
	{
		int32_t unitValue;
		BattleHex hexValue;
		bool operator==(const DestinationInfo &) const = default;

		template <typename Handler> void serialize(Handler & h)
		{
			h & unitValue;
			h & hexValue;
		}
	};

	std::vector<DestinationInfo> target;
};

DLL_EXPORT std::ostream & operator<<(std::ostream & os, const BattleAction & ba); //todo: remove
