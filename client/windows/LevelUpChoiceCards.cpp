/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LevelUpChoiceCards.h"
#include "../gui/CIntObject.h"
#include "../widgets/CComponent.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "render/Canvas.h"
namespace
{
// Secondary skill choice card (Screen 1: Skill Choices)
class CLevelUpSkillCardComponent : public CSelectableComponent
{
public:
	using PerkRowInfo = levelUpPresentation::PerkRow;

private:
	std::string titleText;
	std::vector<PerkRowInfo> perkRows;
	std::vector<std::shared_ptr<CAnimImage>> perkImages;
	std::vector<std::shared_ptr<CLabel>> perkLabels;
	std::shared_ptr<CMultiLineLabel> titleLabel;

public:
	CLevelUpSkillCardComponent(SecondarySkill skill, int offeredRank,
		const std::string & skillName, const std::string & offeredRankText,
		const std::vector<PerkRowInfo> & perks)
		: CSelectableComponent(ComponentType::SEC_SKILL, skill, offeredRank, CComponent::medium),
		  perkRows(perks)
	{
		OBJECT_CONSTRUCTION;
		clearLines();
		pos.w = 252;
		pos.h = 206;

		titleText = offeredRankText + " " + skillName;
		customSubtitle = offeredRankText + "\n" + skillName;

		if(image)
		{
			image->moveTo(pos.topLeft() + Point(10, 8));
		}

		titleLabel = std::make_shared<CMultiLineLabel>(Rect(60, 8, 184, 44), FONT_MEDIUM,
			ETextAlignment::CENTERLEFT, Colors::WHITE, titleText);
		if(titleLabel->textSize.y > 40)
			titleLabel = std::make_shared<CMultiLineLabel>(Rect(60, 8, 184, 44), FONT_SMALL,
				ETextAlignment::CENTERLEFT, Colors::WHITE, titleText);
		addChild(titleLabel.get());

		const size_t rowCount = std::min<size_t>(perkRows.size(), 4);
		for(size_t i = 0; i < rowCount; ++i)
		{
			const int rowY = 58 + static_cast<int>(i) * 36;
			const auto & row = perkRows[i];

			auto pImg = std::make_shared<CAnimImage>(AnimationPath::builtin(row.icon), 0,
				Rect(14, rowY + 5, 26, 26));
			perkImages.push_back(pImg);
			addChild(pImg.get());

			auto pLbl = std::make_shared<CLabel>(48, rowY + 11,
				FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, row.name, 196);
			perkLabels.push_back(pLbl);
			addChild(pLbl.get());
		}
	}

	void showAll(Canvas & to) override
	{
		to.drawColor(pos, ColorRGBA(28, 20, 15, 255));
		if(selected)
		{
			to.drawBorder(pos, Colors::BRIGHT_YELLOW);
			to.drawBorder(Rect(pos.x + 1, pos.y + 1, pos.w - 2, pos.h - 2), ColorRGBA(255, 215, 0, 255));
		}
		else
		{
			to.drawBorder(pos, ColorRGBA(140, 105, 45, 255));
		}
		if(image)
		{
			to.drawBorder(Rect(image->pos.x - 1, image->pos.y - 1, image->pos.w + 2, image->pos.h + 2),
				ColorRGBA(180, 140, 50, 255));
		}
		const int dividerY = pos.y + 56;
		to.drawLine(Point(pos.x + 8, dividerY), Point(pos.x + pos.w - 8, dividerY),
			ColorRGBA(140, 105, 45, 255), ColorRGBA(140, 105, 45, 255));

		for(size_t i = 0; i < perkImages.size(); ++i)
		{
			if(perkImages[i])
			{
				to.drawBorder(Rect(perkImages[i]->pos.x - 1, perkImages[i]->pos.y - 1,
					perkImages[i]->pos.w + 2, perkImages[i]->pos.h + 2),
					ColorRGBA(150, 115, 40, 255));
			}
			const int rowY = pos.y + 58 + static_cast<int>(i) * 36;
			if(i + 1 < perkImages.size())
			{
				to.drawLine(Point(pos.x + 12, rowY + 36), Point(pos.x + pos.w - 12, rowY + 36),
					ColorRGBA(100, 75, 35, 255), ColorRGBA(100, 75, 35, 255));
			}
		}
		CIntObject::showAll(to);
	}
};

// Perk choice card (Screen 2: Perk Choices)
class CLevelUpPerkComponent : public CSelectableComponent
{
	std::shared_ptr<CMultiLineLabel> titleLabel;

public:
	CLevelUpPerkComponent(SecondarySkill skill, bool hasSkill,
		const std::string & name, const AnimationPath & icon)
		: CSelectableComponent(hasSkill ? ComponentType::SEC_SKILL : ComponentType::NONE,
			hasSkill ? skill : SecondarySkill(0), name, CComponent::medium)
	{
		OBJECT_CONSTRUCTION;
		clearLines();
		setCustomIcon(icon);
		pos.w = 236;
		pos.h = 64;

		if(image)
		{
			image->moveTo(pos.topLeft() + Point(10, 10));
		}

		titleLabel = std::make_shared<CMultiLineLabel>(Rect(62, 0, 166, 64), FONT_MEDIUM,
			ETextAlignment::CENTERLEFT, Colors::WHITE, name);
		if(titleLabel->textSize.y > 52)
			titleLabel = std::make_shared<CMultiLineLabel>(Rect(62, 0, 166, 64), FONT_SMALL,
				ETextAlignment::CENTERLEFT, Colors::WHITE, name);
		addChild(titleLabel.get());

	}

	void showAll(Canvas & to) override
	{
		to.drawColor(pos, ColorRGBA(28, 20, 15, 255));
		if(selected)
		{
			to.drawBorder(pos, Colors::BRIGHT_YELLOW);
			to.drawBorder(Rect(pos.x + 1, pos.y + 1, pos.w - 2, pos.h - 2), ColorRGBA(255, 215, 0, 255));
		}
		else
		{
			to.drawBorder(pos, ColorRGBA(140, 105, 45, 255));
		}
		if(image)
		{
			to.drawBorder(Rect(image->pos.x - 1, image->pos.y - 1, image->pos.w + 2, image->pos.h + 2),
				ColorRGBA(180, 140, 50, 255));
		}
		CIntObject::showAll(to);
	}
};
}

namespace levelUpPresentation
{
std::shared_ptr<CSelectableComponent> skillCard(SecondarySkill skill, int offeredRank,
	const std::string & skillName, const std::string & rankText, const std::vector<PerkRow> & perks)
{
	return std::make_shared<CLevelUpSkillCardComponent>(skill, offeredRank, skillName, rankText, perks);
}

std::shared_ptr<CSelectableComponent> perkCard(SecondarySkill skill, bool hasSkill,
	const std::string & name, const AnimationPath & icon)
{
	return std::make_shared<CLevelUpPerkComponent>(skill, hasSkill, name, icon);
}
}
