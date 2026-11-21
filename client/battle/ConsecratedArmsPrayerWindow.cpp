/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsPrayerWindow.h"
#include "ConsecratedArmsPrayerChoice.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"
#include "CurrentFactionSkillPresentation.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "../render/Colors.h"
#include "render/IImage.h"
#include "render/IRenderHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/InfoWindows.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
using consecratedArms::Prayer;
using consecratedArmsPrayerChoice::Selection;

constexpr int WINDOW_WIDTH = 640;
constexpr int WINDOW_HEIGHT = 560;
constexpr int CARD_WIDTH = 196;
constexpr int CARD_HEIGHT = 100;
constexpr int ROW_HEIGHT = 36;
constexpr size_t VISIBLE_ROWS = 5;
constexpr int LIST_TOP = 314;

struct PrayerValues
{
	consecratedArms::Policy policy;
	int faith = 0;
	int potency = 0;
	int rounds = 0;
};

PrayerValues prayerValues(const BattleInterface & owner, BattleSide side)
{
	PrayerValues result;
	if(const auto * hero = owner.getBattle()->battleGetFightingHero(side))
	{
		result.policy = consecratedArms::capturePolicy(hero);
		result.faith = consecratedArms::actualFaith(*hero);
		if(result.policy.rank)
		{
			result.potency = consecratedArms::potencyFaith(result.faith, result.policy);
			result.rounds = consecratedArms::duration(result.faith, result.policy);
		}
	}
	return result;
}

std::string roundsText(int rounds)
{
	return std::to_string(rounds) + (rounds == 1 ? " round" : " rounds");
}

std::string prayerDetails(Prayer prayer, const PrayerValues & values)
{
	const consecratedArms::ActivePrayer snapshot{prayer, values.potency, values.rounds, values.policy};
	std::string result = "{" + std::string(consecratedArms::name(prayer)) + "}\n\n"
		+ passiveFactionSkillPresentation::prayerEffect(snapshot, 1)
		+ "\n\nPotency Faith: " + std::to_string(values.potency) + ". Duration: " + roundsText(values.rounds) + ".";
	if(values.policy.has(consecratedArms::Perk::SACRED_ACCORD))
		result += "\nSacred Accord strengthens prayers when the recipient holds several distinct prayers;"
			" the values above show one active prayer.";
	return result;
}

std::string unitText(const BattleInterface & owner, uint32_t id)
{
	const auto * unit = owner.getBattle()->battleGetUnitByID(id);
	if(!unit || !unit->alive())
		return "This stack is no longer available";
	const auto * type = unit->unitType();
	const std::string name = !type ? "Unknown stack"
		: (unit->getCount() == 1 ? type->getNameSingularTranslated() : type->getNamePluralTranslated());
	return std::to_string(unit->getCount()) + " " + name;
}

std::string unitDetails(const BattleInterface & owner, uint32_t id)
{
	const auto * unit = owner.getBattle()->battleGetUnitByID(id);
	if(!unit || !unit->alive())
		return {};
	const auto position = unit->getPosition();
	std::string result = position.isValid()
		? "Row " + std::to_string(position.getY() + 1) + ", column " + std::to_string(position.getX() + 1)
		: std::string("Off battlefield");
	const int active = consecratedArms::activePrayerCount(*unit);
	result += active ? ". Active prayers: " + std::to_string(active) + "." : ". No active prayers.";
	return result;
}

class PrayerCard final : public CIntObject
{
	std::weak_ptr<Selection> selection;
	Prayer prayer;
	std::function<void()> changed;
	std::shared_ptr<TransparentFilledRectangle> frame;
	std::shared_ptr<TransparentFilledRectangle> chosenFrame;
	std::shared_ptr<CLabel> title;
	std::shared_ptr<CButton> chooseButton;
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::optional<std::pair<bool, bool>> shown; // chosen, blocked

