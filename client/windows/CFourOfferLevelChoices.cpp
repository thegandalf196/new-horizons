/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CFourOfferLevelWindow.h"
#include "LevelUpChoiceCards.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsSylvanHybridPerkIcons.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/TextControls.h"

namespace
{
std::optional<SecondarySkill> groupSkill(const std::string & id)
{
	if(id == "new-horizons:necromancy")
		return SecondarySkill(SecondarySkill::NECROMANCY);
	return newHorizonsPerkHelp::skillEntity(id);
}

class AdvancementComponent : public CSelectableComponent
{
public:
	AdvancementComponent(SecondarySkill skill, int rank, const std::string & caption)
		: CSelectableComponent(ComponentType::SEC_SKILL, skill, rank, CComponent::medium)
	{
		customSubtitle = caption;
		setHorizontalLayout(252, fourOfferLayout::OFFER_HEIGHT);
	}
};
}

std::shared_ptr<CSelectableComponent> CFourOfferLevelWindow::offerComponent(size_t slot) const
{
	if(slot < offers.skills.size())
	{
		const auto skill = offers.skills[slot];
		const int current = currentRanks[slot];
		const auto currentName = current ? newHorizonsPerkHelp::tierName(current) : "Unlearned";
		const auto nextName = newHorizonsPerkHelp::tierName(offers.targetRanks[slot]);
		auto component = std::make_shared<AdvancementComponent>(skill, offers.targetRanks[slot],
			newHorizonsPerkHelp::skillLabel(hero, skill) + "\n" + currentName + " -> " + nextName);
		component->customDescription = newHorizonsPerkHelp::skillDescription(hero, skill, offers.targetRanks[slot]);
		return component;
	}
	const auto & group = perkGroups[slot - 2];
	const auto & id = offers.perkSkills[slot - 2];
	const auto skill = groupSkill(id);
	const int rank = currentRanks[slot];
	auto component = std::make_shared<AdvancementComponent>(skill.value_or(SecondarySkill(SecondarySkill::NONE)), rank,
		newHorizonsPerkHelp::skillName(hero, id) + "\n" + newHorizonsPerkHelp::tierName(rank)
		+ " — choose a perk");
	component->customDescription = "Choose one perk from " + newHorizonsPerkHelp::skillName(hero, id) + ".";
	for(const auto & perk : group)
		component->customDescription += "\n\n" + perk.name + ": " + perk.description;
	return component;
}

std::shared_ptr<CSelectableComponent> CFourOfferLevelWindow::perkComponent(size_t index) const
{
	const auto & perk = perkGroups[*openGroup].at(index);
	const auto skill = groupSkill(perk.selection.skillId);
	auto component = levelUpPresentation::perkCard(skill.value_or(SecondarySkill(SecondarySkill::NONE)), skill.has_value(),
		perk.name, AnimationPath::builtin(newHorizonsHybridCapturedPerkIcon(perk.selection.perkId,
			perk.name, hero->getPerkState().getRules())));
	component->customDescription = newHorizonsPerkHelp::format(hero, perk.selection.skillId,
		perk.name, newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description);
	return component;
}

void CFourOfferLevelWindow::clearChoices()
{
	choiceLabels.clear();
	slotBoxes = {};
	perkBox.reset();
	instruction.reset();
}

void CFourOfferLevelWindow::createChoices()
{
	OBJECT_CONSTRUCTION;
	clearChoices();
	// No description box: right-click an option for its full help.
	instruction = std::make_shared<CLabel>(320, fourOfferLayout::INSTRUCTION, FONT_SMALL, ETextAlignment::CENTER,
		Colors::YELLOW, openGroup ? "Choose one perk, or return to the offers. Right-click a perk for details."
			: "Choose one advancement. Right-click an option for details.", 580);
	if(openGroup)
		createPerkChoices();
	else
		createOfferSlots();
	refreshControls();
	redraw();
}

