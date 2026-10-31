/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanTactics.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
namespace newHorizonsSylvanTactics
{
std::string capturedSkillName(const JsonNode & rules)
{
	const auto captured = newHorizonsHeroes::perkSkill(rules, "new-horizons:sylvanLuck");
	if(captured) return captured->name;
	return newHorizonsHeroes::usesSylvanTactics(rules) ? "Sylvan Tactics" : "Sylvan Luck";
}
std::string capturedRankDescription(const JsonNode & rules, int rank)
{
	if(rank < 1 || rank > 3) return {};
	const auto captured = newHorizonsHeroes::perkSkill(rules, "new-horizons:sylvanLuck");
	constexpr const char * ranks[] = {"basic", "advanced", "expert"};
	if(captured)
	{
		const auto & description = captured->ranks[ranks[rank - 1]]["description"];
		if(description.isString()) return description.String();
	}
	if(newHorizonsHeroes::usesSylvanTactics(rules))
		return "+" + std::to_string(rank) + " Initiative for army creatures.";
	constexpr const char * legacy[] = {
		"+1 Luck. Lucky Strike damage multiplier becomes 2.25x.",
		"+2 Luck total. Lucky Strike multiplier becomes 2.60x.",
		"+3 Luck total. Lucky Strike multiplier becomes 3.00x."};
	return legacy[rank - 1];
}
std::vector<std::pair<std::string, int>> speedContributions(const CBattleInfoCallback & battle,
	const battle::Unit * unit)
{
	if(!ordinary(unit)) return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return {};
	const auto tactics = battle.getBattle()->getSylvanLuckState(side).tactics;
	if(!tactics.enabled) return {};
	std::vector<std::pair<std::string, int>> result;
	const auto id = unit->unitId();
	if(tactics.fleetfoot && (!tactics.activated.contains(id) || tactics.firstActivation == id))
		result.emplace_back("Fleetfoot", SylvanTacticsState::FLEETFOOT_SPEED);
	if(tactics.redeploymentActivation == id) result.emplace_back("Quick Redeployment", SylvanTacticsState::REDEPLOYMENT_SPEED);
	if(tactics.huntTriggered) result.emplace_back("Wild Hunt", SylvanTacticsState::HUNT_SPEED);
	return result;
}
std::vector<std::pair<std::string, int>> rangedProtectionContributions(const CBattleInfoCallback & battle,
	const battle::Unit * unit)
{
	if(!ordinary(unit)) return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return {};
	const auto tactics = battle.getBattle()->getSylvanLuckState(side).tactics;
	std::vector<std::pair<std::string, int>> result;
	if(tactics.enabled && tactics.dappledCover && unit->waited()
		&& !tactics.dappledExpired.contains(unit->unitId())) result.emplace_back("Dappled Cover", DAPPLED_COVER_REDUCTION_PERCENT);
	for(auto provider : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle.getBattle()->getSylvanLuckState(provider).tactics.covered.contains(unit->unitId()))
		{
			result.emplace_back("Covering Fire", COVERING_FIRE_REDUCTION_PERCENT);
			break;
		}
	return result;
}
std::vector<int> rangedProtection(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	std::vector<int> result;
	for(const auto & [name, percent] : rangedProtectionContributions(battle, unit)) result.push_back(percent);
	return result;
}
}
