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
	HeroCommand command = HeroCommand::NONE;

	BattleAction();
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

	template <typename Handler> void serialize(Handler & h)
	{
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
		h & side;
		h & stackNumber;
		h & actionType;
		h & spell;
		h & target;
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
		if(h.hasFeature(Handler::Version::HERO_COMMANDS))
		{
			h & command;
		}
		else if(!h.saving)
		{
			command = HeroCommand::NONE;
		}
		if(!h.saving && command == HeroCommand::FOCUS_FIRE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
			throw std::runtime_error("Targeted command requires the new protocol");
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
