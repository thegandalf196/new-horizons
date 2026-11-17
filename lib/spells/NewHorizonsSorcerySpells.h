/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../battle/BattleHex.h"
#include "../bonuses/Bonus.h"
#include <optional>
#include <memory>
#include <string>
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace spells { class Mechanics; namespace effects { class Effect; } }
namespace newHorizonsSorcerySpells
{
inline constexpr const char * FORCED_RECALL = "new-horizons:forcedRecall";
inline constexpr const char * FORCE_LANCE = "new-horizons:forceLance";
DLL_LINKAGE std::optional<BattleHex> forcedRecallDestination(const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleHex anchor);
DLL_LINKAGE std::optional<BattleHex> forcedRecallDestination(const CBattleInfoCallback & battle,
	const battle::Unit & unit);
/// Caster scaling only: intentionally excludes every recipient magical defense.
DLL_LINKAGE int64_t forceLanceDamage(const spells::Mechanics & mechanics);
DLL_LINKAGE Bonus forceLancePin(SpellID spell);
}
namespace nativeSpells
{
/// Native owner attests the exact loaded final implementation, not its name.
DLL_LINKAGE bool isFocusEnchantmentEffect(const spells::effects::Effect &);
DLL_LINKAGE bool isForceLanceEffect(const spells::effects::Effect &);
DLL_LINKAGE std::shared_ptr<spells::effects::Effect> createFocusEnchantmentEffect(const std::string & path);
DLL_LINKAGE std::shared_ptr<spells::effects::Effect> createForcedRecallEffect(const std::string & path);
DLL_LINKAGE std::shared_ptr<spells::effects::Effect> createForceLanceEffect(const std::string & path);
}
