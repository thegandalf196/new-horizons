/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../battle/NewHorizonsCrisisCommand.h"
#include "../battle/NewHorizonsReactiveDefense.h"
#include <set>
class BattleInfo;
struct BattleStackAttacked;
class UnitChanges;
namespace battleDestructionAftermath
{
std::set<uint32_t> bloodrageDeathCandidates(BattleInfo &, const std::vector<BattleStackAttacked> &);
std::vector<newHorizonsCrisisCommand::DeathReceipt> crisisDeathCandidates(BattleInfo &, const std::vector<BattleStackAttacked> &);
void recordCrisisDeaths(BattleInfo &, const std::vector<newHorizonsCrisisCommand::DeathReceipt> &);
void recordBloodrageDeaths(BattleInfo &, const std::set<uint32_t> &);
void refreshBloodrageLivingUnits(BattleInfo &, const std::vector<BattleStackAttacked> &);
std::vector<newHorizonsReactiveDefense::Destruction> solidarityDeathCandidates(BattleInfo &, const std::vector<BattleStackAttacked> &);
std::vector<newHorizonsReactiveDefense::Destruction> solidarityDeathCandidates(BattleInfo &, const std::vector<UnitChanges> &);
void recordSolidarityDeaths(BattleInfo &, std::span<const newHorizonsReactiveDefense::Destruction>);
}
