/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SpellbookPrayers.h"
#include "InfoWindows.h"
#include "../battle/CurrentFactionSkillPresentation.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/TextOperations.h"
#include <array>

namespace
{
using consecratedArms::Prayer;
using passiveFactionSkillPresentation::prayerImage;
constexpr size_t PRAYER_COUNT = 6;
using passiveFactionSkillPresentation::prayerSummary;

std::string grantHint(const consecratedArms::Policy & policy)
{
	return policy.chosen ? "Chosen when this hero issues an Order" : "Granted automatically by Orders";
}

class PrayerDetails final : public CInfoWindow
{
public:
	PrayerDetails(const std::string & text, PlayerColor player)
		: CInfoWindow(text, player, TCompsInfo{},
			TButtonsInfo{{AnimationPath::builtin("IOKAY.DEF"), nullptr}})
	{
	}
	void close() override
	{
		// This nested explanation does not resolve its spellbook's query.
		WindowBase::close();
	}
};

class PrayerEntry final : public CIntObject
{
	std::string title;
	std::string details;
	std::string hoverHint;
	std::shared_ptr<CGStatusBar> status;
	PlayerColor player;
	std::vector<std::shared_ptr<CIntObject>> elements;
public:
	PrayerEntry(Rect bounds, Prayer prayer, std::string explanation, std::string hint,
		std::shared_ptr<CGStatusBar> status, PlayerColor player)
		: CIntObject(LCLICK | SHOW_POPUP | HOVER, bounds.topLeft()), title(consecratedArms::name(prayer)),
		  details(std::move(explanation)), hoverHint(std::move(hint)), status(std::move(status)), player(player)
	{
		pos.w = bounds.w;
		pos.h = bounds.h;
		OBJECT_CONSTRUCTION;
		auto image = std::make_shared<CPicture>(ImagePath::builtin(prayerImage(prayer)), 0, 4);
		image->removeUsedEvents(LCLICK | SHOW_POPUP);
		elements.push_back(std::move(image));
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(48, 0, bounds.w - 48, 32),
			FONT_TINY, ETextAlignment::TOPLEFT, Colors::BLACK, title));
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(48, 34, bounds.w - 48, bounds.h - 34),
			FONT_TINY, ETextAlignment::TOPLEFT, Colors::BLACK, prayerSummary(prayer)));
	}
	void clickPressed(const Point &) override
	{
		ENGINE->windows().pushWindow(std::make_shared<PrayerDetails>("{" + title + "}\n\n" + details, player));
	}
	void showPopupWindow(const Point &) override
	{
		CRClickPopup::createAndPush("{" + title + "}\n\n" + details);
	}
	void hover(bool on) override
	{
		if(on) status->write(title + " — " + hoverHint);
		else status->clear();
	}
};

std::string prayerDetails(Prayer prayer, const consecratedArms::Policy & policy, int faith)
{
	const std::string automatic = std::string(policy.chosen
		? "When this hero issues an Order, choose which prayer it grants and which of its recipients receives it. "
		: "Orders grant prayers automatically to their recipients. ")
		+ "These entries provide information and cannot be cast from the spellbook.";
	if(!policy.rank)
		return std::string(prayerSummary(prayer))
			+ "\n\nLearn Consecrated Arms to grant these effects through Orders. "
			"Strength depends on Faith and the acquired rank and perks.\n\n" + automatic;
	const auto potency = consecratedArms::potencyFaith(faith, policy);
	const auto rounds = consecratedArms::duration(faith, policy);
	const consecratedArms::ActivePrayer snapshot{prayer, potency, rounds, policy};
	std::string result = passiveFactionSkillPresentation::prayerEffect(snapshot, 1)
		+ "\n\nDuration: " + std::to_string(rounds) + " rounds.";
	if(const int strengthBonus = potency - std::max(0, faith); strengthBonus > 0)
		result += "\nRank and perks add " + std::to_string(strengthBonus) + " to prayer strength.";
	result += "\n\n" + automatic;
	if(policy.has(consecratedArms::Perk::SACRED_ACCORD))
		result += "\n\nSacred Accord strengthens prayers when the recipient has several distinct prayers. "
			"The values above show one active prayer.";
	if(policy.has(consecratedArms::Perk::ANTIPHONAL_PRAYER))
		result += policy.chosen ? "\nAntiphonal Prayer: choose two different prayers with each Order."
			: "\nAntiphonal Prayer grants two different prayers with each Order.";
	if(policy.has(consecratedArms::Perk::LIVING_LITURGY))
		result += "\nLiving Liturgy refreshes existing prayers when an Order grants new prayers.";
	return result;
}
}

bool SpellbookPrayers::availableFor(const CGHeroInstance & hero)
{
	return hero.getFactionID() == FactionID::CASTLE
		&& consecratedArms::enabled(hero.getPerkState().getRules());
}

SpellbookPrayers::SpellbookPrayers(Rect bounds, int rightColumn, bool large,
	const CGHeroInstance & hero, std::shared_ptr<CGStatusBar> status)
	: CIntObject(0, bounds.topLeft())
{
	pos.w = bounds.w;
	pos.h = bounds.h;
	OBJECT_CONSTRUCTION;
	const auto policy = consecratedArms::capturePolicy(&hero);
	const auto faith = consecratedArms::actualFaith(hero);
	const int entryWidth = large ? 270 : 184;
	// Each heading stays on its own parchment page, clear of the book's gutter.
	elements.push_back(std::make_shared<CLabel>(8 + entryWidth / 2, 22, FONT_TINY,
		ETextAlignment::CENTER, Colors::BLACK, "Prayers"));
	elements.push_back(std::make_shared<CLabel>(8 + entryWidth / 2, 42, FONT_TINY,
		ETextAlignment::CENTER, Colors::BLACK, "Faith " + std::to_string(faith)));
	const std::string hint = policy.rank ? grantHint(policy)
		: "Learn Consecrated Arms to grant prayers through Orders";
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(rightColumn, 16, entryWidth, 46),
		FONT_TINY, ETextAlignment::CENTER, Colors::BLACK, hint));
	const int rowHeight = large ? 115 : 86;
	for(size_t index = 0; index < PRAYER_COUNT; ++index)
	{
		const auto prayer = static_cast<Prayer>(index);
		const int column = static_cast<int>(index) / 3;
		const int x = column ? rightColumn : 8;
		const int width = entryWidth;
		auto entry = std::make_shared<PrayerEntry>(
			Rect(x, 72 + (index % 3) * rowHeight, width, rowHeight - 6),
			prayer, prayerDetails(prayer, policy, faith), grantHint(policy), status, hero.getOwner());
		entries.push_back(std::move(entry));
		entryNames.emplace_back(consecratedArms::name(prayer));
	}
}

void SpellbookPrayers::filter(const std::string & search)
{
	for(size_t index = 0; index < entries.size(); ++index)
		entries[index]->setEnabled(search.empty() || TextOperations::isFuzzyMatch(search, entryNames[index]));
}
