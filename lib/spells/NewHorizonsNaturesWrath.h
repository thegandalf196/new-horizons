/*
 * NewHorizonsNaturesWrath.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../battle/Destination.h"
#include <vcmi/spells/Magic.h>
#include <cstdint>
#include <string_view>

namespace spells { class Mechanics; }
namespace battle { class Unit; }
class CBattleInfoCallback;
class CGHeroInstance;

namespace newHorizonsNaturesWrath
{
constexpr std::string_view SPELL_KEY = "new-horizons:naturesWrath";
constexpr int BASE_RECIPIENTS = 17;
constexpr int WORLDROOT_RECIPIENTS = 19;
constexpr int BASE_POWER = 110;
constexpr int SPELL_POWER_MULTIPLIER = 2;
constexpr int HOP_RETENTION_PERCENT = 93;

DLL_LINKAGE bool enabled(const spells::Mechanics & mechanics);
DLL_LINKAGE bool hasWorldroot(const CGHeroInstance * hero);
DLL_LINKAGE bool validConductor(const battle::Unit * unit);
/// Pure proximity routing. Immune, invincible and healthy living stacks remain
/// conductors; neither resistance nor damage can reroute this captured chain.
DLL_LINKAGE spells::Target route(const CBattleInfoCallback & battle,
	const battle::Unit * first, int maximumRecipients);
DLL_LINKAGE spells::Target route(const spells::Mechanics & mechanics, const battle::Unit * first);
/// Exact rational attenuation, with a single final floor for this recipient.
/// Worldroot multiplies only the Spell-Power-derived term by 110%.
DLL_LINKAGE int64_t hopPower(int32_t spellPower, int32_t coefficientBasisPoints,
	int32_t warcastingPercent, int32_t empowerPercent, bool worldroot, int32_t hopIndex);
DLL_LINKAGE int64_t hopPower(const spells::Mechanics & mechanics, int32_t hopIndex);
/// Read-only recipient damage. Applies caster modifiers and normal recipient
/// defenses once; never draws or resolves resistance (the cast owns that).
DLL_LINKAGE int64_t damage(const spells::Mechanics & mechanics,
	const battle::Unit * recipient, int32_t hopIndex);
}
