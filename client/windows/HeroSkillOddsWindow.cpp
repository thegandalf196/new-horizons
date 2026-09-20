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
#include "HeroSkillOddsWindow.h"

#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

HeroSkillOddsWindow::HeroSkillOddsWindow(const CGHeroInstance & hero)
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;
	pos = Rect(0, 0, 600, 440);
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w, pos.h),
		ColorRGBA(39, 35, 41), ColorRGBA(180, 154, 98), 2));
	elements.push_back(std::make_shared<CLabel>(300, 24, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Class skill odds"));
	elements.push_back(std::make_shared<CLabel>(300, 53, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE,
		GAME->translator().translate(hero.getClassNameTextID()), 560));

	const auto & rules = hero.getPrimaryGrowthRules();
	const bool canonical = newHorizonsHeroes::usesSkillOfferWeights(rules);
	std::vector<std::pair<std::string, int>> weights;
	if(canonical)
	{
		// Read this hero's saved class row, not the current module's defaults.
		for(const auto & [id, weight] : rules["skillOfferWeights"].Struct())
		{
			const int decoded = SecondarySkill::decode(id);
			const auto name = decoded >= 0 ? SecondarySkill(decoded).toSkill()->getNameTranslated() : id;
			weights.emplace_back(name, std::max<int>(0, weight.Integer()));
		}
	}
	else
	{
		for(const auto & [skill, weight] : hero.getHeroClass()->secSkillProbability)
			weights.emplace_back(skill.toSkill()->getNameTranslated(), std::max(1, weight));
	}
	std::sort(weights.begin(), weights.end());
	int total = 0;
	for(const auto & [name, weight] : weights)
		total += weight;
	std::string text;
	for(const auto & [name, weight] : weights)
	{
		const double share = total > 0 ? 100.0 * weight / total : 0.0;
		text += name + "  —  weight " + std::to_string(weight)
			+ "  /  base share " + (boost::format("%.1f%%") % share).str() + "\n\n";
	}
	if(text.empty())
		text = "No class skill weights are available.";
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(18, 73, 564, 40), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE, canonical
			? "Saved class weights. Base share = weight / total class weight; zero means no ordinary offer."
			: "Legacy class weights (minimum 1). Base share = weight / total class weight; special offer rules may differ."));
	elements.push_back(std::make_shared<CTextBox>(text, Rect(18, 120, 564, 252), 0, FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(18, 382, 478, 48), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE,
		"Not exact next-level probabilities. Learned skills, eligibility, available slots and Wisdom/school scheduling change the candidate pool."));
	elements.push_back(std::make_shared<CButton>(Point(526, 390), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Close", "Return to the hero screen."), [this]() { close(); }, EShortcut::GLOBAL_CANCEL));
	updateShadow();
	center();
}
