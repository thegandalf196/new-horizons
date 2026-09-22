/*
 * BattleActionsController.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/battle/CBattleInfoCallback.h"
#include "MagicArrowOverchargeWindow.h"
#include "SelectiveDispelWindow.h"
#include "TemporalFieldWindow.h"

#include <functional>
#include <optional>
#include <vector>

class BattleAction;
class CStack;
namespace spells {
class Caster;
enum class Mode;
}

class BattleInterface;

using MagicArrowOverchargeFactory = std::function<std::optional<MagicArrowOverchargeContext>(
	const BattleAction &, const BattleHex &, const CStack *)>;
using SelectiveDispelFactory = std::function<std::optional<SelectiveDispelContext>(const BattleAction &, const CStack *)>;
using TemporalFieldFactory = std::function<std::optional<TemporalFieldContext>(const BattleAction &)>;

/// Class that controls actions that can be performed by player, e.g. moving stacks, attacking, etc
/// As well as all relevant feedback for these actions in user interface
class BattleActionsController
{
	BattleInterface & owner;
	
	/// all actions possible to call at the moment by player
	std::vector<PossiblePlayerBattleAction> possibleActions;

	/// spell for which player's hero is choosing destination
	std::shared_ptr<BattleAction> heroSpellToCast;

	/// Optional New Horizons adapter.  Empty preserves the legacy generic cast
	/// path; Runtime installs it only for an admitted V2 Magic Arrow battle.
	MagicArrowOverchargeFactory magicArrowOverchargeFactory;
	/// Optional post-target Selective Dispel prompt.
	SelectiveDispelFactory selectiveDispelFactory;
	/// Optional pre-target Temporal Field choice for Sorcery Slow.
	TemporalFieldFactory temporalFieldFactory;

	// targets of multi-target spells cast by monsters
	std::vector<BattleHex> monsterSpellTargets;

	// the monster that casts the spell 
	const CStack * monsterCaster = nullptr;

	/// cached message that was set by this class in status bar
	std::string currentConsoleMsg;

	/// if true, active stack could possibly cast some target spell
	std::vector<const CSpell *> creatureSpells;
	/// The next Hero Spell selected from the spellbook is an authoritative
	/// immediate Metamagic follow-up.  The pending sequence itself lives in the
	/// battle snapshot; this flag only carries the user's current UI intent.
	bool metamagicFollowupMode = false;
	/// Explicit Expert Grand Metamagic choice for the first follow-up.  It is
	/// reset whenever the prompt/cast ends and never mutates battle state.
	bool metamagicGrandMode = false;

	/// stack that has been selected as first target for multi-target spells (Teleport & Sacrifice)
	const CStack * selectedStack;

	/// Ordered player selection for the canonical New Horizons Land Mine.  The
	/// order is preserved all the way into BattleAction::target; the server
	/// still validates the complete request before applying it.
	std::vector<BattleHex> landMineSelectedHexes;

	/// Two-click selector state for canonical New Horizons Fire Wall.  The
	/// first click chooses the line's start; the second click chooses one of
	/// its six adjacent directions.  Only the compact start+direction action is
	/// sent to the server.
	BattleHex fireWallSelectedStart = BattleHex::INVALID;

	/// Battlefield selector for a targeted New Horizons Order.  This is a
	/// presentation-only request mode: the authoritative callback is queried
	/// again for every hover/click and the submitted action contains only the
	/// selected unit identities.
	std::optional<HeroCommand> selectedHeroOrderCommand;
	std::optional<uint32_t> heroOrderTargetingFirst;
	/// Presentation-only reserve choice for the next Demonic Gate placement.
	CreatureID demonicGatingCreature;
	/// Mobile Gate's first battlefield click. INVALID means the player is still
	/// choosing where the acting stack moves before placing the Gate.
	BattleHex demonicGatingMovement = BattleHex::INVALID;

	bool isCastingPossibleHere (const CSpell * spell, const CStack *shere, const BattleHex & myNumber);
	std::vector<PossiblePlayerBattleAction> getPossibleActionsForStack (const CStack *stack) const; //called when stack gets its turn
	void reorderPossibleActionsPriority(const CStack * stack, const CStack * targetStack);

	bool actionIsLegal(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);

	void actionSetCursor(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);
	void actionSetCursorBlocked(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);

	std::string actionGetStatusMessage(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);
	std::string actionGetStatusMessageBlocked(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);

	void actionRealize(PossiblePlayerBattleAction action, const BattleHex & hoveredHex);

	PossiblePlayerBattleAction selectAction(const BattleHex & myNumber);

	const CStack * getStackForHex(const BattleHex & myNumber) ;

	/// attempts to initialize spellcasting action for stack
	/// will silently return if stack is not a spellcaster
	void tryActivateStackSpellcasting(const CStack *casterStack);

	/// returns spell that is currently being cast by hero or nullptr if none
	const CSpell * getHeroSpellToCast() const;

	/// if current stack is spellcaster, returns spell being cast, or null othervice
	const CSpell * getStackSpellToCast(const BattleHex & hoveredHex);

	/// returns true if current stack is a spellcaster
	bool isActiveStackSpellcaster() const;

	bool landMinePlacementTargetsValid() const;
	void updateLandMinePlacementStatus(const BattleHex & hoveredHex);
	void selectOrUndoLandMineHex(const BattleHex & clickedHex);
	void updateFireWallPlacementStatus(const BattleHex & hoveredHex);
	void selectFireWallStartOrDirection(const BattleHex & clickedHex);
	bool fireWallPlacementLineIsLegal(const BattleHex & start, BattleHex::EDir direction) const;
	bool heroOrderTargetingContextIsCurrent() const;
	std::vector<uint32_t> heroOrderTargetIds() const;
	bool heroOrderTargetIdIsLegal(uint32_t unitId) const;
	void updateHeroOrderTargetingStatus(const BattleHex & hoveredHex);
	void selectHeroOrderTarget(const BattleHex & clickedHex);

public:
	BattleActionsController(BattleInterface & owner);

	/// initialize list of potential actions for new active stack
	void activateStack();

	/// returns true if UI is currently in hero spell target selection mode
	bool heroSpellcastingModeActive() const;
	/// True only for the state-backed canonical New Horizons Land Mine.  Legacy
	/// Land Mine continues to use the ordinary generic spell selector.
	bool landMinePlacementModeActive() const;
	/// Number of hexes required by the active canonical Land Mine cast.
	int landMinePlacementRequiredHexes() const;
	/// Number-only readiness gate for the explicit confirmation shortcut.
	bool landMinePlacementReady() const;
	/// Current ordered selection, for battlefield presentation and tests.
	const std::vector<BattleHex> & landMinePlacementSelectedHexes() const;
	/// Return whether a hex is currently an empty legal placement candidate.
	bool landMinePlacementHexIsLegal(const BattleHex & hex) const;
	/// Return whether a hex is already in the ordered selection.
	bool landMinePlacementHexIsSelected(const BattleHex & hex) const;
	/// Return all currently legal empty placement candidates.
	BattleHexArray getLandMinePlacementLegalHexes() const;

	/// True only for the saved-ruleset canonical Fire Wall selector.
	bool fireWallPlacementModeActive() const;
	bool fireWallPlacementStartSelected() const;
	BattleHex fireWallPlacementStart() const;
	bool fireWallPlacementStartIsLegal(const BattleHex & hex) const;
	bool fireWallPlacementEndpointIsLegal(const BattleHex & hex) const;
	BattleHexArray getFireWallPlacementLegalStartHexes() const;
	BattleHexArray getFireWallPlacementLegalEndpoints() const;

	/// Start/cancel the direct battlefield selector used by targeted Orders.
	/// No battle state is changed until the callback receives the final action.
	bool beginHeroOrderTargeting(HeroCommand command);
	bool heroOrderTargetingModeActive() const;
	HeroCommand heroOrderTargetingCommand() const;
	bool heroOrderTargetingFirstSelected() const;
	BattleHexArray getHeroOrderTargetingLegalHexes() const;
	BattleHexArray getHeroOrderTargetingSelectedHexes() const;
	bool heroOrderTargetingHexIsLegal(const BattleHex & hex) const;
	void cancelHeroOrderTargeting();

	/// Confirm the exact selection after revalidating the live battle snapshot.
	void confirmLandMinePlacement();
	/// Remove the most recently selected hex without spending the hero action.
	void undoLandMinePlacement();
	/// returns true if UI is currently in "F" hotkey creature spell target selection mode
	bool creatureSpellcastingModeActive() const;

	/// returns true if one of the following is true:
	/// - we are casting spell by hero
	/// - we are casting spell by creature in targeted mode (F hotkey)
	/// - current creature is spellcaster and preferred action for current hex is spellcast
	bool currentActionSpellcasting(const BattleHex & hoveredHex);

	/// returns true if current hex action is "walk and spellcast" with active stack
	bool currentActionWalkAndCast(const BattleHex& hoveredHex);

	/// returns true if currently selected action allows long weapon reach for melee attacks
	bool currentActionUsesLongWeapon(const BattleHex & hoveredHex);

	/// enter targeted spellcasting mode for creature, e.g. via "F" hotkey
	void enterCreatureCastingMode();

	/// initialize hero spellcasting mode, e.g. on selecting spell in spellbook
	void castThisSpell(SpellID spellID);
	/// Enter the spellbook for a pending authoritative Metamagic follow-up.
	void beginMetamagicFollowup();
	bool metamagicFollowupModeActive() const;
	void toggleMetamagicGrandFollowup();
	bool metamagicGrandModeActive() const;

	/// Install the authority-backed post-target Magic Arrow UI adapter.  The
	/// adapter owns all formula, target identity, cost and request validation;
	/// this controller only decides when the normal targeted cast may pause for
	/// the compact overcharge window.
	void setMagicArrowOverchargeFactory(MagicArrowOverchargeFactory factory);
	void setSelectiveDispelFactory(SelectiveDispelFactory factory);
	void setTemporalFieldFactory(TemporalFieldFactory factory);

	/// Continue the ordinary Slow path after the Temporal Field modal chose
	/// Ordinary. This preserves the existing target-selection behavior.
	bool continueOrdinarySpellcast();

	/// ends casting spell (eg. when spell has been cast or canceled)
	void endCastingSpell();

	/// update cursor and status bar according to new active hex
	void onHexHovered(const BattleHex & hoveredHex);

	/// called when cursor is no longer over battlefield and cursor/battle log should be reset
	void onHoverEnded();

	/// performs action according to selected hex
	void onHexLeftClicked(const BattleHex & clickedHex);

	/// performs action according to selected hex
	void onHexRightClicked(const BattleHex & clickedHex);

	const spells::Caster * getCurrentSpellcaster() const;
	const CSpell * getCurrentSpell(const BattleHex & hoveredHex);
	spells::Mode getCurrentCastMode() const;

	/// New Horizons Transfigure Matter targets ordinary visible scenery only.
	/// These helpers keep the client overlay and click-time legality check in
	/// lockstep without changing the authoritative spell rules.
	static bool isTransfigureMatterSpell(const CSpell * spell);
	bool isValidTransfigureMatterTarget(const BattleHex & targetHex) const;
	BattleHexArray getTransfigureMatterTargetHexes(const CSpell * spell);

	/// methods to work with array of possible actions, needed to control special creatures abilities
	const std::vector<PossiblePlayerBattleAction> & getPossibleActions() const;
	
	/// sets list of high-priority actions that should be selected before any other actions
	void setPriorityActions(const std::vector<PossiblePlayerBattleAction> &);
	void selectDemonicGatingCreature(CreatureID creature);

	/// resets possible actions to original state
	void resetCurrentStackPossibleActions();
};
