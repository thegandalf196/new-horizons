/*
 * NewHorizonsOrderPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "NewHorizonsBattleStatus.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

namespace newHorizonsOrderPresentation
{
struct CommandDisplay
{
	HeroCommand command;
	const char * image;
	const char * name;
	const char * description;
};
inline const std::array<CommandDisplay, 10> commandDisplays = {{
	{HeroCommand::CHARGE, "NH_charge_button", "Charge", "The first melee attack after moving at least 3 hexes gains bonus damage; extra movement adds more."},
	{HeroCommand::FOCUS_FIRE, "NH_focusFire_button", "Focus Fire", "Choose one enemy stack. Friendly shooters gain damage against it and reduce range and obstacle penalties."},
	{HeroCommand::RIPOSTE, "NH_riposte_button", "Riposte", "Friendly stacks take less melee damage and deal increased retaliation damage this round."},
	{HeroCommand::HOLD_THE_LINE, "NH_holdTheLine_button", "Hold the Line", "Friendly stacks take reduced physical damage while they remain in their issued positions. Iron Discipline also reduces magical damage."},
	{HeroCommand::BRACE, "NH_brace_button", "Brace", "Friendly stacks pre-emptively attack enemies that moved at least 3 hexes before a melee attack."},
	{HeroCommand::PROTECT, "NH_protect_button", "Protect", "Choose a Protector and adjacent Ward. The first qualifying melee attack is redirected; Shield Master allows the first two."},
	{HeroCommand::FLANK, "NH_flank_button", "Flank", "Choose one enemy stack. Friendly melee damage increases from additional distinct attack sides."},
	{HeroCommand::FEINT, "NH_orders_gauntlet_framed", "Feint", "Friendly troops take reduced physical retaliation damage through the end of this round."},
	{HeroCommand::VANGUARD, "NH_orders_gauntlet_framed", "Vanguard", "Friendly troops gain Creature Attack, Defense, Morale and Initiative. Undead and mechanical troops are unaffected; Morale immunity remains. Refreshes without stacking; issuing round counts toward duration."},
	{HeroCommand::SECOND_WIND, "NH_secondWind_button", "Second Wind", "Choose a friendly stack that already completed its normal activation for an additional activation at reduced direct damage."}
}};

inline const CommandDisplay & commandDisplay(HeroCommand command)
{
	for(const auto & display : commandDisplays)
		if(display.command == command)
			return display;
	return commandDisplays.front();
}

inline std::string orderAllowanceSourceName(HeroActionAllowanceState::GrantSource source)
{
	switch(source)
	{
		case HeroActionAllowanceState::GrantSource::ROUND: return {};
		case HeroActionAllowanceState::GrantSource::METAMAGIC: return "Metamagic";
		case HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND: return "Grand Metamagic";
		case HeroActionAllowanceState::GrantSource::PERK: return "Perk";
		case HeroActionAllowanceState::GrantSource::ARTIFACT: return "Artifact";
		case HeroActionAllowanceState::GrantSource::OTHER: return "Special ability";
		case HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND: return "Double Command";
		case HeroActionAllowanceState::GrantSource::BATTLE_PLAN: return "Battle Plan";
		case HeroActionAllowanceState::GrantSource::DIVINE_MANDATE: return "Divine Mandate";
		case HeroActionAllowanceState::GrantSource::CRISIS_COMMAND: return "Crisis Command";
		case HeroActionAllowanceState::GrantSource::HEROIC_SPIRIT: return "Heroic Spirit";
	}
	return "Additional ability";
}

inline std::string orderActionOpportunityText(const HeroActionAllowanceState::Selection & selection)
{
	const auto source = orderAllowanceSourceName(selection.source);
	if(source.empty())
		return {};

	if(selection.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND)
		return "Double Command Order follow-up: choose a different Order now; expires in round "
			+ std::to_string(selection.expiryRound) + ".";
	if(selection.source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
		return "Divine Mandate Order follow-up available through the end of round "
			+ std::to_string(selection.expiryRound) + ".";
	return source + " Order opportunity available through the end of round "
		+ std::to_string(selection.expiryRound) + ".";
}

inline std::vector<CommandDisplay> visibleCommandDisplays(const JsonNode * rules)
{
	std::vector<CommandDisplay> result;
	for(const auto & display : commandDisplays)
	{
		if(rules ? heroCommands::supportedByRules(*rules, display.command) : heroCommands::isIssuable(display.command))
			result.push_back(display);
	}
	return result;
}

inline bool isTargeted(HeroCommand command)
{
	return command == HeroCommand::FOCUS_FIRE || command == HeroCommand::PROTECT
		|| command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND;
}

inline std::string combinedArmsHelp(const JsonNode & rules, HeroCommand command, const CGHeroInstance * hero)
{
	if(!heroCommands::isCanonicalRules(rules) || !heroCommands::hasCombinedArms(hero))
		return {};
	if(command == HeroCommand::FOCUS_FIRE)
		return "\n\nCombined Arms: eligible friendly melee attacks gain half the damage bonus against the marked target. Shooting-only penalty reductions and Target Caller do not apply to melee.";
	if(command == HeroCommand::FLANK)
		return "\n\nCombined Arms: friendly ranged attacks gain half the Attack-derived damage bonus against the marked target, without the flat base or extra-side bonuses. Ranged attacks do not add Flank sides.";
	return {};
}

inline std::string percentText(int value)
{
	return (value > 0 ? "+" : "") + std::to_string(value) + "%";
}

inline std::string basisPointPercentText(int basisPoints)
{
	const int absolute = std::abs(basisPoints);
	std::string result = basisPoints > 0 ? "+" : (basisPoints < 0 ? "-" : "");
	result += std::to_string(absolute / 100);
	const int remainder = absolute % 100;
	if(remainder != 0)
	{
		result += ".";
		if(remainder < 10)
			result += "0";
		result += std::to_string(remainder);
		if(result.back() == '0')
			result.pop_back();
	}
	return result + "%";
}

inline std::string effectLabel(const std::string & key, int value)
{
	if(key == "meleeDamagePercent")
		return "Melee " + percentText(value);
	if(key == "rangedDamagePercent")
		return "Ranged " + percentText(value);
	if(key == "damageReductionPercent")
		return "Taken " + percentText(-value);
	if(key == "meleeDamageReductionPercent")
		return "Melee taken " + percentText(-value);
	if(key == "speedPercent")
		return "Speed " + percentText(value);
	if(key == "retaliationDamagePercent")
		return "Retaliation " + percentText(value);
	if(key == "preemptiveAttackPercent")
		return "Pre-emptive " + std::to_string(value) + "%";
	if(key == "preemptiveDamagePercent")
		return "Pre-emptive " + std::to_string(value) + "%";
	if(key == "interceptedDamageReductionPercent")
		return "Intercepted taken " + percentText(-value);
	if(key == "secondWindDamagePercent")
		return "Extra activation " + std::to_string(value) + "%";
	if(key == "additionalActivationDamagePercent")
		return "Extra activation " + std::to_string(value) + "%";
	if(key == "sideDamagePercent")
		return "Each extra side " + percentText(value);
	if(key == "additionalSidePercent")
		return "Each extra side " + percentText(value);
	return key + " " + percentText(value);
}

inline std::string revisedSnapshotText(const HeroOrderState & state, int currentRound, bool compact = false)
{
	if(!state.revision)
		return {};
	const auto & snapshot = *state.revision;
	if(state.command == HeroCommand::FEINT)
		return "Physical retaliation taken -" + std::to_string(snapshot.feintReductionPercent) + "%";
	if(compact)
		return "A/D/M +" + std::to_string(snapshot.vanguardStatBonus)
			+ "; Init +" + std::to_string(snapshot.vanguardInitiativePercent) + "%"
			+ "; scheduled end " + std::to_string(snapshot.expiresAfterRound) + "; stack timers: see badges";
	return "Attack/Defense/Morale +" + std::to_string(snapshot.vanguardStatBonus)
		+ "; Initiative +" + std::to_string(snapshot.vanguardInitiativePercent) + "%"
		+ "; " + std::to_string(std::max(0, snapshot.expiresAfterRound - currentRound + 1))
		+ " rounds remaining (ends round " + std::to_string(snapshot.expiresAfterRound) + ")";
}

inline std::string revisedActiveHelp(const std::optional<HeroOrderState> & state, int currentRound)
{
	return state && state->revision ? "\n\nIssued snapshot: " + revisedSnapshotText(*state, currentRound) : "";
}

inline std::string commandName(HeroCommand command)
{
	return command == HeroCommand::NONE ? "None" : commandDisplay(command).name;
}

inline std::string activeOrderReadback(const CPlayerBattleCallback & callback, BattleSide side,
	const JsonNode & rules, HeroCommand order)
{
	std::string readback = heroCommands::isRevisedRules(rules)
		? "Focus Fire chooses an enemy; Second Wind chooses a spent friendly activation. Other Orders affect your troops."
		: "Focus Fire/Flank choose an enemy; Protect chooses Protector then Ward; Second Wind chooses a spent friendly activation.";
	const auto activeOrders = callback.battleGetHeroOrderStates(side);
	if(!activeOrders.empty())
		readback.clear();
	for(const auto & activeOrder : activeOrders)
	{
		const auto * active = &activeOrder;
		if(!readback.empty())
			readback += "\n";
		readback += "Active Order: " + commandName(active->command) + " (round " + std::to_string(active->issuedRound) + ")";
		if(active->revision)
			readback += " | " + revisedSnapshotText(*active, callback.battleGetRound(), true);
		else if(active->command == HeroCommand::HOLD_THE_LINE)
		{
			readback += " | anchored stacks: " + std::to_string(active->anchors.size());
			if(active->holdMagicalReductionBasisPoints > 0)
				readback += " | magical taken " + basisPointPercentText(
					-static_cast<int>(active->holdMagicalReductionBasisPoints));
		}
		else if(active->command == HeroCommand::FOCUS_FIRE)
			readback += " | target " + std::to_string(active->primaryTargetUnitId);
		else if(active->command == HeroCommand::PROTECT)
		{
			const int limit = callback.battleHeroOrderProtectInterceptionLimit(side);
			const int used = active->protectInterceptionsConsumed;
			readback += " | Protector " + std::to_string(active->primaryTargetUnitId) + " -> Ward " + std::to_string(active->secondaryTargetUnitId)
				+ " | interceptions " + std::to_string(used) + "/" + std::to_string(limit)
				+ " used (" + std::to_string(std::max(0, limit - used)) + " remaining)";
		}
		else if(active->command == HeroCommand::FLANK && !active->flankTargets.empty())
		{
			unsigned sides = active->flankTargets.front().sideMask;
			int sideCount = 0;
			while(sides)
			{
				sideCount += sides & 1u;
				sides >>= 1;
			}
			readback += " | target " + std::to_string(active->flankTargets.front().unitId)
				+ " | sides " + std::to_string(sideCount);
		}
		else if(active->command == HeroCommand::SECOND_WIND)
			readback += " | stack " + std::to_string(active->primaryTargetUnitId)
				+ (active->secondWindActive ? " | extra activation active" : " | pending activation");
	}
	if(activeOrders.empty() && order != HeroCommand::NONE)
		readback = "Active Order: " + commandName(order) + ". Targeted effects remain subject to current unit state.";
	if(activeOrders.size() > 1)
	{
		// Keep the fixed-height readback economical; detailed recipient effects
		// remain available on the stack's individual Order indicators.
		readback = "Active Orders: ";
		for(size_t index = 0; index < activeOrders.size(); ++index)
		{
			if(index != 0)
				readback += ", ";
			readback += commandName(activeOrders[index].command);
		}
	}
	return readback;
}

struct RevisedOrderPreview
{
	std::string label;
	std::string help;
};

inline RevisedOrderPreview revisedOrderPreview(const CPlayerBattleCallback & callback, BattleSide side,
	HeroCommand command, const CGHeroInstance * hero)
{
	if(command != HeroCommand::FEINT && command != HeroCommand::VANGUARD)
		return {};
	// Preparation captures Leadership, efficiencies, perks and saved rules. It
	// remains read-only; finalize this owned copy without rolling a Luck proc.
	auto prepared = hero ? callback.battlePrepareHeroOrderState(side, command, std::vector<uint32_t>{})
		: std::optional<HeroOrderState>{};
	const bool preview = prepared.has_value();
	if(prepared)
		heroCommands::finalizeRevisedOrderSnapshot(*prepared, hero->getMagicRules());
	else
		prepared = callback.battleGetHeroOrderState(side, command);
	if(!prepared || !prepared->revision)
		return {"Preview unavailable", "\n\nNo eligible prepared snapshot is available."};
	const auto & snapshot = *prepared->revision;
	const int remaining = std::max(0, snapshot.expiresAfterRound - callback.battleGetRound() + 1);
	const auto label = command == HeroCommand::FEINT
		? "Retaliation taken -" + std::to_string(snapshot.feintReductionPercent) + "%"
		: "A/D/M +" + std::to_string(snapshot.vanguardStatBonus)
			+ "; Init +" + std::to_string(snapshot.vanguardInitiativePercent) + "%\n"
			+ std::to_string(remaining) + " rounds (incl. now)";
	return {label, std::string(preview ? "\n\nPreview without a Lucky Day proc: " : "\n\nIssued snapshot: ")
		+ revisedSnapshotText(*prepared, callback.battleGetRound())
		+ (command == HeroCommand::VANGUARD ? ". Stack badges show native timers, including Time Stop pauses." : ".")};
}

inline std::string commandEffects(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent)
{
	if(command == HeroCommand::FEINT || command == HeroCommand::VANGUARD)
		return {}; // Revised previews use prepared snapshots, independently of the legacy rating cache.
	const auto & effects = rules["commands"][heroCommands::key(command)]["effects"];
	if(!effects.isStruct())
		return {};
	std::string result;
	for(const auto & [key, formula] : effects.Struct())
	{
		if(!formula.isStruct() || !formula["base"].isNumber() || !formula["attack"].isNumber() || !formula["defense"].isNumber())
			continue;
		try
		{
			int value = command == HeroCommand::SECOND_WIND && key == "additionalActivationDamagePercent"
				? heroCommands::secondWindPercent(hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent)
				: heroCommands::coefficient(formula, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
			// Revised companions add fixed physical percentage points only to the
			// primary effect; historical formulas and secondary riders stay intact.
			if(heroCommands::isRevisedRules(rules)
				&& ((command == HeroCommand::CHARGE && key == "meleeDamagePercent")
					|| (command == HeroCommand::HOLD_THE_LINE && key == "damageReductionPercent")))
				value += heroCommands::revisedPerkBonus(rules, &hero, command);
			const auto line = effectLabel(key, value);
			if(!result.empty())
				result += '\n';
			result += line;
		}
		catch(const std::exception &)
		{
			// The battle snapshot has already been validated by the authority. A
			// malformed optional preview must never make the client window fail.
		}
	}
	if(command == HeroCommand::HOLD_THE_LINE
		&& hero.hasActivePerk(newHorizonsIronDiscipline::SKILL, newHorizonsIronDiscipline::PERK))
	{
		const int physicalReduction = std::clamp(heroCommands::coefficient(
			effects["damageReductionPercent"], hero, warcastingBonusPercent,
			divineMandateEfficiencyBonusPercent), 0, 100);
		if(!result.empty())
			result += '\n';
		result += "Magical taken " + basisPointPercentText(
			-physicalReduction * newHorizonsIronDiscipline::BASIS_POINTS_PER_PHYSICAL_PERCENT);
	}
	return result;
}

}
