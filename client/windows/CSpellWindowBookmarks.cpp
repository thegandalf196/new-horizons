/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "CSpellWindow.h"
#include "SpellbookPrayers.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Images.h"
#include "../widgets/CTextInput.h"
#include "render/CAnimation.h"
#include "render/IRenderHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/SpellSchoolHandler.h"
#include <algorithm>

int getAnimFrameFromSchool(SpellSchool school);
bool isLegacySpellSchool(SpellSchool school);

void CSpellWindow::prepareSchoolNavigation()
{
	readSchoolContext();
	int maxCustomSchools = (isBigSpellbook ? MAX_CUSTOM_SPELL_SCHOOLS_BIG : MAX_CUSTOM_SPELL_SCHOOLS) * 2;
	int customSchoolsAvailable = 0;
	std::vector<SpellSchool> sortedSchools = availableSchools;
	if(usesLegacyTabs)
		std::ranges::sort(sortedSchools, [&](SpellSchool a, SpellSchool b) {
			auto cnt = [&](SpellSchool s) {
				return std::ranges::count_if(LIBRARY->spellh->objects, [&](auto const & sp) {
					return sp && rosterSpellIDs.contains(sp->getId())
						&& myHero->canCastThisSpell(sp.get()) && spellSchools.at(sp->getId()).count(s);
				});
			};
			return cnt(a) > cnt(b);
		});
	// New school tabs retain the saved context's order as spells are learned.
	for(const auto schoolId : sortedSchools)
		if(
			!isLegacySpellSchool(schoolId) &&
			!LIBRARY->spellSchoolHandler->getById(schoolId)->getSchoolBookmarkPath().empty() &&
			!LIBRARY->spellSchoolHandler->getById(schoolId)->getSchoolHeaderPath().empty()
		)
		{
			customSchoolsAvailable++;
			if(customSpellSchools.size() < maxCustomSchools)
				customSpellSchools.push_back(schoolId);
		}

	if(customSchoolsAvailable > maxCustomSchools)
		logGlobal->warn("Too many custom spell schools (%d) — showing only first %d", customSchoolsAvailable, maxCustomSchools);

	if(usesLegacyTabs)
	{
		for(const auto school : {SpellSchool::AIR, SpellSchool::EARTH, SpellSchool::FIRE, SpellSchool::WATER})
			if(vstd::contains(availableSchools, school))
				schoolNavigation.push_back(school);
		schoolNavigation.push_back(SpellSchool::ANY);
	}
	schoolNavigation.insert(schoolNavigation.end(), customSpellSchools.begin(), customSpellSchools.end());
	if(!usesLegacyTabs)
		schoolNavigation.push_back(SpellSchool::ANY);

}

void CSpellWindow::createSchoolBookmarks()
{
	OBJECT_CONSTRUCTION;
	if(usesLegacyTabs)
		schoolTab = std::make_shared<CAnimImage>(AnimationPath::builtin("SpelTab"), getAnimFrameFromSchool(selectedTab), 0, 524 + offR, 88);
	else
	{
		// Restore this region from the actual book background, including its edge.
		// SpelBack and generated SpellBookLarge contain no school emblems: the
		// legacy four-school strip is a separate SpelTab image, created only above.
		// Reuse the already player-colored surface, at the same source coordinates.
		schoolTabPanel = std::make_shared<CPicture>(background->getSurface(), Rect(524 + offR, 88, 83, 294), 524 + offR, 88);
		schoolTabPanel->removeUsedEvents(LCLICK | SHOW_POPUP);
		// SPELTAB is a five-bookmark strip: frame 4 selects All, while frame 0
		// leaves All inactive. Crop only the yellow bookmark, at native size.
		// Resolve purchaser resources at runtime; never ship extracted pixels.
		auto tabs = ENGINE->renderHandler().loadAnimation(AnimationPath::builtin("SPELTAB"), EImageBlitMode::COLORKEY);
		const Rect allBookmark(0, 236, 83, 57);
		allSchoolsInactive = std::make_shared<CPicture>(tabs->getImage(0), allBookmark, 524 + offR, 324);
		allSchoolsSelected = std::make_shared<CPicture>(tabs->getImage(4), allBookmark, 524 + offR, 324);
		// Cropped CPicture registers input by default; these images are decoration.
		allSchoolsInactive->removeUsedEvents(LCLICK | SHOW_POPUP);
		allSchoolsSelected->removeUsedEvents(LCLICK | SHOW_POPUP);
		// Preserve the previous 64x64 All control's hit/help footprint.
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(Rect(534 + offR + pos.x, 318 + pos.y, 64, 64),
			std::bind(&CSpellWindow::selectSchool, this, SpellSchool::ANY), 458, this));
	}
	const bool prayersAvailable = SpellbookPrayers::availableFor(*myHero);
	const int customSchoolCount = customSpellSchools.size();
	const int bookmarkCount = customSchoolCount + int(prayersAvailable);
	const int fullSizeCapacity = isBigSpellbook ? MAX_CUSTOM_SPELL_SCHOOLS_BIG : MAX_CUSTOM_SPELL_SCHOOLS;
	constexpr int bookmarkWidth = 80;
	constexpr int bookmarkHeight = 60;
	constexpr int bookmarkSpacing = 62;
	constexpr int yStart = 93;
	const bool compactBookmarks = bookmarkCount > fullSizeCapacity;
	const int stripHeight = (fullSizeCapacity - 1) * bookmarkSpacing + bookmarkHeight;
	const int step = compactBookmarks ? stripHeight / bookmarkCount : bookmarkSpacing;
	const int scaledHeight = std::min(bookmarkHeight, step);
	const int scaledWidth = bookmarkWidth * scaledHeight / bookmarkHeight;
	for(int i = 0; i < customSchoolCount; i++)
	{
		const auto path = LIBRARY->spellSchoolHandler->getById(customSpellSchools[i])->getSchoolBookmarkPath();
		const Point position(isBigSpellbook ? 0 : 15, yStart + step * i);
		// Six schools must fit the small book without overlapping their glyphs.
		// Keep ordinary layouts unchanged; use the existing aspect-preserving
		// image constructor only when the reserved strip would otherwise overflow.
		if(compactBookmarks)
			schoolTabCustom.push_back(std::make_shared<CAnimImage>(path, 1, Rect(position, Point(scaledWidth, scaledHeight))));
		else
			schoolTabCustom.push_back(std::make_shared<CAnimImage>(path, 1, 0, position.x, position.y));
	}
	if(prayersAvailable)
	{
		const Point position(isBigSpellbook ? 0 : 15, yStart + step * customSchoolCount);
		prayerTab = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_prayers_bookmark"), 1,
			Rect(position, Point(scaledWidth, scaledHeight)));
	}
}

