/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "CCastleInterface.h"
#include "InfoWindows.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesArtifactQuote.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
std::string hoarderWeekStatus(const CGHeroInstance * hero)
{
	if(!newHorizonsEstates::hasRevisedPerk(hero, "new-horizons:estates.hoarder"))
		return {};
	const auto calendar = hero->cb->getCalendar();
	const auto week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	return hero->getNewHorizonsHoarderLastWeek() == week
		? "Hoarder: this week's discount has been used."
		: "Hoarder: first purchase this week costs 50% less Gold.";
}
}

CBlacksmithDialog::CBlacksmithDialog(bool possible, ArtifactID aid, ArtifactID existingArtifact, ObjectInstanceID hid):
	CWindowObject(PLAYER_COLORED, ImagePath::builtin("TPSMITH"))
{
	OBJECT_CONSTRUCTION;

	Rect barRect(8, pos.h - 26, pos.w - 16, 19);

	auto statusbarBackground = std::make_shared<CPicture>(background->getSurface(), barRect, 8, pos.h - 26);
	statusbar = CGStatusBar::create(statusbarBackground);

	animBG = std::make_shared<CPicture>(ImagePath::builtin("TPSMITBK"), 64, 50);
	animBG->needRefresh = true;

	const CCreature * creature = aid.toArtifact()->getWarMachine().toCreature();
	anim = std::make_shared<CCreatureAnim>(64, 50, creature->animDefName, CShowableAnim::CREATURE_PREVIEW_MODE);
	anim->clipRect(113,125,200,150);

	MetaString titleString;
	titleString.appendTextID("core.genrltxt.274");
	titleString.replaceTextID(creature->getNameSingularTextID());

	MetaString buyText;
	buyText.appendTextID("core.genrltxt.595");
	buyText.replaceTextID(creature->getNameSingularTextID());

	MetaString cancelText;
	cancelText.appendTextID("core.genrltxt.596");
	cancelText.replaceTextID(creature->getNameSingularTextID());

	const auto * hero = GAME->interface()->cb->getHero(hid);
	ResourceSet baseCost;
	baseCost[EGameResID::GOLD] = aid.toEntity(LIBRARY)->getPrice();
	const auto quote = newHorizonsEstates::artifactPurchaseQuote(baseCost, hero);
	std::string costString = std::to_string(quote.cost[EGameResID::GOLD]);

	title = std::make_shared<CLabel>(165, 28, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, titleString.toString(&GAME->translator()));
	costText = std::make_shared<CLabel>(165, 218, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->translate("core.jktext.43"));
	costValue = std::make_shared<CLabel>(165, 292, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, costString);
	buy = std::make_shared<CButton>(Point(42, 312), AnimationPath::builtin("IBUY30.DEF"), CButton::tooltip(buyText.toString(&GAME->translator())), [&](){ close(); }, EShortcut::GLOBAL_ACCEPT);
	cancel = std::make_shared<CButton>(Point(224, 312), AnimationPath::builtin("ICANCEL.DEF"), CButton::tooltip(cancelText.toString(&GAME->translator())), [&](){ close(); }, EShortcut::GLOBAL_CANCEL);

	if(possible)
	{
		if (existingArtifact.hasValue())
		{
			MetaString message;
			message.appendTextID("vcmi.townWindow.blacksmith.replaceWarMachine");
			message.replaceName(existingArtifact);
			message.replaceName(aid);

			buy->addCallback([=](){
				GAME->interface()->showYesNoDialog(
					message.toString(&GAME->translator()),
					[hid, aid](){ GAME->interface()->cb->buyArtifact(GAME->interface()->cb->getHero(hid),aid); },
					nullptr);
			});

		}
		else
			buy->addCallback([hid, aid](){ GAME->interface()->cb->buyArtifact(GAME->interface()->cb->getHero(hid),aid); });
	}
	else
		buy->block(true);

	costIcon = std::make_shared<CAnimImage>(AnimationPath::builtin("RESOURCE"), GameResID(EGameResID::GOLD).getNum(), 0, 148, 244);
	const auto weeklyStatus = hoarderWeekStatus(hero);
	if(!weeklyStatus.empty())
		elements.push_back(std::make_shared<CLabel>(165, 235, FONT_TINY, ETextAlignment::CENTER,
			Colors::YELLOW, weeklyStatus, 300));
}

