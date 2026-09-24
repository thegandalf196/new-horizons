/*
 * HeroGrowthWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SpellPointPresentation.h"
#include "HeroGrowthWindow.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsPerkIcons.h"

namespace
{
constexpr size_t PERK_SKILLS_PER_PAGE = 6;

std::string perkRankName(int rank)
{
	switch(rank)
	{
	case 1:
		return "Basic";
	case 2:
		return "Advanced";
	case 3:
		return "Expert";
	default:
		return "Not learned";
	}
}

std::string joinPerkNames(const std::vector<std::string> & names)
{
	std::string result;
	for(size_t i = 0; i < names.size(); ++i)
	{
		if(i > 0)
			result += ", ";
		result += names[i];
	}
	return result;
}

size_t selectedPerksForSkill(const newHorizonsHeroes::PerkState & state, const std::string & skillId)
{
	return std::count_if(state.selected.begin(), state.selected.end(), [&skillId](const auto & selection)
	{
		return selection.skillId == skillId;
	});
}
}

void HeroGrowthWindow::selectPerkSkill(const std::string & skillId)
{
	const auto selected = std::find(perkSkillIds.begin(), perkSkillIds.end(), skillId);
	if(selected == perkSkillIds.end())
		return;

	selectedPerkSkill = skillId;
	const auto index = static_cast<size_t>(std::distance(perkSkillIds.begin(), selected));
	if(perkSkillHeader && index < perkSkillNames.size() && index < perkSkillRanks.size())
	{
		const size_t page = index / PERK_SKILLS_PER_PAGE + 1;
		const size_t pageCount = (perkSkillIds.size() + PERK_SKILLS_PER_PAGE - 1) / PERK_SKILLS_PER_PAGE;
		perkSkillHeader->setText("Skill: " + perkSkillNames[index] + " (" + perkRankName(perkSkillRanks[index])
			+ ") | Ten-perk pool | Page " + std::to_string(page) + "/" + std::to_string(pageCount) + " (select below)");
	}

	for(size_t i = 0; i < perkSkillPanels.size(); ++i)
		for(const auto & element : perkSkillPanels[i])
			element->setEnabled(i == index);
	for(size_t i = 0; i < perkSkillButtons.size(); ++i)
		if(perkSkillButtons[i])
			perkSkillButtons[i]->setBorderColor(i == index ? std::make_optional(Colors::YELLOW) : std::nullopt);
	redraw();
}

void HeroGrowthWindow::selectSection(HeroDevelopmentSection section)
{
	const bool wasPerkPool = navigation.selected() == HeroDevelopmentSection::PERKS;
	if(!navigation.select(section))
		return;
	if(wasPerkPool || section == HeroDevelopmentSection::PERKS)
	{
		if(const auto * currentHero = GAME->interface()->cb->getHero(heroID))
			refresh(*currentHero);
		return;
	}
	if(!sectionText)
		return;

	sectionText->setText(sectionTexts[static_cast<size_t>(section)]);
	if(sectionText->slider)
		sectionText->slider->scrollTo(0);
	sectionText->sliderMoved(0);
	updateSectionButtons();
	redraw();
}

void HeroGrowthWindow::updateSectionButtons()
{
	for(size_t i = 0; i < sectionButtons.size(); ++i)
		if(sectionButtons[i])
			sectionButtons[i]->setBorderColor(navigation.selected() == static_cast<HeroDevelopmentSection>(i)
				? std::make_optional(Colors::YELLOW) : std::nullopt);
}

HeroGrowthWindow::HeroGrowthWindow(const CGHeroInstance & hero)
	: CWindowObject(BORDERED), heroID(hero.id)
{
	refresh(hero);
}

void HeroGrowthWindow::refresh(const CGHeroInstance & hero)
{
	if(hero.id != heroID)
		return;
	OBJECT_CONSTRUCTION;
	sectionText.reset();
	sectionButtons.fill(nullptr);
	perkSkillHeader.reset();
	perkSkillPrevious.reset();
	perkSkillNext.reset();
	perkSkillIds.clear();
	perkSkillNames.clear();
	perkSkillRanks.clear();
	perkSkillButtons.clear();
	perkSkillPanels.clear();
	elements.clear();
	closeButton.reset();
	// These saved families are independent: primary growth does not opt an old
	// hero into capacity rules, and capacity does not supply a growth profile.
	const auto growth = hero.getPrimaryGrowthView();
	const auto leadership = hero.getLeadershipCapacity();
	const auto siege = hero.getSiegeCapabilities();
	const auto masteries = hero.getMasteryView();
	const auto & perkState = hero.getPerkState();
	const bool perks = newHorizonsHeroes::usesPerkRules(perkState.rules);
	if(perks)
	{
		for(const auto & entry : perkState.rules["skills"].Struct())
		{
			const auto & skillId = entry.first;
			if(hero.getPerkSkillRank(skillId) > 0 || selectedPerksForSkill(perkState, skillId) > 0)
				perkSkillIds.push_back(skillId);
		}
		std::stable_sort(perkSkillIds.begin(), perkSkillIds.end(), [&hero](const std::string & left, const std::string & right)
		{
			return hero.getPerkSkillRank(left) > hero.getPerkSkillRank(right);
		});
	}
	navigation.refresh({growth.has_value(), leadership.has_value(), siege.has_value(), masteries.has_value(), perks});
	const bool showPerkPool = navigation.selected() == HeroDevelopmentSection::PERKS && !perkSkillIds.empty();
	// The pool is deliberately compact: all ten entries remain visible in a
	// two-column grid, while the learned-Skill selector pages independently.
	// Keep the complete window inside the 800x600 logical UI baseline.
	constexpr int perkCardTop = 421;
	constexpr int perkCardHeight = 26;
	constexpr int perkCardGap = 1;
	constexpr int perkCardRows = 5;
	constexpr int perkCloseTop = perkCardTop + perkCardRows * (perkCardHeight + perkCardGap) + 10;
	pos = Rect(0, 0, 700, showPerkPool ? 600 : 560);
	const ColorRGBA panelColor(52, 46, 43);
	const ColorRGBA rimColor(180, 154, 98);
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w, pos.h), ColorRGBA(39, 35, 41), rimColor, 2));
	elements.push_back(std::make_shared<CLabel>(350, 24, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Hero development"));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(22, 44, 656, 38), FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE,
		GAME->translator().translate(hero.getNameTextID()) + " - " + GAME->translator().translate(hero.getClassNameTextID()) + " - Level " + std::to_string(hero.level)));
	elements.push_back(std::make_shared<CLabel>(350, 91, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, growth ? "Saved hero profile; displayed snapshot" : "Saved development rules; displayed snapshot"));

	const std::array<const char *, GameConstants::PRIMARY_SKILLS> primaryImages = {
		"NH_hero_attack_32", "NH_hero_defense_32", "NH_hero_power_32", "NH_hero_knowledge_32"
	};
	const auto addSmallLabel = [this](int x, int y, int width, const std::string & text)
	{
		elements.push_back(std::make_shared<CLabel>(x, y, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, text, width));
	};
	for(size_t i = 0; i < primaryImages.size(); ++i)
	{
		const int x = 22 + static_cast<int>(i) * 167;
		elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x, 110, 155, 158), panelColor, rimColor));
		elements.push_back(std::make_shared<CPicture>(ImagePath::builtin(primaryImages[i]), x + 8, 117));
		addSmallLabel(x + 47, 125, 100, GAME->translator().translate("core.priskill", i));
		const int total = growth ? growth->modified[i] : hero.getPrimSkillLevel(static_cast<PrimarySkill>(i));
		elements.push_back(std::make_shared<CLabel>(x + 77, 164, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Total: " + std::to_string(total), 135));
		if(growth)
		{
			addSmallLabel(x + 10, 182, 135, "Base: " + std::to_string(growth->base[i]));
			addSmallLabel(x + 10, 200, 135, "Class start: " + std::to_string(growth->profile.starting[i]));
			addSmallLabel(x + 10, 218, 135, "Class / level: +" + std::to_string(growth->profile.growth[i]));
			addSmallLabel(x + 10, 236, 135, "Last level: " + std::string(growth->lastGains[i] > 0 ? "+" : "") + std::to_string(growth->lastGains[i]));
		}
		else
			addSmallLabel(x + 10, 182, 135, "No growth snapshot");
	}

	// Retain the existing live-instance queries. Do not derive mana or movement
	// from displayed primary ratings, or invent leadership/siege capabilities.
	const std::array<std::string, 4> values = {
		spellPointPresentation::readout(hero.getManaAvailable(), hero.manaLimit(), hero.getBufferSpellPoints()),
		std::to_string(hero.movementPointsRemaining()) + "/" + std::to_string(hero.movementPointsLimit()),
		std::to_string(hero.moraleVal()), std::to_string(hero.luckVal())
	};
	const std::array<const char *, 4> names = {"Mana", "Movement", "Morale", "Luck"};
	const std::array<const char *, 4> images = {
		"NH_hero_mana_32", "NH_hero_movement_painted_32", "NH_hero_morale_32", "NH_hero_luck_32"
	};
	for(size_t i = 0; i < values.size(); ++i)
	{
		const int x = 22 + static_cast<int>(i) * 167;
		elements.push_back(std::make_shared<CPicture>(ImagePath::builtin(images[i]), x + 8, 291));
		addSmallLabel(x + 47, 285, 108, names[i]);
		addSmallLabel(x + 47, 307, 108, values[i]);
	}

	// Separate independently saved families instead of burying mastery readback
	// below leadership/siege paragraphs in one long scroll region.
	sectionTexts = {
		"No saved primary growth profile for this hero.",
		"No saved leadership capacity view for this hero.",
		"No saved siege capability view for this hero.",
		"No saved mastery rules for this hero.",
		"This hero has no perks available."
	};
	auto & growthText = sectionTexts[static_cast<size_t>(HeroDevelopmentSection::GROWTH)];
	auto & leadershipText = sectionTexts[static_cast<size_t>(HeroDevelopmentSection::LEADERSHIP)];
	auto & siegeText = sectionTexts[static_cast<size_t>(HeroDevelopmentSection::SIEGE)];
	auto & masteryText = sectionTexts[static_cast<size_t>(HeroDevelopmentSection::MASTERIES)];
	auto & perkText = sectionTexts[static_cast<size_t>(HeroDevelopmentSection::PERKS)];
	if(growth)
	{
		growthText = "Primary growth is deterministic.\n"
			"Every level grants the class vector shown above: no skill-based extra points "
			"and no level-based probability transition.";
	}
	if(leadership)
	{
		if(hero.getCapabilityRules()["rulesetVersion"].Integer() >= 2)
		{
			leadershipText = "Leadership: " + std::to_string(leadership->capacity) + ".\n";
			leadershipText += "Every army slot is checked independently. Maximum creatures in a stack = floor(Hero Leadership / Creature Leadership Requirement).\n";
			leadershipText += "Command and Recruitment do not increase this capacity or discount requirements.";
		}
		else
		{
			leadershipText = "Legacy aggregate Leadership preview: " + std::to_string(leadership->used) + " / " + std::to_string(leadership->capacity) + ".";
		}
	}
	if(siege)
	{
		const auto rankName = [](int rank) -> std::string
		{
			if(rank == 0)
				return "Untrained";
			if(rank >= 1 && rank <= 3)
				return GAME->translator().translate("core.skilllev", rank - 1);
			return "Rank " + std::to_string(rank);
		};
		if(hero.getCapabilityRules()["rulesetVersion"].Integer() >= 3)
		{
			siegeText = "War Machines: " + rankName(siege->warMachinesRank)
				+ "; Siege rating: " + std::to_string(siege->siegeRating) + ".\n";
			siegeText += "Ballista base damage: " + std::to_string(siege->ballistaDamage)
				+ "; Catapult structural damage: " + std::to_string(siege->catapultStructuralDamage) + ".\n";
			siegeText += "First Aid healing: " + std::to_string(siege->firstAidHealing)
				+ "; Defensive tower base damage: " + std::to_string(siege->defensiveTowerDamage) + ".\n";
			siegeText += "Direct control: Ballista " + std::to_string(siege->ballistaControlChance)
				+ "%, Catapult " + std::to_string(siege->catapultControlChance)
				+ "%, First Aid Tent " + std::to_string(siege->firstAidControlChance) + "%.\n";
			siegeText += "Siege is a rating, not a spendable resource. Control still requires an eligible machine action.";
		}
		else
		{
			siegeText = "Legacy siege capabilities - no automatic hero-level growth.\n";
			siegeText += "Artillery: " + rankName(siege->artilleryRank) + "; Ballista base damage range: x" + std::to_string(siege->ballistaDamageMultiplier) + ".\n";
			siegeText += "Ballistics: " + rankName(siege->ballisticsRank) + "; First Aid: " + rankName(siege->firstAidRank) + ".\n";
			siegeText += "Control chances: Ballista " + std::to_string(siege->ballistaControlChance) + "%, Catapult " + std::to_string(siege->catapultControlChance) + "%, First Aid " + std::to_string(siege->firstAidControlChance) + "%.\n";
			siegeText += "Multiplier affects base damage range only, not total damage.\nControl chances do not guarantee an eligible action.";
		}
	}
	if(masteries)
	{
		masteryText = "Saved masteries - separate from skill ranks\n";
		for(const auto & choice : masteries->choices)
		{
			const auto & option = choice.selection.option;
			masteryText += GAME->translator().translate(option.nameTextId) + (choice.active ? " (active)" : " (inactive)") + ": "
				+ newHorizonsHeroes::formatMasteryDescription(option, GAME->translator().translate(option.descriptionTextId)) + "\n";
		}
		if(masteries->choices.empty())
			masteryText += "No mastery chosen.\n";
		if(masteries->pending)
			masteryText += "A saved mastery offer awaits its mandatory choice dialog.\n";
		for(const auto & skill : masteries->awaitingChoice)
			masteryText += skill.toEntity(LIBRARY)->getNameTranslated() + ": awaiting mastery choice.\n";
		for(const auto & skill : masteries->eligibleNextLevel)
			masteryText += skill.toEntity(LIBRARY)->getNameTranslated() + ": eligible on a future level gain.\n";
	}
	if(perks)
	{
		const int maxPerksPerSkill = perkState.rules["maxPerksPerSkill"].Integer();
		const int maxPerkChoices = perkState.rules["maxPerkChoices"].Integer();
		const size_t selectedCount = perkState.selected.size();
		size_t capacity = 0;
		size_t eligibleCount = 0;
		size_t lockedCount = 0;
		const auto projected = perkState.project([&hero](const std::string & skillId)
		{
			return hero.getPerkSkillRank(skillId);
		});

		std::string details = "Perks\n";
		details += "Selected: " + std::to_string(selectedCount);

		std::vector<std::string> skillIds;
		for(const auto & entry : perkState.rules["skills"].Struct())
			skillIds.push_back(entry.first);
		std::stable_sort(skillIds.begin(), skillIds.end(), [&hero, &perkState](const std::string & left, const std::string & right)
		{
			const auto selectedCountFor = [&perkState](const std::string & skillId)
			{
				return std::count_if(perkState.selected.begin(), perkState.selected.end(),
					[&skillId](const auto & selection) { return selection.skillId == skillId; });
			};
			const bool leftLearned = hero.getPerkSkillRank(left) > 0 || selectedCountFor(left) > 0;
			const bool rightLearned = hero.getPerkSkillRank(right) > 0 || selectedCountFor(right) > 0;
			return leftLearned > rightLearned;
		});

		for(const auto & skillId : skillIds)
		{
			const auto skill = newHorizonsHeroes::perkSkill(perkState.rules, skillId);
			if(!skill)
				continue;

			const int rank = hero.getPerkSkillRank(skillId);
			const size_t selectedForSkill = std::count_if(perkState.selected.begin(), perkState.selected.end(),
				[&skillId](const auto & selection) { return selection.skillId == skillId; });
			if(rank > 0)
				capacity += static_cast<size_t>(maxPerksPerSkill);

			std::vector<std::string> learned;
			std::vector<std::string> eligible;
			std::array<size_t, 3> lockedByRank{};
			std::array<size_t, 3> lockedByTier{};
			std::array<bool, 3> occupiedTiers{};
			size_t lockedByCapacity = 0;
			for(const auto & selection : perkState.selected)
			{
				if(selection.skillId != skillId)
					continue;
				const auto selectedPerk = newHorizonsHeroes::perkDefinition(perkState.rules,
					selection.skillId, selection.perkId);
				if(selectedPerk)
					occupiedTiers[static_cast<size_t>(newHorizonsHeroes::perkRequiredRank(selectedPerk->requiredRank) - 1)] = true;
			}
			for(const auto & perk : skill->perks)
			{
				if(perkState.hasSelection(skillId, perk.id))
				{
					const int requiredRank = newHorizonsHeroes::perkRequiredRank(perk.requiredRank);
					const auto modifier = std::find_if(projected.begin(), projected.end(), [&](const auto & candidate)
					{
						return candidate.skillId == skillId && candidate.perkId == perk.id;
					});
					const bool active = modifier != projected.end() && modifier->enabled;
					const bool planned = perk.effect["status"].String() != "active";
					std::string status = planned ? "planned; inactive" : active ? "active" : "inactive";
					if(!active && rank < requiredRank)
						status += "; requires " + perkRankName(requiredRank);
					learned.push_back(perk.name + " (" + status + ")");
					continue;
				}
				if(rank <= 0)
					continue;
				if(selectedForSkill >= static_cast<size_t>(maxPerksPerSkill))
				{
					++lockedByCapacity;
					continue;
				}
				const int requiredRank = newHorizonsHeroes::perkRequiredRank(perk.requiredRank);
				if(rank < requiredRank)
					++lockedByRank[static_cast<size_t>(requiredRank - 1)];
				else if(occupiedTiers[static_cast<size_t>(requiredRank - 1)])
					++lockedByTier[static_cast<size_t>(requiredRank - 1)];
				else
					eligible.push_back(perk.name);
			}
			if(rank <= 0)
			{
				lockedCount += skill->perks.size() - selectedForSkill;
				details += "\n" + skill->name + " - locked: skill not learned";
				if(!learned.empty())
					details += "\n  Selected: " + joinPerkNames(learned);
				continue;
			}

			eligibleCount += eligible.size();
			lockedCount += lockedByCapacity;
			lockedCount += lockedByRank[0] + lockedByRank[1] + lockedByRank[2];
			lockedCount += lockedByTier[0] + lockedByTier[1] + lockedByTier[2];
			details += "\n" + skill->name + " (" + perkRankName(rank) + ") - "
				+ std::to_string(selectedForSkill) + "/" + std::to_string(maxPerksPerSkill) + " selected, "
				+ std::to_string(static_cast<size_t>(maxPerksPerSkill) - std::min(selectedForSkill, static_cast<size_t>(maxPerksPerSkill))) + " remaining";
			if(!learned.empty())
				details += "\n  Selected: " + joinPerkNames(learned);
			if(!eligible.empty())
				details += "\n  Eligible for future level-up offers: " + joinPerkNames(eligible);
			if(lockedByCapacity > 0)
				details += "\n  Locked: " + std::to_string(lockedByCapacity) + " (per-skill capacity reached)";
			for(int requiredRank = rank + 1; requiredRank <= 3; ++requiredRank)
				if(lockedByRank[static_cast<size_t>(requiredRank - 1)] > 0)
					details += "\n  Locked: " + std::to_string(lockedByRank[static_cast<size_t>(requiredRank - 1)])
						+ " (requires " + perkRankName(requiredRank) + ")";
			for(int requiredRank = 1; requiredRank <= 3; ++requiredRank)
				if(lockedByTier[static_cast<size_t>(requiredRank - 1)] > 0)
					details += "\n  Locked: " + std::to_string(lockedByTier[static_cast<size_t>(requiredRank - 1)])
						+ " (" + perkRankName(requiredRank) + " tier already selected)";
			}

		perkText = "Selected: " + std::to_string(selectedCount) + " / " + std::to_string(capacity)
			+ " capacity for learned skills; " + std::to_string(eligibleCount) + " eligible for future level-up offers; "
			+ std::to_string(lockedCount) + " locked.\nEach learned skill can hold up to "
			+ std::to_string(maxPerksPerSkill) + " perks; a level-up offers at most "
			+ std::to_string(maxPerkChoices) + ".\n" + details;

		if(showPerkPool)
		{
			const size_t pageCount = (perkSkillIds.size() + PERK_SKILLS_PER_PAGE - 1) / PERK_SKILLS_PER_PAGE;
			perkSkillPage = std::min(perkSkillPage, pageCount - 1);
			const size_t pageStart = perkSkillPage * PERK_SKILLS_PER_PAGE;
			const size_t pageEnd = std::min(perkSkillIds.size(), pageStart + PERK_SKILLS_PER_PAGE);
			const auto selectedSkill = std::find(perkSkillIds.begin() + static_cast<std::ptrdiff_t>(pageStart),
				perkSkillIds.begin() + static_cast<std::ptrdiff_t>(pageEnd), selectedPerkSkill);
			if(selectedSkill == perkSkillIds.begin() + static_cast<std::ptrdiff_t>(pageEnd))
				selectedPerkSkill = perkSkillIds[pageStart];

			const int selectorTop = 365;
			const int cardWidth = 322;
			const int cardHeight = perkCardHeight;
			const int cardGap = perkCardGap;
			const int cardLeft = 22;
			const int cardTop = perkCardTop;
			perkSkillHeader = std::make_shared<CLabel>(22, selectorTop, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW,
				"Learned Skills (select one; page " + std::to_string(perkSkillPage + 1) + "/" + std::to_string(pageCount) + ")", 480);
			elements.push_back(perkSkillHeader);

			perkSkillButtons.resize(perkSkillIds.size());
			perkSkillPanels.resize(perkSkillIds.size());
			perkSkillNames.resize(perkSkillIds.size());
			perkSkillRanks.resize(perkSkillIds.size());
			perkSkillPrevious = std::make_shared<CButton>(Point(516, selectorTop + 17), AnimationPath::builtin("settingsWindow/button80"),
				CButton::tooltip("Previous Skills", "Show the previous page of learned Skills."), [this]
				{
					if(perkSkillPage == 0)
						return;
					--perkSkillPage;
					if(const auto * currentHero = GAME->interface()->cb->getHero(heroID))
						refresh(*currentHero);
				});
			perkSkillPrevious->setTextOverlay("Prev", FONT_TINY, Colors::WHITE);
			perkSkillPrevious->setHoverable(true);
			perkSkillPrevious->block(perkSkillPage == 0);
			elements.push_back(perkSkillPrevious);
			perkSkillNext = std::make_shared<CButton>(Point(598, selectorTop + 17), AnimationPath::builtin("settingsWindow/button80"),
				CButton::tooltip("Next Skills", "Show the next page of learned Skills."), [this, pageCount]
				{
					if(perkSkillPage + 1 >= pageCount)
						return;
					++perkSkillPage;
					if(const auto * currentHero = GAME->interface()->cb->getHero(heroID))
						refresh(*currentHero);
				});
			perkSkillNext->setTextOverlay("Next", FONT_TINY, Colors::WHITE);
			perkSkillNext->setHoverable(true);
			perkSkillNext->block(perkSkillPage + 1 >= pageCount);
			elements.push_back(perkSkillNext);

			for(size_t skillIndex = 0; skillIndex < perkSkillIds.size(); ++skillIndex)
			{
				const auto & skillId = perkSkillIds[skillIndex];
				const auto skill = newHorizonsPerkHelp::skillDefinition(&hero, skillId);
				if(!skill)
					continue;
				const int rank = hero.getPerkSkillRank(skillId);
				perkSkillNames[skillIndex] = skill->name;
				perkSkillRanks[skillIndex] = rank;
				if(skillIndex >= pageStart && skillIndex < pageEnd)
				{
					const int buttonX = 20 + static_cast<int>(skillIndex - pageStart) * 82;
					const auto button = std::make_shared<CButton>(Point(buttonX, selectorTop + 17), AnimationPath::builtin("settingsWindow/button80"),
						CButton::tooltip(skill->name, "Show the ten-perk pool for " + skill->name + ". This is read-only."),
						[this, skillId] { selectPerkSkill(skillId); });
					button->setTextOverlay(skill->name, FONT_TINY, Colors::WHITE);
					button->setHoverable(true);
					perkSkillButtons[skillIndex] = button;
					elements.push_back(button);
				}

				std::vector<std::shared_ptr<CIntObject>> panel;
				for(size_t perkIndex = 0; perkIndex < skill->perks.size(); ++perkIndex)
				{
					const auto & perk = skill->perks[perkIndex];
					const int requiredRank = newHorizonsHeroes::perkRequiredRank(perk.requiredRank);
					const size_t selectedForSkill = selectedPerksForSkill(perkState, skillId);
					std::array<bool, 3> occupiedTiers{};
					for(const auto & selection : perkState.selected)
					{
						if(selection.skillId != skillId)
							continue;
						if(const auto selectedPerk = newHorizonsPerkHelp::perkDefinition(&hero, selection.skillId, selection.perkId))
						{
							const int selectedRank = newHorizonsHeroes::perkRequiredRank(selectedPerk->requiredRank);
							if(selectedRank >= 1 && selectedRank <= 3)
								occupiedTiers[static_cast<size_t>(selectedRank - 1)] = true;
						}
					}

					std::string status;
					std::string reason;
					ColorRGBA borderColor = Colors::WHITE;
					if(perkState.hasSelection(skillId, perk.id))
					{
						status = "Acquired";
						borderColor = Colors::GREEN;
					}
					else if(hero.getPerkSkillRank(skillId) <= 0)
					{
						status = "Locked";
						reason = "requires learning " + skill->name;
						borderColor = Colors::RED;
					}
					else if(selectedForSkill >= static_cast<size_t>(std::max(0, maxPerksPerSkill)))
					{
						status = "Locked";
						reason = "per-Skill limit reached (maximum " + std::to_string(maxPerksPerSkill) + ")";
						borderColor = Colors::RED;
					}
					else if(hero.getPerkSkillRank(skillId) < requiredRank)
					{
						status = "Locked";
						reason = "requires " + newHorizonsPerkHelp::tierName(requiredRank) + " " + skill->name;
						borderColor = Colors::RED;
					}
					else if(requiredRank >= 1 && requiredRank <= 3 && occupiedTiers[static_cast<size_t>(requiredRank - 1)])
					{
						status = "Locked";
						reason = newHorizonsPerkHelp::tierName(requiredRank) + " tier already selected";
						borderColor = Colors::RED;
					}
					else
					{
						status = "Available";
						reason = "eligible on a future level-up offer";
						borderColor = Colors::YELLOW;
					}

					const int column = static_cast<int>(perkIndex % 2);
					const int row = static_cast<int>(perkIndex / 2);
					const int x = cardLeft + column * (cardWidth + 12);
					const int y = cardTop + row * (cardHeight + cardGap);
					const auto background = std::make_shared<TransparentFilledRectangle>(Rect(x, y, cardWidth, cardHeight), panelColor, borderColor);
					panel.push_back(background);
					elements.push_back(background);
					const auto icon = std::make_shared<CPicture>(ImagePath::builtin(newHorizonsPerkIcon(perk.id)), x + 4, y + 3);
					icon->scaleTo(Point(20, 20));
					panel.push_back(icon);
					elements.push_back(icon);
					// Keep the owning Skill visible beside the perk art.  The text
					// identifies it, while this native icon makes the relationship
					// immediately recognizable in a dense ten-perk pool.
					if(const auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId))
					{
						try
						{
							const int iconFrame = parentSkill->toSkill()->getIconIndex(
								static_cast<uint8_t>(std::clamp(hero.getPerkSkillRank(skillId), 1, 3)));
							const auto skillIcon = std::make_shared<CAnimImage>(AnimationPath::builtin("SECSKILL"), iconFrame,
								Rect(x + 25, y + 5, 16, 16));
							panel.push_back(skillIcon);
							elements.push_back(skillIcon);
						}
						catch(const std::exception &)
						{
							// Unknown saved skill art must not make the read-only pool fail.
						}
					}
					const std::string cardText = status + ": " + perk.name + "\n"
						+ "Tier: " + newHorizonsPerkHelp::tierName(requiredRank) + " | Skill: " + skill->name;
					const auto caption = std::make_shared<CMultiLineLabel>(Rect(x + 44, y + 1, cardWidth - 48, cardHeight - 2),
						FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, cardText);
					panel.push_back(caption);
					elements.push_back(caption);
					const auto help = newHorizonsPerkHelp::format(&hero, skillId, perk.name,
						newHorizonsPerkHelp::tierName(requiredRank), perk.description)
						+ "\n\nStatus: " + status + (reason.empty() ? std::string() : "\nReason: " + reason);
					std::shared_ptr<LRClickableAreaWText> area;
					if(const auto parentSkill = newHorizonsPerkHelp::skillEntity(skillId))
					{
						const auto componentArea = std::make_shared<LRClickableAreaWTextComp>(Rect(x, y, cardWidth, cardHeight),
							ComponentType::SEC_SKILL);
						componentArea->component.subType = *parentSkill;
						componentArea->component.value = std::clamp(hero.getPerkSkillRank(skillId), 1, 3);
						componentArea->text = help;
						area = componentArea;
					}
					else
						area = std::make_shared<LRClickableAreaWText>(Rect(x, y, cardWidth, cardHeight),
							status + ": " + perk.name, help);
					panel.push_back(area);
					elements.push_back(area);
				}
				perkSkillPanels[skillIndex] = std::move(panel);
			}

			selectPerkSkill(selectedPerkSkill);
		}
	}

	const std::array<EShortcut, HeroDevelopmentNavigation::SECTION_COUNT> sectionShortcuts = {
		EShortcut::SELECT_INDEX_1, EShortcut::SELECT_INDEX_2, EShortcut::SELECT_INDEX_3, EShortcut::SELECT_INDEX_4, EShortcut::SELECT_INDEX_5
	};
	const std::array<const char *, HeroDevelopmentNavigation::SECTION_COUNT> sectionNames = {"Growth", "Leadership", "Siege", "Masteries", "Perks"};
	for(size_t i = 0; i < sectionButtons.size(); ++i)
	{
		const auto section = static_cast<HeroDevelopmentSection>(i);
		const std::string hint = navigation.isAvailable(section)
			? "View saved " + std::string(sectionNames[i]) + " details (" + std::to_string(i + 1) + "). Read-only; no choices or points are spent."
			: sectionTexts[i];
		const int buttonX = 20 + static_cast<int>(i) * ((pos.w - 40 - 80) / static_cast<int>(sectionButtons.size() - 1));
		sectionButtons[i] = std::make_shared<CButton>(Point(buttonX, 332), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip(sectionNames[i], hint), [this, section] { selectSection(section); }, sectionShortcuts[i]);
		sectionButtons[i]->setTextOverlay(sectionNames[i], FONT_SMALL, Colors::WHITE);
		sectionButtons[i]->setHoverable(true);
		sectionButtons[i]->block(!navigation.isAvailable(section));
	}
	const auto selected = navigation.selected();
	if(!showPerkPool)
		sectionText = std::make_shared<CTextBox>(selected ? sectionTexts[static_cast<size_t>(*selected)] : "No saved development details.",
			Rect(22, 365, 656, 108), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	updateSectionButtons();
	if(!showPerkPool)
	{
		const std::string footer = growth
			? "Base includes level/quest gains; Total includes items/effects.\nClass start may differ from authored stats; growth proposals are before the cap.\nPrimary cap: " + std::to_string(growth->maximumPrimary) + "; Power scaling divisor: " + std::to_string(growth->powerDivisor) + "."
			: "Totals are current hero attributes; no saved primary growth profile.\nDevelopment details are read-only values from this hero's saved rules.\nSelect a section above for its scope and limitations.";
		elements.push_back(std::make_shared<CTextBox>(footer, Rect(22, 484, 560, 58), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE));
	}
	closeButton = std::make_shared<CButton>(Point(614, showPerkPool ? perkCloseTop : 485), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Close", "Return to the hero screen without changing anything."), [this] { close(); }, EShortcut::GLOBAL_CANCEL);
	closeButton->setHoverable(true);
	updateShadow();
	center();
}
