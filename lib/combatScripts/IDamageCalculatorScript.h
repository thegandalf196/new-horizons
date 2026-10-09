/*
 * IDamageCalculatorScript.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "../battle/BattleHex.h"
#include "../battle/IBattleInfoCallback.h"

#include <vcmi/scripting/ApiTags.h>

#include <vector>

class CBattleInfoCallback;

namespace battle
{
class Unit;
}

/// One attack, as the damage calculator script is told about it. Positions are resolved before the
/// script sees them, so an attack that has not happened yet looks like any other.
struct DLL_LINKAGE DamageAttackInfo final : public scripting::ApiSerializable<DamageAttackInfo>
{
	const battle::Unit * attacker = nullptr;
	const battle::Unit * defender = nullptr;

	BattleHex attackerHex;
	BattleHex defenderHex;

	int chargeDistance = 0;
	bool shooting = false;
	/// Whether this attack deals physical creature damage. Spell-like shots and other
	/// explicitly nonphysical attacks use the magical Phantom Army damage multiplier.
	bool physicalDamage = true;
	/// -1 retains legacy reduction factors; nonnegative opts into independent capped PDR.
	int physicalDamageReductionCapPercent = -1;
	/// Percentage of the already-combined and capped physical damage reduction ignored by this attack.
	int physicalDamageReductionIgnorePercent = 0;
	/// Heavenly Gale's independent ranged physical reduction in basis points (10000 = 100%).
	int heavenlyGaleDamageReductionBasisPoints = 0;
	bool luckyStrike = false;
	bool unluckyStrike = false;
	bool deathBlow = false;
	bool doubleDamage = false;

	/// Zero keeps the legacy base/artifact Attack formula. Positive values come
	/// exclusively from the owning hero's saved capability rules and Artillery.
	int siegeSkillMultiplier = 0;
	/// Positive values replace ordinary creature base damage with the canonical
	/// absolute Siege output for this Ballista or defensive tower attack.
	int machineBaseDamage = 0;
	/// Additive ranged premium for this exact primary target; zero is legacy/no mark.
	int targetedRangedCommandPercent = 0;
	/// Focus Fire's reduced range/obstacle penalty for this exact primary shot.
	bool targetedRangedCommand = false;
	/// Target Caller's Focus Fire shot ignores all wall/obstacle damage penalties.
	bool archeryIgnoreObstaclePenalty = false;
	/// This physical shot targets an adjacent unit and uses the ordinary -50% ranged penalty.
	bool archeryAdjacentRangedTarget = false;
	/// Point-Blank Shot removes only the ordinary adjacent-target penalty from this shot.
	bool archeryIgnoreAdjacentRangedPenalty = false;
	/// Percentage of the target's Creature Defense ignored by this exact attack.  This is
	/// populated from authoritative saved perk state, not from installed content alone.
	int luckyRangedDefenseIgnorePercent = 0;
	/// Percentage of target Creature Defense ignored by the owning hero's Archery perks.
	int archeryRangedDefenseIgnorePercent = 0;
	/// Percentage of target Creature Defense ignored by the owning hero's War Machines perk.
	int warMachinesPiercingBoltsDefenseIgnorePercent = 0;
	/// Independent final ranged physical multiplier against enemy war machines.
	int counterBatteryFinalDamageMultiplier = 100;
	/// Independent final physical creature-hit multiplier against Frozen.
	int frozenShatterFinalDamageMultiplier = 100;
	/// Crossfire's additive ranged premium for this shot only.
	int archeryCrossfireDamagePercent = 0;
	/// High Arc halves distance penalties and ignores obstacle penalties on physical shots.
	bool archeryHighArc = false;
	/// Deadeye makes the creature's rolled base damage use its maximum value.
	bool archeryMaximumCreatureDamage = false;
	/// Percentage of the target's Creature Defense ignored by this exact Charge attack.
	/// This is populated from the authoritative active Order and saved perk state.
	int chargeDefenseIgnorePercent = 0;
	/// Percentage of the target's Creature Defense ignored by this melee attack.
	/// This is populated from the authoritative Armor Piercer perk state.
	int meleeDefenseIgnorePercent = 0;
	/// Ranged Creature Defense ignored by marks for the attacker's current controlling side,
	/// in basis points (10000 = 100%).
	int rangedDefenseIgnoreBasisPoints = 0;
	/// Additive melee damage premium against a target below the Executioner threshold.
	int executionerDamagePercent = 0;
	/// Additive direct damage component from a canonical New Horizons Order.
	int heroOrderDamagePercent = 0;
	/// Fractional Combined Arms premium; kept separate so half-percent values survive to final damage rounding.
	double combinedArmsDamagePercent = 0.0;
	/// Battle-long additive creature attack/retaliation damage from Bloodrage.
	int bloodrageDamagePercent = 0;
	/// Additive melee premium from a positional Shroud of Malassa flank.
	int shroudFlankingDamagePercent = 0;
	/// Half-rank ranged premium preserves fractional percentage points.
	double shroudDeepFlankDamagePercent = 0.0;
	/// Canonical New Horizons Archery premium for this physical ranged blow.
	int newHorizonsArcheryDamagePercent = 0;
	/// Canonical New Horizons Armorer reduction for this physical creature blow.
	int newHorizonsArmorerReductionPercent = 0;
	/// Independent Formation Fighting reduction while the defender is in formation.
	int formationFightingReductionPercent = 0;
	/// Independent Pavise reduction for a ranged physical hit against a Defending stack.
	int paviseDamageReductionPercent = 0;
	/// One-shot physical premium earned by Waiting under Battlecraft.
	int battlecraftWaitDamagePercent = 0;
	/// Additive damage premium supplied by the current Relentless Assault chain.
	int relentlessAssaultDamagePercent = 0;
	/// Independent physical reduction while Defending under Battlecraft.
	int battlecraftDefendReductionPercent = 0;
	/// Physical damage reduction supplied by the defending stack's canonical Order.
	int heroOrderDamageReductionPercent = 0;
	/// Independent canonical Order reductions, each multiplied before the shared cap.
	std::vector<int> heroOrderDamageReductionPercents;
	/// Bulwark reduction in basis points (one hundredth of one percentage point).
	/// This preserves Advanced's half-percent base and 0.15% Defense coefficient.
	int bulwarkDamageReductionBasisPoints = 0;
	/// Immovable's post-reduction physical damage multiplier; 100 is neutral.
	int bulwarkImmovableFinalDamageMultiplier = 100;
	/// Armorer Bastion's once-per-round post-reduction physical damage multiplier; 100 is neutral.
	int armorerBastionFinalDamageMultiplier = 100;
	/// Fraction of the explicit Defend-state defense contribution ignored by a melee blow.
	int defensiveStanceDamageReductionIgnorePercent = 0;
	/// Defend's temporary Creature Defense contribution, before Breakthrough applies its
	/// mundane reduction bypass. This is kept separate from ordinary Creature Defense.
	int defensiveStanceDefenseBonus = 0;
	/// Final damage multiplier supplied by a canonical Order. This is applied
	/// after normal additive attack/defense factors so a penalty cannot be
	/// cancelled by Offense/Archery bonuses. 100 is neutral.
	int heroOrderFinalDamageMultiplier = 100;
	/// Independent final multipliers supplied by all applicable canonical Orders.
	std::vector<int> heroOrderFinalDamageMultipliers;
	/// Final multiplier for a non-Order pre-emptive attack such as Bulwark.
	/// Kept separate from Order multipliers so neither source overwrites the other.
	int preemptiveDamageMultiplier = 100;
	/// Independent final multiplier for an automatic Cleave strike. This composes
	/// with Orders instead of overwriting their explicit final multiplier.
	int cleaveFinalDamageMultiplier = 100;
	/// Independent final multiplier for an explicitly reduced-strength Archery attack.
	int archeryRangedDamageMultiplierPercent = 100;
	/// Independent final multiplier for an earned Ballista follow-up shot.
	int rangedFollowUpDamagePercent = 100;
	/// Final output percentage for a genuine reduced-effectiveness activation.
	int activationOutputPercent = 100;

	/// Which of the bonus types the script declared an interest in each of the two carries
	std::unordered_map<std::string, bool> attackerBonuses;
	std::unordered_map<std::string, bool> defenderBonuses;

	// tuning constants of the game, handed over rather than looked up so that the script needs no
	// access to the settings
	double attackFactorPerPoint = 0.0;
	double attackFactorCap = 0.0;
	double defenseFactorPerPoint = 0.0;
	double defenseFactorCap = 0.0;

	template<typename Serializer>
	void serializeScript(Serializer & s)
	{
		s("attacker", attacker, "Unit dealing the blow.");
		s("defender", defender, "Unit receiving it.");
		s("attackerHex", attackerHex, "Hex the blow is dealt from.");
		s("defenderHex", defenderHex, "Hex the blow lands on.");
		s("attackerBonuses", attackerBonuses, "Bonus types the attacker carries.");
		s("defenderBonuses", defenderBonuses, "Bonus types the defender carries.");
		s("chargeDistance", chargeDistance, "Hexes crossed to reach the target, which is what jousting scales with.");
		s("shooting", shooting, "Whether the blow is a shot.");
		s("physicalDamage", physicalDamage, "Whether this attack deals physical creature damage.");
		s("physicalDamageReductionCapPercent", physicalDamageReductionCapPercent,
			"Combined independent physical reduction cap; -1 preserves legacy calculations.");
		s("physicalDamageReductionIgnorePercent", physicalDamageReductionIgnorePercent,
			"Attack-local percentage of combined capped physical damage reduction ignored.");
		s("heavenlyGaleDamageReductionBasisPoints", heavenlyGaleDamageReductionBasisPoints,
			"Heavenly Gale's independent ranged physical reduction in basis points.");
		s("targetedRangedCommandPercent", targetedRangedCommandPercent, "Target-specific additive ranged premium.");
		s("targetedRangedCommand", targetedRangedCommand, "Whether Focus Fire halves range and obstacle penalties for this primary shot.");
		s("archeryIgnoreObstaclePenalty", archeryIgnoreObstaclePenalty,
			"Whether Target Caller removes every obstacle penalty from this Focus Fire shot.");
		s("archeryAdjacentRangedTarget", archeryAdjacentRangedTarget,
			"Whether this ranged shot is against an adjacent target and receives the ordinary adjacent-shot penalty.");
		s("archeryIgnoreAdjacentRangedPenalty", archeryIgnoreAdjacentRangedPenalty,
			"Whether Point-Blank Shot removes the ordinary adjacent-target ranged penalty from this shot.");
		s("luckyRangedDefenseIgnorePercent", luckyRangedDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this lucky ranged attack.");
		s("archeryRangedDefenseIgnorePercent", archeryRangedDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by an Archery ranged attack.");
		s("warMachinesPiercingBoltsDefenseIgnorePercent", warMachinesPiercingBoltsDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by a Ballista shot under Piercing Bolts.");
		s("counterBatteryFinalDamageMultiplier", counterBatteryFinalDamageMultiplier,
			"Independent final physical shot multiplier from Counter-Battery against an enemy war machine.");
		s("frozenShatterFinalDamageMultiplier", frozenShatterFinalDamageMultiplier,
			"Final physical creature damage multiplier for a Frozen target.");
		s("archeryCrossfireDamagePercent", archeryCrossfireDamagePercent,
			"Crossfire's additive damage premium for this ranged attack.");
		s("archeryHighArc", archeryHighArc, "Whether High Arc halves distance penalties and ignores obstacle penalties.");
		s("archeryMaximumCreatureDamage", archeryMaximumCreatureDamage,
			"Whether Deadeye sets the creature's base damage roll to its maximum.");
		s("chargeDefenseIgnorePercent", chargeDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this Shock Assault Charge attack.");
		s("meleeDefenseIgnorePercent", meleeDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this Armor Piercer melee attack.");
		s("rangedDefenseIgnoreBasisPoints", rangedDefenseIgnoreBasisPoints,
			"Target Creature Defense ignored by current-side ranged marks, in basis points.");
		s("executionerDamagePercent", executionerDamagePercent,
			"Conditional melee damage premium supplied by the active Executioner perk.");
		s("heroOrderDamagePercent", heroOrderDamagePercent, "Direct damage component from the active canonical Order.");
		s("combinedArmsDamagePercent", combinedArmsDamagePercent,
			"Fractional damage component supplied by the active Combined Arms perk.");
		s("bloodrageDamagePercent", bloodrageDamagePercent,
			"Battle-long additive creature attack and retaliation damage from Bloodrage.");
		s("shroudFlankingDamagePercent", shroudFlankingDamagePercent,
			"Positional melee damage premium supplied by Shroud of Malassa.");
		s("shroudDeepFlankDamagePercent", shroudDeepFlankDamagePercent,
			"Fractional ranged premium against current multi-side melee engagement.");
		s("newHorizonsArcheryDamagePercent", newHorizonsArcheryDamagePercent,
			"Canonical New Horizons Archery ranged damage premium.");
		s("newHorizonsArmorerReductionPercent", newHorizonsArmorerReductionPercent,
			"Canonical New Horizons Armorer physical creature damage reduction.");
		s("formationFightingReductionPercent", formationFightingReductionPercent,
			"Independent physical reduction supplied by Formation Fighting while the defender is adjacent to a friendly stack.");
		s("paviseDamageReductionPercent", paviseDamageReductionPercent,
			"Independent ranged physical reduction from Pavise against a Defending target.");
		s("battlecraftWaitDamagePercent", battlecraftWaitDamagePercent,
			"One-shot physical damage premium earned by Waiting under Battlecraft.");
		s("relentlessAssaultDamagePercent", relentlessAssaultDamagePercent,
			"Physical damage premium for an eligible attack action in the Expert Offense target streak.");
		s("battlecraftDefendReductionPercent", battlecraftDefendReductionPercent,
			"Independent physical reduction while Defending under Battlecraft.");
		s("heroOrderDamageReductionPercent", heroOrderDamageReductionPercent,
			"Physical damage reduction supplied by the defending canonical Order.");
		s("heroOrderDamageReductionPercents", heroOrderDamageReductionPercents,
			"Independent physical damage reductions supplied by all applicable canonical Orders.");
		s("bulwarkDamageReductionBasisPoints", bulwarkDamageReductionBasisPoints,
			"Bulwark physical damage reduction in basis points.");
		s("bulwarkImmovableFinalDamageMultiplier", bulwarkImmovableFinalDamageMultiplier,
			"Immovable final physical damage multiplier; 100 is neutral.");
		s("armorerBastionFinalDamageMultiplier", armorerBastionFinalDamageMultiplier,
			"Armorer Bastion final physical damage multiplier; 100 is neutral.");
		s("defensiveStanceDamageReductionIgnorePercent", defensiveStanceDamageReductionIgnorePercent,
			"Percentage of the explicit Defend-state defense contribution ignored by this melee attack.");
		s("defensiveStanceDefenseBonus", defensiveStanceDefenseBonus,
			"Defend's temporary Creature Defense contribution available to Breakthrough.");
		s("heroOrderFinalDamageMultiplier", heroOrderFinalDamageMultiplier,
			"Final multiplicative damage percentage supplied by the active canonical Order; 100 is neutral.");
		s("heroOrderFinalDamageMultipliers", heroOrderFinalDamageMultipliers,
			"Independent final damage percentages supplied by all applicable canonical Orders.");
		s("preemptiveDamageMultiplier", preemptiveDamageMultiplier,
			"Final multiplier for a non-Order pre-emptive attack, independent of Order effects.");
		s("cleaveFinalDamageMultiplier", cleaveFinalDamageMultiplier,
			"Final multiplicative percentage for an automatic Cleave strike; 100 is neutral.");
		s("archeryRangedDamageMultiplierPercent", archeryRangedDamageMultiplierPercent,
			"Final multiplicative percentage for reduced-strength Archery shots; 100 is neutral.");
		s("rangedFollowUpDamagePercent", rangedFollowUpDamagePercent,
			"Final multiplicative percentage for an earned Ballista follow-up shot; 100 is neutral.");
		s("activationOutputPercent", activationOutputPercent,
			"Final multiplicative percentage for a reduced-effectiveness activation; 100 is neutral.");
		s("luckyStrike", luckyStrike, "Whether luck struck.");
		s("unluckyStrike", unluckyStrike, "Whether bad luck struck.");
		s("deathBlow", deathBlow, "Whether a death blow was rolled.");
		s("doubleDamage", doubleDamage, "Whether the attack is a doubled one, as a ballista may roll.");
		s("siegeSkillMultiplier", siegeSkillMultiplier, "Saved skill-only siege range multiplier; zero means legacy formula.");
		s("machineBaseDamage", machineBaseDamage,
			"Saved ruleset-v3 absolute Siege output for this machine attack; zero means legacy formula.");
		s("attackFactorPerPoint", attackFactorPerPoint, "Damage added per point of attack over the target's defense.");
		s("attackFactorCap", attackFactorCap, "Most that attack points alone may add.");
		s("defenseFactorPerPoint", defenseFactorPerPoint, "Damage removed per point of defense over the attacker's attack.");
		s("defenseFactorCap", defenseFactorCap, "Most that defense points alone may remove.");
	}
};

/// Lowest and highest of something the attack produces.
struct DLL_LINKAGE DamageRangePayload final : public scripting::ApiSerializable<DamageRangePayload>
{
	int64_t min = 0;
	int64_t max = 0;

	template<typename Serializer>
	void serializeScript(Serializer & s)
	{
		s("min", min, "Lowest value.");
		s("max", max, "Highest value.");
	}
};

/// What the damage calculator script answers with.
struct DLL_LINKAGE DamageEstimationPayload final : public scripting::ApiSerializable<DamageEstimationPayload>
{
	DamageRangePayload damage;
	DamageRangePayload kills;
	DamageRangePayload damageBeforeDefense;

	template<typename Serializer>
	void serializeScript(Serializer & s)
	{
		s("damage", damage, "Damage the blow deals.");
		s("kills", kills, "Creatures the blow kills.");
		s("damageBeforeDefense", damageBeforeDefense, "Damage the blow would deal with the defences of the target left out, which is what abilities reflecting a strike work from.");
	}
};

/// Answers what one attack is worth. Exactly one of these is active at a time - it is the damage
/// calculator of the game, not an ability some unit carries.
class DLL_LINKAGE IDamageCalculatorScript
{
public:
	virtual ~IDamageCalculatorScript() = default;

	/// `info` arrives with everything the engine knows and is completed by the implementation, which
	/// is what lets a script be told only about the bonuses it asked for.
	virtual DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & info) const = 0;
};
