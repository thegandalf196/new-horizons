/*
 * CRecruitmentWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "GUIClasses.h"
#include "CCreatureWindow.h"
#include "NewHorizonsMusterUI.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../UIHelper.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "render/Canvas.h"
#include "../widgets/Buttons.h"
#include "../widgets/CreatureCostBox.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/NewHorizonsEstates.h"
#include "../../lib/mapObjects/TownRecruitmentOffers.h"
#include "../../lib/texts/CGeneralTextHandler.h"

static std::optional<newHorizonsCreatures::CreatureCategoryView> currentCreatureCategory(const CCreature * creature)
{
	if(!creature || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;
	return GAME->interface()->cb->getCreatureCategory(creature->getId());
}

ImagePath CRecruitmentWindow::getRecruitmentBackground(const CGDwelling * dwelling, int level)
{
	int cardsCount = 0;
	for(int i = 0; i < dwelling->creatures.size(); i++)
	{
		if(level >= 0 && i != level)
			continue;

		if(const auto offer = townRecruitmentOffers::query(*dwelling, i, GAME->interface()->playerID))
			cardsCount += static_cast<int>(offer->choices.size());
	}

	std::string fileName = cardsCount >= 6 ? "TPRCRT6" : cardsCount == 5 ? "TPRCRT5" : "TPRCRT4";
	return ImagePath::builtin(fileName);
}

CRecruitmentWindow::CCreatureCard::CCreatureCard(CRecruitmentWindow * window, const CCreature * crea, int totalAmount, size_t stockRow)
	: CIntObject(LCLICK | SHOW_POPUP),
	parent(window),
	selected(false),
	creature(crea),
	amount(totalAmount),
	row(stockRow)
{
	OBJECT_CONSTRUCTION;
	animation = std::make_shared<CCreaturePic>(1, 1, creature, true, true);
	// 1 + 1 px for borders
	pos.w = animation->pos.w + 2;
	pos.h = animation->pos.h + 2;
}

void CRecruitmentWindow::CCreatureCard::select(bool on)
{
	selected = on;
	redraw();
}

void CRecruitmentWindow::CCreatureCard::clickPressed(const Point & cursorPosition)
{
	parent->select(this->shared_from_this());
}

void CRecruitmentWindow::CCreatureCard::showPopupWindow(const Point & cursorPosition)
{
	const auto * context = parent->dst ? parent->dst->cb : (parent->dwelling ? parent->dwelling->cb : nullptr);
	ENGINE->windows().createAndPushWindow<CStackWindow>(creature, true, context);
}

void CRecruitmentWindow::CCreatureCard::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	if(selected)
		to.drawBorder(pos, Colors::RED);
	else
		to.drawBorder(pos, Colors::YELLOW);
}

void CRecruitmentWindow::select(std::shared_ptr<CCreatureCard> card)
{
	if(card == selected)
		return;

	if(selected)
		selected->select(false);

	selected = card;

	if(selected)
		selected->select(true);

	if(card)
	{
		const auto recruitmentCost = dwelling->getTotalRecruitmentCost(card->creature->getId(), 1, newHorizonsEstates::recruitmentBuyer(dst));
		si32 maxAmount = dwelling->getRecruitmentAmount(card->creature->getId(), GAME->interface()->cb->getResourceAmount(),
			card->amount, newHorizonsEstates::recruitmentBuyer(dst));

		vstd::amin(maxAmount, card->amount);
		const auto slot = UIHelper::recruitmentPosition(dst, card->creature->getId(), maxAmount);
		if(!slot.validSlot() && !card->creature->warMachine.hasValue())
			maxAmount = 0;

		slider->setAmount(maxAmount);

		if(slider->getValue() != maxAmount)
			slider->scrollTo(maxAmount);
		else // if slider already at 0 - emulate call to sliderMoved()
			sliderMoved(maxAmount);

		costPerTroopValue->createItems(recruitmentCost);
		totalCostValue->createItems(recruitmentCost);

		costPerTroopValue->set(recruitmentCost);
		totalCostValue->set(dwelling->getTotalRecruitmentCost(card->creature->getId(), maxAmount,
			newHorizonsEstates::recruitmentBuyer(dst)));

		//Recruit %s
		MetaString recruitText;
		recruitText.appendTextID("core.tcommand.21");
		recruitText.replaceNamePlural(card->creature->getId());
		title->setText(recruitText.toString(&GAME->translator()));

		maxButton->block(maxAmount == 0);
		slider->block(maxAmount == 0);
	}
}

void CRecruitmentWindow::close()
{
	if (onClose)
		onClose();
	CStatusbarWindow::close();
}

void CRecruitmentWindow::buy()
{
	if(!selected)
		return;
	const auto offer = townRecruitmentOffers::query(*dwelling, selected->row, GAME->interface()->playerID);
	if(!offer || !vstd::contains(offer->choices, selected->creature->getId())
		|| slider->getValue() <= 0 || static_cast<uint32_t>(slider->getValue()) > offer->available)
	{
		availableCreaturesChanged();
		return;
	}
	CreatureID crid =  selected->creature->getId();
	const int requestedCount = slider->getValue();
	SlotID dstslot = UIHelper::recruitmentPosition(dst, crid, requestedCount);
	const CGHeroInstance * hero = dynamic_cast<const CGHeroInstance *>(dst);
	if(!selected->creature->warMachine.hasValue() && !UIHelper::checkArmySlot(dst, dstslot, crid))
		return;

	if (selected->creature->warMachine.hasValue() && hero)
	{
		ArtifactID newWarMachine = selected->creature->warMachine;
		ArtifactID currentWarMachine = hero->getReplacedWarMachine(newWarMachine);
		if (currentWarMachine.hasValue() && newWarMachine != currentWarMachine)
		{
			MetaString message;
			message.appendTextID("vcmi.townWindow.blacksmith.replaceWarMachine");
			message.replaceName(currentWarMachine);
			message.replaceName(newWarMachine);

			GAME->interface()->showYesNoDialog(
				message.toString(&GAME->translator()),
				[this, crid](){ onRecruit(crid, slider->getValue()); if(level >= 0) close();},
				nullptr
			);
			return;
		}
	}

	if(!dstslot.validSlot() && (selected->creature->warMachine == ArtifactID::NONE)) //no available slot
	{
		std::pair<SlotID, SlotID> toMerge;
		bool allowMerge = GAME->interface()->cb->getSettings().getBoolean(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_OWNED);

		if (allowMerge && dst->mergeableStacks(toMerge))
		{
			GAME->interface()->cb->mergeStacks( dst, dst, toMerge.first, toMerge.second);
		}
		else
		{
			MetaString txt;
			if(dwelling->ID != Obj::TOWN)
			{
				txt.appendTextID("core.genrltxt.425"); //The %s would join your hero, but there aren't enough provisions to support them.
				txt.replaceName(CreatureID(crid), slider->getValue());
			}
			else
			{
				txt.appendTextID("core.genrltxt.17"); //There is no room in the garrison for this army.
			}

			GAME->interface()->showInfoDialog(txt.toString(&GAME->translator()));
			return;
		}
	}

	onRecruit(crid, slider->getValue());
	if(level >= 0)
		close();
}

void CRecruitmentWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
	const int layoutOffsetX = (pos.w - 484) / 2;

	Rect(172 + layoutOffsetX, 222, 67, 42) + pos.topLeft();

	// recruit\total values
	to.drawBorder(Rect(171 + layoutOffsetX, 222, 67, 42) + pos.topLeft(), Colors::YELLOW);
	to.drawBorder(Rect(246 + layoutOffsetX, 222, 67, 42) + pos.topLeft(), Colors::YELLOW);

	//cost boxes
	to.drawBorder(Rect(63 + layoutOffsetX, 222, 99, 76) + pos.topLeft(), Colors::YELLOW);
	to.drawBorder(Rect(322 + layoutOffsetX, 222, 99, 76) + pos.topLeft(), Colors::YELLOW);

	//buttons borders
	to.drawBorder(Rect(133 + layoutOffsetX, 312, 66, 34) + pos.topLeft(), Colors::METALLIC_GOLD);
	to.drawBorder(Rect(211 + layoutOffsetX, 312, 66, 34) + pos.topLeft(), Colors::METALLIC_GOLD);
	to.drawBorder(Rect(289 + layoutOffsetX, 312, 66, 34) + pos.topLeft(), Colors::METALLIC_GOLD);
	if(musterButton)
		to.drawBorder(Rect(367 + layoutOffsetX, 312, 66, 34) + pos.topLeft(), Colors::METALLIC_GOLD);
}

CRecruitmentWindow::CRecruitmentWindow(const CGDwelling * Dwelling, int Level, const CArmedInstance * Dst, const std::function<void(CreatureID,int)> & Recruit, const std::function<void()> & onClose, int y_offset):
	CWindowObject(PLAYER_COLORED_BORDERED_STATUSBAR, getRecruitmentBackground(Dwelling, Level)),
	onRecruit(Recruit),
	onClose(onClose),
	level(Level),
	dst(Dst),
	selected(nullptr),
	dwelling(Dwelling)
{
	moveBy(Point(0, y_offset));
	const int layoutOffsetX = (pos.w - 484) / 2;

	OBJECT_CONSTRUCTION;

	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(8, pos.h - 26, pos.w - 16, 19), 8, pos.h - 26));

	slider = std::make_shared<CSlider>(Point(173 + layoutOffsetX, 280), 138, std::bind(&CRecruitmentWindow::sliderMoved, this, _1), 0, 0, 0, Orientation::HORIZONTAL);

	maxButton = std::make_shared<CButton>(Point(134 + layoutOffsetX, 313), AnimationPath::builtin("IRCBTNS.DEF"), LIBRARY->generaltexth->zelp[553], std::bind(&CSlider::scrollToMax, slider), EShortcut::RECRUITMENT_MAX);
	const auto * destinationHero = dynamic_cast<const CGHeroInstance *>(Dst);
	if(newHorizonsMusterUI::isEligible(Dwelling, destinationHero))
	{
		musterButton = std::make_shared<CButton>(Point(368 + layoutOffsetX, 313), AnimationPath::builtin("IRCBTNS.DEF"),
			CButton::tooltip("Muster", "Reinforce one eligible dwelling."),
			[Dwelling, destinationHero](){ newHorizonsMusterUI::open(Dwelling, destinationHero); });
		musterButton->setTextOverlay("M", FONT_SMALL, Colors::WHITE);
		if(const auto offer = newHorizonsMusterUI::offerFor(Dwelling, destinationHero))
		{
			musterButton->addHoverText(EButtonState::NORMAL, newHorizonsMusterUI::status(*offer));
			musterButton->block(!GAME->interface()->makingTurn || offer->usedThisWeek || newHorizonsMusterUI::targetsFor(*offer).empty());
		}
	}
	buyButton = std::make_shared<CButton>(Point(212 + layoutOffsetX, 313), AnimationPath::builtin("IBY6432.DEF"), LIBRARY->generaltexth->zelp[554], std::bind(&CRecruitmentWindow::buy, this), EShortcut::GLOBAL_ACCEPT);
	cancelButton = std::make_shared<CButton>(Point(290 + layoutOffsetX, 313), AnimationPath::builtin("ICN6432.DEF"), LIBRARY->generaltexth->zelp[555], std::bind(&CRecruitmentWindow::close, this), EShortcut::GLOBAL_CANCEL);

	title = std::make_shared<CLabel>(243 + layoutOffsetX, 32, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW);
	availableValue = std::make_shared<CLabel>(204 + layoutOffsetX, 253, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);
	toRecruitValue = std::make_shared<CLabel>(279 + layoutOffsetX, 253, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);

	costPerTroopValue = std::make_shared<CreatureCostBox>(Rect(64 + layoutOffsetX, 222, 97, 74), LIBRARY->generaltexth->allTexts[346]);
	totalCostValue = std::make_shared<CreatureCostBox>(Rect(323 + layoutOffsetX, 222, 97, 74), LIBRARY->generaltexth->allTexts[466]);

	availableTitle = std::make_shared<CLabel>(204 + layoutOffsetX, 233, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[465]);
	toRecruitTitle = std::make_shared<CLabel>(279 + layoutOffsetX, 233, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[16]);

	availableCreaturesChanged();
}

void CRecruitmentWindow::availableCreaturesChanged()
{
	OBJECT_CONSTRUCTION;
	if(musterButton)
	{
		if(const auto offer = newHorizonsMusterUI::offerFor(dwelling, dynamic_cast<const CGHeroInstance *>(dst)))
		{
			musterButton->addHoverText(EButtonState::NORMAL, newHorizonsMusterUI::status(*offer));
			musterButton->block(!GAME->interface()->makingTurn || offer->usedThisWeek
				|| newHorizonsMusterUI::targetsFor(*offer).empty());
		}
	}

	size_t selectedIndex = 0;

	if(!cards.empty() && selected) // find position of selected item
		selectedIndex = std::find(cards.begin(), cards.end(), selected) - cards.begin();

	select(nullptr);

	cards.clear();

	for(int i=0; i<dwelling->creatures.size(); i++)
	{
		//find appropriate level
		if(level >= 0 && i != level)
			continue;

		const auto offer = townRecruitmentOffers::query(*dwelling, i, GAME->interface()->playerID);
		if(!offer)
			continue;

		// Alternatives display the same stock; only the selected variant is bought.
		for(const auto creature : std::views::reverse(offer->choices))
			cards.push_back(std::make_shared<CCreatureCard>(this, creature.toCreature(), offer->available, i));
	}

	layoutCards();

	// A changed residence/offer may remove the old selection entirely.
	if(cards.empty())
	{
		buyButton->block(true);
		maxButton->block(true);
		slider->setAmount(0);
		slider->block(true);
		return;
	}
	select(cards[std::min(selectedIndex, cards.size() - 1)]);

	if(slider->getValue() == slider->getAmount())
		slider->scrollToMax();
	else // if slider already at 0 - emulate call to sliderMoved()
		sliderMoved(slider->getAmount());
}

void CRecruitmentWindow::layoutCards()
{
	const int creatureWidth = 102;

	//normal distance between cards - 18px
	int requiredSpace = 18;
	//maximum distance we can use without reaching window borders
	int availableSpace = pos.w - 50 - creatureWidth * (int)cards.size();

	if (cards.size() > 1) // avoid division by zero
		availableSpace /= (int)cards.size() - 1;
	else
		availableSpace = 0;

	assert(availableSpace >= 0);

	const int spaceBetween = std::min(requiredSpace, availableSpace);
	const int totalCreatureWidth = spaceBetween + creatureWidth;

	// Now we know total amount of cards and can move them to correct position.
	// New Horizons presents independent dwelling choices in Core, Elite,
	// Champion order. Keep the cards vector in historical order so selection,
	// callbacks, and legacy recruitment semantics remain unchanged.
	int curx = pos.w / 2 - (creatureWidth*(int)cards.size()/2) - (spaceBetween*((int)cards.size()-1)/2);
	std::array<std::vector<std::shared_ptr<CCreatureCard>>, 3> categoryCards;
	std::vector<std::shared_ptr<CCreatureCard>> uncategorizedCards;
	for(const auto & card : cards)
	{
		if(const auto category = currentCreatureCategory(card->creature))
			categoryCards[static_cast<size_t>(category->category)].push_back(card);
		else
			uncategorizedCards.push_back(card);
	}

	const bool grouped = uncategorizedCards.empty()
		&& std::ranges::any_of(categoryCards, [](const auto & group){ return !group.empty(); });
	if(grouped)
	{
		for(size_t index = 0; index < categoryCards.size(); ++index)
		{
			const auto & group = categoryCards[index];
			if(group.empty())
				continue;

			for(const auto & card : group)
			{
				card->moveBy(Point(curx, 64));
				curx += totalCreatureWidth;
			}
		}

		for(const auto & card : uncategorizedCards)
		{
			card->moveBy(Point(curx, 64));
			curx += totalCreatureWidth;
		}
	}
	else
	{
		for(auto & card : cards)
		{
			card->moveBy(Point(curx, 64));
			curx += totalCreatureWidth;
		}
	}
}

void CRecruitmentWindow::sliderMoved(int to)
{
	if(!selected)
		return;

	const auto recruitmentCost = dwelling->getTotalRecruitmentCost(selected->creature->getId(), to, newHorizonsEstates::recruitmentBuyer(dst));
	const bool canAfford = GAME->interface()->cb->getResourceAmount().canAfford(recruitmentCost);
	buyButton->block(!to || !canAfford || !GAME->interface()->makingTurn);
	availableValue->setText(std::to_string(selected->amount - to));
	toRecruitValue->setText(std::to_string(to));

	totalCostValue->set(recruitmentCost);
}