	void toggle()
	{
		if(auto model = selection.lock(); model && model->togglePrayer(prayer) && changed)
			changed();
	}

public:
	PrayerCard(Point position, std::weak_ptr<Selection> selection_, Prayer prayer_, const std::string & details,
		std::function<void()> changed_)
		: CIntObject(0, position), selection(std::move(selection_)), prayer(prayer_), changed(std::move(changed_))
	{
		pos.w = CARD_WIDTH;
		pos.h = CARD_HEIGHT;
		OBJECT_CONSTRUCTION;
		frame = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, CARD_WIDTH, CARD_HEIGHT),
			ColorRGBA(26, 23, 18, 155), ColorRGBA(119, 93, 56, 255));
		chosenFrame = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, CARD_WIDTH, CARD_HEIGHT),
			ColorRGBA(48, 40, 18, 175), Colors::YELLOW, 2);
		auto image = std::make_shared<CPicture>(ImagePath::builtin(passiveFactionSkillPresentation::prayerImage(prayer)), 6, 6);
		image->removeUsedEvents(LCLICK | SHOW_POPUP);
		elements.push_back(image);
		title = std::make_shared<CLabel>(52, 6, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
			consecratedArms::name(prayer));
		title->setMaxWidth(CARD_WIDTH - 58);
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(52, 24, CARD_WIDTH - 58, 36), FONT_TINY,
			ETextAlignment::TOPLEFT, Colors::WHITE, passiveFactionSkillPresentation::prayerSummary(prayer)));
		// The card body chooses on left click and explains current values on right click.
		elements.push_back(std::make_shared<LRClickableArea>(Rect(0, 0, CARD_WIDTH, 62),
			[this] { toggle(); }, [details] { CRClickPopup::createAndPush(details); }));
		chooseButton = std::make_shared<CButton>(Point(CARD_WIDTH - 86, 64), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip(consecratedArms::name(prayer), details), [this] { toggle(); });
		chooseButton->setHoverable(true);
		refresh();
	}

	void refresh()
	{
		auto model = selection.lock();
		const bool chosen = model && model->chosen(prayer);
		const bool blocked = !model || !model->canToggle(prayer);
		// Setters below request redraws; only touch them when the state changed.
		if(shown == std::pair{chosen, blocked})
			return;
		shown = std::pair{chosen, blocked};
		frame->setEnabled(!chosen);
		chosenFrame->setEnabled(chosen);
		title->setColor(chosen ? Colors::YELLOW : Colors::WHITE);
		chooseButton->setTextOverlay(chosen ? "Chosen" : "Choose", FONT_SMALL, chosen ? Colors::YELLOW : Colors::WHITE);
		chooseButton->setBorderColor(chosen ? std::optional<ColorRGBA>(Colors::YELLOW) : std::nullopt);
		chooseButton->block(blocked);
	}
};

class RecipientRow final : public CIntObject
{
	std::weak_ptr<BattleInterface> battle;
	std::weak_ptr<Selection> selection;
	uint32_t id;
	std::function<void()> changed;
	std::shared_ptr<CLabel> name;
	std::shared_ptr<CLabel> details;
	std::shared_ptr<CButton> chooseButton;
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::optional<std::pair<bool, bool>> shown; // chosen, blocked

	void choose()
	{
		if(auto model = selection.lock(); model && model->chooseRecipient(id) && changed)
			changed();
	}

public:
	RecipientRow(std::weak_ptr<BattleInterface> battle_, std::weak_ptr<Selection> selection_, uint32_t unitId,
		std::function<void()> changed_)
		: battle(std::move(battle_)), selection(std::move(selection_)), id(unitId), changed(std::move(changed_))
	{
		pos.w = 560;
		pos.h = ROW_HEIGHT;
		OBJECT_CONSTRUCTION;
		const auto owner = battle.lock();
		const auto * unit = owner ? owner->getBattle()->battleGetUnitByID(id) : nullptr;
		if(unit)
			elements.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("CPRSMALL"),
				unit->creatureIconIndex(), 0, 4, 2));
		name = std::make_shared<CLabel>(44, 2, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
		details = std::make_shared<CLabel>(44, 20, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "");
		name->setMaxWidth(420);
		details->setMaxWidth(420);
		chooseButton = std::make_shared<CButton>(Point(470, 2), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Choose recipient", "This stack receives the chosen prayer."), [this] { choose(); });
		chooseButton->setHoverable(true);
		refresh();
	}

	void refresh()
	{
		const auto owner = battle.lock();
		const auto model = selection.lock();
		const auto text = owner ? unitText(*owner, id) : std::string("Battle context changed");
		if(name->getText() != text)
			name->setText(text);
		const auto info = owner ? unitDetails(*owner, id) : std::string();
		if(details->getText() != info)
			details->setText(info);
		const bool chosen = model && model->chosenRecipient() == id;
		const bool blocked = !owner || !model || (!chosen && !model->canChooseRecipient(id));
		if(shown == std::pair{chosen, blocked})
			return;
		shown = std::pair{chosen, blocked};
		name->setColor(chosen ? Colors::YELLOW : Colors::WHITE);
		chooseButton->setTextOverlay(chosen ? "Chosen" : "Choose", FONT_SMALL, chosen ? Colors::YELLOW : Colors::WHITE);
		chooseButton->setBorderColor(chosen ? std::optional<ColorRGBA>(Colors::YELLOW) : std::nullopt);
		chooseButton->block(blocked);
	}

	void show(Canvas & to) override { refresh(); CIntObject::show(to); }
	void showAll(Canvas & to) override { refresh(); CIntObject::showAll(to); }
};