void CFourOfferLevelWindow::createOfferSlots()
{
	OBJECT_CONSTRUCTION;
	const auto previous = selectedSlot;
	constexpr std::array<const char *, 4> titles = {"Skill I — improve", "Skill II — learn", "Perk I — specialize", "Perk II — explore"};
	constexpr std::array<const char *, 4> unavailable = {"No Skill to improve", "No new Skill", "No perk group", "No other perk group"};
	const ColorRGBA muted(158, 146, 118);
	for(size_t slot = 0; slot < slotBoxes.size(); ++slot)
	{
		const int x = slot % 2 == 0 ? 44 : 344;
		const int y = fourOfferLayout::OFFER_ROW_TOP[slot < 2 ? 0 : 1];
		const int h = fourOfferLayout::OFFER_HEIGHT;
		choiceLabels.push_back(std::make_shared<CLabel>(x + 126, y - 9, FONT_SMALL,
			ETextAlignment::CENTER, Colors::YELLOW, titles[slot], 252));
		const bool offered = slot < 2 ? offers.skills[slot] != SecondarySkill::NONE : !offers.perkSkills[slot - 2].empty();
		if(!offered)
		{
			choiceLabels.push_back(std::make_shared<CLabel>(x + 126, y + h / 2, FONT_SMALL,
				ETextAlignment::CENTER, muted, unavailable[slot], 236));
			continue;
		}
		auto component = offerComponent(slot);
		slotBoxes[slot] = std::make_shared<CComponentBox>(std::vector{component}, Rect(x, y, 252, h),
			[this, slot](int) { selectSlot(slot); }, 0, 0, 0, 1, false);
		slotBoxes[slot]->setShortcuts({vstd::next(EShortcut::SELECT_INDEX_1, static_cast<int>(slot))});
		slotBoxes[slot]->setInputEnabled(slotAvailable(slot));
	}
	for(const auto & box : slotBoxes)
		if(box)
			box->clearSelection();
	selectedSlot.reset();
	if(previous && slotBoxes[*previous] && slotAvailable(*previous))
		slotBoxes[*previous]->selectIndex(0);
	else
		for(size_t slot = 0; slot < slotBoxes.size(); ++slot)
			if(slotBoxes[slot] && slotAvailable(slot))
			{
				slotBoxes[slot]->selectFirst();
				break;
			}
	if(legalReplies.empty())
		instruction->setText("No advancement remains. Confirm to keep your level and attribute gains.");
}

void CFourOfferLevelWindow::createPerkChoices()
{
	OBJECT_CONSTRUCTION;
	const auto & group = perkGroups[*openGroup];
	const auto previous = selectedPerk;
	choiceLabels.push_back(std::make_shared<CLabel>(320, fourOfferLayout::PERK_TITLE, FONT_MEDIUM, ETextAlignment::CENTER,
		Colors::YELLOW, newHorizonsPerkHelp::skillName(hero, group.front().selection.skillId), 552));
	std::vector<std::shared_ptr<CSelectableComponent>> components;
	for(size_t i = page * 4; i < std::min(group.size(), page * 4 + 4); ++i)
	{
		auto component = perkComponent(i);
		components.push_back(std::move(component));
	}
	perkBox = std::make_shared<CComponentBox>(components, Rect(56, fourOfferLayout::PERK_GRID_TOP, 528, 144),
		[this](int index) { if(index >= 0) selectPerk(page * 4 + static_cast<size_t>(index)); }, 56, 10, 16, 2, false);
	if(previous && *previous / 4 == page)
		perkBox->selectIndex(*previous % 4);
	if(group.size() > 4)
	{
		choiceLabels.push_back(std::make_shared<CLabel>(132, fourOfferLayout::CONTROLS + 16, FONT_SMALL, ETextAlignment::CENTER,
			Colors::WHITE, std::to_string(page + 1) + "/" + std::to_string((group.size() + 3) / 4)));
	}
}
