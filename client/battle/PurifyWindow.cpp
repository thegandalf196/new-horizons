/*
 * PurifyWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PurifyWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "render/IImage.h"
#include "render/IRenderHandler.h"

#include <algorithm>
#include <optional>
#include <ranges>

namespace
{
constexpr int WINDOW_WIDTH = 640;
constexpr int WINDOW_HEIGHT = 500;
constexpr int ROW_HEIGHT = 36;
constexpr size_t VISIBLE_ROWS = 8;

struct PurifyOption
{
	int32_t unitId = -1;
	std::string stackName;
	SpellID spell;
	std::string spellName;
	int maximumSpellEffectChoices = 1;
	bool automaticPhysicalPoison = false;
};

size_t selectedCountFor(const PurifyEffectSelection & selected, int32_t unitId)
{
	return static_cast<size_t>(std::ranges::count_if(selected, [unitId](const auto & choice)
	{
		return choice.first == unitId;
	}));
}
}

struct PurifyWindow::SelectionModel
{
	std::vector<PurifyOption> options;
	PurifyEffectSelection selected;
};

namespace
{
class PurifyEffectRow final : public CIntObject
{
	std::weak_ptr<PurifyWindow::SelectionModel> selection;
	PurifyOption option;
	std::function<void()> changed;
	std::shared_ptr<CLabel> stackLabel;
	std::shared_ptr<CLabel> spellLabel;
	std::shared_ptr<CButton> chooseButton;

	bool isSelected(const PurifyWindow::SelectionModel & model) const
	{
		return !option.automaticPhysicalPoison
			&& std::ranges::find(model.selected, std::pair{option.unitId, option.spell}) != model.selected.end();
	}

	void toggle()
	{
		if(option.automaticPhysicalPoison)
			return;
		auto model = selection.lock();
		if(!model)
			return;

		const auto choice = std::pair{option.unitId, option.spell};
		const auto found = std::ranges::find(model->selected, choice);
		if(found != model->selected.end())
			model->selected.erase(found);
		else if(selectedCountFor(model->selected, option.unitId) < static_cast<size_t>(option.maximumSpellEffectChoices))
			model->selected.push_back(choice);

		if(changed)
			changed();
	}

public:
	PurifyEffectRow(std::weak_ptr<PurifyWindow::SelectionModel> selection_, PurifyOption option_, std::function<void()> changed_)
		: selection(std::move(selection_)), option(std::move(option_)), changed(std::move(changed_))
	{
		pos.w = 560;
		pos.h = ROW_HEIGHT;
		OBJECT_CONSTRUCTION;
		stackLabel = std::make_shared<CLabel>(4, 1, FONT_TINY, ETextAlignment::TOPLEFT, Colors::YELLOW, "");
		spellLabel = std::make_shared<CLabel>(4, 17, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, option.spellName);
		stackLabel->setMaxWidth(452);
		spellLabel->setMaxWidth(452);
		chooseButton = std::make_shared<CButton>(Point(466, 1), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Select effect group"), [this] { toggle(); });
		chooseButton->setHoverable(true);
		refresh();
	}

	void refresh()
	{
		auto model = selection.lock();
		const bool selected = model && isSelected(*model);
		const auto selectedForStack = model ? selectedCountFor(model->selected, option.unitId) : 0;
		const bool atLimit = selectedForStack >= static_cast<size_t>(option.maximumSpellEffectChoices);
		stackLabel->setText(option.stackName + (option.automaticPhysicalPoison ? "" : "  "
			+ std::to_string(selectedForStack) + "/" + std::to_string(option.maximumSpellEffectChoices)));
		chooseButton->setTextOverlay(option.automaticPhysicalPoison ? "Auto" : (selected ? "Chosen" : "Choose"),
			FONT_TINY, selected ? Colors::YELLOW : Colors::WHITE);
		chooseButton->setBorderColor(selected ? std::optional<ColorRGBA>(Colors::YELLOW) : std::nullopt);
		chooseButton->block(option.automaticPhysicalPoison || !model || (!selected && atLimit));
		const bool physicalPoisonChoice = option.spell == SpellID::NONE && !option.automaticPhysicalPoison;
		chooseButton->setHelp(CButton::tooltip(option.automaticPhysicalPoison ? "Automatic Purify effect"
			: (physicalPoisonChoice ? "Select physical Poison"
				: (selected ? "Remove this effect group" : "Select this effect group")),
			option.automaticPhysicalPoison ? "Purifier removes this physical Poison without using a selection slot."
				: (physicalPoisonChoice ? "Remove this stack's stored physical Poison as one per-stack choice."
					: (selected ? "Remove this source-spell group from the Purify selection."
						: (atLimit ? "This stack has reached its per-stack Purify limit."
							: "Choose this temporary negative spell effect group for this stack.")))));
	}
};
}

PurifyWindow::PurifyWindow(PurifyContext context_)
	: CWindowObject(BORDERED), context(std::move(context_)), selection(std::make_shared<SelectionModel>())
{
	pos.w = WINDOW_WIDTH;
	pos.h = WINDOW_HEIGHT;
	center();
	for(const auto & stack : context.stacks)
	{
		for(const auto & [spell, spellName] : stack.eligibleEffects)
			selection->options.push_back({stack.unitId, stack.stackName, spell, spellName, stack.maximumSpellEffectChoices, false});
		if(stack.physicalPoisonAutomaticallyCleared)
			selection->options.push_back({stack.unitId, stack.stackName, SpellID::NONE,
				"Physical Poison (Purifier removes this extra effect)", stack.maximumSpellEffectChoices, true});
	}

	OBJECT_CONSTRUCTION;
	auto leather = ENGINE->renderHandler().createImage(Point(WINDOW_WIDTH, WINDOW_HEIGHT), CanvasScalingPolicy::AUTO);
	auto canvas = leather->getCanvas();
	canvas.fillTexture(ENGINE->renderHandler().loadImage(ImageLocator(ImagePath::builtin("DiBoxBck"), EImageBlitMode::OPAQUE)));
	background = std::make_shared<CPicture>(std::static_pointer_cast<IImage>(leather), Point(0, 0));

	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 19, FONT_MEDIUM,
		ETextAlignment::CENTER, Colors::YELLOW, "Purify"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 41, FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, "Choose negative magical or bodily effects within two hexes"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 60, FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE,
		"Center: row " + std::to_string(context.center.getY() + 1) + ", column " + std::to_string(context.center.getX() + 1)));
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(13, 81, 614, 307),
		ColorRGBA(26, 23, 18, 155), ColorRGBA(119, 93, 56, 255)));

	const std::weak_ptr<SelectionModel> weakSelection = selection;
	options = std::make_shared<CListBox>([weakSelection, this](size_t index) -> std::shared_ptr<CIntObject>
	{
		auto model = weakSelection.lock();
		if(!model || index >= model->options.size())
			return std::make_shared<CIntObject>();
		return std::make_shared<PurifyEffectRow>(weakSelection, model->options[index], [this] { refresh(); });
	}, Point(22, 91), Point(0, ROW_HEIGHT), VISIBLE_ROWS, selection->options.size(), 0,
		selection->options.size() > VISIBLE_ROWS ? 1 : 0, Rect(584, 0, 288, 288));

	stateLabel = std::make_shared<CMultiLineLabel>(Rect(18, 393, WINDOW_WIDTH - 36, 37),
		FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	confirmButton = std::make_shared<CButton>(Point(436, 445), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm Purify", "Submit the selected stack and effect choices through the normal authoritative hero-spell request."),
		[this] { confirm(); });
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);
	cancelButton = std::make_shared<CButton>(Point(536, 445), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Return to battle without spending Mana or the Hero Action."),
		[this] { cancel(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);
	refresh();
}

void PurifyWindow::refresh()
{
	if(options)
		for(const auto & item : options->getItems())
			if(auto row = std::dynamic_pointer_cast<PurifyEffectRow>(item))
				row->refresh();

	const size_t selectedStacks = std::ranges::count_if(context.stacks, [this](const PurifyStackChoices & stack)
	{
		return selectedCountFor(selection->selected, stack.unitId) > 0;
	});
	std::string state = "Selected " + std::to_string(selection->selected.size()) + " effect(s) across "
		+ std::to_string(selectedStacks) + " stack(s). Maximum "
		+ std::to_string(context.maximumSpellEffectChoices) + " per stack.";
	if(context.purifierWillClearPhysicalPoison)
		state += "\nPurifier also removes physical Poison from each affected stack.";
	else if(selection->options.empty())
		state = "No eligible temporary spell effects are in this area. Confirm is unavailable.";
	if(stateLabel && stateLabel->getText() != state)
		stateLabel->setText(state);

	const bool hasSelectableGroups = !selection->selected.empty();
	confirmButton->block(!context.confirm || (!hasSelectableGroups && !context.purifierWillClearPhysicalPoison));
}

void PurifyWindow::confirm()
{
	if(!context.confirm || (selection->selected.empty() && !context.purifierWillClearPhysicalPoison))
	{
		refresh();
		return;
	}
	if(context.confirm(selection->selected))
		close();
	else
	{
		stateLabel->setText("The battle changed. Review the current choices or Cancel.");
		confirmButton->block(true);
	}
}

void PurifyWindow::cancel()
{
	if(context.cancel)
		context.cancel();
	close();
}