/// Same eligibility gate as the Orders window: the local player must still be
/// commanding this battle. The authority validates the request again.
bool canIssue(BattleInterface & owner)
{
	return owner.curInt && owner.curInt->cb && owner.makingTurn() && !owner.curInt->isAutoFightOn
		&& !owner.isInTacticsMode() && owner.currentHero();
}
}

namespace consecratedArmsPrayerUI
{
void submitOrder(const std::shared_ptr<BattleInterface> & owner, const BattleAction & order)
{
	if(!owner || !owner->curInt || !owner->curInt->cb)
		return;
	const auto request = consecratedArms::choiceRequest(*owner->getBattle(), order);
	if(!request.prayers)
	{
		owner->curInt->cb->battleMakeSpellAction(owner->getBattleID(), order);
		return;
	}
	auto selection = std::make_shared<Selection>(request.prayers, request.recipients);
	ENGINE->windows().createAndPushWindow<ConsecratedArmsPrayerWindow>(owner, order, std::move(selection));
}

void submitHeroSpell(const std::shared_ptr<BattleInterface> & owner, const BattleAction & spell, HeroSpellSender send)
{
	const auto request = owner && owner->getBattle() ? consecratedArms::choiceRequest(*owner->getBattle(), spell)
		: consecratedArms::ChoiceRequest{};
	if(!request.prayers)
	{
		send(spell);
		return;
	}
	auto selection = std::make_shared<Selection>(request.prayers, request.recipients);
	ENGINE->windows().createAndPushWindow<ConsecratedArmsPrayerWindow>(owner, spell, std::move(selection), std::move(send));
}
}