CBlacksmithDialog::CBlacksmithDialog(ObjectInstanceID townId, ObjectInstanceID hid, ArtifactID preferredArtifact):
	CWindowObject(BORDERED, ImagePath::builtin("newHorizonsOrdersBackground.png"))
{
	OBJECT_CONSTRUCTION;

	const CGTownInstance * town = GAME->interface()->cb->getTown(townId);
	const CGHeroInstance * hero = GAME->interface()->cb->getHero(hid);
	const auto offers = town ? town->getWarMachineShopOffers() : std::vector<CGTownInstance::WarMachineShopOffer>{};
	const int currentGold = GAME->interface()->cb->getResourceAmount(EGameResID::GOLD);

	const ColorRGBA shopRecess(35, 21, 13, 112);
	const ColorRGBA shopFrame(154, 119, 66, 255);
	const ColorRGBA rowDivider(110, 78, 48, 210);
	elements.push_back(std::make_shared<CLabel>(320, 21, FONT_BIG, ETextAlignment::CENTER,
		Colors::YELLOW, "War Machine Shop", 580));
	elements.push_back(std::make_shared<CLabel>(320, 49, FONT_SMALL, ETextAlignment::CENTER,
		Colors::WHITE, "Choose an ordinary war machine. Shared shop offers appear once at their lowest town price.", 596));

	constexpr int shopLeft = 18;
	constexpr int shopTop = 78;
	constexpr int shopWidth = 604;
	constexpr int shopHeight = 352;
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(shopLeft, shopTop, shopWidth, shopHeight), shopRecess, shopFrame));

	if(offers.empty())
	{
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(38, 215, 564, 46), FONT_MEDIUM,
			ETextAlignment::CENTER, Colors::WHITE,
			"No ordinary war machines are available from this town's completed shops."));
	}
	else
	{
		constexpr int firstRowTop = 86;
		constexpr int iconLeft = 34;
		constexpr int labelLeft = 102;
		constexpr int buyLeft = 535;
		const int visibleOfferCount = static_cast<int>(offers.size());
		const int rowHeight = (shopHeight - 16) / visibleOfferCount;

		for(int index = 0; index < visibleOfferCount; ++index)
		{
			const auto & offer = offers[static_cast<size_t>(index)];
			ResourceSet baseCost;
			baseCost[EGameResID::GOLD] = offer.price;
			const auto quote = newHorizonsEstates::artifactPurchaseQuote(baseCost, hero);
			const auto price = quote.cost[EGameResID::GOLD];
			const int rowTop = firstRowTop + index * rowHeight;
			const int contentTop = rowTop + std::max(0, (rowHeight - 92) / 2);
			const auto * artifact = offer.artifact.toArtifact();
			if(!artifact)
				continue;

			auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin("artifact"), offer.artifact.getNum(),
				Rect(iconLeft, contentTop + 20, 52, 52));
			elements.push_back(icon);

			const std::string machineName = artifact->getNameTranslated();
			elements.push_back(std::make_shared<CLabel>(labelLeft, contentTop + 12, FONT_MEDIUM,
				ETextAlignment::TOPLEFT, Colors::YELLOW, machineName, 392));
			elements.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("RESOURCE"),
				GameResID(EGameResID::GOLD).getNum(), 0, labelLeft, contentTop + 42));
			elements.push_back(std::make_shared<CLabel>(labelLeft + 28, contentTop + 44, FONT_SMALL,
				ETextAlignment::TOPLEFT, Colors::WHITE, std::to_string(price), 120));

			const ArtifactID existingArtifact = hero ? hero->getReplacedWarMachine(offer.artifact) : ArtifactID::NONE;
			const bool alreadyEquipped = existingArtifact.hasValue() && existingArtifact == offer.artifact;
			const bool canAfford = currentGold >= price;
			const bool canBuy = hero && !alreadyEquipped && canAfford;
			const std::string availability = alreadyEquipped ? "Already equipped"
				: (!hero ? "No visiting hero" : (canAfford ? "Ready to buy" : "Not enough Gold"));
			const ColorRGBA availabilityColor = alreadyEquipped ? Colors::YELLOW
				: (hero && canAfford ? Colors::GREEN : Colors::WHITE);
			elements.push_back(std::make_shared<CLabel>(labelLeft, contentTop + 68, FONT_TINY,
				ETextAlignment::TOPLEFT, availabilityColor,
				availability + (quote.usesHoarder ? " — Hoarder price" : ""), 300));

			MetaString buyText;
			buyText.appendTextID("core.genrltxt.595");
			buyText.replaceTextID(artifact->getNameTextID());
			auto buyButton = std::make_shared<CButton>(Point(buyLeft, contentTop + 24),
				AnimationPath::builtin("IBUY30.DEF"), CButton::tooltip(buyText.toString(&GAME->translator()),
					"Purchase for " + std::to_string(price) + " Gold."), [this]() { close(); },
			offer.artifact == preferredArtifact ? EShortcut::GLOBAL_ACCEPT
				: vstd::next(EShortcut::SELECT_INDEX_1, index));
			if(canBuy)
			{
				if(existingArtifact.hasValue())
				{
					MetaString message;
					message.appendTextID("vcmi.townWindow.blacksmith.replaceWarMachine");
					message.replaceName(existingArtifact);
					message.replaceName(offer.artifact);
					const std::string replacementWarning = message.toString(&GAME->translator());
					buyButton->addCallback([hid, artifactId = offer.artifact, replacementWarning]()
					{
						GAME->interface()->showYesNoDialog(replacementWarning,
							[hid, artifactId]()
							{
								GAME->interface()->cb->buyArtifact(GAME->interface()->cb->getHero(hid), artifactId);
							}, nullptr);
					});
				}
				else
					buyButton->addCallback([hid, artifactId = offer.artifact]()
					{
						GAME->interface()->cb->buyArtifact(GAME->interface()->cb->getHero(hid), artifactId);
					});
			}
			else
				buyButton->block(true);
			elements.push_back(buyButton);

			const std::string help = artifact->getDescriptionTranslated() + "\n\nTown price: "
				+ std::to_string(price) + " Gold. " + availability + "."
				+ (quote.usesHoarder ? " Hoarder applies 50% less Gold to this first purchase of the week." : "");
			elements.push_back(std::make_shared<LRClickableAreaWText>(Rect(iconLeft, rowTop + 4, 472, rowHeight - 8),
				"Right-click or click for " + machineName + " help.", help));

			if(index + 1 < visibleOfferCount)
				elements.push_back(std::make_shared<TransparentFilledRectangle>(
					Rect(shopLeft + 12, rowTop + rowHeight - 1, shopWidth - 42, 1), rowDivider));
		}
	}

	const auto weeklyStatus = hoarderWeekStatus(hero);
	elements.push_back(std::make_shared<CLabel>(20, 443, FONT_TINY, ETextAlignment::TOPLEFT,
		Colors::WHITE, weeklyStatus.empty()
			? "Prices use the lowest configured town offer; duplicate machines appear once." : weeklyStatus, 510));
	cancel = std::make_shared<CButton>(Point(555, 433), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Close", "Return to the town."), [this]() { close(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	elements.push_back(cancel);

	Rect barRect(8, pos.h - 21, pos.w - 120, 18);
	auto statusbarBackground = std::make_shared<CPicture>(background->getSurface(), barRect, 8, pos.h - 21);
	statusbar = CGStatusBar::create(statusbarBackground);
	updateShadow();
}
