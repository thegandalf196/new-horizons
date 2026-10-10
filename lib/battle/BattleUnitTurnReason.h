/*
 * BattleUnitTurnReason.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

enum class BattleUnitTurnReason : int8_t
{
	/// Unit gained turn due to becoming first unit in turn queue
	TURN_QUEUE,
	/// Unit gained turn due to morale triggering
	MORALE,
	/// Unit (re)gained	turn due to hero casting a spell while this unit is active
	HERO_SPELLCAST,
	/// Unit gained turn due to casting a spell while having ability to cast spells without spending turn
	UNIT_SPELLCAST,
	/// Unit gained turn for automatic action, player can not select action for this unit
	AUTOMATIC_ACTION,
	/// Hero issued a command; control returns without beginning a new creature turn
	HERO_COMMAND,
	/// Server rejected a submitted action and is returning UI control to the
	/// already-active unit. This is not a new activation and has no lifecycle.
	ACTION_REJECTED,
	/// The first Master Gate continues the same creature activation after Gate.
	/// This does not begin a new activation or expire activation-scoped state.
	MASTER_GATE_CONTINUATION,
	/// Pursuit continues the same activation for movement only after a lethal
	/// melee attack. It does not refresh action resources or lifecycle state.
	PURSUIT_CONTINUATION,
	/// Master Gunner offers the earned second Ballista shot within this activation.
	/// This does not begin a new activation or expire activation-scoped state.
	RANGED_ATTACK_CONTINUATION,
	/// Quartermaster grants a genuine second activation with reduced output.
	REDUCED_EXTRA_ACTIVATION,
	/// Input-only anchor for an off-turn Crisis Order, not a creature activation.
	CRISIS_ORDER,
	/// Restores a completed action's actor without input or activation lifecycles.
	CRISIS_RESUME
};
