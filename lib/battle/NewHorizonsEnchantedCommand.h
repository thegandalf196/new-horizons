/*
 * NewHorizonsEnchantedCommand.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "BattleSide.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"

#include <vector>

class CBattleInfoCallback;
class CGHeroInstance;
class JsonNode;
struct HeroOrderState;

namespace newHorizonsEnchantedCommand
{
inline constexpr const char * SKILL = "new-horizons:warcasting";
inline constexpr const char * PERK = "new-horizons:warcasting.enchantedCommand";
inline constexpr int MORALE_BONUS = 1;

/// Uses the issued/prepared Order snapshot, never readiness after acceptance.
DLL_LINKAGE bool eligible(const JsonNode & magicRules, const CGHeroInstance * hero,
	const HeroOrderState & order);

/// Read-only coverage of a canonical issued/prepared Order. Enemy targets of
/// Focus Fire/Flank are not recipients; Protect and Second Wind are bounded.
DLL_LINKAGE std::vector<uint32_t> recipientIds(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order);

/// Existing genuine-activation expiry handles this in live and detached state.
DLL_LINKAGE Bonus moraleBonus();
DLL_LINKAGE bool isMoraleBonus(const Bonus * bonus);
DLL_LINKAGE CSelector moraleBonusSelector();
}
