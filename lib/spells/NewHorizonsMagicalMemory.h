/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMetamagicResolution.h"
class CBattleInfoCallback;
class ServerCallback;
class IBattleInfo;
struct SetStackEffect;
namespace battle { class Unit; }

namespace newHorizonsMetamagic
{
DLL_LINKAGE bool isMagicalMemory(const Bonus &);
DLL_LINKAGE Bonus magicalMemoryMarker(SpellID family, ResolutionKind removedKind);
DLL_LINKAGE bool blocksEnchantment(const battle::Unit &, const JsonNode & rules,
	SpellID spell, bool positive, bool negative);
/// Trusted live boundary shares this battle's immutable typed identity table.
DLL_LINKAGE bool blocksEnchantment(const battle::Unit &, const IBattleInfo & battle,
	SpellID spell, bool positive, bool negative);
/// Filter only newly applied spell bonus records; damage/removal packets remain independent.
DLL_LINKAGE void filterNewBonuses(const CBattleInfoCallback &, SetStackEffect &);
DLL_LINKAGE void rememberRemoval(ServerCallback &, const CBattleInfoCallback &,
	uint32_t recipient, SpellID family, ResolutionKind removedKind);
}
