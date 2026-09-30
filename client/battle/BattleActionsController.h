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
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "MagicArrowOverchargeWindow.h"
#include "SelectiveDispelWindow.h"
#include "ShadowGiftWindow.h"
#include "TemporalFieldWindow.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class BattleAction;
class CStack;
namespace spells {
class Caster;
enum class Mode;
}

class BattleInterface;

struct StormOfDaggersTargetPreview
{
	uint32_t unitId = 0;
	std::string name;
	std::optional<int64_t> projectedDamage;
};

struct StormOfDaggersSelectionPreview
{
	int32_t selectedTargetCount = 0;
	int32_t maximumTargetCount = 5;
	int64_t totalDamagePool = 0;
	int64_t rawDamagePerTarget = 0;
	bool poolAvailable = false;
	bool canConfirm = false;
	std::string status;
	std::vector<StormOfDaggersTargetPreview> targets;
};

struct SoulChainTargetPreview
{
	uint32_t unitId = 0;
	std::string name;
};

struct SoulChainSelectionPreview
{
	int32_t selectedTargetCount = 0;
	int32_t maximumTargetCount = newHorizonsSoulChain::MAX_TARGETS;
	bool canConfirm = false;
	std::string status;
	std::vector<SoulChainTargetPreview> targets;
};

struct VengefulVinesSelectionPreview
{
	BattleHex origin = BattleHex::INVALID;
	BattleHex::EDir orientation = BattleHex::RIGHT;
	int32_t affectedEnemyStacks = 0;
	bool originSelected = false;
	bool pathFits = false;
	bool canConfirm = false;
	std::string status;
};

using MagicArrowOverchargeFactory = std::function<std::optional<MagicArrowOverchargeContext>(
	const BattleAction &, const BattleHex &, const CStack *)>;
using ShadowGiftFactory = std::function<std::optional<ShadowGiftContext>(
	const BattleAction &, const BattleHex &, const CStack *)>;
