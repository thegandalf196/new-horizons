/*
 * NewHorizonsRecoverySpecialtyPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRecoverySpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
bool resolveRecoverySpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const auto identity = hero.getJsonKey();
	std::optional<RecoverySpecialtyKind> kind;
	if(identity == "core:axsis")
		kind = RecoverySpecialtyKind::INFERNAL_WELLSPRING;
	else if(identity == "core:jaegar")
		kind = RecoverySpecialtyKind::RITUAL_RESERVE;
	else if(identity == "core:rosic")
		kind = RecoverySpecialtyKind::DEEP_TRANCE;
	if(!kind)
		return false;
	const auto rule = recoverySpecialtyRules(development, *kind);
	if(!rule)
		return false;
	result.name = *kind == RecoverySpecialtyKind::INFERNAL_WELLSPRING ? "Infernal Wellspring"
		: *kind == RecoverySpecialtyKind::RITUAL_RESERVE ? "Ritual Reserve" : "Deep Trance";
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
	result.description = "With Mysticism active, its daily recovery is at least 5 Normal Spell Points or 10% of maximum Normal, whichever is greater. "
		"That Mysticism amount increases by " + std::to_string(rule->dailyPercentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->dailyMaximumPercent)
		+ "%. Fractions round down. Stronger ordinary regeneration wins; Meditation and Mage Guild refill keep their usual rules. ";
	if(*kind == RecoverySpecialtyKind::INFERNAL_WELLSPRING)
		result.description += "After each victory, including an enemy retreat or surrender, restore "
			+ std::to_string(rule->victoryPercent) + "% of the enhanced Mysticism amount as Normal Spell Points. Losses and draws do not qualify.";
	else if(*kind == RecoverySpecialtyKind::RITUAL_RESERVE)
		result.description += "At the completed global-day boundary, immediately before recovery, Normal below half its current maximum adds "
			+ std::to_string(rule->lowNormalPercent)
			+ "% of the original Mysticism amount before comparison with stronger regeneration. Equality does not qualify; the initial day has no extra low-Normal bonus.";
	else
		result.description += "The first accepted combat of each absolute game day restores "
			+ std::to_string(rule->firstCombatPercent)
			+ "% of the original Mysticism amount. That opportunity is consumed even when full or Mysticism is inactive. Cancel/restart restores the prior opportunity.";
	result.description += " Restoration is limited by missing Normal and never grants Buffer or capacity.";
	if(capturedHero)
	{
		if(originalMysticismRecovery(*capturedHero) == 0)
			result.description += " Mysticism is not currently active.";
		else
			result.description += " At current values, the completed-day Mysticism floor is "
				+ std::to_string(enhancedMysticismRecovery(*capturedHero, true)) + " Normal Spell Points.";
	}
	return true;
}
}
