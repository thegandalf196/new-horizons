/*
 * OptionsTab.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "OptionsTab.h"

#include "CSelectionBase.h"
#include "../widgets/CComponent.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../windows/HeroStartingPreview.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
std::vector<size_t> associatedCreatureLevels(const CTown & town)
{
	std::vector<size_t> levels;
	const auto append = [&](size_t level)
	{
		if(level < town.creatures.size() && !town.creatures[level].empty() && !vstd::contains(levels, level))
			levels.push_back(level);
	};

	// Castle's construction order can differ from its stable recruitment indices.
	if(town.faction->getId() == FactionID::CASTLE)
		for(const auto & row : town.clientInfo.hallSlots)
			for(const auto & slot : row)
				for(const auto building : slot)
					if(building.isDwelling())
						append(BuildingID::getLevelIndexFromDwelling(building));

	for(size_t level = 0; level < town.creatures.size(); ++level)
		append(level);
	return levels;
}
}

OptionsTab::CPlayerOptionTooltipBox::CPlayerOptionTooltipBox(CPlayerSettingsHelper & helper)
	: CWindowObject(BORDERED | RCLICK_POPUP), CPlayerSettingsHelper(helper)
{
	OBJECT_CONSTRUCTION;

	switch(selectionType)
	{
		case TOWN:
			genTownWindow();
			break;
		case HERO:
			genHeroWindow();
			break;
		case BONUS:
			genBonusWindow();
			break;
	}

	center();
}

void OptionsTab::CPlayerOptionTooltipBox::genHeader()
{
	backgroundTexture = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), pos);
	updateShadow();

	labelTitle = std::make_shared<CLabel>(pos.w / 2 + 8, 21, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, getTitle());
	labelSubTitle = std::make_shared<CLabel>(pos.w / 2, 88, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, getSubtitle());
	image = std::make_shared<CAnimImage>(getImageName(), getImageIndex(), 0, pos.w / 2 - 24, 45);
}

void OptionsTab::CPlayerOptionTooltipBox::genTownWindow()
{
	auto factionIndex = playerSettings.getCastleValidated();

	if (playerSettings.castle == FactionID::RANDOM)
		return genBonusWindow();

	pos = Rect(0, 0, 228, 290);
	genHeader();
	labelAssociatedCreatures = std::make_shared<CLabel>(pos.w / 2 + 8, 122, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->allTexts[79]);
	std::vector<std::shared_ptr<CComponent>> components;
	const CTown * town = (*LIBRARY->townh)[factionIndex]->town.get();

	for(const size_t level : associatedCreatureLevels(*town))
		components.push_back(std::make_shared<CComponent>(ComponentType::CREATURE,
			town->creatures[level].front(), std::nullopt, CComponent::tiny));
	boxAssociatedCreatures = std::make_shared<CComponentBox>(components, Rect(10, 140, pos.w - 20, 140), 20, 10, 22, 4);
}

void OptionsTab::CPlayerOptionTooltipBox::genHeroWindow()
{
	auto heroIndex = playerSettings.getHeroValidated();

	if (playerSettings.hero == HeroTypeID::RANDOM)
		return genBonusWindow();

	pos = Rect(0, 0, 292, 226);
	genHeader();
	labelHeroSpeciality = std::make_shared<CLabel>(pos.w / 2 + 4, 117, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->allTexts[78]);

	const auto starting = heroStartingPreview(heroIndex, SEL->getMapInfo(), playerSettings.color);
	imageSpeciality = std::make_shared<CAnimImage>(starting.specialty.animation(), starting.specialty.frame(),
		Rect(pos.w / 2 - 22, 134, 44, 44));
	labelSpecialityName = std::make_shared<CLabel>(pos.w / 2, 188, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, starting.specialty.name);
}

void OptionsTab::CPlayerOptionTooltipBox::genBonusWindow()
{
	pos = Rect(0, 0, 228, 162);
	genHeader();

	textBonusDescription = std::make_shared<CTextBox>(getDescription(), Rect(10, 100, pos.w - 20, 70), 0, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);
}
