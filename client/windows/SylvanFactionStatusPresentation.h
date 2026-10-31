/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/CStack.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../battle/NewHorizonsBattleStatus.h"
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace newHorizonsSylvanFactionPresentation
{
inline constexpr std::string_view SYLVAN_LUCK_SKILL_KEY = "new-horizons:sylvanLuck";

inline bool usesNewHorizonsBattleRules(const CGHeroInstance * hero)
{
	if(!hero)
		return false;

	const auto & rules = hero->getCapabilityRules();
	return newHorizonsHeroes::usesRules(rules) && rules["rulesetVersion"].Integer() >= 3;
}

inline newHorizonsBattleStatus::SylvanLuckStackStatus currentSylvanLuckStatus(
	const CStack * stack, const CPlayerBattleCallback * battleCallback,
	const newHorizonsBattleStatus::BattleLuckReadback & luckReadback)
{
	if(!stack || !battleCallback || !battleCallback->getBattle()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack))
		return {};

	const auto ownerSide = battleCallback->playerToSide(battleCallback->battleGetOwner(stack));
	if(ownerSide != BattleSide::ATTACKER && ownerSide != BattleSide::DEFENDER)
		return {};

	// Match the actual attack/speed queries' current controller, and do not
	// disclose a hidden opposing hero's perk state through the inspector.
	const auto * hero = battleCallback->battleGetFightingHero(ownerSide);
	if(!usesNewHorizonsBattleRules(hero) || newHorizonsSylvanTactics::enabled(hero)
		|| hero->getPerkSkillRank(std::string(SYLVAN_LUCK_SKILL_KEY)) <= 0)
		return {};

	return newHorizonsBattleStatus::makeSylvanLuckStackStatus(
		battleCallback->getBattle()->getSylvanLuckState(ownerSide), stack->unitId(),
		luckReadback.active() ? luckReadback.ordinaryAttackLuck
			: battleCallback->battleGetAttackLuck(stack, nullptr, false, false), true, stack->isShooter());
}

/// Observed effects belong to the visible recipient, even after control changes.
/// Only current-owner perk readback needs catalogue visibility; no catalogue is
/// consulted to decide whether an observed ranged guard exists.
inline std::vector<std::string> formatCurrentRows(
	const std::vector<std::pair<std::string, int>> & observedProtection,
	const std::vector<std::pair<std::string, int>> & ownerSpeed,
	bool ownerPerksVisible, int initiative)
{
	std::vector<std::string> result;
	for(const auto & [name, percent] : observedProtection)
		result.push_back(name + ": " + std::to_string(percent) + "% ranged creature-damage protection (current).");
	if(ownerPerksVisible)
		for(const auto & [name, speed] : ownerSpeed)
			result.push_back(name + ": +" + std::to_string(speed) + " Speed (current).");
	if(initiative < 0)
		result.push_back("Disruptive Strike: " + std::to_string(initiative)
			+ "% Initiative until this stack's next activation.");
	return result;
}

/// The existing creature bonus list owns rendering. Shared battle queries own
/// every numeric contribution and eligibility rule; this only binds readbacks.
inline std::vector<std::string> currentRows(const CPlayerBattleCallback & battle,
	const battle::Unit * unit)
{
	if(!unit || !battle.getBattle() || !newHorizonsSylvanTactics::ordinary(unit))
		return {};
	const auto observedProtection = newHorizonsSylvanTactics::rangedProtectionContributions(battle, unit);
	std::vector<std::pair<std::string, int>> ownerSpeed;
	bool ownerPerksVisible = false;
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		// Do not expose a current controller's private perk catalogue.
		const auto * hero = battle.battleGetFightingHero(side);
		ownerPerksVisible = newHorizonsSylvanTactics::enabled(hero);
		if(ownerPerksVisible)
			ownerSpeed = newHorizonsSylvanTactics::speedContributions(battle, unit);
	}
	return formatCurrentRows(observedProtection, ownerSpeed, ownerPerksVisible,
		battle.battleSylvanTacticsInitiativePercent(unit));
}
}
