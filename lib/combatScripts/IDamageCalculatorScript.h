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
	/// Percentage of the target's Creature Defense ignored by this exact attack.  This is
	/// populated from authoritative saved perk state, not from installed content alone.
	int luckyRangedDefenseIgnorePercent = 0;
	/// Percentage of the target's Creature Defense ignored by this exact Charge attack.
	/// This is populated from the authoritative active Order and saved perk state.
	int chargeDefenseIgnorePercent = 0;
	/// Percentage of the target's Creature Defense ignored by this melee attack.
	/// This is populated from the authoritative Armor Piercer perk state.
	int meleeDefenseIgnorePercent = 0;
	/// Additive melee damage premium against a target below the Executioner threshold.
	int executionerDamagePercent = 0;
	/// Additive direct damage component from a canonical New Horizons Order.
	int heroOrderDamagePercent = 0;
	/// Battle-long additive creature attack/retaliation damage from Bloodrage.
	int bloodrageDamagePercent = 0;
	/// Additive melee premium from a positional Shroud of Malassa flank.
	int shroudFlankingDamagePercent = 0;
	/// Canonical New Horizons Archery premium for this physical ranged blow.
	int newHorizonsArcheryDamagePercent = 0;
	/// Canonical New Horizons Armorer reduction for this physical creature blow.
	int newHorizonsArmorerReductionPercent = 0;
	/// Physical damage reduction supplied by the defending stack's canonical Order.
	int heroOrderDamageReductionPercent = 0;
	/// Bulwark reduction in basis points (one hundredth of one percentage point).
	/// This preserves Advanced's half-percent base and 0.15% Defense coefficient.
	int bulwarkDamageReductionBasisPoints = 0;
	/// Fraction of the explicit Defend-state defense contribution ignored by a melee blow.
	int defensiveStanceDamageReductionIgnorePercent = 0;
	/// Defend's temporary Creature Defense contribution, before Breakthrough applies its
	/// mundane reduction bypass. This is kept separate from ordinary Creature Defense.
	int defensiveStanceDefenseBonus = 0;
	/// Final damage multiplier supplied by a canonical Order. This is applied
	/// after normal additive attack/defense factors so a penalty cannot be
	/// cancelled by Offense/Archery bonuses. 100 is neutral.
	int heroOrderFinalDamageMultiplier = 100;

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
		s("targetedRangedCommandPercent", targetedRangedCommandPercent, "Target-specific additive ranged premium.");
		s("targetedRangedCommand", targetedRangedCommand, "Whether Focus Fire halves range and obstacle penalties for this primary shot.");
		s("luckyRangedDefenseIgnorePercent", luckyRangedDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this lucky ranged attack.");
		s("chargeDefenseIgnorePercent", chargeDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this Shock Assault Charge attack.");
		s("meleeDefenseIgnorePercent", meleeDefenseIgnorePercent,
			"Percentage of target Creature Defense ignored by this Armor Piercer melee attack.");
		s("executionerDamagePercent", executionerDamagePercent,
			"Conditional melee damage premium supplied by the active Executioner perk.");
		s("heroOrderDamagePercent", heroOrderDamagePercent, "Direct damage component from the active canonical Order.");
		s("bloodrageDamagePercent", bloodrageDamagePercent,
			"Battle-long additive creature attack and retaliation damage from Bloodrage.");
		s("shroudFlankingDamagePercent", shroudFlankingDamagePercent,
			"Positional melee damage premium supplied by Shroud of Malassa.");
		s("newHorizonsArcheryDamagePercent", newHorizonsArcheryDamagePercent,
			"Canonical New Horizons Archery ranged damage premium.");
		s("newHorizonsArmorerReductionPercent", newHorizonsArmorerReductionPercent,
			"Canonical New Horizons Armorer physical creature damage reduction.");
		s("heroOrderDamageReductionPercent", heroOrderDamageReductionPercent,
			"Physical damage reduction supplied by the defending canonical Order.");
		s("bulwarkDamageReductionBasisPoints", bulwarkDamageReductionBasisPoints,
			"Bulwark physical damage reduction in basis points.");
		s("defensiveStanceDamageReductionIgnorePercent", defensiveStanceDamageReductionIgnorePercent,
			"Percentage of the explicit Defend-state defense contribution ignored by this melee attack.");
		s("defensiveStanceDefenseBonus", defensiveStanceDefenseBonus,
			"Defend's temporary Creature Defense contribution available to Breakthrough.");
		s("heroOrderFinalDamageMultiplier", heroOrderFinalDamageMultiplier,
			"Final multiplicative damage percentage supplied by the active canonical Order; 100 is neutral.");
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
