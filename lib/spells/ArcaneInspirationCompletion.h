/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../battle/BattleSide.h"
#include <span>
#include <optional>
class CBattleInfoCallback;
class ServerCallback;
struct BattleStackAttacked;
namespace spells { class Mechanics; }
namespace arcaneInspiration
{
/// Proven Hero-origin damage receipt only: never a cast completion or army refresh.
DLL_LINKAGE void publishSpellDamage(ServerCallback &, const CBattleInfoCallback &,
	BattleSide originalHeroSide, std::span<const BattleStackAttacked> actualInjuries);
/// Captured originating Hero identity admits obstacle damage, never a same-player proxy.
DLL_LINKAGE void publishDelayedSpellDamage(ServerCallback &, const CBattleInfoCallback &,
	const spells::Mechanics &, std::span<const BattleStackAttacked> actualInjuries);
/// Caller has already validated and accepted the originating successful hero cast.
DLL_LINKAGE void publishCompletedSpell(ServerCallback & server, const CBattleInfoCallback & battle,
	BattleSide originalHeroSide, std::span<const BattleStackAttacked> originalInjuries);
/// Publish once after successful originating combat HERO cast resolution. Automatic
/// hero casts qualify; mirrored injuries, Echo and creature abilities are not casts.
DLL_LINKAGE void publishCompletedSpell(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, bool negated, std::span<const BattleStackAttacked> originalInjuries,
	std::optional<BattleSide> originalHeroSide = std::nullopt);
}