using SelectiveDispelFactory = std::function<std::optional<SelectiveDispelContext>(const BattleAction &, const CStack *)>;
using PurifyPicker = std::function<bool(const BattleAction &, const BattleHex &)>;
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
	ShadowGiftFactory shadowGiftFactory;
	/// Optional post-target Selective Dispel prompt.
	SelectiveDispelFactory selectiveDispelFactory;
	/// Optional post-center Purify effect-group picker.
	PurifyPicker purifyPicker;
	std::function<bool(const BattleAction &, const CStack *)> cureAfflictionPicker;
	uint64_t castingSession = 0;
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

	/// stack that has been selected as first target for multi-target spells (Teleport & Sacrifice)
	const CStack * selectedStack;

	/// Ordered player selection for canonical repeated-placement spells. The
	/// order is preserved all the way into BattleAction::target; the server
	/// still validates the complete request before applying it.
	std::vector<BattleHex> repeatedPlacementSelectedHexes;
	/// Ordered unit identities for New Horizons Storm of Daggers.  This is a
	/// presentation-only selection; all identities and full-vector legality are
	/// checked again before the ordinary hero spell request is sent.
	std::vector<uint32_t> stormOfDaggersSelectedUnitIds;
	BattleID stormOfDaggersBattleID;
	std::optional<PlayerColor> stormOfDaggersPlayer;
	BattleSide stormOfDaggersSide = BattleSide::NONE;
	int32_t stormOfDaggersRound = -1;
	ObjectInstanceID stormOfDaggersHeroID = ObjectInstanceID::NONE;
	/// Ordered player selection for canonical New Horizons Soul Chain. The first
	/// live unit identity is the primary; following IDs are optional secondaries.
	std::vector<uint32_t> soulChainSelectedUnitIds;
	BattleID soulChainBattleID;
	std::optional<PlayerColor> soulChainPlayer;
	BattleSide soulChainSide = BattleSide::NONE;
	int32_t soulChainRound = -1;
	ObjectInstanceID soulChainHeroID = ObjectInstanceID::NONE;
	/// Ordered enemy/friendly identities selected for New Horizons Life Drain.
	std::vector<uint32_t> lifeDrainSelectedUnitIds;
	BattleID lifeDrainBattleID;
	std::optional<PlayerColor> lifeDrainPlayer;
	BattleSide lifeDrainSide = BattleSide::NONE;
	int32_t lifeDrainRound = -1;
	ObjectInstanceID lifeDrainHeroID = ObjectInstanceID::NONE;

	/// Two-click selector state for canonical New Horizons Fire Wall.  The
	/// first click chooses the line's start; the second click chooses one of
	/// its six adjacent directions.  Only the compact start+direction action is
	/// sent to the server.
	BattleHex fireWallSelectedStart = BattleHex::INVALID;
	/// Explicit origin/orientation selection for saved-v3 New Horizons Vengeful
	/// Vines. The spell action is sent only after a separate Confirm.
	BattleHex vengefulVinesOrigin = BattleHex::INVALID;
	BattleHex::EDir vengefulVinesOrientation = BattleHex::RIGHT;
	BattleID vengefulVinesBattleID;
	std::optional<PlayerColor> vengefulVinesPlayer;
	BattleSide vengefulVinesSide = BattleSide::NONE;
	int32_t vengefulVinesRound = -1;
	ObjectInstanceID vengefulVinesHeroID = ObjectInstanceID::NONE;

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
	/// Two-click selector for a player-chosen Skirmisher destination.
	BattleHex skirmisherTargetHex = BattleHex::INVALID;
	/// The move-and-fire target and destination candidates are stable while this
	/// action is selected. Cache them instead of running pathfinding on each UI hover.
	mutable bool skirmisherTargetHexesCached = false;
	mutable BattleHexArray skirmisherTargetHexesCache;
	mutable bool skirmisherFiringHexesCached = false;
	mutable BattleHexArray skirmisherFiringHexesCache;
	mutable ReachabilityInfo::TDistances skirmisherFiringDistancesCache{};

	void invalidateSkirmisherTargetCache();
	void invalidateSkirmisherFiringCache();

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

	bool repeatedPlacementTargetsValid() const;
	void updateRepeatedPlacementStatus(const BattleHex & hoveredHex);
	void selectOrUndoRepeatedPlacementHex(const BattleHex & clickedHex);
	void updateFireWallPlacementStatus(const BattleHex & hoveredHex);
	void selectFireWallStartOrDirection(const BattleHex & clickedHex);
	bool fireWallPlacementLineIsLegal(const BattleHex & start, BattleHex::EDir direction) const;
	bool vengefulVinesSelectionContextIsCurrent() const;
	bool vengefulVinesTargetsAreLegal(const BattleHex & origin, BattleHex::EDir direction) const;
	int32_t vengefulVinesEnemyTargetCount(const BattleHexArray & footprint) const;
	void updateVengefulVinesStatus(const BattleHex & hoveredHex);
	void selectVengefulVinesOriginOrOrientation(const BattleHex & clickedHex);
	bool stormOfDaggersSelectionContextIsCurrent() const;
	bool stormOfDaggersTargetIsLegal(uint32_t unitId) const;
	bool stormOfDaggersTargetsAreLegal(const std::vector<uint32_t> & unitIds) const;
	void updateStormOfDaggersSelectionStatus(const BattleHex & hoveredHex);
	void selectStormOfDaggersTarget(const BattleHex & clickedHex);
	bool soulChainSelectionContextIsCurrent() const;
	bool soulChainTargetsAreLegal(const std::vector<uint32_t> & unitIds) const;
	bool soulChainTargetIsLegal(uint32_t unitId) const;
	void updateSoulChainSelectionStatus(const BattleHex & hoveredHex);
	void selectSoulChainTarget(const BattleHex & clickedHex);
	bool lifeDrainTargetSelectionModeActive() const;
	bool lifeDrainSelectionContextIsCurrent() const;
	bool lifeDrainTargetsAreLegal(const std::vector<uint32_t> & unitIds) const;
	bool lifeDrainTargetIsLegal(uint32_t unitId) const;
	bool lifeDrainTargetHexIsLegal(const BattleHex & hex) const;
	void updateLifeDrainSelectionStatus(const BattleHex & hoveredHex);
	void selectLifeDrainTarget(const BattleHex & clickedHex);
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
	/// True only for a saved-v3 Quicksand row carrying the selected-placement
	/// marker. Markerless v1/v2 and v3 battles keep the legacy random cast.
	bool quicksandPlacementModeActive() const;
	/// True while the human is choosing exact hexes for Land Mine or selected
	/// mode Quicksand.
	bool repeatedPlacementModeActive() const;
	/// Number of hexes required by the active repeated-placement spell.
	int repeatedPlacementRequiredHexes() const;
	/// Number-only readiness gate for the explicit confirmation shortcut.
	bool repeatedPlacementReady() const;
	/// Current ordered selection, for battlefield presentation and tests.
	const std::vector<BattleHex> & getRepeatedPlacementSelectedHexes() const;
	/// Return whether a hex is currently an empty legal placement candidate for
	/// the active repeated-placement spell.
	bool repeatedPlacementHexIsLegal(const BattleHex & hex) const;
	/// Return whether a hex is already in the ordered selection.
	bool repeatedPlacementHexIsSelected(const BattleHex & hex) const;
	/// Return all currently legal empty placement candidates.
	BattleHexArray getRepeatedPlacementLegalHexes() const;

	/// Storm of Daggers uses ordered live stack IDs, not hexes, to avoid
	/// retargeting a moved stack if the battlefield changes during selection.
	bool stormOfDaggersTargetSelectionModeActive() const;
	const std::vector<uint32_t> & stormOfDaggersSelectedTargetIds() const;
	int stormOfDaggersSelectionOrder(uint32_t unitId) const;
	bool stormOfDaggersTargetHexIsLegal(const BattleHex & hex) const;
	StormOfDaggersSelectionPreview getStormOfDaggersSelectionPreview() const;
	void confirmStormOfDaggersTargets();
	void undoStormOfDaggersTarget();
	/// Soul Chain selects an ordered primary followed by up to two secondary IDs.
	bool soulChainTargetSelectionModeActive() const;
	const std::vector<uint32_t> & soulChainSelectedTargetIds() const;
	int soulChainSelectionOrder(uint32_t unitId) const;
	bool soulChainTargetHexIsLegal(const BattleHex & hex) const;
	SoulChainSelectionPreview getSoulChainSelectionPreview() const;
	void confirmSoulChainTargets();
	void undoSoulChainTarget();

	/// True only for the saved-ruleset canonical Fire Wall selector.
	bool fireWallPlacementModeActive() const;
	bool fireWallPlacementStartSelected() const;
	BattleHex fireWallPlacementStart() const;
	bool fireWallPlacementStartIsLegal(const BattleHex & hex) const;
	bool fireWallPlacementEndpointIsLegal(const BattleHex & hex) const;
	BattleHexArray getFireWallPlacementLegalStartHexes() const;
	BattleHexArray getFireWallPlacementLegalEndpoints() const;

	/// Saved-v3 Vengeful Vines selects a battlefield origin and one of six
	/// directions, then requires explicit confirmation of the complete footprint.
	bool vengefulVinesTargetSelectionModeActive() const;
	bool vengefulVinesOriginSelected() const;
	BattleHex vengefulVinesSelectedOrigin() const;
	bool vengefulVinesOriginIsLegal(const BattleHex & hex) const;
	bool vengefulVinesEndpointIsLegal(const BattleHex & hex) const;
	BattleHexArray getVengefulVinesLegalStartHexes() const;
	BattleHexArray getVengefulVinesRotationHexes() const;
	BattleHexArray getVengefulVinesPreviewFootprint() const;
	VengefulVinesSelectionPreview getVengefulVinesSelectionPreview() const;
	void rotateVengefulVinesOrientation();
	void confirmVengefulVines();

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
	void confirmRepeatedPlacement();
	/// Remove the most recently selected hex without spending the hero action.
	void undoRepeatedPlacement();
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

	/// Install the authority-backed post-target Magic Arrow UI adapter.  The
	/// adapter owns all formula, target identity, cost and request validation;
	/// this controller only decides when the normal targeted cast may pause for
	/// the compact overcharge window.
	void setMagicArrowOverchargeFactory(MagicArrowOverchargeFactory factory);
	void setShadowGiftFactory(ShadowGiftFactory factory);
	void setSelectiveDispelFactory(SelectiveDispelFactory factory);
	void setPurifyPicker(PurifyPicker picker);
	void setCureAfflictionPicker(std::function<bool(const BattleAction &, const CStack *)> picker);
	uint64_t getCastingSession() const { return castingSession; }
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

	/// New Horizons Summon Trolls targets one legal empty battlefield hex.
	/// Use mechanics validation for the selectable placement overlay.
	static bool isSummonTrollsSpell(const CSpell * spell);
	BattleHexArray getSummonTrollsTargetHexes(const CSpell * spell);

	/// New Horizons Verdant Prison previews the legal spawned ring returned by
	/// the shared spell effect geometry for the currently hovered enemy stack.
	static bool isVerdantPrisonSpell(const CSpell * spell);
	BattleHexArray getVerdantPrisonTargetHexes(const CSpell * spell, const BattleHex & targetHex);

	/// methods to work with array of possible actions, needed to control special creatures abilities
	const std::vector<PossiblePlayerBattleAction> & getPossibleActions() const;
	
	/// sets list of high-priority actions that should be selected before any other actions
	void setPriorityActions(const std::vector<PossiblePlayerBattleAction> &);
	void selectDemonicGatingCreature(CreatureID creature);
	bool skirmisherActionModeActive() const;
	const BattleHexArray & getSkirmisherLegalTargetHexes() const;
	const BattleHexArray & getSkirmisherLegalFiringHexes() const;

	/// resets possible actions to original state
	void resetCurrentStackPossibleActions();
};
