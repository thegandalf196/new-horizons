/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "NewHorizonsMetamagicMarks.h"
#include <span>
#include <optional>

class CBattleInfoCallback;
class ServerCallback;
struct BattleStackAttacked;
namespace spells { class Mechanics; }
namespace newHorizonsMetamagic
{
/// Runs after the completed originating spell, never through its recorder.
DLL_LINKAGE void publishPointCompletion(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, std::span<const uint32_t> resolvedRecipientIds,
	std::span<const BattleStackAttacked> injuries,
	std::optional<BattleSide> acceptedManualCaster = std::nullopt);
/// After BattleNextRound has granted its point, before other passive round ticks.
DLL_LINKAGE void publishRoundEffects(ServerCallback & server, const CBattleInfoCallback & battle);
DLL_LINKAGE void refreshAttunement(ServerCallback & server, const CBattleInfoCallback & battle, BattleSide side);
}
