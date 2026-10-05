/*
 * CSpellWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SpellPointPresentation.h"
#include "CSpellWindow.h"

#include "../../lib/ScopeGuard.h"
#include "../../lib/CSkillHandler.h"

#include "GUIClasses.h"
#include "InfoWindows.h"
#include "CCastleInterface.h"

#include "../CPlayerInterface.h"
#include "../PlayerLocalState.h"

#include "../battle/BattleInterface.h"
#include "../battle/BattleActionsController.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "media/IVideoPlayer.h"
#include "render/CAnimation.h"
#include "render/IRenderHandler.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/CComponent.h"
#include "../widgets/CTextInput.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/VideoWidget.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "events/InputHandler.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/spells/adventure/AdventureSpellEffect.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/SpellSchoolHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"

namespace
{
std::string spellAllowanceSourceName(HeroActionAllowanceState::GrantSource source)
{
	switch(source)
	{
		case HeroActionAllowanceState::GrantSource::ROUND: return {};
		case HeroActionAllowanceState::GrantSource::METAMAGIC: return "Metamagic";
		case HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND: return "Grand Metamagic";
		case HeroActionAllowanceState::GrantSource::PERK: return "Perk";
		case HeroActionAllowanceState::GrantSource::ARTIFACT: return "Artifact";
		case HeroActionAllowanceState::GrantSource::OTHER: return "Special ability";
		case HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND: return "Double Command";
		case HeroActionAllowanceState::GrantSource::BATTLE_PLAN: return "Battle Plan";
		case HeroActionAllowanceState::GrantSource::DIVINE_MANDATE: return "Divine Mandate";
	}
	return "Additional ability";
}
}

// Ordering of spell school tabs in SpelTab.def
static const std::array schoolTabOrder =
{
	SpellSchool::AIR,
	SpellSchool::FIRE,
	SpellSchool::WATER,
	SpellSchool::EARTH,
	SpellSchool::ANY
};

int getAnimFrameFromSchool(SpellSchool school)
{
	auto it = std::find(schoolTabOrder.begin(), schoolTabOrder.end(), school);
	if (it != schoolTabOrder.end())
		return std::distance(schoolTabOrder.begin(), it);
	else
		return -1;
}

bool isLegacySpellSchool(SpellSchool school)
{
	return getAnimFrameFromSchool(school) != -1;
}

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & myRect, const std::function<void()> & funcL, int helpTextId, CSpellWindow * _owner)
{
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	pos = myRect;
	onLeft = funcL;
	hoverText = LIBRARY->generaltexth->zelp[helpTextId].first;
	helpText = LIBRARY->generaltexth->zelp[helpTextId].second;
	spellPointsHelp = helpTextId == 459;
	owner = _owner;
}

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & myRect, const std::function<void()> & funcL, std::string textId, CSpellWindow * _owner)
{
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	pos = myRect;
	onLeft = funcL;
	auto hoverTextTmp = MetaString::createFromTextID("vcmi.spellBook.tab.hover");
	hoverTextTmp.replaceTextID(textId);
	hoverText = hoverTextTmp.toString(&GAME->translator());
	auto helpTextTmp = MetaString::createFromTextID("vcmi.spellBook.tab.help");
	helpTextTmp.replaceTextID(textId);
	helpText = helpTextTmp.toString(&GAME->translator());
	owner = _owner;
}

void CSpellWindow::InteractiveArea::clickPressed(const Point & cursorPosition)
{
	ENGINE->input().hapticFeedback();
	onLeft();
}

void CSpellWindow::InteractiveArea::showPopupWindow(const Point & cursorPosition)
{
	CRClickPopup::createAndPush(spellPointsHelp
		? spellPointPresentation::tooltip(owner->myHero->getManaAvailable(),
			owner->myHero->manaLimit(), owner->myHero->getBufferSpellPoints())
		: helpText);
}

void CSpellWindow::InteractiveArea::hover(bool on)
{
	if(on)
		owner->statusBar->write(hoverText);
	else
		owner->statusBar->clear();
}

class SpellbookSpellSorter
{
	const std::vector<SpellSchool> & availableSchools;
	const std::map<SpellID, std::set<SpellSchool>> & spellSchools;
	const std::map<SpellID, int> & spellLevels;
public:
	SpellbookSpellSorter(const std::vector<SpellSchool> & availableSchools, const std::map<SpellID, std::set<SpellSchool>> & spellSchools, const std::map<SpellID, int> & spellLevels)
		: availableSchools(availableSchools), spellSchools(spellSchools), spellLevels(spellLevels)
	{
	}

	bool operator()(const CSpell * A, const CSpell * B)
	{
		if(spellLevels.at(A->getId()) < spellLevels.at(B->getId()))
			return true;
		if(spellLevels.at(A->getId()) > spellLevels.at(B->getId()))
			return false;

		const auto & schoolsA = spellSchools.at(A->getId());
		const auto & schoolsB = spellSchools.at(B->getId());
		for(const auto schoolId : availableSchools)
		{
			if(schoolsA.count(schoolId) && !schoolsB.count(schoolId))
				return true;
			if(!schoolsA.count(schoolId) && schoolsB.count(schoolId))
				return false;
		}

		return TextOperations::compareLocalizedStrings(A->getNameTranslated(), B->getNameTranslated());
	}
};

CSpellWindow::CSpellWindow(const CGHeroInstance * _myHero, CPlayerInterface * _myInt, bool openOnBattleSpells, const std::function<void(SpellID)> & onSpellSelect):
	CWindowObject(PLAYER_COLORED | (settings["gameTweaks"]["enableLargeSpellbook"].Bool() ? BORDERED : 0)),
	battleSpellsOnly(openOnBattleSpells),
	selectedTab(SpellSchool::ANY),
	currentPage(0),
	myHero(_myHero),
	myInt(_myInt),
	openOnBattleSpells(openOnBattleSpells),
	onSpellSelect(onSpellSelect),
	isBigSpellbook(settings["gameTweaks"]["enableLargeSpellbook"].Bool()),
	spellsPerPage(24),
	offL(-11),
	offR(195),
	offRM(110),
	offT(-37),
	offB(56)
{
	OBJECT_CONSTRUCTION;

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

	if(isBigSpellbook)
	{
		background = std::make_shared<CPicture>(ImagePath::builtin("SpellBookLarge"), 0, 0);
		updateShadow();
	}
	else
	{
		background = std::make_shared<CPicture>(ImagePath::builtin("SpelBack"), 0, 0);
		offL = offR = offT = offB = offRM = 0;
		spellsPerPage = 12;
	}

	background->setPlayerColor(GAME->interface()->playerID);

	pos = background->center(Point(pos.w/2 + pos.x, pos.h/2 + pos.y));

	Rect r(90, isBigSpellbook ? 480 : 420, isBigSpellbook ? 160 : 110, 16);
	if(settings["general"]["enableUiEnhancements"].Bool())
	{
		const ColorRGBA rectangleColor = ColorRGBA(0, 0, 0, 75);
		const ColorRGBA borderColor = ColorRGBA(128, 100, 75);
		const ColorRGBA grayedColor = ColorRGBA(158, 130, 105);
		searchBoxRectangle = std::make_shared<TransparentFilledRectangle>(r.resize(1), rectangleColor, borderColor);
		searchBoxDescription = std::make_shared<CLabel>(r.center().x, r.center().y, FONT_SMALL, ETextAlignment::CENTER, grayedColor, LIBRARY->generaltexth->translate("vcmi.spellBook.search"));

		searchBox = std::make_shared<CTextInput>(r, FONT_SMALL, ETextAlignment::CENTER, false);
		searchBox->setCallback(std::bind(&CSpellWindow::searchInput, this));
	}

	if(onSpellSelect)
	{
		Point boxPos = r.bottomLeft() + Point(-2, 5);
		showAllSpells = std::make_shared<CToggleButton>(boxPos, AnimationPath::builtin("sysopchk.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.help.458.hover"), LIBRARY->generaltexth->translate("core.help.458.hover")), [this](bool state){ searchInput(); });
		showAllSpellsDescription = std::make_shared<CLabel>(boxPos.x + 40, boxPos.y + 12, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("core.help.458.hover"));
	}

	processSpells();

	//numbers of spell pages computed

	leftCorner = std::make_shared<CPicture>(ImagePath::builtin("SpelTrnL.bmp"), 97 + offL, 77 + offT);
	rightCorner = std::make_shared<CPicture>(ImagePath::builtin("SpelTrnR.bmp"), 487 + offR, 72 + offT);

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
	const int customSchoolCount = customSpellSchools.size();
	const int fullSizeCapacity = isBigSpellbook ? MAX_CUSTOM_SPELL_SCHOOLS_BIG : MAX_CUSTOM_SPELL_SCHOOLS;
	constexpr int bookmarkWidth = 80;
	constexpr int bookmarkHeight = 60;
	constexpr int bookmarkSpacing = 62;
	constexpr int yStart = 93;
	const bool compactBookmarks = customSchoolCount > fullSizeCapacity;
	const int stripHeight = (fullSizeCapacity - 1) * bookmarkSpacing + bookmarkHeight;
	const int step = compactBookmarks ? stripHeight / customSchoolCount : bookmarkSpacing;
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
	schoolPicture = std::make_shared<CAnimImage>(AnimationPath::builtin("Schools"), 0, 0, 117 + offL, 74 + offT);

	mana = std::make_shared<CLabel>(435 + (isBigSpellbook ? 159 : 0), 426 + offB, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, std::to_string(myHero->getManaAvailable()));

	if(isBigSpellbook)
		statusBar = CGStatusBar::create(400, 587);
	else
		statusBar = CGStatusBar::create(7, 569, ImagePath::builtin("Spelroll.bmp"));

	Rect schoolRect( 549 + pos.x + offR, 94 + pos.y, 45, 35);
	interactiveAreas.push_back(std::make_shared<InteractiveArea>( Rect( 479 + pos.x + (isBigSpellbook ? 175 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fexitb,         this),    460, this));
	interactiveAreas.push_back(std::make_shared<InteractiveArea>( Rect( 221 + pos.x + (isBigSpellbook ? 43 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fbattleSpellsb, this),    453, this));
	interactiveAreas.push_back(std::make_shared<InteractiveArea>( Rect( 355 + pos.x + (isBigSpellbook ? 110 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fadvSpellsb,    this),    452, this));
	interactiveAreas.push_back(std::make_shared<InteractiveArea>( Rect( 418 + pos.x + (isBigSpellbook ? 142 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fmanaPtsb,      this),    459, this));
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

	leftCornerArea = std::make_shared<InteractiveArea>( Rect(  97 + offL + pos.x, 77 + offT + pos.y, leftCorner->pos.h,  leftCorner->pos.w  ), std::bind(&CSpellWindow::fLcornerb, this), 450, this);
	rightCornerArea = std::make_shared<InteractiveArea>( Rect( 487 + offR + pos.x, 72 + offT + pos.y, rightCorner->pos.h, rightCorner->pos.w ), std::bind(&CSpellWindow::fRcornerb, this), 451, this);

	//areas for spells
	int xpos = 117 + offL + pos.x;
	int ypos = 90 + offT + pos.y;

	for(int v=0; v<spellsPerPage; ++v)
	{
		spellAreas[v] = std::make_shared<SpellArea>( Rect(xpos, ypos, 65, 78), this);

		if(v == (spellsPerPage / 2) - 1) //to right page
		{
			xpos = offRM + 336 + pos.x; ypos = 90 + offT + pos.y;
		}
		else
		{
			if(v%(isBigSpellbook ? 3 : 2) == 0 || (v%3 == 1 && isBigSpellbook))
			{
				xpos+=85;
			}
			else
			{
				xpos -= (isBigSpellbook ? 2 : 1)*85; ypos+=97;
			}
		}
	}

	SpellSchool school = battleSpellsOnly ? myInt->localState->getSpellbookSettings().spellbookLastTabBattle : myInt->localState->getSpellbookSettings().spellbookLastTabAdvmap;
	bool schoolFound = vstd::contains(schoolNavigation, school);
	if(schoolFound)
		selectedTab = school;
	setSchoolImages(selectedTab);
	int cp = battleSpellsOnly ? myInt->localState->getSpellbookSettings().spellbookLastPageBattle : myInt->localState->getSpellbookSettings().spellbookLastPageAdvmap;
	// spellbook last page battle index is not reset after battle, so this needs to stay here
	vstd::abetween(cp, 0, std::max(0, pagesWithinCurrentTab() - 1));
	if(!schoolFound)
		cp = 0;
	setCurrentPage(cp);
	computeSpellsPerArea();
	addUsedEvents(KEYBOARD);
}

CSpellWindow::~CSpellWindow()
{
}

void CSpellWindow::readSchoolContext()
{
	// Read the saved game/battle context, never reinterpret old games from the
	// currently installed module or mutate global CSpell definitions.
	rosterSpellIDs.clear();
	spellSchools.clear();
	spellLevels.clear();
	availableSchools.clear();
	usesLegacyTabs = false;
	const bool inBattleContext = myInt->battleInt != nullptr;
	std::shared_ptr<CPlayerBattleCallback> battleCallback;
	if(inBattleContext)
	{
		if(!myInt->battleInt->curInt || !myInt->battleInt->curInt->cb)
			return;
		try
		{
			battleCallback = myInt->battleInt->getBattle();
		}
		catch(const std::runtime_error &)
		{
			// Only the registry lookup is caught: an ended/missing battle must
			// not fall through to the world or run spell metadata lookups.
			return;
		}
		if(!battleCallback || !battleCallback->getBattle())
			return;
	}
	availableSchools = battleCallback ? battleCallback->battleGetActiveSpellSchools() : myInt->cb->getActiveSpellSchools();
	usesLegacyTabs = std::ranges::any_of(availableSchools, [](SpellSchool school)
	{
		return school != SpellSchool::ANY && isLegacySpellSchool(school);
	});
	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell)
			continue;
		const auto id = spell->getId();
		const bool admitted = battleCallback
			? newHorizonsMagic::spellAllowedByBattleRoster(*battleCallback, id)
			: newHorizonsMagic::spellAllowedByWorldRoster(*myInt->cb, id);
		if(!admitted || !newHorizonsMagic::spellAllowedByHeroRoster(myHero->getMagicRules(), id))
			continue;
		// Admission must precede school/level lookup for newly installed spells
		// that did not exist in this saved roster. Cache the same set for all UI paths.
		const auto schools = battleCallback ? battleCallback->battleGetSpellSchools(id) : myInt->cb->getSpellSchools(id);
		spellSchools.emplace(id, std::set<SpellSchool>(schools.begin(), schools.end()));
		spellLevels.emplace(id, battleCallback ? battleCallback->battleGetSpellLevel(id) : myInt->cb->getSpellLevel(id));
		rosterSpellIDs.insert(id);
	}
}

bool CSpellWindow::canUseSpellForCurrentDivineMandateFollowup(SpellID spell) const
{
	if(!battleSpellsOnly || !myInt->battleInt)
		return true;

	try
	{
		const auto battleCallback = myInt->battleInt->getBattle();
		if(!battleCallback)
			return true;
		const auto side = battleCallback->battleGetMySide();
		if(side == BattleSide::NONE)
			return true;

		const auto status = battleCallback->battleGetDivineMandateStatus(side);
		const auto & pending = status.pendingFollowup;
		if(!pending
			|| pending->source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
			|| pending->allowance != HeroActionAllowanceState::AllowanceKind::SPELL)
			return true;

		// Use the same payload-aware query as authoritative casting. In
		// particular, another unrestricted Spell grant may still pay for a
		// non-Light spell while Divine Mandate's typed grant is pending.
		return battleCallback->battleGetSpellActionAllowance(side, spell).has_value();
	}
	catch(const std::runtime_error &)
	{
		// If the battle has ended while this view is closing, keep the spell
		// inspectable; the authoritative cast path still decides legality.
		return true;
	}
}

std::string CSpellWindow::currentDivineMandateFollowupText() const
{
	if(!battleSpellsOnly || !myInt->battleInt)
		return {};

	try
	{
		const auto battleCallback = myInt->battleInt->getBattle();
		if(!battleCallback)
			return {};
		const auto side = battleCallback->battleGetMySide();
		if(side == BattleSide::NONE)
			return {};

		const auto status = battleCallback->battleGetDivineMandateStatus(side);
		const auto & pending = status.pendingFollowup;
		if(!pending
			|| pending->source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
			|| pending->allowance != HeroActionAllowanceState::AllowanceKind::SPELL)
			return {};

		return "Divine Mandate Light Spell follow-up; available through the end of round "
			+ std::to_string(pending->expiryRound) + ".";
	}
	catch(const std::runtime_error &)
	{
		return {};
	}
}

std::string CSpellWindow::spellActionOpportunityText(SpellID spell) const
{
	if(!battleSpellsOnly || !myInt->battleInt)
		return {};

	try
	{
		const auto battleCallback = myInt->battleInt->getBattle();
		if(!battleCallback)
			return {};
		const auto side = battleCallback->battleGetMySide();
		if(side == BattleSide::NONE)
			return {};

		const auto selection = battleCallback->battleGetSpellActionAllowance(side, spell);
		if(!selection)
			return {};

		const auto source = spellAllowanceSourceName(selection->source);
		if(source.empty())
			return {};

		const std::string opportunity = selection->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
			? " Light Spell follow-up" : " Spell follow-up";
		return source + opportunity + " available through the end of round "
			+ std::to_string(selection->expiryRound) + ".";
	}
	catch(const std::runtime_error &)
	{
		return {};
	}
}

void CSpellWindow::searchInput()
{
	if(searchBox)
		searchBoxDescription->setEnabled(searchBox->getText().empty());

	processSpells();

	int cp = 0;
	// spellbook last page battle index is not reset after battle, so this needs to stay here
	vstd::abetween(cp, 0, std::max(0, pagesWithinCurrentTab() - 1));
	setCurrentPage(cp);
	computeSpellsPerArea();
}

void CSpellWindow::processSpells()
{
	mySpells.clear();
	sitesPerTabAdv.clear(); // hold page counts of previous run - would be added up otherwise
	sitesPerTabBattle.clear();

	//initializing castable spells
	mySpells.reserve(LIBRARY->spellh->objects.size());
	for(auto const & spell : LIBRARY->spellh->objects)
	{
		// Show-all can bypass possession, never the originating saved roster.
		if(!spell || !rosterSpellIDs.contains(spell->getId()))
			continue;
		bool searchTextFound = !searchBox || TextOperations::isFuzzyMatch(searchBox->getText(), spell->getNameTranslated());

		if(onSpellSelect)
		{
			bool spellAvailable = myHero->canCastThisSpell(spell.get()) || (showAllSpells->isSelected() && !spell->isSpecial());

			if(spell->isCombat() == openOnBattleSpells
				&& !spell->isCreatureAbility()
				&& searchTextFound
				&& spellAvailable)
			{
				mySpells.push_back(spell.get());
			}
			continue;
		}

		const bool ownedOrGranted = !myHero->getSourcesForSpell(spell->getId()).empty();
		if(!spell->isCreatureAbility() && (myHero->canCastThisSpell(spell.get()) || ownedOrGranted) && searchTextFound)
			mySpells.push_back(spell.get());
	}

	SpellbookSpellSorter spellsorter(availableSchools, spellSchools, spellLevels);
	std::sort(mySpells.begin(), mySpells.end(), spellsorter);

	for(const auto spell : mySpells)
	{
		auto& sitesPerOurTab = spell->isCombat() ? sitesPerTabBattle : sitesPerTabAdv;

		++sitesPerOurTab[SpellSchool::ANY];

		for(const auto school : availableSchools)
			if(spellSchools.at(spell->getId()).count(school))
				++sitesPerOurTab[school];
	}
	if(sitesPerTabAdv[SpellSchool::ANY] % spellsPerPage == 0)
		sitesPerTabAdv[SpellSchool::ANY]/=spellsPerPage;
	else
		sitesPerTabAdv[SpellSchool::ANY] = sitesPerTabAdv[SpellSchool::ANY]/spellsPerPage + 1;

	for(const auto v : availableSchools)
	{
		if(v == SpellSchool::ANY)
			continue;
		if(sitesPerTabAdv[v] <= spellsPerPage - 2)
			sitesPerTabAdv[v] = 1;
		else
		{
			if((sitesPerTabAdv[v] - (spellsPerPage - 2)) % spellsPerPage == 0)
				sitesPerTabAdv[v] = (sitesPerTabAdv[v] - (spellsPerPage - 2)) / spellsPerPage + 1;
			else
				sitesPerTabAdv[v] = (sitesPerTabAdv[v] - (spellsPerPage - 2)) / spellsPerPage + 2;
		}
	}

	if(sitesPerTabBattle[SpellSchool::ANY] % spellsPerPage == 0)
		sitesPerTabBattle[SpellSchool::ANY]/=spellsPerPage;
	else
		sitesPerTabBattle[SpellSchool::ANY] = sitesPerTabBattle[SpellSchool::ANY]/spellsPerPage + 1;

	for(const auto v : availableSchools)
	{
		if(v == SpellSchool::ANY)
			continue;
		if(sitesPerTabBattle[v] <= spellsPerPage - 2)
			sitesPerTabBattle[v] = 1;
		else
		{
			if((sitesPerTabBattle[v] - (spellsPerPage - 2)) % spellsPerPage == 0)
				sitesPerTabBattle[v] = (sitesPerTabBattle[v] - (spellsPerPage - 2)) / spellsPerPage + 1;
			else
				sitesPerTabBattle[v] = (sitesPerTabBattle[v] - (spellsPerPage - 2)) / spellsPerPage + 2;
		}
	}
}

void CSpellWindow::fexitb()
{
	closeSpellbook();
}

void CSpellWindow::closeForSpellSelection()
{
	closeSpellbook();
}

void CSpellWindow::closeSpellbook()
{
	auto spellBookState = myInt->localState->getSpellbookSettings();
	if(myInt->battleInt)
	{
		spellBookState.spellbookLastTabBattle = selectedTab;
		spellBookState.spellbookLastPageBattle = currentPage;
	}
	else
	{
		spellBookState.spellbookLastTabAdvmap = selectedTab;
		spellBookState.spellbookLastPageAdvmap = currentPage;
	}
	myInt->localState->setSpellbookSettings(spellBookState);

	if(onSpellSelect)
		onSpellSelect(SpellID::NONE);

	close();
}

void CSpellWindow::fadvSpellsb()
{
	if(battleSpellsOnly == true)
	{
		turnPageRight();
		battleSpellsOnly = false;
		setCurrentPage(0);
	}
	computeSpellsPerArea();
}

void CSpellWindow::fbattleSpellsb()
{
	if(battleSpellsOnly == false)
	{
		turnPageLeft();
		battleSpellsOnly = true;
		setCurrentPage(0);
	}
	computeSpellsPerArea();
}

void CSpellWindow::toggleSearchBoxFocus()
{
	if(searchBox != nullptr)
	{
		searchBox->hasFocus() ? searchBox->removeFocus() : searchBox->giveFocus();
	}
}

void CSpellWindow::fmanaPtsb()
{
}

void CSpellWindow::selectSchool(SpellSchool school)
{
	if(!vstd::contains(schoolNavigation, school))
		return;
	if(selectedTab != school)
	{
		const bool forward = usesLegacyTabs ? selectedTab < school
			: std::find(schoolNavigation.begin(), schoolNavigation.end(), selectedTab) < std::find(schoolNavigation.begin(), schoolNavigation.end(), school);
		if(forward)
			turnPageLeft();
		else
			turnPageRight();
		selectedTab = school;
		setSchoolImages(selectedTab);
		setCurrentPage(0);
	}
	if(allSchoolsSelected)
	{
		allSchoolsSelected->setEnabled(selectedTab == SpellSchool::ANY);
		allSchoolsInactive->setEnabled(selectedTab != SpellSchool::ANY);
	}
	computeSpellsPerArea();
}

void CSpellWindow::fLcornerb()
{
	if(currentPage>0)
	{
		turnPageLeft();
		setCurrentPage(currentPage - 1);
	}
	computeSpellsPerArea();
}

void CSpellWindow::fRcornerb()
{
	if((currentPage + 1) < (pagesWithinCurrentTab()))
	{
		turnPageRight();
		setCurrentPage(currentPage + 1);
	}
	computeSpellsPerArea();
}

void CSpellWindow::show(Canvas & to)
{
	if(video)
		video->show(to);
	statusBar->show(to);
}

void CSpellWindow::computeSpellsPerArea()
{
	std::vector<const CSpell *> spellsCurSite;
	spellsCurSite.reserve(mySpells.size());
	for(const CSpell * spell : mySpells)
	{
		if(spell->isCombat() ^ !battleSpellsOnly
		   && ((selectedTab == SpellSchool::ANY) || spellSchools.at(spell->getId()).count(selectedTab))
			)
		{
			spellsCurSite.push_back(spell);
		}
	}

	if(selectedTab == SpellSchool::ANY)
	{
		if(spellsCurSite.size() > spellsPerPage)
		{
			spellsCurSite = std::vector<const CSpell *>(spellsCurSite.begin() + currentPage*spellsPerPage, spellsCurSite.end());
			if(spellsCurSite.size() > spellsPerPage)
			{
				spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage, spellsCurSite.end());
			}
		}
	}
	else
	{
		if(spellsCurSite.size() > spellsPerPage - 2)
		{
			if(currentPage == 0)
			{
				spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage-2, spellsCurSite.end());
			}
			else
			{
				spellsCurSite = std::vector<const CSpell *>(spellsCurSite.begin() + (currentPage-1)*spellsPerPage + spellsPerPage-2, spellsCurSite.end());
				if(spellsCurSite.size() > spellsPerPage)
				{
					spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage, spellsCurSite.end());
				}
			}
		}
	}
	//applying
	if(selectedTab == SpellSchool::ANY || currentPage != 0)
	{
		for(size_t c=0; c<spellsPerPage; ++c)
		{
			if(c < spellsCurSite.size())
			{
				spellAreas[c]->setSpell(spellsCurSite[c]);
			}
			else
			{
				spellAreas[c]->setSpell(nullptr);
			}
		}
	}
	else
	{
		spellAreas[0]->setSpell(nullptr);
		spellAreas[1]->setSpell(nullptr);
		for(size_t c=0; c<spellsPerPage-2; ++c)
		{
			if(c < spellsCurSite.size())
				spellAreas[c+2]->setSpell(spellsCurSite[c]);
			else
				spellAreas[c+2]->setSpell(nullptr);
		}
	}
	redraw();
}

void CSpellWindow::setSchoolImages(SpellSchool school)
{
	OBJECT_CONSTRUCTION;

	schoolTabAnyDisabled.reset();
	if(schoolTab)
	{
		if(isLegacySpellSchool(school))
		{
			schoolTab->setFrame(getAnimFrameFromSchool(school), 0);
			schoolTab->visible = true;
		}
		else
		{
			schoolTabAnyDisabled = std::make_shared<CPicture>(ImagePath::builtin("SpelTabNone.png"), 524 + offR, 88);
			schoolTab->visible = false;
		}
	}
	if(allSchoolsSelected)
	{
		allSchoolsSelected->setEnabled(school == SpellSchool::ANY);
		allSchoolsInactive->setEnabled(school != SpellSchool::ANY);
	}

	auto it = std::find(customSpellSchools.begin(), customSpellSchools.end(), school);
	int pos = (it == customSpellSchools.end()) ? -1 : std::distance(customSpellSchools.begin(), it);
	for(int i = 0; i < schoolTabCustom.size(); i++)
		schoolTabCustom[i]->setFrame(i == pos ? 0 : 1, 0);
	for(int i = 0; i < schoolTabCustom.size(); i++)
		moveChildForeground(schoolTabCustom[i].get());
	if(pos >= 0)
		moveChildForeground(schoolTabCustom[pos].get());

	schoolPicture->visible = school != SpellSchool::ANY && currentPage == 0 && isLegacySpellSchool(school);
	if(school != SpellSchool::ANY && isLegacySpellSchool(school))
		schoolPicture->setFrame(getAnimFrameFromSchool(school), 0);
	
	schoolPictureCustom.reset();
	if(!isLegacySpellSchool(school) && currentPage == 0) // on later pages the header would cover spells
		schoolPictureCustom = std::make_shared<CPicture>(LIBRARY->spellSchoolHandler->getById(school)->getSchoolHeaderPath(), 117 + offL, 74 + offT);
}

void CSpellWindow::setCurrentPage(int value)
{
	currentPage = value;
	setSchoolImages(selectedTab);

	bool canTurnLeft = currentPage != 0;
	bool canTurnRight = currentPage + 1 < pagesWithinCurrentTab();

	leftCorner->setEnabled(canTurnLeft);
	rightCorner->setEnabled(canTurnRight);
	leftCornerArea->setEnabled(canTurnLeft);
	rightCornerArea->setEnabled(canTurnRight);

	ENGINE->fakeMouseMove(); // refresh hover state so a stale page-turn hint clears when the corner is disabled under the cursor

	mana->setText(std::to_string(myHero->getManaAvailable()));//just in case, it will be possible to cast spell without closing book
}

void CSpellWindow::turnPageLeft()
{
	OBJECT_CONSTRUCTION;
	if(settings["video"]["spellbookAnimation"].Bool() && !isBigSpellbook)
		video = std::make_shared<VideoWidgetOnce>(Point(13, 14), VideoPath::builtin("PGTRNLFT.SMK"), false, this);
}

void CSpellWindow::turnPageRight()
{
	OBJECT_CONSTRUCTION;
	if(settings["video"]["spellbookAnimation"].Bool() && !isBigSpellbook)
		video = std::make_shared<VideoWidgetOnce>(Point(13, 14), VideoPath::builtin("PGTRNRGH.SMK"), false, this);
}

void CSpellWindow::onVideoPlaybackFinished()
{
	video.reset();
	redraw();
}

void CSpellWindow::keyPressed(EShortcut key)
{
	switch(key)
	{
		case EShortcut::GLOBAL_RETURN:
			fexitb();
			break;

		case EShortcut::MOVE_LEFT:
			fLcornerb();
			break;
		case EShortcut::MOVE_RIGHT:
			fRcornerb();
			break;
		case EShortcut::MOVE_UP:
		case EShortcut::MOVE_DOWN:
		{
			bool down = key == EShortcut::MOVE_DOWN;
			int idx = std::distance(schoolNavigation.begin(), std::find(schoolNavigation.begin(), schoolNavigation.end(), selectedTab));
			idx = (idx + (down ? 1 : -1) + static_cast<int>(schoolNavigation.size())) % static_cast<int>(schoolNavigation.size());
			if(selectedTab != schoolNavigation[idx])
				selectSchool(schoolNavigation[idx]);
			break;
		}
		case EShortcut::SPELLBOOK_TAB_COMBAT:
			fbattleSpellsb();
			break;
		case EShortcut::SPELLBOOK_TAB_ADVENTURE:
			fadvSpellsb();
			break;
		case EShortcut::SPELLBOOK_SEARCH_FOCUS:
			toggleSearchBoxFocus();
			break;
	}
}

int CSpellWindow::pagesWithinCurrentTab()
{
	return battleSpellsOnly ? sitesPerTabBattle[selectedTab] : sitesPerTabAdv[selectedTab];
}

CSpellWindow::SpellArea::SpellArea(Rect pos, CSpellWindow * owner)
{
	this->pos = pos;
	this->owner = owner;
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);

	schoolLevel = -1;
	schoolLocked = false;
	mySpell = nullptr;

	OBJECT_CONSTRUCTION;

	image = std::make_shared<CAnimImage>(AnimationPath::builtin("Spells"), 0, 0);
	image->visible = false;

	name = std::make_shared<CLabel>(39, 70, FONT_TINY, ETextAlignment::CENTER);
	level = std::make_shared<CLabel>(39, 82, FONT_TINY, ETextAlignment::CENTER);
	cost = std::make_shared<CLabel>(39, 94, FONT_TINY, ETextAlignment::CENTER);

	for(auto l : {name, level, cost})
		l->setAutoRedraw(false);
}

CSpellWindow::SpellArea::~SpellArea() = default;

void CSpellWindow::SpellArea::clickPressed(const Point & cursorPosition)
{
	if(mySpell)
	{
		ENGINE->input().hapticFeedback();
		if(schoolLocked)
		{
			GAME->interface()->showInfoDialog(schoolRequirementText);
			return;
		}
		if(!owner->canUseSpellForCurrentDivineMandateFollowup(mySpell->id))
		{
			GAME->interface()->showInfoDialog(owner->currentDivineMandateFollowupText()
				+ "\nThis Spell cannot use that follow-up.");
			return;
		}

		if(owner->onSpellSelect)
		{
			owner->onSpellSelect(mySpell->id);
			owner->close();
			return;
		}

		const auto battleInterface = owner->myInt->battleInt;
		const auto battleCallback = battleInterface ? battleInterface->getBattle() : nullptr;
		const auto metamagicSide = battleCallback ? battleCallback->battleGetMySide() : BattleSide::NONE;
		const bool metamagicFollowup = battleCallback && metamagicSide != BattleSide::NONE
			&& battleCallback->battleCanUseMetamagicFollowup(metamagicSide, mySpell->id);
		auto spellCost = owner->myInt->cb->getSpellCost(mySpell, owner->myHero);
		if(metamagicFollowup && newHorizonsMagic::hasMetamagicPerk(owner->myHero, newHorizonsMagic::METAMAGIC_ARCANE_ECONOMY))
			spellCost = std::max(1, spellCost - 2);
		if(spellCost > owner->myHero->getManaAvailable() && !metamagicFollowup) //insufficient mana; follow-ups use authoritative preview below
		{
			MetaString message = MetaString::createFromTextID("core.genrltxt.206"); // That spell costs %d spell points. Your hero only has %d spell points...
			message.replaceNumber(spellCost);
			message.replaceNumber(owner->myHero->getManaAvailable());
			GAME->interface()->showInfoDialog(message.toString(&GAME->translator()));
			return;
		}

		//anything that is not combat spell is adventure spell
		//this not an error in general to cast even creature ability with hero
		const bool combatSpell = mySpell->isCombat();
		if(combatSpell == mySpell->isAdventure())
		{
			logGlobal->error("Spell have invalid flags");
			return;
		}

		const bool inCombat = battleInterface != nullptr;
		const bool inCastle = owner->myInt->castleInt != nullptr;

		//battle spell on adv map or adventure map spell during combat => display infowindow, not cast
		if((combatSpell != inCombat) || inCastle || (!combatSpell && !GAME->interface()->makingTurn))
		{
			std::vector<std::shared_ptr<CComponent>> hlp(1, std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
			GAME->interface()->showInfoDialog(newHorizonsMagic::spellDescriptionForHero(owner->myHero, mySpell, schoolLevel), hlp);
		}
		else if(combatSpell)
		{
			spells::detail::ProblemImpl problem;
			const bool canCast = mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO,
				owner->myHero);
				if(canCast)
			{
				// Close the spellbook before cast setup: a NO_LOCATION spell may
				// synchronously open a post-selection modal (Selective Dispel), and
				// only the topmost window may be closed.
				const SpellID selectedSpell = mySpell->id;
				owner->closeForSpellSelection();
				battleInterface->castThisSpell(selectedSpell);
				return;
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
		else //adventure spell
		{
			const CGHeroInstance * h = owner->myHero;
			ENGINE->windows().popWindows(1);

			auto guard = vstd::makeScopeGuard([this]()
			{
				auto spellBookState = owner->myInt->localState->getSpellbookSettings();
				spellBookState.spellbookLastTabAdvmap = owner->selectedTab;
				spellBookState.spellbookLastPageAdvmap = owner->currentPage;
				owner->myInt->localState->setSpellbookSettings(spellBookState);
			});

			spells::detail::ProblemImpl problem;
			if (mySpell->getAdventureMechanics().canBeCast(problem, GAME->interface()->cb.get(), owner->myHero))
			{
				const auto * spellEffect = mySpell->getAdventureMechanics().getEffectAs<IAdventureSpellEffect>(owner->myHero);

				if(spellEffect && spellEffect->requiresTargetSelection(owner->myHero))
					adventureInt->enterCastingMode(mySpell);
				else
					owner->myInt->cb->castSpell(h, mySpell->id);
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
	}
}

void CSpellWindow::SpellArea::showPopupWindow(const Point & cursorPosition)
{
	if(mySpell)
	{
		std::string dmgInfo;
		auto causedDmg = owner->myInt->cb->estimateSpellDamage(mySpell, owner->myHero);
		if(causedDmg == 0 || mySpell->id == SpellID::TITANS_LIGHTNING_BOLT) //Titan's Lightning Bolt already has damage info included
			dmgInfo.clear();
		else
		{
			MetaString dmgText = MetaString::createFromTextID("core.genrltxt.343");
			dmgText.replaceNumber(causedDmg);
			dmgInfo = dmgText.toString(&GAME->translator());
		}

		std::string requirements;
		if(schoolLocked)
			requirements += "\n\n" + schoolRequirementText;
		if(divineMandateLocked)
			requirements += "\n\n" + divineMandateRequirementText;
		const auto followup = owner->spellActionOpportunityText(mySpell->id);
		const auto followupInfo = followup.empty() ? std::string() : "\n\n" + followup;
		CRClickPopup::createAndPush(newHorizonsMagic::spellDescriptionForHero(owner->myHero, mySpell, schoolLevel)
			+ dmgInfo + requirements + followupInfo,
			std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
	}
}

void CSpellWindow::SpellArea::hover(bool on)
{
	if(mySpell)
	{
		if(on)
		{
			MetaString message = MetaString::createFromRawString("%s (%s)");
			message.replaceTextID(mySpell->getNameTextID());
			const int spellLevel = owner->spellLevels.at(mySpell->getId());
			if(spellLevel > 0)
				message.replaceTextID("core.genrltxt", 171 + spellLevel);
			else
				message.replaceTextID("vcmi.spellBook.zero_level.hint");
			auto statusText = message.toString(&GAME->translator());
			const auto followup = owner->spellActionOpportunityText(mySpell->id);
			if(!followup.empty())
				statusText += " — " + followup;
			owner->statusBar->write(statusText);
		}
		else
			owner->statusBar->clear();
	}
}

void CSpellWindow::SpellArea::setSpell(const CSpell * spell)
{
	schoolBorder.reset();
	schoolLocked = false;
	divineMandateLocked = false;
	schoolRequirementLabel.clear();
	schoolRequirementText.clear();
	divineMandateRequirementText.clear();
	image->visible = false;
	// setSpell reuses each slot; restore the legacy top-left frame origin before
	// selecting a new frame so a preceding centered PNG cannot shift a DEF icon.
	image->moveTo(pos.topLeft());
	name->setText("");
	level->setText("");
	cost->setText("");
	mySpell = spell;
	if(mySpell)
	{
		const int requiredRank = newHorizonsMagic::requiredSchoolRank(owner->myHero->getMagicRules(), mySpell->getId());
		const bool inscribedInSpellbook = owner->myHero->hasSpellbook()
			&& owner->myHero->isSpellInscribedForCasting(mySpell->getId());
		// Durable and eligible temporary inscriptions are usable regardless of
		// School rank; learning and other non-inscribed sources remain gated.
		schoolLocked = requiredRank > 0 && !inscribedInSpellbook
			&& !newHorizonsMagic::hasSchoolProficiency(owner->myHero, mySpell->getId());
		divineMandateLocked = !owner->canUseSpellForCurrentDivineMandateFollowup(mySpell->getId());
		if(divineMandateLocked)
			divineMandateRequirementText = "This spell is not eligible for the pending Divine Mandate Light Spell follow-up.";
		if(schoolLocked)
		{
			const auto rankName = GAME->translator().translate(TextIdentifier("core.skilllev", requiredRank - 1).get());
			schoolRequirementLabel = "Locked: " + rankName;
			std::string schools;
			for(const auto skill : newHorizonsMagic::spellSchoolSkills(owner->myHero->getMagicRules(), mySpell->getId()))
			{
				if(!schools.empty())
					schools += " or ";
				schools += skill.toEntity(LIBRARY)->getNameTranslated();
			}
			schoolRequirementText = "Requires " + rankName + " " + schools + ".";
		}
		SpellSchool whichSchool;
		schoolLevel = owner->myHero->getSpellSchoolLevel(mySpell, &whichSchool);
		auto spellCost = owner->myInt->cb->getSpellCost(mySpell, owner->myHero);
		if(owner->myInt->battleInt)
		{
			const auto battle = owner->myInt->battleInt->getBattle();
			const auto side = battle->battleGetMySide();
			if(side != BattleSide::NONE && battle->battleCanUseMetamagicFollowup(side, mySpell->id)
				&& newHorizonsMagic::hasMetamagicPerk(owner->myHero, newHorizonsMagic::METAMAGIC_ARCANE_ECONOMY))
				spellCost = std::max(1, spellCost - 2);
		}

		image->setFrame(mySpell->id.getNum());
		image->visible = true;

		{
			OBJECT_CONSTRUCTION;

			schoolBorder.reset();
			if (owner->selectedTab == SpellSchool::ANY)
			{
				if (whichSchool.hasValue())
					schoolBorder = std::make_shared<CAnimImage>(LIBRARY->spellSchoolHandler->getById(whichSchool)->getSpellBordersPath(), schoolLevel);
			}
			else
				schoolBorder = std::make_shared<CAnimImage>(LIBRARY->spellSchoolHandler->getById(owner->selectedTab)->getSpellBordersPath(), schoolLevel);
		}

		// New spell icons are registered as independent PNG frames in SPELLS.
		// Keep legacy DEF frames at their original offsets, but center dynamic
		// frames in the school-border canvas (or the original icon canvas when no
		// border is available) using the dimensions actually loaded at runtime.
		if(!mySpell->getIconBook().empty())
		{
			constexpr int originalIconCanvasWidth = 78;
			constexpr int originalIconCanvasHeight = 65;
			Rect iconCanvas(pos.x, pos.y, originalIconCanvasWidth, originalIconCanvasHeight);
			if(schoolBorder && schoolBorder->pos.w > 0 && schoolBorder->pos.h > 0)
				iconCanvas = schoolBorder->pos;

			image->moveTo(Point(
				iconCanvas.x + (iconCanvas.w - image->pos.w) / 2,
				iconCanvas.y + (iconCanvas.h - image->pos.h) / 2));
		}

		ColorRGBA firstLineColor, secondLineColor;
		if(divineMandateLocked || ((spellCost > owner->myHero->getManaAvailable() || schoolLocked) && !owner->onSpellSelect)) //hero cannot cast this spell
		{
			firstLineColor = Colors::WHITE;
			secondLineColor = Colors::ORANGE;
		}
		else
		{
			firstLineColor = Colors::YELLOW;
			secondLineColor = Colors::WHITE;
		}

		name->color = firstLineColor;
		name->setText(mySpell->getNameTranslated());

		level->color = secondLineColor;
		const int spellLevel = owner->spellLevels.at(mySpell->getId());
		std::string levelTextID = spellLevel > 0 ? TextIdentifier("core.genrltxt", 171 + spellLevel).get()
														  : "vcmi.spellBook.zero_level.hint";

		if(schoolLevel > 0)
		{
			MetaString levelText = MetaString::createFromRawString("%s/%s");
			levelText.replaceTextID(levelTextID);
			levelText.replaceTextID("core.skilllev", 3 + (schoolLevel - 1)); //lines 4-6
			level->setText(levelText.toString(&GAME->translator()));
		}
		else
			level->setText(GAME->translator().translate(levelTextID));

		cost->color = secondLineColor;
		if(schoolLocked)
			cost->setText(schoolRequirementLabel);
		else if(divineMandateLocked)
			cost->setText("Light spells only");
		else
		{
			MetaString costText = MetaString::createFromRawString("%s: %d");
			costText.replaceTextID("core.genrltxt.387"); // Spell Points
			costText.replaceNumber(spellCost);
			cost->setText(costText.toString(&GAME->translator()));
		}
	}
}
