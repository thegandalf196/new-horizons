/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CLevelWindow.h"
#include "LevelUpChoiceCards.h"
#include "NewHorizonsCapturedPerkIcons.h"
#include "NewHorizonsPerkHelp.h"
#include "../GameInstance.h"
#include "../widgets/CComponent.h"
#include "../widgets/TextControls.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

std::shared_ptr<CSelectableComponent> CLevelWindow::skillComponent(size_t index) const
{
	const auto skill = skills.at(index);
	const int offeredRank = hero->getSecSkillLevel(skill) + 1;
	const auto rankText = GAME->translator().translate("core.skilllev", offeredRank - 1);
	const auto skillName = newHorizonsPerkHelp::skillLabel(hero, skill);
	std::vector<levelUpPresentation::PerkRow> rows;
	const auto & captured = hero->getPerkState().rules["skills"];
	if(captured.isStruct())
	{
		const auto & definitions = captured.Struct();
		const std::string rawId = skill.toSkill() ? skill.toSkill()->getJsonKey() : "";
		auto definition = definitions.find(rawId);
		if(definition == definitions.end())
			definition = definitions.find(SecondarySkill::encode(skill.getNum()));
		if(definition == definitions.end() && rawId.starts_with("core:"))
			definition = definitions.find("new-horizons:" + rawId.substr(5));
		if(definition == definitions.end() && !rawId.starts_with("new-horizons:"))
			definition = definitions.find("new-horizons:" + rawId);
		if(definition != definitions.end() && definition->second["perks"].isVector())
			for(const auto & perk : definition->second["perks"].Vector())
			{
				if(perk["effect"]["status"].String() != "active"
					|| newHorizonsHeroes::perkRequiredRank(perk["requires"].String()) != offeredRank)
					continue;
				const auto & id = perk["id"].String();
				const auto & name = perk["name"].String();
				if(!hero->getPerkState().hasSelection(definition->first, id))
					rows.push_back({id, name, newHorizonsPerkIcon(id, name, hero->getPerkState().rules)});
			}
	}
	auto component = levelUpPresentation::skillCard(skill, offeredRank, skillName, rankText, rows);
	component->customDescription = newHorizonsPerkHelp::skillDescription(hero, skill, offeredRank);
	component->customSubtitle = rankText + "\n" + skillName;
	return component;
}

std::shared_ptr<CSelectableComponent> CLevelWindow::perkComponent(size_t index) const
{
	const auto & perk = perks.at(index);
	auto skill = newHorizonsPerkHelp::skillEntity(perk.selection.skillId);
	if(!skill && perk.selection.skillId == "new-horizons:necromancy")
		skill = SecondarySkill(SecondarySkill::NECROMANCY);
	auto component = levelUpPresentation::perkCard(skill.value_or(SecondarySkill(SecondarySkill::NONE)), skill.has_value(),
		perk.name, AnimationPath::builtin(newHorizonsPerkIcon(perk.selection.perkId, perk.name, hero->getPerkState().rules)));
	component->customDescription = newHorizonsPerkHelp::format(hero, perk.selection.skillId,
		perk.name, newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description);
	return component;
}

std::string CLevelWindow::perkHeaderText() const
{
	if(perks.empty()) return {};
	const auto & first = perks.front();
	return newHorizonsPerkHelp::tierName(first.requiredRank) + " "
		+ newHorizonsPerkHelp::skillName(hero, first.selection.skillId);
}

void CLevelWindow::refreshPerkDescription(size_t index)
{
	if(perkDescription && index < perks.size())
		perkDescription->setText(perks[index].description);
}