void CSpellWindow::createSchoolBookmarkAreas()
{
	OBJECT_CONSTRUCTION;
	Rect schoolRect(549 + pos.x + offR, 94 + pos.y, 45, 35);
	const int customSchoolCount = customSpellSchools.size();
	if(usesLegacyTabs)
	{
		interactiveAreas.push_back(std::make_shared<InteractiveArea>( schoolRect + Point(0, 0),   std::bind(&CSpellWindow::selectSchool,   this, SpellSchool::AIR), 454, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>( schoolRect + Point(0, 57),  std::bind(&CSpellWindow::selectSchool,   this, SpellSchool::EARTH), 457, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>( schoolRect + Point(0, 116), std::bind(&CSpellWindow::selectSchool,   this, SpellSchool::FIRE), 455, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>( schoolRect + Point(0, 176), std::bind(&CSpellWindow::selectSchool,   this, SpellSchool::WATER), 456, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>( schoolRect + Point(0, 236), std::bind(&CSpellWindow::selectSchool,   this, SpellSchool::ANY), 458, this));
	}
	for(int i = 0; i < customSchoolCount; i++)
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(schoolTabCustom[i]->pos, std::bind(&CSpellWindow::selectSchool, this, customSpellSchools[i]), LIBRARY->spellSchoolHandler->getById(customSpellSchools[i])->getNameTextID(), this));
	if(prayerTab)
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(prayerTab->pos,
			[this] { selectPrayers(); }, "Prayers",
			"Inspect the prayers granted automatically by Consecrated Arms when this hero issues an Order.", this));
}

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & bounds, const std::function<void()> & action,
	std::string hover, std::string help, CSpellWindow * book)
	: onLeft(action), owner(book), hoverText(std::move(hover)), helpText(std::move(help))
{
	pos = bounds;
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
}

void CSpellWindow::createPrayerCatalogue()
{
	if(!prayerTab)
		return;
	OBJECT_CONSTRUCTION;
	const int x = 100 + offL;
	prayerCatalogue = std::make_shared<SpellbookPrayers>(
		Rect(x, 70 + offT, 425 + offR - offL, 330 + offB - offT),
		236 + offRM - offL, isBigSpellbook, *myHero, statusBar);
	prayerCatalogue->setEnabled(false);
}

void CSpellWindow::selectPrayers()
{
	if(!prayerCatalogue || prayerCatalogueSelected)
		return;
	prayerCatalogueSelected = true;
	setCurrentPage(currentPage);
	computeSpellsPerArea();
}

bool CSpellWindow::showPrayerCatalogue()
{
	if(prayerCatalogue)
		prayerCatalogue->setEnabled(prayerCatalogueSelected);
	if(!prayerCatalogueSelected)
		return false;
	for(int index = 0; index < spellsPerPage; ++index)
		spellAreas[index]->setSpell(nullptr);
	prayerCatalogue->filter(searchBox ? searchBox->getText() : std::string());
	updateAdventureSpellDailyStatus();
	redraw();
	return true;
}

void CSpellWindow::navigateSpellbookTabs(bool down)
{
	if(schoolNavigation.empty())
		return;
	if(prayerCatalogueSelected)
	{
		selectSchool(down ? schoolNavigation.front() : schoolNavigation.back());
		return;
	}
	const auto current = std::find(schoolNavigation.begin(), schoolNavigation.end(), selectedTab);
	const int index = std::distance(schoolNavigation.begin(), current);
	const int count = static_cast<int>(schoolNavigation.size());
	if(prayerCatalogue && ((down && index == count - 1) || (!down && index == 0)))
	{
		selectPrayers();
		return;
	}
	selectSchool(schoolNavigation[(index + (down ? 1 : -1) + count) % count]);
}
