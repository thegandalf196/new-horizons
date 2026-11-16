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
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "render/Canvas.h"

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
		setHorizontalLayout(252, 80);
	}

	void showAll(Canvas & canvas) override
	{
		canvas.drawColor(pos, ColorRGBA(28, 20, 15, 255));
		canvas.drawBorder(pos, selected ? Colors::BRIGHT_YELLOW : ColorRGBA(140, 105, 45, 255));
		CSelectableComponent::showAll(canvas);
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
			newHorizonsPerkHelp::skillLabel(hero, skill) + "\n" + currentName + " → " + nextName);
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
			perk.name, hero->getPerkState().rules)));
	component->customDescription = newHorizonsPerkHelp::format(hero, perk.selection.skillId,
		perk.name, newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description);
	return component;
}

void CFourOfferLevelWindow::clearChoices()
{
	choiceLabels.clear();
	slotBoxes = {};
	perkBox.reset();
	description.reset();
	instruction.reset();
}

void CFourOfferLevelWindow::createChoices()
{
	OBJECT_CONSTRUCTION;
	clearChoices();
	instruction = std::make_shared<CLabel>(320, 260, FONT_SMALL, ETextAlignment::CENTER,
		Colors::YELLOW, openGroup ? "Choose one perk, or return to the offers." : "Choose one advancement.", 580);
	description = std::make_shared<CTextBox>("", openGroup ? Rect(44, 454, 552, 52) : Rect(44, 470, 552, 36), 0, FONT_SMALL);
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
	constexpr std::array<const char *, 4> unavailable = {"No Skill can advance.", "No new Skill is available.", "No perk group is available.", "No other perk group is available."};
	for(size_t slot = 0; slot < slotBoxes.size(); ++slot)
	{
		const int x = slot % 2 == 0 ? 44 : 344;
		const int y = slot < 2 ? 286 : 384;
		choiceLabels.push_back(std::make_shared<CLabel>(x + 126, y - 13, FONT_SMALL,
			ETextAlignment::CENTER, Colors::YELLOW, titles[slot], 252));
		const bool offered = slot < 2 ? offers.skills[slot] != SecondarySkill::NONE : !offers.perkSkills[slot - 2].empty();
		if(!offered)
		{
			choiceLabels.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x, y, 252, 80),
				ColorRGBA(28, 20, 15, 255), ColorRGBA(100, 75, 35, 255)));
			choiceLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(x + 8, y + 8, 236, 64),
				FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, unavailable[slot]));
			continue;
		}
		auto component = offerComponent(slot);
		slotBoxes[slot] = std::make_shared<CComponentBox>(std::vector{component}, Rect(x, y, 252, 80),
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
		description->setText("No advancement remains available from these offers. Continue to retain your level and attribute gains.");
}

void CFourOfferLevelWindow::createPerkChoices()
{
	OBJECT_CONSTRUCTION;
	const auto & group = perkGroups[*openGroup];
	const auto previous = selectedPerk;
	choiceLabels.push_back(std::make_shared<CLabel>(320, 282, FONT_MEDIUM, ETextAlignment::CENTER,
		Colors::YELLOW, newHorizonsPerkHelp::skillName(hero, group.front().selection.skillId), 552));
	std::vector<std::shared_ptr<CSelectableComponent>> components;
	for(size_t i = page * 4; i < std::min(group.size(), page * 4 + 4); ++i)
	{
		auto component = perkComponent(i);
		components.push_back(std::move(component));
	}
	perkBox = std::make_shared<CComponentBox>(components, Rect(56, 304, 528, 144),
		[this](int index) { if(index >= 0) selectPerk(page * 4 + static_cast<size_t>(index)); }, 56, 10, 16, 2, false);
	if(previous && *previous / 4 == page)
		perkBox->selectIndex(*previous % 4);
	if(group.size() > 4)
	{
		choiceLabels.push_back(std::make_shared<CLabel>(132, 532, FONT_SMALL, ETextAlignment::CENTER,
			Colors::WHITE, std::to_string(page + 1) + "/" + std::to_string((group.size() + 3) / 4)));
	}
}
