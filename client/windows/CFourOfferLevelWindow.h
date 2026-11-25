/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CWindowObject.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class CGHeroInstance;
class CButton;
class CComponentBox;
class CLabel;
class CSelectableComponent;

/// Compact four-offer layout (window coordinates). The native header from
/// newHorizonsLevelUpBackground.png keeps its rows; everything below it is
/// tight, and each option's full description is its right-click help.
namespace fourOfferLayout
{
constexpr int WIDTH = 640;
constexpr int HEIGHT = 474;
constexpr int STAT_ROW = 186;
constexpr int INSTRUCTION = 248;
constexpr int OFFER_ROW_TOP[2] = {278, 358};
constexpr int OFFER_HEIGHT = 64;
constexpr int PERK_TITLE = 268;
constexpr int PERK_GRID_TOP = 284;
constexpr int CONTROLS = 432;
}

/// A single saved advancement. Opening a perk group is local navigation;
/// only a final Skill or perk choice replies to the authoritative query.
class CFourOfferLevelWindow : public CWindowObject
{
public:
	struct PrimaryGainSnapshot
	{
		ui32 level;
		std::array<int, GameConstants::PRIMARY_SKILLS> gains;
	};

private:
	const CGHeroInstance * hero;
	newHorizonsHeroes::FourOfferProgressionOffer offers;
	newHorizonsHeroes::FourOfferPerkGroups perkGroups;
	std::vector<int32_t> legalReplies;
	QueryID query;
	std::function<int(int)> submit;
	std::function<int(size_t)> reroll;
	std::optional<PrimaryGainSnapshot> primaryGains;
	std::array<bool, 2> rerollable{};
	std::array<int, 4> currentRanks{};
	std::vector<std::shared_ptr<CIntObject>> header;
	std::vector<std::shared_ptr<CIntObject>> choiceLabels;
	std::array<std::shared_ptr<CComponentBox>, 4> slotBoxes;
	std::array<std::shared_ptr<CButton>, 2> rerollButtons;
	std::shared_ptr<CComponentBox> perkBox;
	std::shared_ptr<CLabel> instruction;
	std::shared_ptr<CButton> confirm;
	std::shared_ptr<CButton> back;
	std::shared_ptr<CButton> previousPage;
	std::shared_ptr<CButton> nextPage;
	std::optional<uint32_t> request;
	std::optional<size_t> selectedSlot;
	std::optional<size_t> selectedPerk;
	std::optional<size_t> openGroup;
	size_t page = 0;
	bool rerollAvailable = false;
	bool waiting = false;
	bool resolved = false;

	void createHeader(PrimarySkill primary);
	void createControls();
	void createChoices();
	void createOfferSlots();
	void createPerkChoices();
	void clearChoices();
	void selectSlot(size_t slot);
	void selectPerk(size_t index);
	void showOffers();
	void changePage(int direction);
	void confirmSelection();
	void submitReply(int reply);
	void requestReroll(size_t slot);
	void refreshControls();
	void calculateEligibility(bool available);
	bool slotAvailable(size_t slot) const;
	int32_t perkReply(size_t group, size_t index) const;
	std::shared_ptr<CSelectableComponent> offerComponent(size_t slot) const;
	std::shared_ptr<CSelectableComponent> perkComponent(size_t index) const;

public:
	CFourOfferLevelWindow(const CGHeroInstance * hero, PrimarySkill primary,
		const newHorizonsHeroes::FourOfferProgressionOffer & offers, QueryID query,
		std::function<int(int)> submit, bool rerollAvailable, std::function<int(size_t)> reroll,
		const std::optional<PrimaryGainSnapshot> & gains = std::nullopt);
	void updateOffer(const newHorizonsHeroes::FourOfferProgressionOffer & offers,
		QueryID query, std::function<int(int)> submit, bool rerollAvailable,
		std::function<int(size_t)> reroll);
	bool matchesQuery(QueryID queryID) const;
	bool matchesHero(const CGHeroInstance * heroInstance) const;
	bool isWaiting() const { return waiting; }
	bool refreshEligibility();
	void requestApplied(uint32_t requestID, bool success);
	void queryResolved(QueryID queryID);
	void close() override;
};
