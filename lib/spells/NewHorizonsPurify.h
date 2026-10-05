/*
 * NewHorizonsPurify.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "../battle/CUnitState.h"
#include "../battle/Unit.h"
#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"

class CBattleInfoCallback;
class CGHeroInstance;

namespace newHorizonsPurify
{
inline constexpr std::string_view SPELL_ID = "new-horizons:purify";
inline constexpr std::string_view PURIFIER_PERK = "new-horizons:lightMagic.purifier";
inline constexpr std::string_view LIGHT_MAGIC_SKILL = "new-horizons:lightMagic";
constexpr int AREA_RADIUS = 2;
constexpr int MAX_SPELL_EFFECT_CHOICES = 2;
constexpr int SPELL_POWER_FOR_SECOND_CHOICE = 120;

struct DLL_LINKAGE EligibleStack
{
	int32_t unitId = -1;
	std::vector<SpellID> spellEffectGroups;
	/// Current stored physical Poison is also selectable as one base effect, using
	/// physicalPoisonChoiceID() in the action payload.
	bool physicalPoison = false;
	/// Purifier removes this same current physical affliction automatically, outside the base cap.
	bool physicalPoisonAutomaticallyCleared = false;
	int maximumSpellEffectChoices = 0;
};

/// The stable content identity of Purify. This spell is installed by the New Horizons module;
/// saved battle rules below still decide whether the battle may use it.
DLL_LINKAGE SpellID spellID();
/// Action payload sentinel distinguishing stored physical Poison from any spell-source Poison ID.
DLL_LINKAGE SpellID physicalPoisonChoiceID();
/// True only when a saved v3 roster contains the canonical level-4 Light Purify at 15 Mana.
DLL_LINKAGE bool enabled(const JsonNode & magicRules, SpellID spell);
/// True only when the hero has the registered Purifier perk.
DLL_LINKAGE bool hasPurifierPerk(const CGHeroInstance * hero);
/// Per-stack choice count: 1 below 120 Spell Power, 2 at 120 or above.
DLL_LINKAGE int maximumSpellEffectChoices(int32_t spellPower);
/// Enumerates sorted, unique source spell IDs for eligible temporary negative combat spell groups.
DLL_LINKAGE std::vector<SpellID> eligibleSpellEffectGroups(const JsonNode & magicRules,
	const battle::Unit * unit);
/// Returns a copy of the selected source spell's complete SPELL_EFFECT group.
DLL_LINKAGE std::vector<Bonus> spellEffectGroupBonuses(const battle::Unit * unit, SpellID sourceSpell);
/// Classify an existing selected group before removal. Spell-effect transport does not make
/// physical afflictions (including legacy Poison/Disease groups) magical.
DLL_LINKAGE bool isMagicalSpellEffectGroup(const battle::Unit * unit, SpellID sourceSpell);
/// Whether this unit currently carries the distinct stored physical Poison affliction.
DLL_LINKAGE bool hasPhysicalPoison(const battle::Unit * unit);
/// Clears only the stored physical Poison fields on a copied/projected unit state.
DLL_LINKAGE bool clearPhysicalPoison(battle::CUnitState * state);
/// Enumerates all eligible friendly stacks in radius 2 of center for UI/AI use.
/// Physical Poison is a selectable base effect regardless of Purifier; the perk bonus is reported
/// separately. Each double-wide unit is included once if either occupied hex is in range.
DLL_LINKAGE std::vector<EligibleStack> eligibleStacks(const CBattleInfoCallback & battle,
	BattleSide casterSide, const BattleHex & center, int32_t spellPower, bool purifierPerk);
}
