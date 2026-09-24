/*
 * NewHorizonsPerkBrowser.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsPerkBrowser.h"

#include "InfoWindows.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsPerkIcons.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
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
	std::optional<SecondarySkill> parentSkill;
	int skillRank;

public:
	PerkBrowserHelpArea(const Rect & area, std::string description_, std::optional<SecondarySkill> parentSkill_, int skillRank_)
		: description(std::move(description_)), parentSkill(std::move(parentSkill_)), skillRank(skillRank_)
	{
		// CIntObject has already adjusted the child to the parent's screen origin.
		// Match LRClickableAreaWText's local-rectangle convention when replacing
		// that position with this card's bounds.
		pos = area + pos.topLeft();
		hoverText = "Right-click for the perk explanation.";
		addUsedEvents(SHOW_POPUP);
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
}

NewHorizonsPerkBrowser::NewHorizonsPerkBrowser(const CGHeroInstance & hero, const std::string & skillId)
	: CWindowObject(BORDERED, ImagePath::builtin("newHorizonsOrdersBackground.png"))
{
	OBJECT_CONSTRUCTION;

	const auto & perkState = hero.getPerkState();
	const auto skill = newHorizonsPerkHelp::skillDefinition(&hero, skillId);
	if(!skill)
	{
		elements.push_back(std::make_shared<CLabel>(320, 214, FONT_MEDIUM, ETextAlignment::CENTER,
			Colors::WHITE, "No saved perk catalogue is available for this Skill.", 560));
	}
	else
	{
		std::array<std::vector<const newHorizonsHeroes::PerkDefinition *>, 3> perksByRank;
		for(const auto & perk : skill->perks)
		{
			const int rank = perkRank(perk);
			if(rank >= 1 && rank <= static_cast<int>(perksByRank.size()))
				perksByRank[static_cast<size_t>(rank - 1)].push_back(&perk);
		}

		elements.push_back(std::make_shared<CLabel>(320, 19, FONT_BIG, ETextAlignment::CENTER,
			Colors::YELLOW, "Perks: " + skill->name, 590));
		elements.push_back(std::make_shared<CLabel>(320, 51, FONT_SMALL, ETextAlignment::CENTER,
			Colors::WHITE, "Read-only catalogue. Learned perks are marked Learned; browsing changes nothing.", 600));

		constexpr int columnLeft = 12;
		constexpr int columnStride = 208;
		constexpr int columnWidth = 200;
		constexpr int columnTop = 77;
		constexpr int columnHeight = 381;
		constexpr int cardsTop = 116;
		constexpr int cardsBottom = 450;
		constexpr int cardGap = 3;
		const std::array<const char *, 3> rankNames = {"Basic", "Advanced", "Expert"};
		const ColorRGBA panelFill(35, 21, 13, 92);
		const ColorRGBA panelBorder(154, 119, 66, 255);
		const ColorRGBA cardFill(20, 13, 9, 86);
		const ColorRGBA unlearnedBorder(130, 69, 51, 255);
		const ColorRGBA learnedBorder(184, 143, 62, 255);

		for(size_t rankIndex = 0; rankIndex < perksByRank.size(); ++rankIndex)
		{
			const int left = columnLeft + static_cast<int>(rankIndex) * columnStride;
			elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(left, columnTop, columnWidth, columnHeight),
				panelFill, panelBorder));
			elements.push_back(std::make_shared<CLabel>(left + columnWidth / 2, 86, FONT_MEDIUM,
				ETextAlignment::CENTER, Colors::YELLOW, rankNames[rankIndex]));

			const auto & rankPerks = perksByRank[rankIndex];
			if(rankPerks.empty())
			{
				elements.push_back(std::make_shared<CLabel>(left + columnWidth / 2, 130, FONT_SMALL,
					ETextAlignment::CENTER, Colors::WHITE, "No perks in this rank.", columnWidth - 20));
				continue;
			}

			const int contentHeight = cardsBottom - cardsTop;
			const int availableHeight = contentHeight - cardGap * static_cast<int>(rankPerks.size() - 1);
			const int cardHeight = std::min(56, availableHeight / static_cast<int>(rankPerks.size()));
			const int totalCardsHeight = cardHeight * static_cast<int>(rankPerks.size())
				+ cardGap * static_cast<int>(rankPerks.size() - 1);
			const int firstCardTop = cardsTop + (contentHeight - totalCardsHeight) / 2;
			const int cardLeft = left + 6;
			const int cardWidth = columnWidth - 12;
			for(size_t perkIndex = 0; perkIndex < rankPerks.size(); ++perkIndex)
			{
				const auto & perk = *rankPerks[perkIndex];
				const bool learned = perkState.hasSelection(skillId, perk.id);
				const auto effectStatus = implementationStatus(perk);
				const int top = firstCardTop + static_cast<int>(perkIndex) * (cardHeight + cardGap);
				const Rect card(cardLeft, top, cardWidth, cardHeight);
				elements.push_back(std::make_shared<TransparentFilledRectangle>(card, cardFill,
					learned ? learnedBorder : unlearnedBorder));

				auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin(newHorizonsPerkIcon(perk.id)), 0,
					Rect(cardLeft + 4, top + 3, 24, 24));
				elements.push_back(icon);
				elements.push_back(std::make_shared<CMultiLineLabel>(Rect(cardLeft + 34, top + 2, cardWidth - 40, 26),
					FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, perk.name));
				elements.push_back(std::make_shared<CLabel>(cardLeft + 34, top + 30, FONT_TINY,
					ETextAlignment::TOPLEFT, learned ? Colors::YELLOW : Colors::WHITE,
					learned ? "Learned" : "Not learned", cardWidth - 40));
				elements.push_back(std::make_shared<CLabel>(cardLeft + 34, top + 42, FONT_TINY,
					ETextAlignment::TOPLEFT, effectStatus == "Implemented" ? Colors::GREEN : Colors::WHITE,
					effectStatus, cardWidth - 40));

				const auto description = newHorizonsPerkHelp::format(&hero, skillId, perk.name,
					newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description)
					+ "\n\nImplementation: " + effectStatus + ". This browser is read-only; it does not unlock perks.";
				auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId);
				const int skillRank = std::clamp(hero.getPerkSkillRank(skillId), 1, 3);
				elements.push_back(std::make_shared<PerkBrowserHelpArea>(card, description,
					std::move(parentSkill), skillRank));
			}
		}
	}

	elements.push_back(std::make_shared<CLabel>(16, 477, FONT_TINY, ETextAlignment::TOPLEFT,
		Colors::WHITE, "Right-click a perk for its explanation.", 500));
	closeButton = std::make_shared<CButton>(Point(555, 462), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Close", "Return to the hero screen without changing any Skills or perks."),
		[this] { close(); }, EShortcut::GLOBAL_CANCEL);
	closeButton->setHoverable(true);
	updateShadow();
}
