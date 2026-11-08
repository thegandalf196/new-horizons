/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../bonuses/BonusEnum.h"
#include "NativePhysicalProtection.h"

#include <array>
#include <bitset>
#include <string>

class CBattleInfoCallback;
struct DamageAttackInfo;
struct DamageEstimation;

namespace nativeDamage
{
constexpr size_t bonusTypeCount = static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT);

/// Owned numeric observations for one calculation. These contain no Unit,
/// callback, bonus-list or borrowed-state references and are never cached.
struct UnitFacts
{
	std::bitset<bonusTypeCount> bonuses;
	std::array<double, bonusTypeCount> values{};
	double minDamage = 0, maxDamage = 0, count = 0;
	double attack = 0, defense = 0;
	double firstHealth = 0, maxHealth = 0, phantomIntegrity = 0;
	double baseAmount = 0, availableHealth = 0;
	bool shooter = false, turret = false;
	std::string creatureKey;
	bool has(BonusType type) const { return bonuses[static_cast<size_t>(type)]; }
	double value(BonusType type) const { return values[static_cast<size_t>(type)]; }
};

struct Facts
{
	UnitFacts attacker, defender;
	PhysicalProtectionFacts physicalProtection;
	double attackerDefenseReduction = 0, defenderDefenseReduction = 0;
	double defenderAttackReduction = 0, attackerAttackReduction = 0;
	double damageBoost = 0, doubleDamageBonus = 0, hatedCreature = 0;
	double petrification = 0, hatedTraits = 0;
	double heroArtifactAttack = 0, heroBaseAttack = 0;
	bool hasSlayer = false;
	double slayerMastery = 0, slayerValue = 0;
	bool distancePenalty = false, wallPenalty = false, toReverse = false;
};

/// Capture fresh engine observations. The calculator below does not access
/// DamageAttackInfo's unit pointers or bonus maps; it reads only its modifiers.
DLL_LINKAGE Facts capture(const CBattleInfoCallback & battle, const DamageAttackInfo & info);
DLL_LINKAGE DamageEstimation calculate(const DamageAttackInfo & info, const Facts & facts);
}
