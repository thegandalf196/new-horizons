/*
 * HeroSkillOddsWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsPerkBrowser.h"
#include "HeroSkillOddsWindow.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
constexpr int CELL_WIDTH = 132;
constexpr int CELL_HEIGHT = 36;
constexpr int CELL_ICON = 32;

struct SkillWeight
{
	SecondarySkill skill = SecondarySkill::NONE;
	std::string key;
	std::string name;
	int weight = 0;
};

std::string skillName(const CGHeroInstance & hero, SecondarySkill skill, const std::string & fallback)
{
	try
	{
		return newHorizonsPerkHelp::skillLabel(&hero, skill);
	}
	catch(const std::exception &)
	{
		return fallback;
	}
}

int skillIconFrame(SecondarySkill skill)
{
	try
	{
		// A class-odds pane describes a prospective Basic offer.  The normal
		// CSkill icon family still carries the school/faction identity, while
		// the rank marker makes the preview distinct from a learned rank.
		return skill.toSkill()->getIconIndex(1);
	}
	catch(const std::exception &)
	{
		return -1;
	}
}

/// One odds-grid entry.  Left-click opens the Skill's read-only perk
/// catalogue exactly like a learned Skill in the hero window; Skills without
/// a saved catalogue keep the ordinary Skill information dialog.
class SkillOddsArea : public LRClickableAreaWTextComp
{
	const CGHeroInstance * hero;
	std::string skillId;

public:
	SkillOddsArea(const Rect & area, const CGHeroInstance & hero_, SecondarySkill skill)
		: LRClickableAreaWTextComp(area, ComponentType::SEC_SKILL), hero(&hero_)
	{
		component.subType = skill;
		component.value = 1;
		try
		{
			// Same identity as the hero window's learned-Skill click.
			skillId = skill.toSkill()->getJsonKey();
			text = newHorizonsPerkHelp::skillDescription(hero, skill, 1);
		}
		catch(const std::exception &)
		{
			component.type = ComponentType::NONE;
		}
	}

	void clickPressed(const Point & cursorPosition) override
	{
		if(!skillId.empty() && newHorizonsPerkHelp::skillDefinition(hero, skillId))
		{
			ENGINE->windows().createAndPushWindow<NewHorizonsPerkBrowser>(*hero, skillId);
			return;
		}
		if(!text.empty())
			LRClickableAreaWTextComp::clickPressed(cursorPosition);
	}
};

std::vector<SkillWeight> classSkillWeights(const CGHeroInstance & hero, const JsonNode & rules, bool canonical)
{
	const auto ownFactionSkill = canonical
		? newHorizonsHeroes::factionSkill(rules, hero.getFactionID())
		: std::optional<SecondarySkill>();
	const auto isForeignFactionSkill = [&](SecondarySkill skill)
	{
		// Legacy classes have no New Horizons faction-skill registry.  Keep the
		// old pane's exact roster there; this filter is deliberately scoped to a
		// saved canonical New Horizons rules snapshot.
		return canonical && newHorizonsHeroes::isFactionSkill(rules, skill)
			&& (!ownFactionSkill || !newHorizonsHeroes::isFactionSkillForFaction(
				rules, hero.getFactionID(), skill));
	};

	std::vector<SkillWeight> weights;
	if(canonical)
	{
		// Read this hero's saved class row, not the current module's defaults.
		for(const auto & [id, weight] : rules["skillOfferWeights"].Struct())
		{
			const int decoded = SecondarySkill::decode(id);
			if(decoded < 0)
				continue;
			const SecondarySkill skill(decoded);
			if(isForeignFactionSkill(skill))
				continue;
			weights.push_back({skill, id, skillName(hero, skill, id), std::max<int>(0, weight.Integer())});
		}
	}
	else
	{
		for(const auto & [skill, weight] : hero.getHeroClass()->secSkillProbability)
			weights.push_back({skill, SecondarySkill::encode(skill.getNum()), skillName(hero, skill, "Unknown"), std::max(1, weight)});
	}
	std::stable_sort(weights.begin(), weights.end(), [](const SkillWeight & left, const SkillWeight & right)
	{
		return left.name < right.name;
	});
	return weights;
}

void addSkillCell(std::vector<std::shared_ptr<CIntObject>> & elements, const CGHeroInstance & hero, const SkillWeight & entry, int total, const Point & topLeft)
{
	// Icon, name and weight sit directly on the dialog surface, in the same
	// two-line arrangement as the hero window's secondary-skill slots.
	if(const int frame = skillIconFrame(entry.skill); frame >= 0)
		elements.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SECSKILL"), frame,
			Rect(topLeft.x, topLeft.y + 2, CELL_ICON, CELL_ICON)));
	const double share = total > 0 ? 100.0 * entry.weight / total : 0.0;
	const int textLeft = topLeft.x + CELL_ICON + 5;
	const int textWidth = CELL_WIDTH - CELL_ICON - 5;
	elements.push_back(std::make_shared<CLabel>(textLeft, topLeft.y + 2, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, entry.name, textWidth));
	elements.push_back(std::make_shared<CLabel>(textLeft, topLeft.y + 19, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, std::to_string(entry.weight) + " / " + (boost::format("%.1f%%") % share).str(), textWidth));
	elements.push_back(std::make_shared<SkillOddsArea>(Rect(topLeft.x, topLeft.y, CELL_WIDTH, CELL_HEIGHT), hero, entry.skill));
}
}

HeroSkillOddsWindow::HeroSkillOddsWindow(const CGHeroInstance & hero)
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;
	const auto & rules = hero.getPrimaryGrowthRules();
	const bool canonical = newHorizonsHeroes::usesSkillOfferWeights(rules);
	const auto weights = classSkillWeights(hero, rules, canonical);

	int total = 0;
	for(const auto & entry : weights)
		total += entry.weight;

	// A five-column grid keeps the complete class table inspectable without
	// falling back to a names-only scrolling list.
	constexpr int columns = 5;
	constexpr int cellGapX = 6;
	constexpr int cellGapY = 10;
	constexpr int margin = 18;
	constexpr int gridTop = 126;
	const int rows = std::max(1, static_cast<int>((weights.size() + columns - 1) / columns));
	const int footerTop = gridTop + rows * (CELL_HEIGHT + cellGapY) + 4;
	pos = Rect(0, 0, margin * 2 + columns * CELL_WIDTH + (columns - 1) * cellGapX, footerTop + 84);
	// The same tiled leather surface as the perk browser and other New
	// Horizons dialogs; BORDERED supplies the original player-coloured frame.
	elements.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h)));
	elements.push_back(std::make_shared<CLabel>(pos.w / 2, 24, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Class skill odds"));
	elements.push_back(std::make_shared<CLabel>(pos.w / 2, 53, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE,
		GAME->translator().translate(hero.getClassNameTextID()), pos.w - 36));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(18, 73, pos.w - 36, 40), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE, canonical
			? "Saved class weights. Base share = weight / total class weight; zero means no ordinary offer. Foreign faction Skills are hidden."
			: "Legacy class weights (minimum 1). Base share = weight / total class weight; special offer rules may differ."));

	for(size_t i = 0; i < weights.size(); ++i)
	{
		const int column = static_cast<int>(i % columns);
		const int row = static_cast<int>(i / columns);
		addSkillCell(elements, hero, weights[i], total,
			Point(margin + column * (CELL_WIDTH + cellGapX), gridTop + row * (CELL_HEIGHT + cellGapY)));
	}
	if(weights.empty())
		elements.push_back(std::make_shared<CLabel>(pos.w / 2, gridTop + 20, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE,
		"No class skill weights are available.", pos.w - 36));

	// Keep the established wording in a compact footer below the grid.
	elements.push_back(std::make_shared<CTextBox>(
		"Not exact next-level probabilities. Base share = weight / total class weight. Learned skills, eligibility and available slots change the candidate pool. Click a Skill to browse its perks.",
		Rect(18, footerTop, pos.w - 126, 56), 0, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE));
	elements.push_back(std::make_shared<CButton>(Point(pos.w - 90, footerTop + 12), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Close", "Return to the hero screen."), [this]() { close(); }, EShortcut::GLOBAL_CANCEL));
	updateShadow();
	center();
}
