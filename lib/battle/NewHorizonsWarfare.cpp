/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsWarfare.h"
#include "NewHorizonsCommandResources.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "HeroCommand.h"
#include "BattleAttackInfo.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../json/JsonNode.h"
#include <algorithm>
#include <stdexcept>

namespace newHorizonsWarfare
{
namespace
{
bool integer(const JsonNode & value, int maximum)
{
	return value.getType() == JsonNode::JsonType::DATA_INTEGER
		&& value.Integer() >= 0 && value.Integer() <= maximum;
}

const JsonNode & parameters(const JsonNode & rules, const char * id)
{
	for(const auto & perk : rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == id) return perk["effect"]["parameters"];
	throw std::runtime_error("Missing Warfare perk definition");
}

int value(const JsonNode & parameters, const char * key, int maximum = 1000)
{
	if(!integer(parameters[key], maximum)) throw std::runtime_error("Invalid Warfare numerical parameter");
	return parameters[key].Integer();
}

Policy readPolicy(const JsonNode & rules)
{
	Policy result;
	result.rankEfficiencyPercent[0] = 100;
	constexpr std::array<const char *, 3> ranks{"basic", "advanced", "expert"};
	for(size_t i = 0; i < ranks.size(); ++i)
		result.rankEfficiencyPercent[i + 1] = value(rules["skills"][SKILL]["ranks"][ranks[i]]["effect"]["parameters"], "orderEfficiencyPercent");
	result.aggressiveEfficiencyBonusPercent = value(parameters(rules, "new-horizons:command.aggressiveCommander"), "attackEfficiencyBonusPP");
	result.defensiveEfficiencyBonusPercent = value(parameters(rules, "new-horizons:command.defensiveCommander"), "defenseEfficiencyBonusPP");
	result.battlePlanEfficiencyBonusPercent = value(parameters(rules, "new-horizons:command.battlePlan"), "firstOrderEfficiencyBonusPP");
	const auto & veteran = parameters(rules, "new-horizons:command.veteranCommander");
	result.veteranCommanderStats = value(veteran, "creatureAttack");
	if(result.veteranCommanderStats != value(veteran, "creatureDefense"))
		throw std::runtime_error("Veteran Commander requires equal Attack and Defense contributions");
	result.masterCommanderEfficiencyBonusPercent = value(parameters(rules, "new-horizons:command.doubleCommand"), "allEfficiencyBonusPP");
	result.decisiveOrdersDamagePercent = value(parameters(rules, "new-horizons:command.decisiveOrders"), "directAttackDamagePercent");
	result.unbrokenRanksReductionPercent = value(parameters(rules, "new-horizons:command.crisisCommand"), "attackAndSpellReductionPercent", 100);
	result.combinedArmsFocusFirePercent = value(parameters(rules, "new-horizons:command.combinedArms"), "focusFireMeleeBonusPercent", 100);
	return result;
}

const CGHeroInstance * benefitingHero(const CBattleInfoCallback & battle, const battle::Unit * unit, const char * perk)
{
	const auto * hero = unit ? battle.battleGetOwnerHero(unit) : nullptr;
	return enabled(hero) && hero->hasActivePerk(SKILL, perk)
		&& newHorizonsCommand::beneficiary(battle, unit) ? hero : nullptr;
}
}

bool usesRules(const JsonNode & rules)
{
	return rules["warfarePolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["warfarePolicyVersion"].Integer() == 1;
}

bool enabled(const JsonNode & rules)
{
	return usesRules(rules);
}

bool enabled(const CGHeroInstance * hero)
{
	return hero && enabled(hero->getPerkState().rules);
}

void validatePolicy(const JsonNode & rules)
{
	if(rules["warfarePolicyVersion"].isNull()) return;
	if(!enabled(rules)) throw std::runtime_error("Invalid Warfare policy version");
	readPolicy(rules);
}

void validateRulesSerialization(const JsonNode & rules, bool supported)
{
	if(usesRules(rules) && !supported)
		throw std::runtime_error("Warfare rules require the Command/Authority serialization feature");
}

std::optional<Policy> policy(const CGHeroInstance * hero)
{
	if(!enabled(hero)) return {};
	return readPolicy(hero->getPerkState().rules);
}

int perkValue(const CGHeroInstance & hero, const char * perkId, const char * parameter)
{
	return value(parameters(hero.getPerkState().rules, perkId), parameter);
}

int battlePlanBonus(const CBattleInfoCallback & battle, BattleSide side)
{
	if(!battle.getBattle() || !usesRules(battle.getBattle()->getHeroCommandRules())
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| battle.getBattle()->getHeroActionAllowances(side).orderIssuedThisCombat) return 0;
	const auto * hero = battle.battleGetFightingHero(side);
	const auto values = policy(hero);
	return values && hero->hasActivePerk(SKILL, "new-horizons:command.battlePlan")
		? values->battlePlanEfficiencyBonusPercent : 0;
}

int statBonus(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	constexpr auto perk = "new-horizons:command.veteranCommander";
	const auto * hero = benefitingHero(battle, unit, perk);
	return hero ? perkValue(*hero, perk, "creatureAttack") : 0;
}

int directAttackBonus(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)) return 0;
	constexpr auto perk = "new-horizons:command.decisiveOrders";
	const auto * hero = benefitingHero(battle, attack.attacker, perk);
	return hero ? perkValue(*hero, perk, "directAttackDamagePercent") : 0;
}

int incomingReduction(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	constexpr auto perk = "new-horizons:command.crisisCommand";
	const auto * hero = benefitingHero(battle, unit, perk);
	return hero ? value(parameters(hero->getPerkState().rules, perk), "attackAndSpellReductionPercent", 100) : 0;
}

int64_t reduceDamage(int64_t damage, int reductionPercent)
{
	if(damage <= 0) return damage;
	const int remaining = 100 - std::clamp(reductionPercent, 0, 100);
	return damage / 100 * remaining + damage % 100 * remaining / 100;
}
}
