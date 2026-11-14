/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "CWindowObject.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class CGHeroInstance;
class CHeroArea;
class CButton;
class CLabel;
class CAnimImage;
class CComponentBox;
class CSelectableComponent;
class CTextBox;

/// Raised up level window where you can select one out of two skills
class CLevelWindow : public CWindowObject
{
	// Native content640x568 plus CWindowObject's28x29 border fits800x600.
public:
	struct PrimaryGainSnapshot
	{
		ui32 level;
		std::array<int, GameConstants::PRIMARY_SKILLS> gains;
	};

private:
	std::optional<PrimaryGainSnapshot> primaryGains;
	std::vector<std::shared_ptr<CIntObject>> primaryGainWidgets;
	std::vector<std::shared_ptr<CLabel>> choiceHeaders;
	std::shared_ptr<CHeroArea> portrait;
	std::shared_ptr<CButton> ok;
	std::shared_ptr<CLabel> mainTitle;
	std::shared_ptr<CLabel> levelTitle;
	std::shared_ptr<CLabel> choiceInstruction;
	std::shared_ptr<CTextBox> perkDescription;
	std::shared_ptr<CAnimImage> skillIcon;
	std::shared_ptr<CLabel> skillValue;

	std::shared_ptr<CComponentBox> box; //skills to select
	std::shared_ptr<CComponentBox> skillChoiceBox;
	std::shared_ptr<CComponentBox> perkChoiceBox;
	std::function<void(ui32)> cb;
	bool perkChoiceActive = false;

	int skillViewOffset = 0;
	std::shared_ptr<CButton> buttonLeft;
	std::shared_ptr<CButton> buttonRight;

	std::vector<SecondarySkill> skills;
	std::vector<newHorizonsHeroes::PerkOfferCandidate> perks;
	std::vector<size_t> choiceOrder;
	/// The visible grid is arranged by category (skill/perk columns), so its
	/// order is intentionally different from the paging order above.
	std::vector<size_t> displayedChoiceOrder;
	const CGHeroInstance * hero;

	void initLevelUpData(const CGHeroInstance * heroInstance, const std::vector<SecondarySkill> & availableSkills,
		const std::vector<newHorizonsHeroes::PerkOfferCandidate> & availablePerks,
		const std::function<void(ui32)> & callback, const std::optional<PrimaryGainSnapshot> & gains);
	void createLevelUpControls(PrimarySkill pskill);
	void createSkillBox();
	std::shared_ptr<CSelectableComponent> skillComponent(size_t index) const;
	std::shared_ptr<CSelectableComponent> perkComponent(size_t index) const;
	std::string perkHeaderText() const;
	void refreshPerkDescription(size_t index);
	void submitSelection();

public:
	CLevelWindow(const CGHeroInstance *hero, PrimarySkill pskill, const std::vector<SecondarySkill> &skills,
		const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, std::function<void(ui32)> callback,
		const std::optional<PrimaryGainSnapshot> & gains = std::nullopt);
	void updateLevelUpData(const CGHeroInstance * heroInstance, PrimarySkill pskill,
		const std::vector<SecondarySkill> & availableSkills,
		const std::vector<newHorizonsHeroes::PerkOfferCandidate> & availablePerks,
		const std::function<void(ui32)> & callback, const std::optional<PrimaryGainSnapshot> & gains = std::nullopt);
	void setCloseOnSelection(bool value);
	void configureReroll(QueryID queryID, bool available, std::function<int(size_t)> callback);
	bool matchesQuery(QueryID queryID) const;
	bool matchesHero(const CGHeroInstance * heroInstance) const;
	void rerollRequestApplied(uint32_t requestID, bool success);

	void close() override;

private:
	bool closeOnSelection = true;
	bool selectionSubmitted = false;
	QueryID levelQuery = QueryID::NONE;
	std::vector<std::pair<size_t, std::shared_ptr<CButton>>> rerollButtons;
	std::function<int(size_t)> rerollCallback;
	std::optional<uint32_t> rerollRequest;
	bool rerollAvailable = false;
	bool rerollPending = false;
	void refreshRerollControls();
	void requestReroll(size_t skillIndex);
	void createRerollControls();
	std::optional<size_t> selectedOfferIndex() const;
	void restoreOfferSelection(std::optional<size_t> selection);
};
