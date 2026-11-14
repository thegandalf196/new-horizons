/*
 * CommandPresentation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CommandPresentation.h"
#include "../widgets/LeadershipEfficiencyPresentation.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>

namespace commandPresentation
{
using namespace newHorizonsHeroes;

std::string slotName(SlotID slot)
{
	if(!slot.validSlot())
		return LIBRARY->generaltexth->translate("new-horizons.command.slot.unknown");
	const auto grade = commandSlotView(1, slot).grade;
	if(grade == CommandSlotGrade::CHAMPION)
		return LIBRARY->generaltexth->translate("new-horizons.command.slot.name.champion");
	auto text = LIBRARY->generaltexth->translate(grade == CommandSlotGrade::CORE
		? "new-horizons.command.slot.name.core" : "new-horizons.command.slot.name.elite");
	boost::replace_all(text, "%POSITION%", std::to_string(slot.getNum() % 3 + 1));
	return text;
}

std::string recipientName(CommandRecipient recipient)
{
	if(recipient == CommandRecipient::CORE_BLOCK)
		return LIBRARY->generaltexth->translate("new-horizons.command.enhancement.sharedCore");
	for(int slot = 0; slot < 7; ++slot)
		if(commandRecipientContains(recipient, SlotID(slot)))
			return slotName(SlotID(slot));
	return LIBRARY->generaltexth->translate("new-horizons.command.enhancement.unknownRecipient");
}

std::string enhancementName(CommandEnhancement enhancement)
{
	return LIBRARY->generaltexth->translate("new-horizons.command.enhancement.name",
		std::string(COMMAND_ENHANCEMENT_KEYS.at(static_cast<size_t>(enhancement))));
}

std::string modifiers(const CommandModifiers & values)
{
	std::string result;
	const auto append = [&result](int64_t value, const char * key, bool percent = false)
	{
		if(!value)
			return;
		if(!result.empty())
			result += LIBRARY->generaltexth->translate("new-horizons.command.modifier.separator");
		auto text = LIBRARY->generaltexth->translate(percent
			? "new-horizons.command.modifier.percent" : "new-horizons.command.modifier.flat");
		boost::replace_all(text, "%VALUE%", std::to_string(value));
		boost::replace_all(text, "%STAT%", LIBRARY->generaltexth->translate(key));
		result += text;
	};
	append(values.initiative, "new-horizons.command.stat.initiative");
	append(values.speed, "new-horizons.command.stat.speed");
	append(values.retaliations, "new-horizons.command.stat.retaliations");
	append(values.attack, "new-horizons.command.stat.attack");
	append(values.defense, "new-horizons.command.stat.defense");
	append(values.luck, "new-horizons.command.stat.luck");
	append(values.morale, "new-horizons.command.stat.morale");
	append(values.maximumHealthPercent, "new-horizons.command.stat.maximumHealth", true);
	append(values.magicalReductionPercent, "new-horizons.command.stat.magicalReduction", true);
	append(values.directAttackDamagePercent, "new-horizons.command.stat.directAttackDamage", true);
	return result.empty() ? LIBRARY->generaltexth->translate("new-horizons.command.modifier.none") : result;
}

std::string commandHelp(const CGHeroInstance & hero)
{
	if(newHorizonsHeroes::usesLeadershipEfficiency(hero.getCapabilityRules()))
		return leadershipPresentation::heroDetailText(hero);
	if(const auto cap = hero.getLeadershipCapacity())
	{
		const auto & rules = hero.getCapabilityRules();
		int base = 0;
		int perLevel = 0;
		if(rules.isStruct() && rules.Struct().contains("profile") && rules["profile"].isStruct())
		{
			base = rules["profile"]["base"].Integer();
			perLevel = rules["profile"]["perLevel"].Integer();
		}
		const int levelGains = (hero.level - 1) * perLevel;
		const int trainedLeadership = std::max(0, hero.valOfBonuses(BonusType::LEADERSHIP));
		const int authorityRank = hero.getPerkSkillRank("new-horizons:authority");
		int authorityPercent = 0;
		if(authorityRank == 1) authorityPercent = 15;
		else if(authorityRank == 2) authorityPercent = 30;
		else if(authorityRank >= 3) authorityPercent = 45;

		std::string text = "{Leadership: " + std::to_string(cap->capacity) + "}\n";
		text += "Governs the military capacity of the hero's army.\n\n";
		if(base > 0)
		{
			text += "- Starting rating: " + std::to_string(base) + "\n";
			text += "- Level growth: +" + std::to_string(perLevel) + " / level (+" + std::to_string(levelGains) + " total)\n";
		}
		if(authorityPercent > 0)
			text += "- Authority Skill: +" + std::to_string(authorityPercent) + "%\n";
		if(trainedLeadership > 0)
			text += "- Bonus Leadership: +" + std::to_string(trainedLeadership) + "\n";
		text += "- Effective rating: " + std::to_string(cap->capacity) + "\n\n";
		text += "Stacks exceeding full-efficiency capacity (C = floor(L / R)) deal smoothly scaled damage via square root:\n";
		text += "N_eff = C + (N - C) * sqrt(C / N)\n";
		text += "Defense, health, speed, and spells remain fully intact.";
		return text;
	}

	const auto value = hero.getArmyCommand();
	if(!value)
		return LIBRARY->generaltexth->translate("new-horizons.command.unavailable");
	const auto rules = hero.getArmyCommandRules();
	auto text = LIBRARY->generaltexth->translate("new-horizons.command.breakdown");
	boost::replace_all(text, "%COMMAND%", std::to_string(value->effective()));
	boost::replace_all(text, "%NATURAL%", std::to_string(value->natural));
	boost::replace_all(text, "%AUTHORITY%", std::to_string(value->authority));
	if(rules)
	{
		auto progression = LIBRARY->generaltexth->translate("new-horizons.command.progression");
		boost::replace_all(progression, "%INTERVAL%", std::to_string(rules->levelInterval));
		text += "\n" + progression;
	}
	text += "\n" + LIBRARY->generaltexth->translate("new-horizons.command.description");
	for(int slot = 0; slot < 7; ++slot)
	{
		const auto view = commandSlotView(value->effective(), SlotID(slot));
		auto unlock = LIBRARY->generaltexth->translate("new-horizons.command.slot.unlock");
		boost::replace_all(unlock, "%SLOT%", slotName(SlotID(slot)));
		boost::replace_all(unlock, "%STATUS%", LIBRARY->generaltexth->translate(view.unlocked
			? "new-horizons.command.slot.unlockedMarker" : "new-horizons.command.slot.lockedMarker"));
		boost::replace_all(unlock, "%REQUIRED%", std::to_string(view.unlockCommand));
		text += "\n" + unlock;
	}
	return text;
}

namespace
{
[[maybe_unused]] std::string authorityHelp(const CGHeroInstance & hero, SlotID slot, const CommandRules & rules)
{
	auto text = "\n" + LIBRARY->generaltexth->translate("new-horizons.command.authority.sources");
	bool any = false;
	for(size_t index = 0; index < AUTHORITY_PERK_KEYS.size(); ++index)
	{
		const std::string id = "new-horizons:authority." + std::string(AUTHORITY_PERK_KEYS[index]);
		if(!hero.hasActivePerk("new-horizons:authority", id))
			continue;
		std::array<bool, 10> selected{};
		selected[index] = true;
		const auto contribution = authoritySlotModifiers(rules, slot, selected);
		if(contribution == CommandModifiers{})
			continue;
		auto source = LIBRARY->generaltexth->translate("new-horizons.command.authority.source");
		boost::replace_all(source, "%PERK%", LIBRARY->generaltexth->translate(
			"new-horizons.command.authority.perk", std::string(AUTHORITY_PERK_KEYS[index])));
		boost::replace_all(source, "%BONUS%", modifiers(contribution));
		text += "\n" + source;
		any = true;
	}
	return text + (any ? "" : "\n" + LIBRARY->generaltexth->translate("new-horizons.command.authority.none"));
}
}

std::string slotHelp(const CGHeroInstance & hero, SlotID slot)
{
	const auto value = hero.getArmyCommand();
	const auto rules = hero.getArmyCommandRules();
	if(!value || !rules)
		return {};
	const auto view = commandSlotView(value->effective(), slot);

	std::string text = "{" + slotName(slot) + "}\n";

	if(!view.unlocked)
	{
		auto lockedText = LIBRARY->generaltexth->translate("new-horizons.command.slot.locked");
		boost::replace_all(lockedText, "%REQUIRED%", std::to_string(view.unlockCommand));
		text += lockedText + "\n";
	}
	else
	{
		text += LIBRARY->generaltexth->translate("new-horizons.command.slot.unlockedMarker") + "\n";
	}

	text += LIBRARY->generaltexth->translate(
		view.grade == CommandSlotGrade::CORE ? "new-horizons.command.slot.core"
		: view.grade == CommandSlotGrade::ELITE ? "new-horizons.command.slot.elite"
		: "new-horizons.command.slot.champion");

	if(view.unlocked)
	{
		if(const auto totals = hero.getCommandSlotModifiers(slot))
		{
			CommandModifiers total = totals->enhancements;
			total.add(totals->authority);
			const auto modText = modifiers(total);
			if(modText != LIBRARY->generaltexth->translate("new-horizons.command.modifier.none"))
			{
				auto label = LIBRARY->generaltexth->translate("new-horizons.command.enhancement.total");
				boost::replace_all(label, "%BONUS%", modText);
				text += "\n\n" + label;
			}
		}
	}

	return text;
}
}
