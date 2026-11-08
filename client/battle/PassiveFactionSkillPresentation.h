/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/MetamagicPointState.h"
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"
#include <optional>
#include <string>
class CBattleInfoCallback;
class CGHeroInstance;
namespace passiveFactionSkillPresentation
{
struct SkillStatusText
{
	std::string value;
	std::string details;
	bool replacesProviderDescription = false;
};
inline SkillStatusText metamagicPoints(const newHorizonsMetamagic::Profile & facts,
	const MetamagicPointState & state)
{
	const auto bonus = newHorizonsMetamagic::effectivenessBonusPercent(facts, state.points);
	std::string details = "Spells grow stronger as this hero gains Metamagic Points."
		"\n\nCurrent points: " + std::to_string(state.points) + " / 12."
		"\nSpell effectiveness: +" + std::to_string(bonus) + "%."
		"\nSpell penetration: +" + std::to_string(bonus) + " percentage points.";
	if(facts.arcaneAttunement)
		details += "\nArmy Initiative: +" + std::to_string(newHorizonsMetamagic::attunementInitiative(facts, state.points)) + ".";
	if(facts.arcaneSingularity)
		details += state.singularityReached
		? "\nArcane Singularity active: Mana costs reduced by 25% for the rest of combat."
		: "\nArcane Singularity activates upon reaching 12 points.";
	return SkillStatusText{std::to_string(state.points) + "/12", std::move(details), true};
}

std::optional<SkillStatusText> metamagic(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side);
std::optional<SkillStatusText> elementalAttunement(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side);
std::optional<SkillStatusText> divineMandate(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side);
}
