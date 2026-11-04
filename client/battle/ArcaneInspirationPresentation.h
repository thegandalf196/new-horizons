/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/CStack.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include <string>

namespace arcaneInspirationPresentation
{
inline constexpr auto SMALL_ICON = "NH_warcasting_basic_small.png";
struct Readout
{
	std::string compact;
	std::string details;
	bool operator==(const Readout &) const = default;
	bool active() const { return !details.empty(); }
};

/// Native markers are the only status source; never predict a cast or a strike.
inline Readout forStack(const CStack & stack, const CBattleInfoCallback * battle)
{
	if(!battle)
		return {};
	const auto * hero = battle->battleGetOwnerHero(&stack);
	const auto owner = battle->battleGetOwner(&stack);
	const auto active = arcaneInspiration::activeProfile(stack, owner);
	const int core = arcaneInspiration::effectiveCorePercent(hero, active.corePercent);
	const int retained = arcaneInspiration::effectiveCorePercent(hero,
		arcaneInspiration::retainedPercent(stack, owner));
	const bool spellshock = stack.hasBonus(CSelector(arcaneInspiration::isSpellshock));
	Readout result;
	const auto append = [&result](const std::string & text)
	{
		if(!result.details.empty())
			result.details += "\n";
		result.details += text;
	};
	if(core > 0)
	{
		result.compact = "+" + std::to_string(core) + "%";
		append("Arcane Inspiration: attacks and retaliations deal +" + std::to_string(core)
			+ "% damage until the beginning of the next combat round. Recasting refreshes it without stacking.");
		if(active.riders & arcaneInspiration::VALOR)
			append("Arcane Valor: +1 Morale while Inspiration is active.");
		if(active.riders & arcaneInspiration::FOCUS)
			append("Battle Focus: +15 percentage points to retaliation damage while active.");
		if(active.riders & arcaneInspiration::BULWARK)
			append("Arcane Bulwark: 10% less physical damage taken while active.");
		if(active.riders & arcaneInspiration::WEAPONS)
			append("Enchanted Weapons: attacks ignore 15% of enemy Defense while active.");
		if(active.riders & arcaneInspiration::REPRISAL)
			append("Arcane Reprisal: reflects 10% of attack damage as magical damage, without triggering another reflection.");
		if(active.riders & arcaneInspiration::SUPREMACY)
			append("Arcane Supremacy: attacks ignore 20% of applicable damage reduction while active.");
	}
	if(retained > 0)
	{
		if(result.compact.empty())
			result.compact = "+" + std::to_string(retained) + "%";
		append("Lingering Inspiration: +" + std::to_string(retained)
			+ "% core damage on the next attack or retaliation. That strike consumes the charge even while Inspiration is active; the bonuses do not stack.");
	}
	if(spellshock)
	{
		if(result.compact.empty())
			result.compact = "-10%";
		append("Spellshock: attacks and retaliations deal 10% less damage until the beginning of the next combat round.");
	}
	return result;
}
}
