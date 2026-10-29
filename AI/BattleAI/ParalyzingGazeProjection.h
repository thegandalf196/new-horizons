/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <optional>
#include <cstdint>
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
class HypotheticBattle;
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace paralyzingGazeAI
{
struct Consumption
{
	uint32_t opponent;
	uint32_t retaliator;
	bool shooting;
	bool ignoredByDefiant;
	std::optional<newHorizonsAerialSupremacy::Excursion> aerial;
};
bool qualifies(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Consume a pending denial only at an otherwise legal ordinary counter boundary.
bool consumeAndAllowCounter(HypotheticBattle & battle, uint32_t opponent, uint32_t retaliator, bool shooting);
bool cancel(HypotheticBattle & battle, uint32_t opponent, uint32_t retaliator, bool shooting,
	std::optional<Consumption> & receipt);
void apply(HypotheticBattle & battle, uint32_t source, uint32_t target, int64_t damage, bool admitted);
/// Caller proves an otherwise legal ordinary counter, not a reaction attack.
std::optional<Consumption> consume(HypotheticBattle & battle, uint32_t opponent, uint32_t retaliator, bool shooting,
	std::optional<newHorizonsAerialSupremacy::Excursion> aerial = std::nullopt);
/// Replays the typed denial at the same opportunity boundary, never afterAttack.
void replay(HypotheticBattle & battle, const Consumption & receipt);
}
