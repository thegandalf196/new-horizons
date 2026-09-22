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
	/// Additional mana selected for Sorcery Magic Arrow.  Zero preserves the
	/// ordinary fixed-cost cast and all legacy action semantics.
	si32 spellOvercharge = 0;
	/// Requests the target-aware Selective Dispel mode. The server validates
	/// the spell, hero and saved perk before accepting this optional mode.
	bool spellSelectiveDispel = false;
	/// Requests the once-per-combat Sorcery Temporal Field variant of Slow.
	/// The server validates perk ownership, availability and cost.
	bool spellMassSlow = false;
	/// Request only: spent by the authority when the first declared strike occurs.
	bool perfectMoment = false;
	/// Canonical New Horizons Fire Wall placement direction.  The action's
	/// target contains the selected start hex; the server derives and validates
	/// the remaining two line hexes from this direction before casting.
	BattleHex::EDir spellFireWallDirection = BattleHex::NONE;
	/// Requests the immediate additional spell granted by Tower Metamagic.  The
	/// server accepts this only while its saved battle snapshot has a pending
	/// sequence; it never buys another Hero Action or chains recursively.
	bool metamagicFollowup = false;
	/// Chooses the Expert Grand Metamagic variant for the first additional
	/// spell in this offered sequence.  The choice is explicit: merely opening
	/// the follow-up prompt never consumes a use or reserves Grand.
	bool metamagicGrand = false;
	/// Explicitly declines the currently pending Metamagic sequence.  This is
	/// validated by the server and clears only the immediate sequence.  An
	/// initial decline leaves the use available; declining the second leg of a
	/// Grand sequence preserves the already-accepted use and may carry Formula
	/// Reserve's server-derived refund below.
	bool metamagicDecline = false;
	/// Server-authored marker for an owner-authenticated pass at a visible Time
	/// Stop Hero Action boundary. Incoming clients may never set this flag. It is
	/// replicated in StartAction so every game-state copy expires the same origin,
	/// while synthetic AUTOMATIC_ACTION no-ops remain ordinary NO_ACTION actions.
	bool timeStopHeroActionPass = false;
	/// Server-derived Formula Reserve refund attached to a Decline/End after
	/// at least one additional spell of a Grand sequence resolved.  Clients may
	/// never author this value; the action processor fills it from saved state.
	si32 metamagicManaRefund = 0;
	HeroCommand command = HeroCommand::NONE;
	/// Real Inferno reserve stack selected for Demonic Gating. The entire
	/// currently available stack is committed; no creatures are created.
	CreatureID gatingCreature;

	BattleAction();
	/// Explicitly closes a control-visible Time Stop activation without
	/// changing the stack. The server accepts this only for the active stopped
	/// stack; it is not a creature action or a way around stasis.
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
	static BattleAction makeMetamagicDecline(BattleSide side);

	bool isTacticsAction() const;
	bool isUnitAction() const;
	bool isSpellAction() const;
	bool isBattleEndAction() const;
	std::string toString() const;

	void aimToHex(const BattleHex & destination);
	void aimToUnit(const battle::Unit * destination);

	battle::Target getTarget(const CBattleInfoCallback * cb) const;
	void setTarget(const battle::Target & target_);

	template <typename Handler> void serialize(Handler & h)
	{
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
		if(h.saving && spellMassSlow
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
			throw std::runtime_error("Cannot serialize Temporal Field to an older protocol");
		if(h.saving && spellFireWallDirection != BattleHex::NONE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_FIRE_WALL))
			throw std::runtime_error("Cannot serialize Fire Wall direction to an older protocol");
		if(h.saving && metamagicFollowup && !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot serialize Metamagic follow-up to an older protocol");
		if(h.saving && metamagicGrand && !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot serialize Metamagic Grand choice to an older protocol");
		if(h.saving && metamagicDecline && !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot serialize Metamagic decline to an older protocol");
		if(h.saving && metamagicManaRefund != 0 && !h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
			throw std::runtime_error("Cannot serialize Metamagic Formula Reserve refund to an older protocol");
		if(h.saving && timeStopHeroActionPass
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS))
			throw std::runtime_error("Cannot serialize Time Stop Hero Action pass to an older protocol");
		if(h.saving && spell == SpellID(SpellID::LAND_MINE)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_LAND_MINE))
			throw std::runtime_error("Cannot serialize multi-hex Land Mine action to an older protocol");
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
		{
			h & spellOvercharge;
		}
		else if(!h.saving)
		{
			spellOvercharge = 0;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SELECTIVE_DISPEL))
		{
			h & spellSelectiveDispel;
		}
		else if(!h.saving)
		{
			spellSelectiveDispel = false;
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
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
		{
			h & metamagicFollowup;
			h & metamagicGrand;
		}
		else if(!h.saving)
		{
			metamagicFollowup = false;
			metamagicGrand = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_METAMAGIC))
		{
			h & metamagicDecline;
			h & metamagicManaRefund;
		}
		else if(!h.saving)
		{
			metamagicDecline = false;
			metamagicManaRefund = 0;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS))
		{
			h & timeStopHeroActionPass;
		}
		else if(!h.saving)
		{
			timeStopHeroActionPass = false;
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
		if(!h.saving && command == HeroCommand::FOCUS_FIRE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
			throw std::runtime_error("Targeted command requires the new protocol");
		if(!h.saving && spell == SpellID(SpellID::LAND_MINE)
			&& target.size() > 1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_LAND_MINE))
			throw std::runtime_error("Multi-hex Land Mine action requires the new protocol");
		if(!h.saving && (command == HeroCommand::RIPOSTE || command == HeroCommand::BRACE
			|| command == HeroCommand::PROTECT || command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
			throw std::runtime_error("Canonical Order requires the new protocol");
	}

	struct DestinationInfo
	{
		int32_t unitValue;
		BattleHex hexValue;

		template <typename Handler> void serialize(Handler & h)
		{
			h & unitValue;
			h & hexValue;
		}
	};

	std::vector<DestinationInfo> target;
};

DLL_EXPORT std::ostream & operator<<(std::ostream & os, const BattleAction & ba); //todo: remove
