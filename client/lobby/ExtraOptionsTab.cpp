/*
 * ExtraOptionsTab.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "ExtraOptionsTab.h"
#include "CSelectionBase.h"
#include "../CServerHandler.h"
#include "../GameInstance.h"
#include "../widgets/Images.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/texts/MetaString.h"
#include "../../lib/texts/CGeneralTextHandler.h"

ExtraOptionsTab::ExtraOptionsTab()
	: OptionsTabBase(JsonPath::builtin("config/widgets/extraOptionsTab.json"))
{
	if(auto textureCampaignOverdraw = widget<CFilledTexture>("textureCampaignOverdraw"))
		textureCampaignOverdraw->disable();

	addCallback("setNeutralArmySize", [this](int index) { setNeutralArmySize(index); });
	build(JsonNode(JsonPath::builtin("config/widgets/neutralArmySizeOptions.json")));
}

bool ExtraOptionsTab::canChangeNeutralArmySize() const
{
	return SEL && SEL->getStartInfo() && SEL->screenType == ESelectionScreen::newGame
		&& SEL->getStartInfo()->mode == EStartMode::NEW_GAME
		&& GAME->server().isHost() && !GAME->server().battleMode;
}

void ExtraOptionsTab::setNeutralArmySize(int index)
{
	if(!canChangeNeutralArmySize() || index < 0 || index >= 100)
		return;

	auto info = SEL->getStartInfo()->extraOptionsInfo;
	if(info.neutralArmySizePercent == index + 1)
		return;
	info.neutralArmySizePercent = index + 1;
	info.validateNeutralArmySize();
	GAME->server().setExtraOptionsInfo(info);
}

void ExtraOptionsTab::recreate(bool campaign)
{
	OptionsTabBase::recreate(campaign);
	const auto * start = SEL->getStartInfo();
	const auto selected = start->extraOptionsInfo.neutralArmySizePercent;
	const int installed = newHorizonsNeutralArmies::percentage(*LIBRARY->engineSettings());
	const int percent = selected.value_or(installed);

	const bool editable = canChangeNeutralArmySize();
	if(auto slider = widget<CSlider>("sliderNeutralArmySize"))
	{
		if(slider->getValue() != percent - 1)
			slider->scrollTo(percent - 1, false);
		slider->block(!editable);
		slider->setInputEnabled(editable);
	}

	std::string text;
	if(selected)
	{
		const auto fraction = percent % 100;
		text = std::to_string(percent) + "% (" + std::to_string(percent / 100)
			+ "." + (fraction < 10 ? "0" : "") + std::to_string(fraction) + "x)";
	}
	else
		text = LIBRARY->generaltexth->translate(start->mode == EStartMode::LOAD_GAME
			? "vcmi.optionsTab.neutralArmySize.saved" : "vcmi.optionsTab.neutralArmySize.default");

	MetaString help;
	help.appendTextID("vcmi.optionsTab.neutralArmySize.help");
	help.replaceNumber(installed);
	if(auto label = widget<CMultiLineLabel>("labelNeutralArmySizeHelp"))
		if(label->getText() != help.toString(&GAME->translator()))
			label->setText(help.toString(&GAME->translator()));

	if(auto label = widget<CLabel>("labelNeutralArmySizeValue"))
		if(label->getText() != text)
			label->setText(text);
}
