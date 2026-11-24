/*
 * NewHorizonsPerkBrowser.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsPerkBrowser.h"

#include "CMessage.h"
#include "InfoWindows.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsSylvanHybridPerkIcons.h"
#include "NewHorizonsPerkAvailability.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"

#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
class PerkBrowserHelpArea : public CHoverableArea
{
	std::string description;
	std::string details;
	std::optional<SecondarySkill> parentSkill;
	int skillRank;

public:
	PerkBrowserHelpArea(const Rect & area, std::string description_, std::string details_, std::optional<SecondarySkill> parentSkill_, int skillRank_)
		: description(std::move(description_)), details(std::move(details_)), parentSkill(std::move(parentSkill_)), skillRank(skillRank_)
	{
		// CIntObject has already adjusted the child to the parent's screen origin.
		// Match LRClickableAreaWText's local-rectangle convention when replacing
		// that position with this card's bounds.
		pos = area + pos.topLeft();
		hoverText = "Click for the perk explanation.";
		addUsedEvents(LCLICK | SHOW_POPUP);
	}

	void clickPressed(const Point &) override
	{
		CInfoWindow::TCompsInfo components;
		if(parentSkill)
			components.push_back(std::make_shared<CComponent>(ComponentType::SEC_SKILL,
				*parentSkill, skillRank, CComponent::small));
		GAME->interface()->showInfoDialog(description, components, 0, details);
	}

	void showPopupWindow(const Point &) override
	{
		if(parentSkill)
		{
			// Construct popup-only components lazily. Creating one while the
			// browser captures children would attach it at the window origin.
			auto skillComponent = std::make_shared<CComponent>(ComponentType::SEC_SKILL,
				*parentSkill, skillRank, CComponent::small);
			CRClickPopup::createAndPush(description, skillComponent);
		}
		else
			CRClickPopup::createAndPush(description);
	}
};

using PerkRow = const newHorizonsHeroes::PerkDefinition *;

// 640-wide leather dialog: three rank columns, each with the Skill's own rank
// effect above that rank's perks.  The height follows the tallest column.
constexpr int WINDOW_WIDTH = 640;
constexpr int COLUMN_LEFT = 12;
constexpr int COLUMN_STRIDE = 208;
constexpr int COLUMN_WIDTH = 200;
constexpr int COLUMN_TOP = 42;
constexpr int DESCRIPTION_TOP = COLUMN_TOP + 26;
constexpr int DESCRIPTION_HEIGHT = 160;
constexpr int FOOTER_HEIGHT = 46;
constexpr int PERK_ROW_HEIGHT = 50;
constexpr int PERK_ICON = 44;

int columnLeft(size_t rankIndex)
{
	return COLUMN_LEFT + static_cast<int>(rankIndex) * COLUMN_STRIDE;
}

std::shared_ptr<CIntObject> rankHelp(const CGHeroInstance & hero, const std::string & skillId,
	int rank, const Rect & area)
{
	const auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId);
	if(!parentSkill)
		return std::make_shared<PerkBrowserHelpArea>(area, "No Skill description is available.", "", std::nullopt, rank);
	return std::make_shared<PerkBrowserHelpArea>(area,
		newHorizonsPerkHelp::skillDescription(&hero, *parentSkill, rank),
		newHorizonsPerkHelp::skillDetails(&hero, *parentSkill, rank), parentSkill, rank);
}

std::string rankDescription(const CGHeroInstance & hero, const std::string & skillId, int rank)
{
	const auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId);
	return parentSkill ? newHorizonsPerkHelp::skillDescription(&hero, *parentSkill, rank) : std::string();
}

int perkRank(const newHorizonsHeroes::PerkDefinition & perk)
{
	return newHorizonsHeroes::perkRequiredRank(perk.requiredRank);
}

std::string implementationStatus(const newHorizonsHeroes::PerkDefinition & perk)
{
	const auto status = perk.effect["status"].String();
	if(status == "active")
		return "Implemented";
	if(status == "planned")
		return "Not implemented";
	return "Status unknown";
}

std::string perkHelpText(const CGHeroInstance & hero, const std::string & skillId,
	const newHorizonsHeroes::PerkDefinition & perk)
{
	// The window itself shows no state captions; the explanation keeps the
	// complete saved-rule eligibility and implementation disclosure.
	const auto evaluation = newHorizonsPerkAvailability::evaluatePerk(
		hero.getPerkState(), skillId, hero.getPerkSkillRank(skillId), perk);
	const auto effectStatus = implementationStatus(perk);
	return newHorizonsPerkHelp::format(&hero, skillId, perk.name,
		newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description)
		+ "\n\n" + newHorizonsPerkAvailability::explanation(evaluation, newHorizonsPerkHelp::skillName(&hero, skillId))
		+ "\nImplementation: " + effectStatus + ". This browser is read-only; it does not unlock perks.";
}

/// One perk: icon and name directly on the column.  A learned perk gets the
/// Heroes III gold selection frame and a gold name; the rest stay plain.
void addPerkRow(std::vector<std::shared_ptr<CIntObject>> & elements, const CGHeroInstance & hero,
	const std::string & skillId, const newHorizonsHeroes::PerkDefinition & perk, const Rect & row)
{
	const auto & perkState = hero.getPerkState();
	const bool learned = perkState.hasSelection(skillId, perk.id);
	if(learned)
		elements.push_back(std::make_shared<TransparentFilledRectangle>(row, ColorRGBA(231, 194, 86, 28),
			ColorRGBA(231, 194, 86, 255)));
	elements.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin(newHorizonsHybridCapturedPerkIcon(perk.id, perk.name, perkState.getRules())), 0,
		Rect(row.x + 3, row.y + (row.h - PERK_ICON) / 2, PERK_ICON, PERK_ICON)));
	const int nameLeft = row.x + PERK_ICON + 9;
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(nameLeft, row.y + 2, row.x + row.w - nameLeft - 3, row.h - 4),
		FONT_SMALL, ETextAlignment::CENTERLEFT, learned ? Colors::YELLOW : Colors::WHITE, perk.name));

	auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId);
	const int skillRank = std::clamp(hero.getPerkSkillRank(skillId), 1, 3);
	elements.push_back(std::make_shared<PerkBrowserHelpArea>(row, perkHelpText(hero, skillId, perk), perk.details,
		std::move(parentSkill), skillRank));
}

/// Height of the tallest rank text; every column's perk grid starts below it.
int rankTextHeight(const CGHeroInstance & hero, const std::string & skillId)
{
	size_t lines = 1;
	for(int rank = 1; rank <= 3; ++rank)
		lines = std::max(lines, CMessage::breakText(rankDescription(hero, skillId, rank), COLUMN_WIDTH - 16, FONT_SMALL).size());
	const int lineHeight = static_cast<int>(ENGINE->renderHandler().loadFont(FONT_SMALL)->getLineHeight());
	return std::min(DESCRIPTION_HEIGHT, static_cast<int>(lines) * lineHeight);
}

/// Column recess, rank name, the Skill's own effect at that rank, then the
/// rank's perks.  All columns share perksTop so the perk rows stay on one grid.
void addRankColumn(std::vector<std::shared_ptr<CIntObject>> & elements, const CGHeroInstance & hero,
	const std::string & skillId, size_t rankIndex, const std::vector<PerkRow> & perks, int perksTop, int columnBottom)
{
	const std::array<const char *, 3> rankNames = {"Basic", "Advanced", "Expert"};
	const int left = columnLeft(rankIndex);
	const int rank = static_cast<int>(rankIndex) + 1;
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(left, COLUMN_TOP, COLUMN_WIDTH, columnBottom - COLUMN_TOP),
		ColorRGBA(35, 21, 13, 92), ColorRGBA(154, 119, 66, 255)));
	elements.push_back(std::make_shared<CLabel>(left + COLUMN_WIDTH / 2, COLUMN_TOP + 13, FONT_MEDIUM,
		ETextAlignment::CENTER, Colors::YELLOW, rankNames[rankIndex]));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(left + 8, DESCRIPTION_TOP, COLUMN_WIDTH - 16, perksTop - 14 - DESCRIPTION_TOP),
		FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, rankDescription(hero, skillId, rank)));
	// The rank header and effect text open the Skill's own rank explanation.
	elements.push_back(rankHelp(hero, skillId, rank,
		Rect(left + 6, COLUMN_TOP + 3, COLUMN_WIDTH - 12, perksTop - COLUMN_TOP - 6)));

	if(perks.empty())
	{
		elements.push_back(std::make_shared<CLabel>(left + COLUMN_WIDTH / 2, perksTop + 14, FONT_SMALL,
			ETextAlignment::CENTER, Colors::WHITE, "No perks in this rank.", COLUMN_WIDTH - 20));
		return;
	}
	for(size_t perkIndex = 0; perkIndex < perks.size(); ++perkIndex)
	{
		const Rect row(left + 6, perksTop + static_cast<int>(perkIndex) * PERK_ROW_HEIGHT, COLUMN_WIDTH - 12, PERK_ROW_HEIGHT - 4);
		addPerkRow(elements, hero, skillId, *perks[perkIndex], row);
	}
}
}

NewHorizonsPerkBrowser::NewHorizonsPerkBrowser(const CGHeroInstance & hero, const std::string & skillId)
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;

	const auto skill = newHorizonsPerkHelp::skillDefinition(&hero, skillId);
	std::array<std::vector<PerkRow>, 3> perksByRank;
	if(skill)
	{
		for(const auto & perk : skill->perks)
		{
			const int rank = perkRank(perk);
			if(rank >= 1 && rank <= static_cast<int>(perksByRank.size()))
				perksByRank[static_cast<size_t>(rank - 1)].push_back(&perk);
		}
	}
	size_t rows = 1;
	for(const auto & rankPerks : perksByRank)
		rows = std::max(rows, rankPerks.size());
	const int perksTop = DESCRIPTION_TOP + (skill ? rankTextHeight(hero, skillId) : 0) + 14;
	const int columnBottom = perksTop + static_cast<int>(rows) * PERK_ROW_HEIGHT + 4;
	pos = Rect(0, 0, WINDOW_WIDTH, columnBottom + FOOTER_HEIGHT);
	// The same tiled leather as the other New Horizons dialogs, cut to fit the
	// content; BORDERED supplies the original player-coloured frame.
	elements.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h)));

	if(!skill)
	{
		elements.push_back(std::make_shared<CLabel>(pos.w / 2, pos.h / 2 - 20, FONT_MEDIUM, ETextAlignment::CENTER,
			Colors::WHITE, "No saved perk catalogue is available for this Skill.", 560));
	}
	else
	{
		elements.push_back(std::make_shared<CLabel>(pos.w / 2, 19, FONT_BIG, ETextAlignment::CENTER,
			Colors::YELLOW, "Perks: " + newHorizonsPerkHelp::skillName(&hero, skillId), 590));
		for(size_t rankIndex = 0; rankIndex < perksByRank.size(); ++rankIndex)
			addRankColumn(elements, hero, skillId, rankIndex, perksByRank[rankIndex], perksTop, columnBottom);
	}

	elements.push_back(std::make_shared<CLabel>(16, pos.h - 28, FONT_TINY, ETextAlignment::TOPLEFT,
		Colors::WHITE, "Click a perk for its explanation; use i for details.", 500));
	// Read-only information window: the original checkmark closes it (Enter or Escape).
	closeButton = std::make_shared<CButton>(Point(pos.w - 72, pos.h - 38), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Close", "Return to the hero screen without changing any Skills or perks."),
		[this] { close(); }, EShortcut::GLOBAL_RETURN);
	updateShadow();
	center();
}