ConsecratedArmsPrayerWindow::ConsecratedArmsPrayerWindow(const std::shared_ptr<BattleInterface> & owner,
	const BattleAction & order_, std::shared_ptr<Selection> selection_, consecratedArmsPrayerUI::HeroSpellSender sender_)
	: CWindowObject(BORDERED), battle(owner), order(order_), selection(std::move(selection_)), sender(std::move(sender_))
{
	pos.w = WINDOW_WIDTH;
	pos.h = WINDOW_HEIGHT;
	center();
	OBJECT_CONSTRUCTION;
	auto leather = ENGINE->renderHandler().createImage(Point(WINDOW_WIDTH, WINDOW_HEIGHT), CanvasScalingPolicy::AUTO);
	auto canvas = leather->getCanvas();
	canvas.fillTexture(ENGINE->renderHandler().loadImage(ImageLocator(ImagePath::builtin("DiBoxBck"), EImageBlitMode::OPAQUE)));
	background = std::make_shared<CPicture>(std::static_pointer_cast<IImage>(leather), Point(0, 0));

	const auto values = prayerValues(*owner, order.side);
	const bool two = selection->requiredPrayers() == 2;
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 19, FONT_MEDIUM, ETextAlignment::CENTER,
		Colors::YELLOW, order.actionType == EActionType::HERO_SPELL && order.spell.toSpell()
			? "Sermon: " + order.spell.toSpell()->getNameTranslated()
			: "Consecrated Arms: " + HeroCommandUI::name(order.command)));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 40, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE,
		two ? "Choose two different prayers, then the Order recipient that receives both."
			: "Choose a prayer, then the Order recipient that receives it."));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 56, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE,
		"Faith " + std::to_string(values.faith) + ". Potency Faith " + std::to_string(values.potency)
		+ ". New prayers last " + roundsText(values.rounds) + ". Right-click a prayer for its current effect."));
	createPrayerCards(*owner);
	createRecipientList();

	stateLabel = std::make_shared<CMultiLineLabel>(Rect(18, LIST_TOP + 10 + ROW_HEIGHT * static_cast<int>(VISIBLE_ROWS), WINDOW_WIDTH - 36, 30),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "");
	confirmButton = std::make_shared<CButton>(Point(436, WINDOW_HEIGHT - 44), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Issue Order", "Issue the Order with the chosen prayer and recipient. The battle rechecks both first."),
		[this] { confirm(); }, EShortcut::GLOBAL_ACCEPT);
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);
	cancelButton = std::make_shared<CButton>(Point(536, WINDOW_HEIGHT - 44), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Return to battle without issuing the Order or spending the Hero Action."),
		[this] { close(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);
	refresh();
}

void ConsecratedArmsPrayerWindow::createPrayerCards(const BattleInterface & owner)
{
	const auto values = prayerValues(owner, order.side);
	const std::weak_ptr<Selection> weak = selection;
	for(int index = 0; index < 6; ++index)
	{
		const auto prayer = static_cast<Prayer>(index);
		const Point position(14 + (index % 3) * (CARD_WIDTH + 8), 72 + (index / 3) * (CARD_HEIGHT + 8));
		prayerCards.push_back(std::make_shared<PrayerCard>(position, weak, prayer, prayerDetails(prayer, values),
			[this] { refresh(); }));
	}
}

void ConsecratedArmsPrayerWindow::createRecipientList()
{
	decoration.push_back(std::make_shared<CLabel>(18, LIST_TOP - 18, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW,
		"Recipient"));
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(13, LIST_TOP - 2, WINDOW_WIDTH - 26, ROW_HEIGHT * static_cast<int>(VISIBLE_ROWS) + 4),
		ColorRGBA(26, 23, 18, 155), ColorRGBA(119, 93, 56, 255)));
	const std::weak_ptr<Selection> weakSelection = selection;
	const auto weakBattle = battle;
	const auto count = selection->recipients().size();
	recipients = std::make_shared<CListBox>([weakSelection, weakBattle, this](size_t index) -> std::shared_ptr<CIntObject>
	{
		auto model = weakSelection.lock();
		if(!model || index >= model->recipients().size())
			return std::make_shared<CIntObject>();
		return std::make_shared<RecipientRow>(weakBattle, weakSelection, model->recipients()[index], [this] { refresh(); });
	}, Point(22, LIST_TOP), Point(0, ROW_HEIGHT), VISIBLE_ROWS, count, 0,
		count > VISIBLE_ROWS ? 1 : 0, Rect(584, 0, ROW_HEIGHT * static_cast<int>(VISIBLE_ROWS), ROW_HEIGHT * static_cast<int>(VISIBLE_ROWS)));
}

std::shared_ptr<BattleInterface> ConsecratedArmsPrayerWindow::currentBattle() const
{
	auto result = battle.lock();
	if(!result || CPlayerInterface::battleInt != result || !result->curInt)
		return {};
	return result;
}

void ConsecratedArmsPrayerWindow::refresh()
{
	for(const auto & card : prayerCards)
		if(auto prayerCard = std::dynamic_pointer_cast<PrayerCard>(card))
			prayerCard->refresh();
	if(recipients)
		for(const auto & item : recipients->getItems())
			if(auto row = std::dynamic_pointer_cast<RecipientRow>(item))
				row->refresh();
	const auto owner = currentBattle();
	const auto text = owner ? consecratedArmsPrayerChoice::promptText(*selection)
		: std::string("The battle changed. Cancel and reopen Orders.");
	if(stateLabel->getText() != text)
		stateLabel->setText(text);
	const bool blocked = !owner || submitted || !selection->complete();
	if(confirmButton->isBlocked() != blocked)
		confirmButton->block(blocked);
}

void ConsecratedArmsPrayerWindow::confirm()
{
	const auto owner = currentBattle();
	if(!owner || submitted || !selection->complete())
	{
		refresh();
		return;
	}
	auto action = order;
	if(!canIssue(*owner) || !selection->apply(action, consecratedArms::choiceRequest(*owner->getBattle(), order)))
	{
		stateLabel->setText("The battle changed and this choice is no longer valid. Cancel and reopen Orders.");
		confirmButton->block(true);
		return;
	}
	submitted = true; // Local duplicate-submit latch, not the authoritative action budget.
	const auto battleID = owner->getBattleID();
	auto playerCallback = owner->curInt->cb;
	const auto send = sender;
	close();
	if(send)
		send(action);
	else
		playerCallback->battleMakeSpellAction(battleID, action);
}

void ConsecratedArmsPrayerWindow::show(Canvas & to)
{
	refresh();
	CWindowObject::show(to);
}

void ConsecratedArmsPrayerWindow::showAll(Canvas & to)
{
	refresh();
	CWindowObject::showAll(to);
}
