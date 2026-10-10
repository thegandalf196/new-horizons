/* NewHorizonsSpellcraft.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once
#include <optional>
#include "../battle/BattleSide.h"
#include <cstdint>
#include <string_view>
class CSpell;
class CGHeroInstance;
class IBattleInfo;
namespace newHorizonsSpellcraft
{
constexpr std::string_view CONCENTRATION = "new-horizons:spellcraft.concentration";
constexpr std::string_view EXTEND_SPELL = "new-horizons:spellcraft.extendSpell";
DLL_LINKAGE bool roundTemporary(const CSpell & spell, int32_t effectLevel);
DLL_LINKAGE bool extendAvailable(const IBattleInfo & battle, BattleSide side, const CSpell & spell, int32_t effectLevel);
}
