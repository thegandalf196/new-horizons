/*
 * CBattleInfoCallback.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <vcmi/spells/Magic.h>

#include "ReachabilityInfo.h"
#include "BattleAttackInfo.h"
#include "RelentlessAssaultState.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "BattleUnitTurnReason.h"
#include "ReducedExtraActivationState.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"

class CGHeroInstance;
class CStack;
class ISpellCaster;
class SpellCastEnvironment;
class CSpell;
struct CObstacleInstance;
class IBonusBearer;
class PossiblePlayerBattleAction;

namespace vstd
{
class RNG;
}

namespace spells
{
	class Caster;
	class Spell;

	namespace effects
	{
		struct SpellEffectValue;
	}
}

struct DLL_LINKAGE AttackableTiles
{
	/// Hexes on which only hostile units will be targeted
	BattleHexArray hostileCreaturePositions;
	/// for Dragon Breath, hexes on which both friendly and hostile creatures will be targeted
	BattleHexArray friendlyCreaturePositions;
	/// for animation purposes, if any of targets are on specified positions, unit should play alternative animation
	BattleHexArray overrideAnimationPositions;
};

struct DLL_LINKAGE BattleClientInterfaceData
{
	std::vector<SpellID> creatureSpellsToCast;
	ui8 tacticsMode;
};

struct DLL_LINKAGE ForcedAction {
	EActionType type = EActionType::NO_ACTION;
	BattleHex position;
	const battle::Unit * target = nullptr;
};

using SpellEffectValUptr = std::unique_ptr<spells::effects::SpellEffectValue>;

class DLL_LINKAGE CBattleInfoCallback : public virtual CBattleInfoEssentials
{
public:

	/// Battle snapshot only; never falls back to world or installed settings.
	std::optional<newHorizonsCreatures::CreatureCategoryView> battleGetCreatureCategory(CreatureID creature) const;
	/// Whether a living, actual defensive tower is controlled by the saved Fortification Engineer perk.
	bool battleCanUseFortificationEngineer(const battle::Unit * turret) const;
	/// Whether this active Ballista has an earned ranged follow-up allowance.
	bool battleHasPendingRangedFollowUp(const battle::Unit * unit) const;
	/// Snapshot of one side's once-per-combat reduced activation allowance.
	ReducedExtraActivationState battleGetReducedExtraActivationState(BattleSide side) const;
	/// Output percentage for the unit currently carrying a reduced activation identity.
	int32_t battleGetActivationOutputPercent(const battle::Unit * unit) const;
	/// First Aid Tent raw healing after activation output modifiers, before target HP caps.
	int64_t battleGetFirstAidHealingOutput(const battle::Unit * healer) const;
	/// Raw Catapult structural output after activation modifiers, before wall HP caps.
	int32_t battleGetCatapultStructuralDamage(const battle::Unit * attacker, int32_t hitQuality = 1) const;
	/// Whether the earned shot can currently be used against at least one legal enemy.
	bool battleCanTakeRangedFollowUp(const battle::Unit * unit) const;
	/// Saved final damage multiplier for a pending Ballista shot, or 100 when none applies.
	int battleGetRangedFollowUpDamagePercent(const battle::Unit * unit) const;
	std::vector<SpellSchool> battleGetActiveSpellSchools() const;
	std::vector<SpellSchool> battleGetSpellSchools(SpellID spell) const;
	int battleGetSpellLevel(SpellID spell) const;
	bool battleUsesHeroCommands() const;
	bool battleCanUseHeroCommand(BattleSide side, HeroCommand command) const;
	bool battleCanBeginHeroCommand(BattleSide side, HeroCommand command) const;
	/// Shared real/hypothetical ammunition policy, including the off-field bank artifact.
	bool battleUnitHasAmmoCart(const battle::Unit * unit) const;
	/// Effective Morale for the current battle, including dynamic Standard Bearer adjacency.
	int battleGetMorale(const battle::Unit * unit) const;
	/// Effective turn-start FEARFUL chance after the current controller's Fearless protection.
	int battleGetFearChance(const battle::Unit * affected) const;
	bool battleCanConfirmHeroCommand(BattleSide side, HeroCommand command, uint32_t targetUnitId) const;
	std::vector<uint32_t> battleGetHeroCommandTargets(BattleSide side, HeroCommand command) const;
	std::optional<FocusFireState> battlePrepareFocusFireState(BattleSide side, uint32_t targetUnitId) const;
	std::optional<FocusFireState> battleGetFocusFireState(BattleSide side) const;
	/// Roll-only Luck; does not change the unit's displayed/static Luck bonuses.
	int battleGetAttackLuck(const battle::Unit * attacker, const battle::Unit * target, bool shooting) const;
	bool battleCanUsePerfectMoment(const battle::Unit * attacker) const;
	bool battleCanTriggerCleave(const battle::Unit * attacker) const;
	bool battleCanTriggerNoQuarter(const BattleAttackInfo & attack) const;
	const battle::Unit * battleSelectCleaveTarget(const battle::Unit * attacker,
		const battle::Unit * destroyed) const;
	int battleFortuneSpeed(const battle::Unit * unit) const;
	bool battleBeginsActivation(const battle::Unit * unit, BattleUnitTurnReason reason) const;
	std::vector<uint32_t> battleFortuneAdjacentFriends(const battle::Unit * unit) const;
	/// Expected luck damage used by the AI, without consuming RNG.
	int64_t battleExpectedLuckDamage(const BattleAttackInfo & attack) const;
	virtual std::optional<HeroOrderState> battleGetHeroOrderState(BattleSide side) const;
	/// Current cumulative physical creature damage percentage for the unit's side.
	int battleGetBloodrageDamagePercent(const battle::Unit * unit) const;
	/// Projectable side-local Relentless Assault state; hypothetical battles override this view.
	virtual const RelentlessAssaultState & battleGetRelentlessAssaultState(BattleSide side) const;
	/// Additive Expert Offense streak damage for an ordinary primary target.
	int battleGetRelentlessAssaultDamagePercent(const battle::Unit * attacker,
		const battle::Unit * primaryTarget) const;
	/// True for an ordinary hostile melee blow delivered from behind the defender.
	bool battleIsShroudFlankingAttack(const BattleAttackInfo & attack) const;
	/// Whether a living creature stack is protected by an allied formation at the
	/// attack's projected defender position (or its current position when omitted).
	bool battleHasFormationFightingProtection(const battle::Unit * defender,
		const BattleHex & assumedPosition = BattleHex::INVALID) const;
	/// Expert Shroud flanks deny the defender's normal retaliation.
	bool battleShroudDeniesRetaliation(const BattleAttackInfo & attack) const;
	/// Validates target coverage and snapshots all transient state for a canonical Order.
	std::optional<HeroOrderState> battlePrepareHeroOrderState(BattleSide side, HeroCommand command,
		const std::vector<uint32_t> & targetUnitIds) const;
	/// Returns the actual melee defender after a valid Protect interception, without consuming it.
	const battle::Unit * battleResolveHeroOrderTarget(const battle::Unit * attacker,
		const battle::Unit * defender, bool shooting) const;
	/// Number of melee attacks Protect can redirect for this saved hero snapshot.
	/// Ordinary Protect allows one; an active Shield Master perk allows two.
	int battleHeroOrderProtectInterceptionLimit(BattleSide side) const;
	/// Returns true only for an eligible unit still occupying its unbroken Hold anchor.
	bool battleIsHoldTheLineRecipient(const HeroOrderState & state, const battle::Unit * unit) const;
	/// Saved Iron Discipline reduction for an anchored, unbroken Hold recipient.
	int battleGetHoldTheLineMagicalReductionBasisPoints(const battle::Unit * unit) const;
	/// Returns whether Brace is armed for this defender and this qualifying incoming attack.
	bool battleCanTriggerHeroOrderBrace(const battle::Unit * attacker, const battle::Unit * defender,
		int movementDistance, bool shooting, bool counter) const;
	/// Distinct side used by a melee attack against a Flank target, or zero when not adjacent.
	uint8_t battleHeroOrderFlankSide(const battle::Unit * attacker, const battle::Unit * defender) const;
	/// Flank's per-additional-side bonus for this hero snapshot (Encirclement changes 4% to 7%).
	int battleHeroOrderFlankAdditionalSidePercent(BattleSide side, int warcastingBonusPercent = 0) const;
	/// Target liveness/hostility, not permission to issue again or a promise of available shots.
	bool battleIsFocusFireTargetActive(BattleSide side) const;
	bool battleIsTargetedRangedCommand(const battle::Unit * attacker, const battle::Unit * defender,
		bool shooting, bool secondaryAttack = false) const;
	int battleTargetedRangedCommandPercent(const battle::Unit * attacker, const battle::Unit * defender,
		bool shooting, bool secondaryAttack = false) const;
	HeroCommand battleGetActiveDoctrine(BattleSide side) const;
	HeroCommand battleGetActiveOrder(BattleSide side) const;
	const scripting::Pool & getScriptContextPool() const override;
	std::optional<BattleSide> battleIsFinished() const override; //return none if battle is ongoing; otherwise the victorious side (0/1) or 2 if it is a draw

	std::vector<std::shared_ptr<const CObstacleInstance>> battleGetAllObstaclesOnPos(const BattleHex & tile, bool onlyBlocking = true) const override;
	std::vector<std::shared_ptr<const CObstacleInstance>> getAllAffectedObstaclesByStack(const battle::Unit * unit, const BattleHexArray & passed) const override;
	//Handle obstacle damage here, requires SpellCastEnvironment
	bool handleObstacleTriggersForUnit(SpellCastEnvironment & spellEnv, const battle::Unit & unit, const BattleHexArray & passed = {}) const;

	const CStack * battleGetStackByPos(const BattleHex & pos, bool onlyAlive = true) const;

	const battle::Unit * battleGetUnitByPos(const BattleHex & pos, bool onlyAlive = true) const override;

	///returns all alive units excluding turrets
	battle::Units battleAliveUnits() const;
	///returns all alive units from particular side excluding turrets
	battle::Units battleAliveUnits(BattleSide side) const;

	void battleGetTurnOrder(std::vector<battle::Units> & out, const size_t maxUnits, const int maxTurns, const int turn = 0, BattleSide lastMoved = BattleSide::NONE) const;

	///returns reachable hexes (valid movement destinations), DOES contain stack current position (lite version)
	BattleHexArray battleGetAvailableHexes(const battle::Unit * unit, bool obtainMovementRange) const;
	BattleHexArray battleGetAvailableHexes(const ReachabilityInfo & cache, const battle::Unit * unit, bool obtainMovementRange) const;

	//returns hexes the unit can occupy, obtainMovementRange ignores tactics mode (for double-wide units includes both head and tail)
	BattleHexArray battleGetOccupiableHexes(const battle::Unit * unit, bool obtainMovementRange) const;
	BattleHexArray battleGetOccupiableHexes(const BattleHexArray & availableHexes, const battle::Unit * unit) const;
	//returns from which hex the attacker would attack the target from given direction; INVALID if not possible; the hex may be inccessible
	BattleHex fromWhichHexAttack(const battle::Unit * attacker, const BattleHex & target, const BattleHex::EDir & direction, bool allowLongWeapon = true) const;

	//returns to which hex the (head of) unit would move to occupy position (possibly by tail)
	BattleHex toWhichHexMove(const battle::Unit * unit, const BattleHex & position) const;
	BattleHex toWhichHexMove(const BattleHexArray & availableHexes, const battle::Unit * unit, const BattleHex & position) const;

	//return true iff attacker move towards and attack position from direction (spatial reasoning only)
	bool battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const;
	bool battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const; //reuse availableHexes on multiple calls
	bool battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position) const; //check all directions
	bool battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position) const; //reuse availableHexes on multiple calls

	int battleGetSurrenderCost(const PlayerColor & Player) const; //returns cost of surrendering battle, -1 if surrendering is not possible
	ReachabilityInfo::TDistances battleGetDistances(const battle::Unit * unit, const BattleHex & assumedPosition) const;
	BattleHexArray battleGetAttackedHexes(const battle::Unit * attacker, const BattleHex & destinationTile, const BattleHex & attackerPos = BattleHex::INVALID) const;
	bool isEnemyUnitWithinSpecifiedRange(const BattleHex & attackerPosition, const battle::Unit * defenderUnit, unsigned int range) const;
	bool isHexWithinSpecifiedRange(const BattleHex & attackerPosition, const BattleHex & targetPosition, unsigned int range) const;

	std::pair< BattleHexArray, int > getPath(const BattleHex & start, const BattleHex & dest, const battle::Unit * stack) const;

	bool battleCanTargetEmptyHex(const battle::Unit * attacker) const; //determines of stack with given ID can target empty hex to attack - currently used only for SPELL_LIKE_ATTACK shooting
	bool battleCanAttackUnit(const battle::Unit * attacker, const battle::Unit * target) const; //determines if attacker can attack target (no spatial reasoning)
	/// Legal move-then-shoot destinations for Skirmisher, limited to half the unit's current movement.
	BattleHexArray battleGetSkirmisherTargetHexes(const battle::Unit * attacker) const;
	BattleHexArray battleGetSkirmisherAttackFromHexes(const battle::Unit * attacker, const BattleHex & targetHex,
		ReachabilityInfo::TDistances * distances = nullptr) const;
	/// Exact legality test for a player-selected Skirmisher firing destination.
	bool battleCanSkirmisherAttackFromHex(const battle::Unit * attacker, const BattleHex & targetHex, const BattleHex & attackFromHex) const;
	bool battleCanShoot(const battle::Unit * attacker, const BattleHex & dest) const; //determines if stack with given ID shoot at the selected destination
	bool battleCanShoot(const battle::Unit * attacker) const; //determines if stack with given ID shoot in principle
	bool isLongWeaponAttack(const battle::Unit * attacker, const battle::Unit * defender) const;
	//hexes of the defender that the attacker can reach in melee; empty if no melee attack is possible
	BattleHexArray meleeAttackHexes(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPos = BattleHex::INVALID, const BattleHex & defenderPos = BattleHex::INVALID) const;
	bool isMeleeAttackPossible(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPos = BattleHex::INVALID, const BattleHex & defenderPos = BattleHex::INVALID) const;
	bool battleIsUnitBlocked(const battle::Unit * unit) const; //returns true if there is neighboring enemy stack
	battle::Units battleAdjacentUnits(const battle::Unit * unit) const;

	DamageEstimation calculateDmgRange(const BattleAttackInfo & info) const;

	/// estimates damage dealt by attacker to defender;
	/// only non-random bonuses are considered in estimation
	/// returns pair <min dmg, max dmg>
	DamageEstimation battleEstimateDamage(const BattleAttackInfo & bai, DamageEstimation * retaliationDmg = nullptr) const;
	DamageEstimation battleEstimateDamage(const battle::Unit * attacker, const battle::Unit * defender, const BattleHex & attackerPosition, DamageEstimation * retaliationDmg = nullptr) const;
	DamageEstimation battleEstimateDamage(const battle::Unit * attacker, const battle::Unit * defender, int getMovementRange, DamageEstimation * retaliationDmg = nullptr) const;

	/// preview of damage / restored HP and units killed or raised/summoned for given parameters
	/// returns hpDelta and unitsDelta
	SpellEffectValUptr getSpellEffectValue(const CSpell * spell, const spells::Caster * caster, const spells::Mode spellMode, const BattleHex & targetHex) const;

	/// damage estimation for spell-like-attack case, eg. Death Cloud
	DamageEstimation estimateSpellLikeAttackDamage(const battle::Unit * shooter, const CSpell * spell,const BattleHex & aimHex) const;

	int64_t getFirstAidHealValue(const CGHeroInstance * owner, const battle::Unit * target) const;

	bool battleIsInsideWalls(const BattleHex & from) const;
	bool battleHasPenaltyOnLine(const BattleHex & from, const BattleHex & dest, bool checkWall, bool checkMoat) const;
	bool battleHasDistancePenalty(const IBonusBearer * shooter, const BattleHex & shooterPosition, const BattleHex & destHex) const;
	bool battleHasWallPenalty(const IBonusBearer * shooter, const BattleHex & shooterPosition, const BattleHex & destHex) const;
	bool battleHasShootingPenalty(const battle::Unit * shooter, const BattleHex & destHex) const;

	BattleHex wallPartToBattleHex(EWallPart part) const override;
	EWallPart battleHexToWallPart(const BattleHex & hex) const override; //returns part of destructible wall / gate / keep under given hex or -1 if not found
	bool isWallPartPotentiallyAttackable(EWallPart wallPart) const; // returns true if the wall part is potentially attackable (independent of wall state), false if not
	bool isWallPartAttackable(EWallPart wallPart) const override; // returns true if the wall part is actually attackable, false if not
	BattleHexArray getAttackableWallParts() const;

	si8 battleMinSpellLevel(BattleSide side) const; //calculates maximum spell level possible to be cast on battlefield - takes into account artifacts of both heroes; if no effects are set, 0 is returned
	si8 battleMaxSpellLevel(BattleSide side) const; //calculates minimum spell level possible to be cast on battlefield - takes into account artifacts of both heroes; if no effects are set, 0 is returned
	/// Returns battle-adjusted mana cost. listedCostMultiplier is applied to the
	/// spell's listed cost before stack-based reductions/increases.
	int32_t battleGetSpellCost(const spells::Spell * sp, const CGHeroInstance * caster, int32_t listedCostMultiplier = 1) const;
	ESpellCastProblem battleCanCastSpell(const spells::Caster * caster, spells::Mode mode) const; //returns true if there are no general issues preventing from casting a spell

	SpellID getRandomBeneficialSpell(vstd::RNG & rand, const battle::Unit * caster, const battle::Unit * target) const;
	SpellID getRandomCastedSpell(vstd::RNG & rand, const CStack * caster) const; //called at the beginning of turn for Faerie Dragon

	std::vector<PossiblePlayerBattleAction> getClientActionsForStack(const CStack * stack, const BattleClientInterfaceData & data);
	PossiblePlayerBattleAction getCasterAction(const CSpell * spell, const spells::Caster * caster, spells::Mode mode) const;

	//convenience methods using the ones above
	bool isInTacticRange(const BattleHex & dest) const;
	si8 battleGetTacticDist() const; //returns tactic distance for calling player or 0 if this player is not in tactic phase (for ALL_KNOWING actual distance for tactic side)

	AttackableTiles getPotentiallyAttackableHexes(
		const  battle::Unit* attacker,
		const  battle::Unit* defender,
		BattleHex destinationTile,
		BattleHex attackerPos,
		BattleHex defenderPos) const; //TODO: apply rotation to two-hex attacker

	AttackableTiles getPotentiallyAttackableHexes(
		const  battle::Unit * attacker,
		BattleHex destinationTile,
		BattleHex attackerPos) const;

	AttackableTiles getPotentiallyShootableHexes(const  battle::Unit* attacker, const BattleHex & destinationTile, const BattleHex & attackerPos) const;

	battle::Units getAttackedBattleUnits(
		const battle::Unit* attacker,
		const  battle::Unit * defender,
		BattleHex destinationTile,
		bool rangedAttack,
		BattleHex attackerPos = BattleHex::INVALID,
		BattleHex defenderPos = BattleHex::INVALID) const; //calculates range of multi-hex attacks
	
	/// Units a multi-hex attack strikes besides its primary target, in battlefield hex order, and
	/// whether a custom hit animation is to be played. Ordered rather than a set, because who is hit
	/// first decides in which order their abilities react and how the rolls of the attack are drawn.
	std::pair<battle::Units, bool> getAttackedCreatures(const CStack* attacker, const BattleHex & destinationTile, bool rangedAttack, BattleHex attackerPos = BattleHex::INVALID) const;
	bool isToReverse(const battle::Unit * attacker, const battle::Unit * defender, BattleHex attackerHex = BattleHex::INVALID, BattleHex defenderHex = BattleHex::INVALID) const; //determines if attacker standing at attackerHex should reverse in order to attack defender

	ReachabilityInfo getReachability(const battle::Unit * unit) const;
	ReachabilityInfo getReachability(const ReachabilityInfo::Parameters & params) const;
	AccessibilityInfo getAccessibility() const;
	AccessibilityInfo getAccessibility(const battle::Unit * stack) const; //Hexes occupied by stack will be marked as accessible.
	AccessibilityInfo getAccessibility(const BattleHexArray & accessibleHexes) const; //given hexes will be marked as accessible
	/// Returns tied nearest v3 Berserk candidates without RNG, one deterministic legacy action, or empty when none is reachable/legal.
	std::vector<ForcedAction> getBerserkForcedActions(const battle::Unit * berserker) const;
	ForcedAction getBerserkForcedAction(const battle::Unit * berserker) const;
	BattleHex getClosestHexToTargetInRange(const ReachabilityInfo& cache, const battle::Unit& unit, const BattleHex& targetHex) const;

	/// find free hex suitable to place new unit. If no initial position was provided, hex located on left size (attacker) or right side (defender) will be selected
	BattleHex getAvailableHex(const Creature * creature, BattleSide side, BattleHex initialPos = {}) const override;
protected:
	bool battleHeroCommandCommonAvailable(BattleSide side, HeroCommand command) const;
	bool battleIsFocusFireRecipient(const battle::Unit * unit, BattleSide side) const;
	ReachabilityInfo getFlyingReachability(const ReachabilityInfo::Parameters & params) const;
	ReachabilityInfo makeBFS(const AccessibilityInfo & accessibility, const ReachabilityInfo::Parameters & params) const;
	bool isInObstacle(const BattleHex & hex, const BattleHexArray & obstacles, const ReachabilityInfo::Parameters & params) const;
	BattleHexArray getStoppers(BattleSide whichSidePerspective) const; //get hexes with stopping obstacles (quicksands)
};
