/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"
#include "NewHorizonsOrderPresentation.h"
#include "../CPlayerInterface.h"
#include "../widgets/Buttons.h"
#include "../widgets/TextControls.h"

void BattleHeroActionWindow::addOrderDetailsButton(HeroCommand command, Point position,
	const std::shared_ptr<CButton> & order, const std::shared_ptr<CMultiLineLabel> & effect)
{
	OBJECT_CONSTRUCTION;
	const std::weak_ptr<CButton> orderButton = order;
	const std::weak_ptr<CMultiLineLabel> effectLabel = effect;
	// Sibling of the command button, outside its 64px hit box. Reading help
	// never selects a target, submits a command or consumes a Hero Action.
	auto info = std::make_shared<CButton>(position, AnimationPath::builtin("NH_info_button_small"),
		CButton::tooltip("Order information", "Read this Order's effect and detailed rules."),
		[battle = battle, orderButton, effectLabel, command]()
		{
			const auto owner = battle.lock();
			const auto button = orderButton.lock();
			if(!owner || !owner->curInt || !button)
				return;
			const auto & display = newHorizonsOrderPresentation::commandDisplay(command);
			auto details = button->getHelp();
			if(const auto label = effectLabel.lock(); label && !label->getText().empty()
				&& details.find(label->getText()) == std::string::npos)
				details += "\n\nCurrent effect: " + label->getText();
			owner->curInt->showInfoDialog(std::string("{") + display.name + "}\n\n" + display.summary,
				{}, 0, details);
		});
	info->setHoverable(true);
	labels.push_back(std::move(info));
}
