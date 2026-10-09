/*
 * NewHorizonsPandemonium.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../battle/NewHorizonsDebuffStatuses.h"
#include <vcmi/spells/Magic.h>
#include <cstdint>
#include <string_view>

namespace spells { class Mechanics; }
namespace battle { class Unit; }
class CGHeroInstance;

namespace newHorizonsPandemonium
{
constexpr std::string_view SPELL_KEY = "new-horizons:pandemonium";

struct DLL_LINKAGE RecipientSnapshot
{
	const battle::Unit * unit = nullptr;
	newHorizonsDebuffStatuses::StatusSnapshot statuses;
};

DLL_LINKAGE bool enabled(const spells::Mechanics & mechanics);
DLL_LINKAGE bool hasMaster(const CGHeroInstance * hero);
DLL_LINKAGE bool validRecipient(const battle::Unit * unit);
/// Full battlefield recipient set, including allies and immune conductors.
DLL_LINKAGE spells::Target targets(const spells::Mechanics & mechanics);
/// Capture every logical debuff count before any recipient is damaged.
DLL_LINKAGE std::vector<RecipientSnapshot> snapshot(const spells::Mechanics & mechanics);
/// D * (20 + SP/4), rank/Warcasting/Empower scale only the SP term;
/// Master multiplies the whole contribution by 125%, with one final floor.
DLL_LINKAGE int64_t rawPower(int32_t spellPower, int32_t coefficientBasisPoints,
	int32_t warcastingPercent, int32_t empowerPercent, bool master, size_t debuffCount);
DLL_LINKAGE int64_t rawPower(const spells::Mechanics & mechanics, size_t debuffCount);
/// Recipient-adjusted damage from an already captured count. No resistance RNG.
DLL_LINKAGE int64_t damage(const spells::Mechanics & mechanics,
	const battle::Unit * recipient, size_t debuffCount);
}
