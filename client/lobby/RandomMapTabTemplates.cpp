/*
 * RandomMapTabTemplates.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "RandomMapTab.h"

#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/ComboBox.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/rmg/CRmgTemplateStorage.h"
#include "../../lib/texts/CGeneralTextHandler.h"

void RandomMapTab::initializeTemplateSelector()
{
	if(auto w = widget<ComboBox>("templateList"))
	{
		auto getTemplates = [](){
			auto templates = LIBRARY->tplh->getTemplates();
			std::ranges::sort(templates, [](const CRmgTemplate * a, const CRmgTemplate * b){
				return a->getName() < b->getName();
			});
			return templates;
		};

		w->onConstructItems = [getTemplates](std::vector<const void *> & curItems){
			curItems.push_back(nullptr); //default template

			for(auto & t : getTemplates())
				curItems.push_back(t);
		};

		w->onSetItem = [&](const void * item){
			this->setTemplate(reinterpret_cast<const CRmgTemplate *>(item));
		};

		w->getItemText = [this](int idx, const void * item){
			if(item)
				return reinterpret_cast<const CRmgTemplate *>(item)->getName();
			if(idx == 0)
				return readText(variables["randomTemplate"]);
			return std::string("");
		};

		w->addCallback([this, getTemplates]() // no real dropdown... - instead open dialog
		{
			std::vector<std::string> texts;
			std::vector<std::string> popupTexts;
			texts.push_back(readText(variables["randomTemplate"]));
			popupTexts.push_back("");

			templateIndex = 0;
			auto selectedTemplate = mapGenOptions->getMapTemplate();
			const auto& templates = getTemplates();
			for(int i = 0; i < templates.size(); i++)
			{
				if(selectedTemplate)
				{
					if(templates[i]->getId() == selectedTemplate->getId())
						templateIndex = i + 1;
				}
				texts.push_back(templates[i]->getName());
				popupTexts.push_back("{" + templates[i]->getName() + "}" + (templates[i]->getDescription().empty() ? "" : "\n\n") + templates[i]->getDescription());
			}

			ENGINE->windows().popWindows(1);
			auto window = std::make_shared<CObjectListWindow>(texts, nullptr, LIBRARY->generaltexth->translate("vcmi.lobby.templatesSelect.hover"), LIBRARY->generaltexth->translate("vcmi.lobby.templatesSelect.help"), [this](int index){
				widget<ComboBox>("templateList")->setItem(index);
			}, templateIndex, std::vector<std::shared_ptr<IImage>>(), true, true);
			window->onPopup = [popupTexts](int index){
				if(!popupTexts[index].empty())
					CRClickPopup::createAndPush(popupTexts[index]);
			};
			ENGINE->windows().pushWindow(window);
		});
	}
}

void RandomMapTab::updateTemplateSelection()
{
	const auto * selectedTemplate = mapGenOptions->getMapTemplate();
	const auto text = selectedTemplate ? selectedTemplate->getName() : readText(variables["randomTemplate"]);
	for(const auto * name : {"templateButton", "templateList"})
	{
		if(auto button = widget<CButton>(name))
		{
			OBJECT_CONSTRUCTION_TARGETED(button.get());
			// Keep centered text clear of the stock field's arrow and bevels.
			auto label = std::make_shared<CLabel>(0, 0, EFonts::FONT_SMALL, ETextAlignment::CENTER,
				Colors::WHITE, text, std::max(1, button->pos.w - 32));
			button->setOverlay(label);
		}
	}
}

void RandomMapTab::setTemplate(const CRmgTemplate * tmpl)
{
	mapGenOptions->setMapTemplate(tmpl);
	customMapSizeMode = false;
	setMapGenOptions(mapGenOptions);
	updateMapInfoByHost();
}
